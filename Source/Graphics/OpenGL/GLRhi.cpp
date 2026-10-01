#include "pch.h"
#include "Rhi.h"
#include "GLLoader.h"
#include "ShaderCross.h"

// RHI 의 OpenGL 4.5 구현 (DSA).
//  - 컨텍스트: 숨은 창 + wglCreateContextAttribsARB(4.5 core). 만든 스레드에서 현재로 둔다
//  - 좌표: glClipControl(LOWER_LEFT, ZERO_TO_ONE) + 셰이더의 -fvk-invert-y → 깊이 0..1, 텍스처 행 0 = D3D 의 위
//    (창 좌표의 y = D3D 의 행 번호와 같은 값 → 뷰포트·가위·gl_FragCoord 도 D3D 숫자 그대로, D3D 앞면(시계) = GL_CCW)
//  - 효과: ShaderCross 로 .fx → pass 마다 GLSL 프로그램. cbuffer = CPU 사본 + UBO(D3D 배치 그대로),
//    텍스처 변수 = 그 텍스처를 쓰는 결합 샘플러 유닛들, 샘플러 상태 = .fx 의 SamplerState → GL 샘플러 객체, pass 상태 = .fx 상태 블록
namespace
{
	struct GLFormat { GLenum Internal, Format, Type; };

	GLFormat FormatGL(Rhi::Format f)
	{
		switch (f)
		{
		case Rhi::Format::RGBA8_UNorm: return { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE };
		case Rhi::Format::RGBA16_Float: return { GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT };
		case Rhi::Format::RGBA32_Float: return { GL_RGBA32F, GL_RGBA, GL_FLOAT };
		case Rhi::Format::R32_Float: return { GL_R32F, GL_RED, GL_FLOAT };
		case Rhi::Format::RG16_Float: return { GL_RG16F, GL_RG, GL_HALF_FLOAT };
		case Rhi::Format::D32_Float: return { GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT };
		case Rhi::Format::D24_UNorm_S8_UInt: return { GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8 };
		default: return { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE };
		}
	}

	std::string Upper(std::string s)
	{
		for (char& c : s) c = (char)toupper((unsigned char)c);
		return s;
	}

	// "float4(1.0f, 0.5f, ...)" / "{ 0.05f, ... }" / "true" → 숫자들
	std::vector<double> Numbers(const std::string& text)
	{
		std::vector<double> out;
		const char* p = text.c_str();
		while (*p)
		{
			if ((*p >= '0' && *p <= '9') || ((*p == '-' || *p == '+' || *p == '.') && (isdigit((unsigned char)p[1]) || p[1] == '.')))
			{
				// 이름 안의 숫자(float4 의 4)는 건너뛴다
				if (p > text.c_str() && (isalpha((unsigned char)p[-1]) || p[-1] == '_'))
				{
					++p;
					continue;
				}
				char* end = nullptr;
				out.push_back(strtod(p, &end));
				p = end;
				while (*p == 'f' || *p == 'F' || *p == 'u' || *p == 'U') ++p;
				continue;
			}
			if (_strnicmp(p, "true", 4) == 0 && (p == text.c_str() || !isalnum((unsigned char)p[-1]))) { out.push_back(1); p += 4; continue; }
			if (_strnicmp(p, "false", 5) == 0 && (p == text.c_str() || !isalnum((unsigned char)p[-1]))) { out.push_back(0); p += 5; continue; }
			++p;
		}
		return out;
	}

	GLenum CompareFunc(const std::string& v, GLenum def)
	{
		const std::string u = Upper(v);
		if (u.find("LESS_EQUAL") != std::string::npos) return GL_LEQUAL;
		if (u.find("GREATER_EQUAL") != std::string::npos) return GL_GEQUAL;
		if (u.find("NOT_EQUAL") != std::string::npos) return GL_NOTEQUAL;
		if (u.find("LESS") != std::string::npos) return GL_LESS;
		if (u.find("GREATER") != std::string::npos) return GL_GREATER;
		if (u.find("EQUAL") != std::string::npos) return GL_EQUAL;
		if (u.find("ALWAYS") != std::string::npos) return GL_ALWAYS;
		if (u.find("NEVER") != std::string::npos) return GL_NEVER;
		return def;
	}

	GLenum AddressMode(const std::string& v)
	{
		const std::string u = Upper(v);
		if (u.find("MIRROR_ONCE") != std::string::npos) return GL_MIRROR_CLAMP_TO_EDGE;
		if (u.find("MIRROR") != std::string::npos) return GL_MIRRORED_REPEAT;
		if (u.find("WRAP") != std::string::npos) return GL_REPEAT;
		if (u.find("BORDER") != std::string::npos) return GL_CLAMP_TO_BORDER;
		return GL_CLAMP_TO_EDGE;
	}

	GLenum BlendFactor(const std::string& v, GLenum def)
	{
		std::string u = Upper(v);
		if (u.rfind("D3D11_BLEND_", 0) == 0) u = u.substr(12);
		if (u == "ZERO") return GL_ZERO;
		if (u == "ONE") return GL_ONE;
		if (u == "SRC_COLOR") return GL_SRC_COLOR;
		if (u == "INV_SRC_COLOR") return GL_ONE_MINUS_SRC_COLOR;
		if (u == "SRC_ALPHA") return GL_SRC_ALPHA;
		if (u == "INV_SRC_ALPHA") return GL_ONE_MINUS_SRC_ALPHA;
		if (u == "DEST_ALPHA") return GL_DST_ALPHA;
		if (u == "INV_DEST_ALPHA") return GL_ONE_MINUS_DST_ALPHA;
		if (u == "DEST_COLOR") return GL_DST_COLOR;
		if (u == "INV_DEST_COLOR") return GL_ONE_MINUS_DST_COLOR;
		if (u == "SRC_ALPHA_SAT") return GL_SRC_ALPHA_SATURATE;
		if (u == "BLEND_FACTOR") return GL_CONSTANT_COLOR;
		if (u == "INV_BLEND_FACTOR") return GL_ONE_MINUS_CONSTANT_COLOR;
		return def;
	}

