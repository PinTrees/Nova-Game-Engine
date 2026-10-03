#include "pch.h"
#include "ImGuiGfx.h"
#include "PathManager.h"

namespace
{
	struct State
	{
		ComPtr<FxEffect> Fx;
		FxTechnique* Tech = nullptr;
		FxVar* Projection = nullptr;
		FxVar* Texture = nullptr;
		ComPtr<GfxInputLayout> Layout;
		ComPtr<GfxBuffer> Vb, Ib;
		UINT VbBytes = 0, IbBytes = 0;
		ComPtr<GfxShaderResourceView> Font;
		bool Failed = false;   // 셰이더를 못 불렀다 (한 번만 기록)
	};
	State* s_State = nullptr;

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

	bool Grow(ComPtr<GfxBuffer>& buffer, UINT& capacity, UINT needed, UINT bind)
	{
		if (buffer && needed <= capacity) return true;
		capacity = needed + needed / 2 + 64 * 1024;
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = capacity;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = bind;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		buffer.Reset();
		return SUCCEEDED(Gfx::Device()->CreateBuffer(&bd, nullptr, buffer.GetAddressOf()));
	}
}

namespace ImGuiGfx
{
	bool Init()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.BackendRendererName = "nova_imgui_gfx";
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
		s_State = new State();
		return true;   // 효과 · 글꼴 텍스처는 첫 NewFrame 에서 (에디터가 글꼴을 다 넣고 Build 한 뒤 — DX11 백엔드와 같음)
	}

	void Shutdown()
	{
		InvalidateDeviceObjects();
		delete s_State;
		s_State = nullptr;
		ImGui::GetIO().BackendRendererName = nullptr;
	}

	bool CreateDeviceObjects()
	{
		if (!s_State || s_State->Failed) return false;
		std::string error;
		const std::wstring path = (std::filesystem::path(PathManager::GetI()->GetEnginePathW()) / L"Shaders" / L"56. ImGui.fx").wstring();
		s_State->Fx = FxEffect::Load(path, error);
		if (!s_State->Fx)
		{
			s_State->Failed = true;
			EditorLog::Write("ImGuiGfx", "56. ImGui.fx failed to load: %s", error.c_str());
			return false;
		}
		s_State->Tech = s_State->Fx->GetTechniqueByName("ImGuiTech");
		s_State->Projection = s_State->Fx->GetVariableByName("gProjection")->AsMatrix();
		s_State->Texture = s_State->Fx->GetVariableByName("gTexture")->AsShaderResource();
		const D3D11_INPUT_ELEMENT_DESC elements[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)offsetof(ImDrawVert, pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)offsetof(ImDrawVert, uv), D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (UINT)offsetof(ImDrawVert, col), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		D3DX11_PASS_DESC pd;
		s_State->Tech->GetPassByIndex(0)->GetDesc(&pd);
		if (FAILED(Gfx::Device()->CreateInputLayout(elements, _countof(elements), pd.pIAInputSignature, pd.IAInputSignatureSize, s_State->Layout.GetAddressOf())))
		{
			s_State->Failed = true;
			EditorLog::Write("ImGuiGfx", "%s", "input layout failed");
			return false;
		}
		return CreateFont();
	}

	void InvalidateDeviceObjects()
	{
		if (!s_State) return;
		s_State->Fx.Reset();
		s_State->Tech = nullptr;
		s_State->Projection = s_State->Texture = nullptr;
		s_State->Layout.Reset();
		s_State->Vb.Reset();
		s_State->Ib.Reset();
		s_State->VbBytes = s_State->IbBytes = 0;
		s_State->Font.Reset();
		s_State->Failed = false;
		ImGui::GetIO().Fonts->SetTexID(0);
	}

	void NewFrame()
	{
		if (s_State && !s_State->Fx && !s_State->Failed)
			CreateDeviceObjects();
	}

	void RenderDrawData(ImDrawData* dd)
	{
		if (!s_State || !s_State->Fx || !dd || dd->TotalVtxCount == 0) return;
		const float fbW = dd->DisplaySize.x * dd->FramebufferScale.x, fbH = dd->DisplaySize.y * dd->FramebufferScale.y;
		if (fbW <= 0 || fbH <= 0) return;
		State& s = *s_State;
		GfxContext* ctx = Gfx::Context();

		// 정점 · 인덱스를 한 버퍼로 (부족하면 키운다)
		const UINT vb = (UINT)(dd->TotalVtxCount * sizeof(ImDrawVert)), ib = (UINT)(dd->TotalIdxCount * sizeof(ImDrawIdx));
		if (!Grow(s.Vb, s.VbBytes, vb, D3D11_BIND_VERTEX_BUFFER) || !Grow(s.Ib, s.IbBytes, ib, D3D11_BIND_INDEX_BUFFER)) return;
		D3D11_MAPPED_SUBRESOURCE vm, im;
		if (FAILED(ctx->Map(s.Vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &vm))) return;
		if (FAILED(ctx->Map(s.Ib.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &im))) { ctx->Unmap(s.Vb.Get(), 0); return; }
		auto* vdst = static_cast<ImDrawVert*>(vm.pData);
		auto* idst = static_cast<ImDrawIdx*>(im.pData);
		for (int n = 0; n < dd->CmdListsCount; ++n)
		{
			const ImDrawList* l = dd->CmdLists[n];
			memcpy(vdst, l->VtxBuffer.Data, l->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(idst, l->IdxBuffer.Data, l->IdxBuffer.Size * sizeof(ImDrawIdx));
			vdst += l->VtxBuffer.Size;
			idst += l->IdxBuffer.Size;
		}
		ctx->Unmap(s.Vb.Get(), 0);
		ctx->Unmap(s.Ib.Get(), 0);

		// 상태 저장 (끝나면 되돌린다 — imgui_impl_dx11 과 같이)
		UINT vpCount = 16, scCount = 16;
		D3D11_VIEWPORT oldVp[16];
		D3D11_RECT oldSc[16];
		ctx->RSGetViewports(&vpCount, oldVp);
		ctx->RSGetScissorRects(&scCount, oldSc);
		ComPtr<GfxRasterizerState> oldRs;
		ComPtr<GfxBlendState> oldBs;
		ComPtr<GfxDepthStencilState> oldDs;
		float oldFactor[4];
		UINT oldMask = 0, oldRef = 0;
		ctx->RSGetState(oldRs.GetAddressOf());
		ctx->OMGetBlendState(oldBs.GetAddressOf(), oldFactor, &oldMask);
		ctx->OMGetDepthStencilState(oldDs.GetAddressOf(), &oldRef);
		ComPtr<GfxInputLayout> oldLayout;
		ctx->IAGetInputLayout(oldLayout.GetAddressOf());
		D3D11_PRIMITIVE_TOPOLOGY oldTopo;
		ctx->IAGetPrimitiveTopology(&oldTopo);

		const D3D11_VIEWPORT vp = { 0, 0, fbW, fbH, 0, 1 };
		ctx->RSSetViewports(1, &vp);
		const UINT stride = sizeof(ImDrawVert), offset = 0;
		ctx->IASetInputLayout(s.Layout.Get());
		ctx->IASetVertexBuffers(0, 1, s.Vb.GetAddressOf(), &stride, &offset);
		ctx->IASetIndexBuffer(s.Ib.Get(), sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		// 투영: 화면 위(T) → NDC +1 (D3D 규칙 — Vulkan 은 셰이더 변환이 y 를 맞춘다)
		const float L = dd->DisplayPos.x, R = dd->DisplayPos.x + dd->DisplaySize.x;
		const float T = dd->DisplayPos.y, B = dd->DisplayPos.y + dd->DisplaySize.y;
		const float proj[16] = {
			2.0f / (R - L), 0, 0, 0,
			0, 2.0f / (T - B), 0, 0,
			0, 0, 0.5f, 0,
			(R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f };
		s.Projection->SetMatrix(proj);
		FxPass* pass = s.Tech->GetPassByIndex(0);

		const ImVec2 clipOff = dd->DisplayPos, clipScale = dd->FramebufferScale;
		UINT vtxBase = 0, idxBase = 0;
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
				const LONG x0 = (LONG)((cmd.ClipRect.x - clipOff.x) * clipScale.x), y0 = (LONG)((cmd.ClipRect.y - clipOff.y) * clipScale.y);
				const LONG x1 = (LONG)((cmd.ClipRect.z - clipOff.x) * clipScale.x), y1 = (LONG)((cmd.ClipRect.w - clipOff.y) * clipScale.y);
				if (x1 <= x0 || y1 <= y0) continue;
				const D3D11_RECT r = { x0, y0, x1, y1 };
				ctx->RSSetScissorRects(1, &r);
				s.Texture->SetResource(reinterpret_cast<GfxShaderResourceView*>(cmd.GetTexID()));
				pass->Apply(0, ctx);
				ctx->DrawIndexed(cmd.ElemCount, idxBase + cmd.IdxOffset, (INT)(vtxBase + cmd.VtxOffset));
			}
			vtxBase += l->VtxBuffer.Size;
			idxBase += l->IdxBuffer.Size;
		}
		s.Texture->SetResource(nullptr);

		ctx->RSSetViewports(vpCount, oldVp);
		ctx->RSSetScissorRects(scCount, oldSc);
		ctx->RSSetState(oldRs.Get());
		ctx->OMSetBlendState(oldBs.Get(), oldFactor, oldMask);
		ctx->OMSetDepthStencilState(oldDs.Get(), oldRef);
		ctx->IASetInputLayout(oldLayout.Get());
		ctx->IASetPrimitiveTopology(oldTopo);
	}
}
