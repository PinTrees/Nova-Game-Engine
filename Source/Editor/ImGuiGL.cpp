#include "pch.h"
#include "ImGuiGL.h"
#include "GLLoader.h"
#include "GLShared.h"
#include "GfxGL.h"
#include "GLContext.h"
#include "GLState.h"

namespace
{
	struct State
	{
		GLuint Program = 0;
		GLint ProjLoc = -1;
		GLuint Vao = 0, Vbo = 0, Ebo = 0, Sampler = 0;
		size_t VboBytes = 0, EboBytes = 0;
		ComPtr<GfxShaderResourceView> Font;
		std::vector<ImDrawVert> Vertices;
		std::vector<ImDrawIdx> Indices;
	};
	State* s_State = nullptr;

	// 뷰포트(ImGui 가 만든 OS 창)마다: 창 DC (픽셀 형식은 본 창과 같게 — 같은 GL 컨텍스트를 붙일 수 있어야 한다)
	struct ViewportData
	{
		HWND Wnd = nullptr;
		HDC Dc = nullptr;
		bool Ok = false;
	};
	HDC s_MainDc = nullptr;     // 본 창 DC·컨텍스트 (Init 때 현재인 것)
	HGLRC s_MainRc = nullptr;

	void DrawLists(ImDrawData* dd, bool toWindow);

	const char* kVertex = R"(#version 450
layout(location = 0) in vec2 Position;
layout(location = 1) in vec2 UV;
layout(location = 2) in vec4 Color;
uniform mat4 ProjMtx;
out vec2 Frag_UV;
out vec4 Frag_Color;
void main()
{
    Frag_UV = UV;
    Frag_Color = Color;
    gl_Position = ProjMtx * vec4(Position.xy, 0.0, 1.0);
}
)";

	const char* kFragment = R"(#version 450
in vec2 Frag_UV;
in vec4 Frag_Color;
layout(binding = 0) uniform sampler2D Texture;
layout(location = 0) out vec4 Out_Color;
void main()
{
    Out_Color = Frag_Color * texture(Texture, Frag_UV);
}
)";

	GLuint Compile(GLenum type, const char* src)
	{
		GLuint s = glCreateShader(type);
		glShaderSource(s, 1, &src, nullptr);
		glCompileShader(s);
		GLint ok = 0;
		glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
		if (!ok)
		{
			char log[1024] = {};
			glGetShaderInfoLog(s, sizeof(log), nullptr, log);
			EditorLog::Write("ImGuiGL", "shader: %s", log);
			glDeleteShader(s);
			return 0;
		}
		return s;
	}

	bool CreateFont()
	{
		ImGuiIO& io = ImGui::GetIO();
		unsigned char* pixels = nullptr;
		int w = 0, h = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = w;
		td.Height = h;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA sd = { pixels, (UINT)w * 4, 0 };
		ComPtr<GfxTexture2D> tex;
		if (FAILED(Gfx::Device()->CreateTexture2D(&td, &sd, tex.GetAddressOf())) ||
			FAILED(Gfx::Device()->CreateShaderResourceView(tex.Get(), nullptr, s_State->Font.ReleaseAndGetAddressOf())))
			return false;
		io.Fonts->SetTexID((ImTextureID)s_State->Font.Get());
		return true;
	}
}