	GLenum BlendOp(const std::string& v)
	{
		const std::string u = Upper(v);
		if (u.find("REV_SUBTRACT") != std::string::npos) return GL_FUNC_REVERSE_SUBTRACT;
		if (u.find("SUBTRACT") != std::string::npos) return GL_FUNC_SUBTRACT;
		if (u.find("MIN") != std::string::npos) return GL_MIN;
		if (u.find("MAX") != std::string::npos) return GL_MAX;
		return GL_FUNC_ADD;
	}

	GLenum StencilOp(const std::string& v)
	{
		const std::string u = Upper(v);
		if (u.find("INCR_SAT") != std::string::npos) return GL_INCR;
		if (u.find("DECR_SAT") != std::string::npos) return GL_DECR;
		if (u.find("INCR") != std::string::npos) return GL_INCR_WRAP;
		if (u.find("DECR") != std::string::npos) return GL_DECR_WRAP;
		if (u.find("REPLACE") != std::string::npos) return GL_REPLACE;
		if (u.find("INVERT") != std::string::npos) return GL_INVERT;
		if (u.find("ZERO") != std::string::npos) return GL_ZERO;
		return GL_KEEP;
	}

	bool IsTrue(const std::string& v) { const std::string u = Upper(v); return u == "TRUE" || u == "1"; }

	// 상태 블록 값: "blendenable[0]" 또는 "blendenable"
	const std::string* Field(const FxParser::StateBlock& b, const std::string& key)
	{
		auto it = b.Fields.find(key + "[0]");
		if (it != b.Fields.end()) return &it->second;
		it = b.Fields.find(key);
		return it != b.Fields.end() ? &it->second : nullptr;
	}

	void APIENTRY DebugCallback(GLenum, GLenum, GLuint, GLenum severity, GLsizei, const GLchar* message, const void*)
	{
		static std::set<std::string> seen;   // 같은 메시지는 한 번만 (프레임마다 수백 줄이 되지 않게)
		if ((severity == GL_DEBUG_SEVERITY_HIGH || severity == GL_DEBUG_SEVERITY_MEDIUM) && seen.size() < 200 && seen.insert(message).second)
			EditorLog::Write("OpenGL", "%s", message);
	}

	class GLBuffer : public Rhi::Buffer
	{
	public:
		GLBuffer(const Rhi::BufferDesc& d) { _desc = d; }
		~GLBuffer() override { if (Id) glDeleteBuffers(1, &Id); }
		GLuint Id = 0;
	};

	class GLTexture : public Rhi::Texture
	{
	public:
		GLTexture(const Rhi::TextureDesc& d) { _desc = d; }
		~GLTexture() override { if (Id) glDeleteTextures(1, &Id); }
		GLuint Id = 0;
		GLenum Target = GL_TEXTURE_2D;
	};

	class GLInputLayout : public Rhi::InputLayout
	{
	public:
		~GLInputLayout() override { if (Vao) glDeleteVertexArrays(1, &Vao); }
		GLuint Vao = 0;
	};

	class GLDevice;

	class GLEffect : public Rhi::Effect
	{
	public:
		struct Block { std::string Name; int Binding = 0; std::vector<uint8_t> Cpu; GLuint Ubo = 0; bool Dirty = true; const ShaderCross::UniformBlock* Info = nullptr; };
		struct Var
		{
			int BlockIndex = -1;                              // cbuffer 멤버
			const ShaderCross::UniformBlock::Member* Member = nullptr;
			std::vector<const ShaderCross::SamplerBinding*> Samplers;   // 텍스처 변수
		};
		struct PassProgram { GLuint Program = 0; GLint BaseInstance = -1; std::string Error; const FxParser::Pass* Fx = nullptr; };

		GLDevice* Device = nullptr;
		ShaderCross::EffectGlsl Src;
		std::vector<std::string> TechniqueNames;
		std::vector<std::vector<PassProgram>> Programs;
		std::vector<Block> Blocks;
		std::vector<Var> Vars;
		std::map<std::string, Rhi::VarId> VarIds;
		std::map<std::string, GLuint> SamplerObjects;     // .fx 샘플러 이름 → GL 샘플러
		std::vector<GLuint> UnitTextures;                  // 유닛 → 텍스처
		std::vector<GLuint> UnitSamplers;                  // 유닛 → 샘플러
		std::set<std::string> Reported;

		~GLEffect() override
		{
			for (auto& t : Programs)
				for (auto& p : t)
					if (p.Program) glDeleteProgram(p.Program);
			for (auto& b : Blocks)
				if (b.Ubo) glDeleteBuffers(1, &b.Ubo);
			for (auto& [n, s] : SamplerObjects)
				if (s) glDeleteSamplers(1, &s);
		}

		int FindTechnique(const std::string& name) const override
		{
			for (size_t i = 0; i < TechniqueNames.size(); ++i)
				if (TechniqueNames[i] == name) return (int)i;
			return -1;
		}

		int PassCount(int technique) const override
		{
			return technique >= 0 && technique < (int)Programs.size() ? (int)Programs[technique].size() : 0;
		}

		Rhi::VarId FindVariable(const std::string& name) override
		{
			auto it = VarIds.find(name);
			if (it != VarIds.end()) return it->second;
			Var v;
			for (size_t b = 0; b < Blocks.size() && !v.Member; ++b)
				for (const auto& m : Blocks[b].Info->Members)
					if (m.Name == name)
					{
						v.BlockIndex = (int)b;
						v.Member = &m;
						break;
					}
			if (!v.Member)
				for (const auto& [n, s] : Src.Samplers)
					if (s.Texture == name)
						v.Samplers.push_back(&s);
			if (!v.Member && v.Samplers.empty())
				return -1;   // 셰이더가 쓰지 않아 지워진 변수 (D3D 는 그대로 있어도 결과는 같다)
			Vars.push_back(v);
			return VarIds[name] = (int)Vars.size() - 1;
		}

		void Write(const Var& v, uint32_t offset, const void* data, uint32_t bytes)
		{
			Block& b = Blocks[v.BlockIndex];
			const uint32_t at = (uint32_t)v.Member->Offset + offset;
			if (at >= b.Cpu.size()) return;
			bytes = (std::min)(bytes, (uint32_t)b.Cpu.size() - at);
			memcpy(b.Cpu.data() + at, data, bytes);
			b.Dirty = true;
		}

		void SetRaw(Rhi::VarId var, const void* data, uint32_t bytes, uint32_t offset) override
		{
			if (var < 0 || !Vars[var].Member) return;
			Write(Vars[var], offset, data, bytes);
		}

		void WriteMatrix(const Var& v, uint32_t offset, const float m[16])
		{
			if (v.Member->Transpose)
			{
				float t[16];
				for (int r = 0; r < 4; ++r)
					for (int c = 0; c < 4; ++c)
						t[c * 4 + r] = m[r * 4 + c];
				Write(v, offset, t, 64);
			}
			else
				Write(v, offset, m, 64);
		}

		void SetMatrix(Rhi::VarId var, const float m[16]) override
		{
			if (var < 0 || !Vars[var].Member) return;
			WriteMatrix(Vars[var], 0, m);
		}

		void SetMatrixArray(Rhi::VarId var, const float* m, uint32_t first, uint32_t count) override
		{
			if (var < 0 || !Vars[var].Member) return;
			const uint32_t stride = Vars[var].Member->ArrayStride ? Vars[var].Member->ArrayStride : 64;
			for (uint32_t i = 0; i < count; ++i)
				WriteMatrix(Vars[var], (first + i) * stride, m + i * 16);
		}

		void SetTexture(Rhi::VarId var, Rhi::Texture* texture, uint32_t arrayIndex) override
		{
			if (var < 0) return;
			const GLuint id = texture ? static_cast<GLTexture*>(texture)->Id : 0;
			for (const ShaderCross::SamplerBinding* s : Vars[var].Samplers)
				if ((int)arrayIndex < s->Count)
					UnitTextures[s->Unit + arrayIndex] = id;
		}

		void Apply(int technique, int pass) override;
		void ApplyStates(const FxParser::Pass& p);
	};

	class GLDevice : public Rhi::Device
	{
	public:
		HWND Wnd = nullptr;
		HDC Dc = nullptr;
		HGLRC Rc = nullptr;
		GLuint Fbo = 0, ClearFbo = 0;
		GLenum Mode = GL_TRIANGLES;
		GLInputLayout* Layout = nullptr;
		struct VB { GLuint Id = 0; uint32_t Stride = 0, Offset = 0; };
		VB Vbs[16];
		GLuint Ib = 0;
		bool Index32 = true;
		GLint BaseInstanceLoc = -1;
		GLuint CurrentProgram = 0;

		~GLDevice() override
		{
			if (Rc)
			{
				if (Fbo) glDeleteFramebuffers(1, &Fbo);
				if (ClearFbo) glDeleteFramebuffers(1, &ClearFbo);
				::wglMakeCurrent(nullptr, nullptr);
				::wglDeleteContext(Rc);
			}
			if (Dc) ::ReleaseDC(Wnd, Dc);
			if (Wnd) ::DestroyWindow(Wnd);
		}