namespace ImGuiGL
{
	bool Init()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.BackendRendererName = "nova_imgui_opengl";
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
		s_State = new State();
		s_MainDc = ::wglGetCurrentDC();
		s_MainRc = ::wglGetCurrentContext();
		if (s_MainDc && s_MainRc)
		{
			io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
			ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
			pio.Renderer_CreateWindow = [](ImGuiViewport* vp)
			{
				auto* d = IM_NEW(ViewportData)();
				vp->RendererUserData = d;
				d->Wnd = (HWND)vp->PlatformHandleRaw;
				d->Dc = d->Wnd ? ::GetDC(d->Wnd) : nullptr;
				// 본 창과 같은 픽셀 형식 (다르면 wglMakeCurrent 가 실패한다)
				const int pf = ::GetPixelFormat(s_MainDc);
				PIXELFORMATDESCRIPTOR pfd = {};
				d->Ok = d->Dc && pf > 0 && ::DescribePixelFormat(s_MainDc, pf, sizeof(pfd), &pfd) && ::SetPixelFormat(d->Dc, pf, &pfd);
				if (!d->Ok)
					EditorLog::Write("ImGuiGL", "viewport window: pixel format failed (error %lu) - window not drawn", ::GetLastError());
			};
			pio.Renderer_DestroyWindow = [](ImGuiViewport* vp)
			{
				if (auto* d = static_cast<ViewportData*>(vp->RendererUserData))
				{
					if (::wglGetCurrentDC() == d->Dc)
						::wglMakeCurrent(s_MainDc, s_MainRc);
					if (d->Dc) ::ReleaseDC(d->Wnd, d->Dc);
					IM_DELETE(d);
				}
				vp->RendererUserData = nullptr;
			};
			pio.Renderer_RenderWindow = [](ImGuiViewport* vp, void*)
			{
				auto* d = static_cast<ViewportData*>(vp->RendererUserData);
				if (!d || !d->Ok || !s_State || !s_State->Program || !::wglMakeCurrent(d->Dc, s_MainRc))
					return;
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glDisable(GL_FRAMEBUFFER_SRGB);   // 본 창 백버퍼(UNORM)와 같은 값 그대로
				if (!(vp->Flags & ImGuiViewportFlags_NoRendererClear))
				{
					glDisable(GL_SCISSOR_TEST);
					glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
					const float black[4] = { 0, 0, 0, 1 };
					glClearNamedFramebufferfv(0, GL_COLOR, 0, black);   // 0 = 이 창의 기본 프레임버퍼
				}
				DrawLists(vp->DrawData, true);
			};
			pio.Renderer_SwapBuffers = [](ImGuiViewport* vp, void*)
			{
				if (auto* d = static_cast<ViewportData*>(vp->RendererUserData); d && d->Ok)
				{
					GLContext::SetSwapInterval(0);   // 본 창과 같음 (창마다 기다리지 않게)
					::SwapBuffers(d->Dc);
				}
			};
		}
		EditorLog::Write("ImGuiGL", "viewports (windows outside the editor): %s", (s_MainDc && s_MainRc) ? "on" : "off - no current GL context at init");
		return true;   // GL 객체·글꼴 텍스처는 첫 NewFrame 에서 (에디터가 글꼴을 다 넣고 Build 한 뒤 — DX11 백엔드와 같음)
	}

	void EndPlatformWindows()
	{
		if (s_MainDc && s_MainRc && (::wglGetCurrentDC() != s_MainDc || ::wglGetCurrentContext() != s_MainRc))
			::wglMakeCurrent(s_MainDc, s_MainRc);
		glEnable(GL_FRAMEBUFFER_SRGB);
		GfxGL::RestoreState(Gfx::Context());   // 엔진이 아는 타깃·뷰포트·상태로
	}

	void Shutdown()
	{
		ImGui::DestroyPlatformWindows();   // 뷰포트 창의 DC 를 먼저 놓는다 (Renderer_DestroyWindow)
		InvalidateDeviceObjects();
		delete s_State;
		s_State = nullptr;
		ImGuiIO& io = ImGui::GetIO();
		io.BackendRendererName = nullptr;
	}

	bool CreateDeviceObjects()
	{
		if (!s_State) return false;
		GLuint vs = Compile(GL_VERTEX_SHADER, kVertex), fs = Compile(GL_FRAGMENT_SHADER, kFragment);
		if (!vs || !fs) return false;
		s_State->Program = glCreateProgram();
		glAttachShader(s_State->Program, vs);
		glAttachShader(s_State->Program, fs);
		glLinkProgram(s_State->Program);
		glDetachShader(s_State->Program, vs);
		glDetachShader(s_State->Program, fs);
		glDeleteShader(vs);
		glDeleteShader(fs);
		GLint ok = 0;
		glGetProgramiv(s_State->Program, GL_LINK_STATUS, &ok);
		if (!ok)
		{
			EditorLog::Write("ImGuiGL", "%s", "program link failed");
			return false;
		}
		s_State->ProjLoc = glGetUniformLocation(s_State->Program, "ProjMtx");
		glCreateVertexArrays(1, &s_State->Vao);
		const GLuint vao = s_State->Vao;
		glEnableVertexArrayAttrib(vao, 0);
		glEnableVertexArrayAttrib(vao, 1);
		glEnableVertexArrayAttrib(vao, 2);
		glVertexArrayAttribFormat(vao, 0, 2, GL_FLOAT, GL_FALSE, (GLuint)offsetof(ImDrawVert, pos));
		glVertexArrayAttribFormat(vao, 1, 2, GL_FLOAT, GL_FALSE, (GLuint)offsetof(ImDrawVert, uv));
		glVertexArrayAttribFormat(vao, 2, 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLuint)offsetof(ImDrawVert, col));
		glVertexArrayAttribBinding(vao, 0, 0);
		glVertexArrayAttribBinding(vao, 1, 0);
		glVertexArrayAttribBinding(vao, 2, 0);
		glCreateSamplers(1, &s_State->Sampler);
		glSamplerParameteri(s_State->Sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glSamplerParameteri(s_State->Sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glSamplerParameteri(s_State->Sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);   // imgui_impl_dx11 과 같음 (WRAP 이지만 UI 는 0..1 안)
		glSamplerParameteri(s_State->Sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		return CreateFont();
	}

	void InvalidateDeviceObjects()
	{
		if (!s_State) return;
		if (s_State->Program) glDeleteProgram(s_State->Program);
		if (s_State->Vao) glDeleteVertexArrays(1, &s_State->Vao);
		if (s_State->Vbo) glDeleteBuffers(1, &s_State->Vbo);
		if (s_State->Ebo) glDeleteBuffers(1, &s_State->Ebo);
		if (s_State->Sampler) glDeleteSamplers(1, &s_State->Sampler);
		s_State->Program = s_State->Vao = s_State->Vbo = s_State->Ebo = s_State->Sampler = 0;
		GLState::InvalidateBindings();
		s_State->VboBytes = s_State->EboBytes = 0;
		s_State->Font.Reset();
		ImGui::GetIO().Fonts->SetTexID(0);
	}

	void NewFrame()
	{
		if (s_State && !s_State->Program)
			CreateDeviceObjects();
	}

	void RenderDrawData(ImDrawData* dd)
	{
		DrawLists(dd, false);
	}
}

namespace
{
	// toWindow = false: 본 창 백버퍼 텍스처 (행 0 = 위, Present 가 뒤집음) / true: 뷰포트 창의 기본 프레임버퍼 (행 0 = 아래)
	void DrawLists(ImDrawData* dd, bool toWindow)
	{
		if (!s_State || !s_State->Program || !dd || dd->TotalVtxCount == 0) return;
		const float fbW = dd->DisplaySize.x * dd->FramebufferScale.x, fbH = dd->DisplaySize.y * dd->FramebufferScale.y;
		if (fbW <= 0 || fbH <= 0) return;

		// 정점·인덱스를 한 버퍼로 모아 한 번에 올린다 (부족하면 키운다)
		State& s = *s_State;
		s.Vertices.clear();
		s.Indices.clear();
		for (int n = 0; n < dd->CmdListsCount; ++n)
		{
			const ImDrawList* l = dd->CmdLists[n];
			s.Vertices.insert(s.Vertices.end(), l->VtxBuffer.Data, l->VtxBuffer.Data + l->VtxBuffer.Size);
			s.Indices.insert(s.Indices.end(), l->IdxBuffer.Data, l->IdxBuffer.Data + l->IdxBuffer.Size);
		}
		const size_t vb = s.Vertices.size() * sizeof(ImDrawVert), ib = s.Indices.size() * sizeof(ImDrawIdx);
		if (vb > s.VboBytes)
		{
			if (s.Vbo) glDeleteBuffers(1, &s.Vbo);
			s.VboBytes = vb + 64 * 1024;
			glCreateBuffers(1, &s.Vbo);
			glNamedBufferStorage(s.Vbo, s.VboBytes, nullptr, GL_DYNAMIC_STORAGE_BIT);
		}
		if (ib > s.EboBytes)
		{
			if (s.Ebo) glDeleteBuffers(1, &s.Ebo);
			s.EboBytes = ib + 32 * 1024;
			glCreateBuffers(1, &s.Ebo);
			glNamedBufferStorage(s.Ebo, s.EboBytes, nullptr, GL_DYNAMIC_STORAGE_BIT);
		}
		glNamedBufferSubData(s.Vbo, 0, vb, s.Vertices.data());
		glNamedBufferSubData(s.Ebo, 0, ib, s.Indices.data());
		glVertexArrayVertexBuffer(s.Vao, 0, s.Vbo, 0, sizeof(ImDrawVert));
		glVertexArrayElementBuffer(s.Vao, s.Ebo);

		// 상태: 알파 블렌드, 컬링·깊이 없음, 가위
		glEnablei(GL_BLEND, 0);
		glBlendFuncSeparatei(0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glBlendEquationSeparatei(0, GL_FUNC_ADD, GL_FUNC_ADD);
		glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_POLYGON_OFFSET_FILL);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glEnable(GL_SCISSOR_TEST);
		glViewportIndexedf(0, 0, 0, fbW, fbH);
		glDepthRangeIndexed(0, 0.0, 1.0);

		// 투영: 백버퍼 텍스처면 화면 위(T) → NDC -1 (행 0 = 위, 창 y = 행 번호 — Present 가 뒤집어 보인다),
		// 뷰포트 창의 기본 프레임버퍼면 보통 GL 처럼 T → NDC +1
		const float L = dd->DisplayPos.x, R = dd->DisplayPos.x + dd->DisplaySize.x;
		const float T = dd->DisplayPos.y, B = dd->DisplayPos.y + dd->DisplaySize.y;
		const float top = toWindow ? B : T, bottom = toWindow ? T : B;
		const float proj[16] = {
			2.0f / (R - L), 0, 0, 0,
			0, 2.0f / (bottom - top), 0, 0,
			0, 0, -1.0f, 0,
			(R + L) / (L - R), (top + bottom) / (top - bottom), 0, 1.0f };
		glUseProgram(s.Program);
		glProgramUniformMatrix4fv(s.Program, s.ProjLoc, 1, GL_FALSE, proj);
		glBindVertexArray(s.Vao);
		glBindSampler(0, s.Sampler);

		const ImVec2 clipOff = dd->DisplayPos, clipScale = dd->FramebufferScale;
		size_t vtxBase = 0, idxBase = 0;
		for (int n = 0; n < dd->CmdListsCount; ++n)
		{
			const ImDrawList* l = dd->CmdLists[n];
			for (int c = 0; c < l->CmdBuffer.Size; ++c)
			{
				const ImDrawCmd& cmd = l->CmdBuffer[c];
				if (cmd.UserCallback)
				{
					if (cmd.UserCallback != ImDrawCallback_ResetRenderState)
						cmd.UserCallback(l, &cmd);
					continue;
				}
				const float x0 = (cmd.ClipRect.x - clipOff.x) * clipScale.x, y0 = (cmd.ClipRect.y - clipOff.y) * clipScale.y;
				const float x1 = (cmd.ClipRect.z - clipOff.x) * clipScale.x, y1 = (cmd.ClipRect.w - clipOff.y) * clipScale.y;
				if (x1 <= x0 || y1 <= y0) continue;
				// 백버퍼 텍스처: 창 y = 위에서부터 (행 번호) / 기본 프레임버퍼: 아래에서부터
				glScissorIndexed(0, (GLint)x0, toWindow ? (GLint)(fbH - y1) : (GLint)y0, (GLsizei)(x1 - x0), (GLsizei)(y1 - y0));
				glBindTextureUnit(0, GfxGL_TextureName(reinterpret_cast<GfxShaderResourceView*>(cmd.GetTexID())));
				glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, (GLsizei)cmd.ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
					(const void*)((idxBase + cmd.IdxOffset) * sizeof(ImDrawIdx)), 1, (GLint)(vtxBase + cmd.VtxOffset), 0);
			}
			vtxBase += l->VtxBuffer.Size;
			idxBase += l->IdxBuffer.Size;
		}
		glBindSampler(0, 0);
		glBindVertexArray(0);
		glUseProgram(0);
		GLState::InvalidateBindings();   // 캐시를 거치지 않고 묶었다 (GLState 묶기 캐시)
		if (!toWindow)   // 뷰포트 창은 다 그린 뒤 EndPlatformWindows 에서 한 번 (그 전에 되돌리면 본 창 DC 로 바뀐다)
			GfxGL::RestoreState(Gfx::Context());   // 엔진(Gfx 컨텍스트)이 아는 상태로 되돌린다
	}
}