		bool Init(std::string& error)
		{
			static const wchar_t* cls = L"NovaGLRhiWindow";
			static bool registered = false;
			HINSTANCE inst = ::GetModuleHandleW(nullptr);
			if (!registered)
			{
				WNDCLASSW wc = {};
				wc.style = CS_OWNDC;
				wc.lpfnWndProc = ::DefWindowProcW;
				wc.hInstance = inst;
				wc.lpszClassName = cls;
				::RegisterClassW(&wc);
				registered = true;
			}
			Wnd = ::CreateWindowExW(0, cls, L"NOVA OpenGL", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, inst, nullptr);
			if (!Wnd) { error = "cannot create the OpenGL window"; return false; }
			Dc = ::GetDC(Wnd);
			PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1 };
			pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
			pfd.iPixelType = PFD_TYPE_RGBA;
			pfd.cColorBits = 32;
			pfd.cDepthBits = 24;
			pfd.cStencilBits = 8;
			const int pf = ::ChoosePixelFormat(Dc, &pfd);
			if (!pf || !::SetPixelFormat(Dc, pf, &pfd)) { error = "no OpenGL pixel format"; return false; }
			HGLRC temp = ::wglCreateContext(Dc);
			if (!temp || !::wglMakeCurrent(Dc, temp)) { error = "wglCreateContext failed (no OpenGL driver?)"; return false; }
			typedef HGLRC(WINAPI * CreateAttribs)(HDC, HGLRC, const int*);
			auto createAttribs = reinterpret_cast<CreateAttribs>(::wglGetProcAddress("wglCreateContextAttribsARB"));
			if (!createAttribs)
			{
				::wglMakeCurrent(nullptr, nullptr);
				::wglDeleteContext(temp);
				error = "WGL_ARB_create_context not supported";
				return false;
			}
			const int attribs[] = { WGL_CONTEXT_MAJOR_VERSION_ARB, 4, WGL_CONTEXT_MINOR_VERSION_ARB, 5, WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
				WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB, 0 };
			Rc = createAttribs(Dc, nullptr, attribs);
			::wglMakeCurrent(nullptr, nullptr);
			::wglDeleteContext(temp);
			if (!Rc || !::wglMakeCurrent(Dc, Rc)) { error = "OpenGL 4.5 core context not available"; Rc = nullptr; return false; }
			std::string missing;
			if (!GLLoader::Load(missing)) { error = "OpenGL functions missing: " + missing; return false; }
			glEnable(GL_DEBUG_OUTPUT);
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
			glDebugMessageCallback(DebugCallback, nullptr);
			glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
			glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);   // D3D 큐브 샘플링은 면 경계가 이어진다
			glCreateFramebuffers(1, &Fbo);
			glCreateFramebuffers(1, &ClearFbo);
			ResetState();
			return true;
		}

		GraphicsAPI GetAPI() const override { return GraphicsAPI::OpenGL; }

		std::string Description() const override
		{
			auto s = [](GLenum e) { const GLubyte* v = glGetString(e); return v ? std::string((const char*)v) : std::string(); };
			return "OpenGL " + s(GL_VERSION) + " (" + s(GL_RENDERER) + ")";
		}

		std::unique_ptr<Rhi::Buffer> CreateBuffer(const Rhi::BufferDesc& desc, const void* initialData) override
		{
			auto b = std::make_unique<GLBuffer>(desc);
			glCreateBuffers(1, &b->Id);
			glNamedBufferStorage(b->Id, desc.Size, initialData, GL_DYNAMIC_STORAGE_BIT);
			return b;
		}

		void UpdateBuffer(Rhi::Buffer* buffer, const void* data, uint32_t bytes) override
		{
			glNamedBufferSubData(static_cast<GLBuffer*>(buffer)->Id, 0, bytes, data);
		}

		std::unique_ptr<Rhi::Texture> CreateTexture(const Rhi::TextureDesc& desc, const Rhi::SubresourceData* initialData) override
		{
			auto t = std::make_unique<GLTexture>(desc);
			const GLFormat f = FormatGL(desc.Format);
			const uint32_t slices = desc.Type == Rhi::TextureType::Cube ? 6 : desc.ArraySize;
			t->Target = desc.Type == Rhi::TextureType::Cube ? GL_TEXTURE_CUBE_MAP : desc.Type == Rhi::TextureType::Tex2DArray ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
			glCreateTextures(t->Target, 1, &t->Id);
			if (t->Target == GL_TEXTURE_2D_ARRAY)
				glTextureStorage3D(t->Id, desc.MipLevels, f.Internal, desc.Width, desc.Height, slices);
			else
				glTextureStorage2D(t->Id, desc.MipLevels, f.Internal, desc.Width, desc.Height);
			if (initialData)
			{
				const uint32_t bpp = Rhi::BytesPerPixel(desc.Format);
				glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
				for (uint32_t s = 0; s < slices; ++s)
					for (uint32_t m = 0; m < desc.MipLevels; ++m)
					{
						const Rhi::SubresourceData& d = initialData[s * desc.MipLevels + m];
						const GLsizei w = (std::max)(1u, desc.Width >> m), h = (std::max)(1u, desc.Height >> m);
						glPixelStorei(GL_UNPACK_ROW_LENGTH, d.RowPitch ? d.RowPitch / bpp : 0);
						if (t->Target == GL_TEXTURE_2D)
							glTextureSubImage2D(t->Id, m, 0, 0, w, h, f.Format, f.Type, d.Data);
						else   // 큐브 면·배열 조각 = z
							glTextureSubImage3D(t->Id, m, 0, 0, s, w, h, 1, f.Format, f.Type, d.Data);
					}
				glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
			}
			return t;
		}

		std::unique_ptr<Rhi::Effect> LoadEffect(const std::wstring& fxPath, std::string& error) override;

		std::unique_ptr<Rhi::InputLayout> CreateInputLayout(const Rhi::VertexElement* elements, uint32_t count, Rhi::Effect* effect, int technique, int pass, std::string& error) override
		{
			auto* fx = static_cast<GLEffect*>(effect);
			if (technique < 0 || technique >= (int)fx->Programs.size() || pass < 0 || pass >= (int)fx->Programs[technique].size())
			{
				error = "bad technique/pass";
				return nullptr;
			}
			// pass 의 정점 입력 (의미 → location)
			const ShaderCross::PassGlsl* pg = nullptr;
			int index = 0;
			for (int t = 0; t < technique; ++t) index += (int)fx->Programs[t].size();
			pg = &fx->Src.Passes[index + pass];
			auto l = std::make_unique<GLInputLayout>();
			glCreateVertexArrays(1, &l->Vao);
			std::set<std::string> provided;
			for (uint32_t i = 0; i < count; ++i)
			{
				const Rhi::VertexElement& e = elements[i];
				const std::string sem = Upper(e.Semantic) + std::to_string(e.SemanticIndex);
				provided.insert(sem);
				int loc = -1;
				for (const auto& [s, l2] : pg->VertexInputs)
					if (s == sem) loc = l2;
				if (loc < 0) continue;   // 셰이더가 쓰지 않는 요소 (D3D 와 같이 무시)
				glEnableVertexArrayAttrib(l->Vao, loc);
				switch (e.Format)
				{
				case Rhi::VertexFormat::Float1: glVertexArrayAttribFormat(l->Vao, loc, 1, GL_FLOAT, GL_FALSE, e.Offset); break;
				case Rhi::VertexFormat::Float2: glVertexArrayAttribFormat(l->Vao, loc, 2, GL_FLOAT, GL_FALSE, e.Offset); break;
				case Rhi::VertexFormat::Float3: glVertexArrayAttribFormat(l->Vao, loc, 3, GL_FLOAT, GL_FALSE, e.Offset); break;
				case Rhi::VertexFormat::Float4: glVertexArrayAttribFormat(l->Vao, loc, 4, GL_FLOAT, GL_FALSE, e.Offset); break;
				case Rhi::VertexFormat::UByte4_UNorm: glVertexArrayAttribFormat(l->Vao, loc, 4, GL_UNSIGNED_BYTE, GL_TRUE, e.Offset); break;
				case Rhi::VertexFormat::UInt4: glVertexArrayAttribIFormat(l->Vao, loc, 4, GL_UNSIGNED_INT, e.Offset); break;
				}
				glVertexArrayAttribBinding(l->Vao, loc, e.Slot);
				glVertexArrayBindingDivisor(l->Vao, e.Slot, e.PerInstance ? 1 : 0);
			}
			for (const auto& [s, loc] : pg->VertexInputs)
				if (!provided.count(s))
				{
					error = "vertex input " + s + " is not provided by the layout";
					return nullptr;
				}
			return l;
		}

		void ResetState() override
		{
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LESS);
			glDepthMask(GL_TRUE);
			glDisable(GL_STENCIL_TEST);
			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);
			glFrontFace(GL_CCW);   // = D3D 기본 (시계 방향이 앞면) — 파일 맨 위 설명
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
			glDisable(GL_POLYGON_OFFSET_FILL);
			glDisable(GL_DEPTH_CLAMP);
			glDisable(GL_SCISSOR_TEST);
			glDisable(GL_BLEND);
			glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		}

		void SetRenderTargets(Rhi::Texture* const* colors, uint32_t count, Rhi::Texture* depth, uint32_t depthSlice) override
		{
			GLenum bufs[8];
			for (uint32_t i = 0; i < 8; ++i)
			{
				GLuint id = i < count && colors[i] ? static_cast<GLTexture*>(colors[i])->Id : 0;
				glNamedFramebufferTexture(Fbo, GL_COLOR_ATTACHMENT0 + i, id, 0);
				bufs[i] = id ? GL_COLOR_ATTACHMENT0 + i : GL_NONE;
			}
			glNamedFramebufferDrawBuffers(Fbo, 8, bufs);
			const bool stencil = depth && depth->Desc().Format == Rhi::Format::D24_UNorm_S8_UInt;
			glNamedFramebufferTexture(Fbo, GL_DEPTH_STENCIL_ATTACHMENT, 0, 0);
			auto* d = static_cast<GLTexture*>(depth);
			const GLenum att = stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
			if (d && d->Target != GL_TEXTURE_2D)
				glNamedFramebufferTextureLayer(Fbo, att, d->Id, 0, depthSlice);   // 배열·큐브: 조각 하나
			else
				glNamedFramebufferTexture(Fbo, att, d ? d->Id : 0, 0);
			glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
		}

		void SetViewport(float x, float y, float width, float height) override
		{
			glViewport((GLint)x, (GLint)y, (GLsizei)width, (GLsizei)height);
		}

		void ClearColor(Rhi::Texture* target, const float rgba[4]) override
		{
			// D3D Clear 는 쓰기 마스크·가위와 상관없다
			GLboolean mask[4];
			glGetBooleanv(GL_COLOR_WRITEMASK, mask);
			const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glDisable(GL_SCISSOR_TEST);
			glNamedFramebufferTexture(ClearFbo, GL_COLOR_ATTACHMENT0, static_cast<GLTexture*>(target)->Id, 0);
			glClearNamedFramebufferfv(ClearFbo, GL_COLOR, 0, rgba);
			glNamedFramebufferTexture(ClearFbo, GL_COLOR_ATTACHMENT0, 0, 0);
			glColorMask(mask[0], mask[1], mask[2], mask[3]);
			if (scissor) glEnable(GL_SCISSOR_TEST);
		}

		void ClearDepth(Rhi::Texture* target, float depth) override
		{
			GLboolean mask;
			glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);
			const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
			glDepthMask(GL_TRUE);
			glDisable(GL_SCISSOR_TEST);
			const bool stencil = target->Desc().Format == Rhi::Format::D24_UNorm_S8_UInt;
			const GLenum att = stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
			glNamedFramebufferTexture(ClearFbo, att, static_cast<GLTexture*>(target)->Id, 0);
			if (stencil)
				glClearNamedFramebufferfi(ClearFbo, GL_DEPTH_STENCIL, 0, depth, 0);
			else
				glClearNamedFramebufferfv(ClearFbo, GL_DEPTH, 0, &depth);
			glNamedFramebufferTexture(ClearFbo, att, 0, 0);
			glDepthMask(mask);
			if (scissor) glEnable(GL_SCISSOR_TEST);
		}

		void SetInputLayout(Rhi::InputLayout* layout) override { Layout = static_cast<GLInputLayout*>(layout); }

		void SetVertexBuffer(uint32_t slot, Rhi::Buffer* buffer, uint32_t stride, uint32_t offset) override
		{
			if (slot < 16) Vbs[slot] = { buffer ? static_cast<GLBuffer*>(buffer)->Id : 0, stride, offset };
		}

		void SetIndexBuffer(Rhi::Buffer* buffer, bool use32Bit) override
		{
			Ib = buffer ? static_cast<GLBuffer*>(buffer)->Id : 0;
			Index32 = use32Bit;
		}

		void SetTopology(Rhi::Topology topology) override
		{
			static const GLenum map[] = { GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_LINES, GL_LINE_STRIP, GL_POINTS };
			Mode = map[(int)topology];
		}

		bool BindGeometry()
		{
			if (!Layout || !CurrentProgram) return false;
			for (GLuint i = 0; i < 16; ++i)
				if (Vbs[i].Id)
					glVertexArrayVertexBuffer(Layout->Vao, i, Vbs[i].Id, Vbs[i].Offset, Vbs[i].Stride);
			glVertexArrayElementBuffer(Layout->Vao, Ib);
			glBindVertexArray(Layout->Vao);
			return true;
		}

		// SPIRV-Cross 는 SV_InstanceID 를 gl_InstanceID + SPIRV_Cross_BaseInstance 로 만든다.
		// D3D 의 SV_InstanceID 는 StartInstanceLocation 을 더하지 않으므로 늘 0 을 넣는다
		void SetBaseInstance()
		{
			if (BaseInstanceLoc >= 0) glProgramUniform1i(CurrentProgram, BaseInstanceLoc, 0);
		}

		void Draw(uint32_t vertexCount, uint32_t startVertex) override
		{
			if (!BindGeometry()) return;
			SetBaseInstance();
			glDrawArraysInstancedBaseInstance(Mode, startVertex, vertexCount, 1, 0);
		}

		void DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex) override
		{
			DrawIndexedInstanced(indexCount, 1, startIndex, baseVertex, 0);
		}

		void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance) override
		{
			if (!BindGeometry()) return;
			SetBaseInstance();
			const size_t size = Index32 ? 4 : 2;
			glDrawElementsInstancedBaseVertexBaseInstance(Mode, indexCount, Index32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, (const void*)(startIndex * size),
				instanceCount, baseVertex, startInstance);
		}

		bool ReadPixels(Rhi::Texture* texture, std::vector<uint8_t>& rgba, std::string& error) override
		{
			auto* t = static_cast<GLTexture*>(texture);
			const uint32_t w = t->Desc().Width, h = t->Desc().Height;
			rgba.resize((size_t)w * h * 4);
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			if (Rhi::IsDepth(t->Desc().Format))
			{
				std::vector<float> d((size_t)w * h);
				glGetTextureImage(t->Id, 0, GL_DEPTH_COMPONENT, GL_FLOAT, (GLsizei)(d.size() * 4), d.data());
				for (size_t i = 0; i < d.size(); ++i)
				{
					const uint8_t v = (uint8_t)std::clamp(d[i] * 255.0f + 0.5f, 0.0f, 255.0f);
					rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = v;
					rgba[i * 4 + 3] = 255;
				}
			}
			else
				glGetTextureImage(t->Id, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)rgba.size(), rgba.data());
			if (glGetError() != GL_NO_ERROR)
			{
				error = "glGetTextureImage failed";
				return false;
			}
			return true;
		}

		void Finish() override { glFinish(); }
	};

	GLuint CompileStage(GLenum type, const std::string& src, std::string& error)
	{
		GLuint s = glCreateShader(type);
		const char* text = src.c_str();
		glShaderSource(s, 1, &text, nullptr);
		glCompileShader(s);
		GLint ok = 0;
		glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
		if (!ok)
		{
			GLint len = 0;
			glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
			std::string log((std::max)(len, 1), '\0');
			glGetShaderInfoLog(s, len, nullptr, log.data());
			error = log.c_str();
			glDeleteShader(s);
			return 0;
		}
		return s;
	}

	GLuint MakeSampler(const FxParser::StateBlock* b)
	{
		GLuint s = 0;
		glCreateSamplers(1, &s);
		// D3D11 기본값: MIN_MAG_MIP_LINEAR, CLAMP, 테두리 (1,1,1,1), 비교 NEVER
		std::string filter = b ? (Field(*b, "filter") ? Upper(*Field(*b, "filter")) : "MIN_MAG_MIP_LINEAR") : "MIN_MAG_MIP_LINEAR";
		bool comparison = filter.find("COMPARISON_") != std::string::npos;
		bool minLinear = true, magLinear = true, mipLinear = true;
		float aniso = 1.0f;
		if (filter.find("ANISOTROPIC") != std::string::npos)
		{
			aniso = 16.0f;
			if (b && Field(*b, "maxanisotropy")) aniso = (float)atof(Field(*b, "maxanisotropy")->c_str());
		}
		else
		{
			// MIN_MAG_LINEAR_MIP_POINT, MIN_POINT_MAG_MIP_LINEAR … → 이름들 뒤의 LINEAR/POINT 가 그 이름들에 적용
			std::vector<std::string> pending;
			size_t p = 0;
			while (p <= filter.size())
			{
				size_t e = filter.find('_', p);
				if (e == std::string::npos) e = filter.size();
				const std::string tok = filter.substr(p, e - p);
				if (tok == "MIN" || tok == "MAG" || tok == "MIP") pending.push_back(tok);
				else if (tok == "LINEAR" || tok == "POINT")
				{
					for (const std::string& n : pending)
					{
						if (n == "MIN") minLinear = tok == "LINEAR";
						if (n == "MAG") magLinear = tok == "LINEAR";
						if (n == "MIP") mipLinear = tok == "LINEAR";
					}
					pending.clear();
				}
				p = e + 1;
			}
		}
		const GLenum minF = minLinear ? (mipLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST) : (mipLinear ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST);
		glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, minF);
		glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, magLinear ? GL_LINEAR : GL_NEAREST);
		if (aniso > 1.0f) glSamplerParameterf(s, GL_TEXTURE_MAX_ANISOTROPY, aniso);
		glSamplerParameteri(s, GL_TEXTURE_WRAP_S, b && Field(*b, "addressu") ? AddressMode(*Field(*b, "addressu")) : GL_CLAMP_TO_EDGE);
		glSamplerParameteri(s, GL_TEXTURE_WRAP_T, b && Field(*b, "addressv") ? AddressMode(*Field(*b, "addressv")) : GL_CLAMP_TO_EDGE);
		glSamplerParameteri(s, GL_TEXTURE_WRAP_R, b && Field(*b, "addressw") ? AddressMode(*Field(*b, "addressw")) : GL_CLAMP_TO_EDGE);
		float border[4] = { 1, 1, 1, 1 };
		if (b && Field(*b, "bordercolor"))
		{
			const auto n = Numbers(*Field(*b, "bordercolor"));
			for (size_t i = 0; i < 4 && i < n.size(); ++i) border[i] = (float)n[i];
		}
		glSamplerParameterfv(s, GL_TEXTURE_BORDER_COLOR, border);
		if (b && Field(*b, "miplodbias")) glSamplerParameterf(s, GL_TEXTURE_LOD_BIAS, (float)atof(Field(*b, "miplodbias")->c_str()));
		if (b && Field(*b, "minlod")) glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, (float)atof(Field(*b, "minlod")->c_str()));
		if (b && Field(*b, "maxlod")) glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, (float)atof(Field(*b, "maxlod")->c_str()));
		if (comparison)
		{
			glSamplerParameteri(s, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
			glSamplerParameteri(s, GL_TEXTURE_COMPARE_FUNC, b && Field(*b, "comparisonfunc") ? CompareFunc(*Field(*b, "comparisonfunc"), GL_NEVER) : GL_NEVER);
		}
		return s;
	}

	std::unique_ptr<Rhi::Effect> GLDevice::LoadEffect(const std::wstring& fxPath, std::string& error)
	{
		auto e = std::make_unique<GLEffect>();
		e->Device = this;
		if (!ShaderCross::CompileEffect(fxPath, e->Src) || !e->Src.Error.empty())
		{
			error = e->Src.Error.empty() ? "shader conversion failed" : e->Src.Error;
			return nullptr;
		}
		// pass 마다 프로그램 (변환·링크 실패는 그 pass 만 못 쓴다)
		size_t index = 0;
		for (const FxParser::Technique& tech : e->Src.Fx.Techniques)
		{
			e->TechniqueNames.push_back(tech.Name);
			std::vector<GLEffect::PassProgram> passes;
			for (const FxParser::Pass& pass : tech.Passes)
			{
				const ShaderCross::PassGlsl& pg = e->Src.Passes[index++];
				GLEffect::PassProgram pp;
				pp.Fx = &pass;
				pp.Error = pg.Error;
				if (pp.Error.empty())
				{
					static const GLenum types[] = { GL_VERTEX_SHADER, GL_TESS_CONTROL_SHADER, GL_TESS_EVALUATION_SHADER, GL_GEOMETRY_SHADER, GL_FRAGMENT_SHADER, GL_COMPUTE_SHADER };
					std::vector<GLuint> shaders;
					for (const ShaderCross::StageGlsl& sg : pg.Stages)
					{
						std::string err;
						GLuint s = CompileStage(types[(int)sg.StageType], sg.Glsl, err);
						if (!s)
						{
							pp.Error = std::string(FxParser::StageName(sg.StageType)) + " " + sg.Entry + ": GLSL: " + err;
							break;
						}
						shaders.push_back(s);
					}
					if (pp.Error.empty())
					{
						pp.Program = glCreateProgram();
						for (GLuint s : shaders) glAttachShader(pp.Program, s);
						glLinkProgram(pp.Program);
						GLint ok = 0;
						glGetProgramiv(pp.Program, GL_LINK_STATUS, &ok);
						if (!ok)
						{
							GLint len = 0;
							glGetProgramiv(pp.Program, GL_INFO_LOG_LENGTH, &len);
							std::string log((std::max)(len, 1), '\0');
							glGetProgramInfoLog(pp.Program, len, nullptr, log.data());
							pp.Error = "link: " + std::string(log.c_str());
							glDeleteProgram(pp.Program);
							pp.Program = 0;
						}
						else
							pp.BaseInstance = glGetUniformLocation(pp.Program, "SPIRV_Cross_BaseInstance");
					}
					for (GLuint s : shaders)
					{
						if (pp.Program) glDetachShader(pp.Program, s);
						glDeleteShader(s);
					}
				}
				if (!pp.Error.empty())
					EditorLog::Write("OpenGL", "%s %s/%s: %s", wstring_to_string(std::filesystem::path(fxPath).filename().wstring()).c_str(), tech.Name.c_str(), pass.Name.c_str(), pp.Error.c_str());
				passes.push_back(pp);
			}
			e->Programs.push_back(std::move(passes));
		}
		// cbuffer: CPU 사본(초기값 포함) + UBO
		for (const auto& [name, info] : e->Src.Blocks)
		{
			GLEffect::Block b;
			b.Name = name;
			b.Binding = info.Binding;
			b.Info = &info;
			b.Cpu.assign((info.Size + 15) / 16 * 16, 0);
			for (const auto& m : info.Members)
			{
				auto d = e->Src.Fx.Defaults.find(m.Name);
				if (d == e->Src.Fx.Defaults.end() || m.Struct) continue;
				const std::vector<double> nums = Numbers(d->second);
				const int comps = (std::max)(1, m.Rows * (std::max)(1, m.Columns));
				for (size_t i = 0; i < nums.size(); ++i)
				{
					const size_t elem = i / comps, c = i % comps;
					if (m.ArrayCount ? elem >= (size_t)m.ArrayCount : elem > 0) break;
					const size_t at = m.Offset + elem * m.ArrayStride + c * 4;
					if (at + 4 > b.Cpu.size()) break;
					if (m.Integer) { const int v = (int)nums[i]; memcpy(b.Cpu.data() + at, &v, 4); }
					else { const float v = (float)nums[i]; memcpy(b.Cpu.data() + at, &v, 4); }
				}
			}
			glCreateBuffers(1, &b.Ubo);
			glNamedBufferStorage(b.Ubo, b.Cpu.size(), b.Cpu.data(), GL_DYNAMIC_STORAGE_BIT);
			b.Dirty = false;
			e->Blocks.push_back(std::move(b));
		}
		// 샘플러: .fx 의 샘플러 상태 → GL 샘플러 객체, 유닛 표
		int units = 0;
		for (const auto& [n, s] : e->Src.Samplers)
		{
			units = (std::max)(units, s.Unit + s.Count);
			if (s.Sampler != "nosampler" && !e->SamplerObjects.count(s.Sampler))
			{
				auto st = e->Src.Fx.States.find(s.Sampler);
				e->SamplerObjects[s.Sampler] = MakeSampler(st != e->Src.Fx.States.end() ? &st->second : nullptr);
			}
		}
		e->UnitTextures.assign(units, 0);
		e->UnitSamplers.assign(units, 0);
		for (const auto& [n, s] : e->Src.Samplers)
			for (int i = 0; i < s.Count; ++i)
				e->UnitSamplers[s.Unit + i] = s.Sampler == "nosampler" ? 0 : e->SamplerObjects[s.Sampler];
		return e;
	}

	void GLEffect::Apply(int technique, int pass)
	{
		if (technique < 0 || technique >= (int)Programs.size() || pass < 0 || pass >= (int)Programs[technique].size())
			return;
		const PassProgram& pp = Programs[technique][pass];
		if (!pp.Program)
		{
			if (Reported.insert(TechniqueNames[technique] + "/" + std::to_string(pass)).second)
				EditorLog::Write("OpenGL", "pass %s/%d is not available: %s", TechniqueNames[technique].c_str(), pass, pp.Error.c_str());
			Device->CurrentProgram = 0;
			return;
		}
		glUseProgram(pp.Program);
		Device->CurrentProgram = pp.Program;
		Device->BaseInstanceLoc = pp.BaseInstance;
		for (Block& b : Blocks)
		{
			if (b.Dirty)
			{
				glNamedBufferSubData(b.Ubo, 0, b.Cpu.size(), b.Cpu.data());
				b.Dirty = false;
			}
			glBindBufferBase(GL_UNIFORM_BUFFER, b.Binding, b.Ubo);
		}
		for (size_t u = 0; u < UnitTextures.size(); ++u)
		{
			glBindTextureUnit((GLuint)u, UnitTextures[u]);
			glBindSampler((GLuint)u, UnitTextures[u] ? UnitSamplers[u] : 0);   // 빈 유닛 + 비교 샘플러 = 드라이버 경고
		}
		ApplyStates(*pp.Fx);
	}

	// pass 가 정한 상태만 바꾼다 (Effects11 과 같이 정하지 않은 상태는 그대로)
	void GLEffect::ApplyStates(const FxParser::Pass& p)
	{
		auto find = [&](const std::string& name) -> const FxParser::StateBlock* {
			if (name.empty()) return nullptr;
			auto it = Src.Fx.States.find(name);
			return it != Src.Fx.States.end() ? &it->second : nullptr;
		};
		if (const FxParser::StateBlock* ds = find(p.DepthStencilState))
		{
			const std::string* en = Field(*ds, "depthenable");
			if (!en || IsTrue(*en)) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
			const std::string* wm = Field(*ds, "depthwritemask");
			glDepthMask(wm && Upper(*wm).find("ZERO") != std::string::npos ? GL_FALSE : GL_TRUE);
			const std::string* fn = Field(*ds, "depthfunc");
			glDepthFunc(fn ? CompareFunc(*fn, GL_LESS) : GL_LESS);
			const std::string* se = Field(*ds, "stencilenable");
			if (se && IsTrue(*se))
			{
				glEnable(GL_STENCIL_TEST);
				const GLuint readMask = Field(*ds, "stencilreadmask") ? (GLuint)strtoul(Field(*ds, "stencilreadmask")->c_str(), nullptr, 0) : 0xFF;
				const GLuint writeMask = Field(*ds, "stencilwritemask") ? (GLuint)strtoul(Field(*ds, "stencilwritemask")->c_str(), nullptr, 0) : 0xFF;
				glStencilMask(writeMask);
				auto face = [&](GLenum f, const char* prefix) {
					const std::string pre = prefix;
					const std::string* func = Field(*ds, pre + "stencilfunc");
					const std::string* fail = Field(*ds, pre + "stencilfailop");
					const std::string* dfail = Field(*ds, pre + "stencildepthfailop");
					const std::string* pass2 = Field(*ds, pre + "stencilpassop");
					glStencilFuncSeparate(f, func ? CompareFunc(*func, GL_ALWAYS) : GL_ALWAYS, p.StencilRef, readMask);
					glStencilOpSeparate(f, fail ? StencilOp(*fail) : GL_KEEP, dfail ? StencilOp(*dfail) : GL_KEEP, pass2 ? StencilOp(*pass2) : GL_KEEP);
				};
				face(GL_FRONT, "frontface");
				face(GL_BACK, "backface");
			}
			else
				glDisable(GL_STENCIL_TEST);
		}
		if (const FxParser::StateBlock* rs = find(p.RasterizerState))
		{
			const std::string* fm = Field(*rs, "fillmode");
			glPolygonMode(GL_FRONT_AND_BACK, fm && Upper(*fm).find("WIREFRAME") != std::string::npos ? GL_LINE : GL_FILL);
			const std::string* cm = Field(*rs, "cullmode");
			const std::string cull = cm ? Upper(*cm) : "BACK";
			if (cull.find("NONE") != std::string::npos) glDisable(GL_CULL_FACE);
			else
			{
				glEnable(GL_CULL_FACE);
				glCullFace(cull.find("FRONT") != std::string::npos ? GL_FRONT : GL_BACK);
			}
			const std::string* ccw = Field(*rs, "frontcounterclockwise");
			glFrontFace(ccw && IsTrue(*ccw) ? GL_CW : GL_CCW);
			const std::string* bias = Field(*rs, "depthbias");
			const std::string* slope = Field(*rs, "slopescaleddepthbias");
			const float units = bias ? (float)atof(bias->c_str()) : 0.0f, factor = slope ? (float)atof(slope->c_str()) : 0.0f;
			if (units != 0.0f || factor != 0.0f)
			{
				glEnable(GL_POLYGON_OFFSET_FILL);
				glPolygonOffset(factor, units);
			}
			else
				glDisable(GL_POLYGON_OFFSET_FILL);
			const std::string* clip = Field(*rs, "depthclipenable");
			if (clip && !IsTrue(*clip)) glEnable(GL_DEPTH_CLAMP); else glDisable(GL_DEPTH_CLAMP);
			const std::string* sc = Field(*rs, "scissorenable");
			if (sc && IsTrue(*sc)) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
		}
		if (const FxParser::StateBlock* bs = find(p.BlendState))
		{
			const std::string* en = Field(*bs, "blendenable");
			if (en && IsTrue(*en))
			{
				glEnable(GL_BLEND);
				const std::string* src = Field(*bs, "srcblend");
				const std::string* dst = Field(*bs, "destblend");
				const std::string* op = Field(*bs, "blendop");
				const std::string* srcA = Field(*bs, "srcblendalpha");
				const std::string* dstA = Field(*bs, "destblendalpha");
				const std::string* opA = Field(*bs, "blendopalpha");
				glBlendFuncSeparate(src ? BlendFactor(*src, GL_ONE) : GL_ONE, dst ? BlendFactor(*dst, GL_ZERO) : GL_ZERO,
					srcA ? BlendFactor(*srcA, GL_ONE) : GL_ONE, dstA ? BlendFactor(*dstA, GL_ZERO) : GL_ZERO);
				glBlendEquationSeparate(op ? BlendOp(*op) : GL_FUNC_ADD, opA ? BlendOp(*opA) : GL_FUNC_ADD);
				glBlendColor(p.BlendFactor[0], p.BlendFactor[1], p.BlendFactor[2], p.BlendFactor[3]);
			}
			else
				glDisable(GL_BLEND);
			const std::string* wm = Field(*bs, "rendertargetwritemask");
			const unsigned mask = wm ? (unsigned)strtoul(wm->c_str(), nullptr, 0) : 0x0F;
			glColorMask((mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0, (mask & 8) != 0);
			const std::string* a2c = Field(*bs, "alphatocoverageenable");
			if (a2c && IsTrue(*a2c)) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE); else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
		}
	}
}

std::unique_ptr<Rhi::Device> CreateGLRhiDevice(std::string& error)
{
	auto d = std::make_unique<GLDevice>();
	if (!d->Init(error))
		return nullptr;
	return d;
}
