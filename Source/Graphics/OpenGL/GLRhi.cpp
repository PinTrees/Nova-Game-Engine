#include "pch.h"
#include "Rhi.h"
#include "GLLoader.h"
#include "ShaderCross.h"
#include "GLState.h"
#include "GLContext.h"
#include "GLShared.h"

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

	class GLBuffer : public Rhi::Buffer
	{
	public:
		GLBuffer(const Rhi::BufferDesc& d) { _desc = d; }
		~GLBuffer() override { if (Id) { glDeleteBuffers(1, &Id); GLState::InvalidateBindings(); } }
		GLuint Id = 0;
	};

	class GLTexture : public Rhi::Texture
	{
	public:
		GLTexture(const Rhi::TextureDesc& d) { _desc = d; }
		~GLTexture() override { if (Id) { glDeleteTextures(1, &Id); GLState::InvalidateBindings(); } }
		GLuint Id = 0;
		GLenum Target = GL_TEXTURE_2D;
	};

	class GLInputLayout : public Rhi::InputLayout
	{
	public:
		~GLInputLayout() override { if (Vao) { glDeleteVertexArrays(1, &Vao); GLState::InvalidateBindings(); } }
		GLuint Vao = 0;
	};

	class GLDevice;

	class GLEffect : public Rhi::Effect
	{
	public:
		// Cpu = cbuffer 사본. 바뀐 바이트 범위 [DirtyLo, DirtyHi) 만 Apply 때 올린다 (예전엔 값 하나가 바뀌어도 블록 전체 — 본 행렬 16 KB 등)
		struct Block { std::string Name; int Binding = 0; std::vector<uint8_t> Cpu; GLuint Ubo = 0; bool Dirty = true; uint32_t DirtyLo = 0, DirtyHi = 0; const ShaderCross::UniformBlock* Info = nullptr; };
		struct Var
		{
			int BlockIndex = -1;                              // cbuffer 멤버
			const ShaderCross::UniformBlock::Member* Member = nullptr;
			std::vector<const ShaderCross::SamplerBinding*> Samplers;   // 텍스처 변수
		};
		struct PassProgram
		{
			GLuint Program = 0;
			std::string Error;
			const FxParser::Pass* Fx = nullptr;
			GLInputSignature Signature;                   // FxPass::GetDesc → Gfx CreateInputLayout
			bool StatesMade = false;                      // Gfx 컨텍스트로 보낼 상태 객체 (처음 Apply 때 만듦)
			ComPtr<GfxRasterizerState> Rs;
			ComPtr<GfxBlendState> Bs;
			ComPtr<GfxDepthStencilState> Ds;
		};

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
		std::vector<ComPtr<GfxShaderResourceView>> UnitViews;   // 유닛에 넣은 Gfx 뷰 (Effects11 처럼 묶인 동안 잡아 둔다)
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
			GLState::InvalidateBindings();
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

		// 변수 안 offset 에 쓰기. 변수 크기(배열이면 전체)와 블록 끝을 넘지 않는다 (Effects11 과 같이)
		void Write(const Var& v, uint32_t offset, const void* data, uint32_t bytes)
		{
			Block& b = Blocks[v.BlockIndex];
			if (offset >= (uint32_t)v.Member->Size) return;
			bytes = (std::min)(bytes, (uint32_t)v.Member->Size - offset);
			const uint32_t at = (uint32_t)v.Member->Offset + offset;
			if (at >= b.Cpu.size()) return;
			bytes = (std::min)(bytes, (uint32_t)b.Cpu.size() - at);
			if (memcmp(b.Cpu.data() + at, data, bytes) == 0)
				return;   // 같은 값 (렌더러가 그리기마다 같은 빛·카메라 값을 다시 넣는다) — 올릴 것 없음
			memcpy(b.Cpu.data() + at, data, bytes);
			if (!b.Dirty) { b.DirtyLo = at; b.DirtyHi = at + bytes; }
			else { b.DirtyLo = (std::min)(b.DirtyLo, at); b.DirtyHi = (std::max)(b.DirtyHi, at + bytes); }
			b.Dirty = true;
		}

		const Var* Uniform(Rhi::VarId var) const { return var >= 0 && var < (int)Vars.size() && Vars[var].Member ? &Vars[var] : nullptr; }

		// 성분 하나 (정수 변수면 정수로 바꿔)
		void WriteComponent(const Var& v, uint32_t offset, double value)
		{
			if (v.Member->Integer) { const int i = (int)value; Write(v, offset, &i, 4); }
			else { const float f = (float)value; Write(v, offset, &f, 4); }
		}

		int TechniqueCount() const override { return (int)TechniqueNames.size(); }
		std::string TechniqueName(int technique) const override { return technique >= 0 && technique < (int)TechniqueNames.size() ? TechniqueNames[technique] : std::string(); }

		void SetRaw(Rhi::VarId var, const void* data, uint32_t bytes, uint32_t offset) override
		{
			if (const Var* v = Uniform(var)) Write(*v, offset, data, bytes);
		}

		void SetFloat(Rhi::VarId var, float value) override { if (const Var* v = Uniform(var)) WriteComponent(*v, 0, value); }
		void SetInt(Rhi::VarId var, int value) override { if (const Var* v = Uniform(var)) WriteComponent(*v, 0, value); }
		void SetBool(Rhi::VarId var, bool value) override { if (const Var* v = Uniform(var)) WriteComponent(*v, 0, value ? 1.0 : 0.0); }

		void SetVector(Rhi::VarId var, const float value[4]) override
		{
			if (const Var* v = Uniform(var))
				for (int c = 0; c < (std::min)(4, (std::max)(1, v->Member->Rows)); ++c)
					WriteComponent(*v, c * 4, value[c]);
		}

		void SetFloatArray(Rhi::VarId var, const float* values, uint32_t first, uint32_t count) override
		{
			const Var* v = Uniform(var);
			if (!v) return;
			const uint32_t stride = v->Member->ArrayStride ? v->Member->ArrayStride : 16;
			for (uint32_t i = 0; i < count; ++i)
				WriteComponent(*v, (first + i) * stride, values[i]);
		}

		void SetVectorArray(Rhi::VarId var, const float* values, uint32_t first, uint32_t count) override
		{
			const Var* v = Uniform(var);
			if (!v) return;
			const uint32_t stride = v->Member->ArrayStride ? v->Member->ArrayStride : 16;
			const int comps = (std::min)(4, (std::max)(1, v->Member->Rows));
			for (uint32_t i = 0; i < count; ++i)
				for (int c = 0; c < comps; ++c)
					WriteComponent(*v, (first + i) * stride + c * 4, values[i * 4 + c]);
		}

		void SetView(Rhi::VarId var, GfxShaderResourceView* view, uint32_t arrayIndex) override
		{
			if (var < 0) return;
			const GLuint id = view ? GfxGL_TextureName(view) : 0;
			if (view && !id && Reported.insert("non-gl-view").second)
				EditorLog::Write("OpenGL", "%s: a texture view that is not an OpenGL view was set - ignored", wstring_to_string(std::filesystem::path(Src.File).filename().wstring()).c_str());
			for (const ShaderCross::SamplerBinding* s : Vars[var].Samplers)
				if ((int)arrayIndex < s->Count)
				{
					UnitTextures[s->Unit + arrayIndex] = id;
					UnitViews[s->Unit + arrayIndex] = id ? view : nullptr;
				}
		}

		void GetVector(Rhi::VarId var, float out[4]) override
		{
			const Var* v = Uniform(var);
			if (!v) return;
			const Block& b = Blocks[v->BlockIndex];
			for (int c = 0; c < (std::min)(4, (std::max)(1, v->Member->Rows)); ++c)
			{
				const size_t at = v->Member->Offset + c * 4;
				if (at + 4 > b.Cpu.size()) break;
				if (v->Member->Integer) { int i; memcpy(&i, b.Cpu.data() + at, 4); out[c] = (float)i; }
				else memcpy(&out[c], b.Cpu.data() + at, 4);
			}
		}

		void SetUav(Rhi::VarId, GfxUnorderedAccessView*) override {}
		bool NativeInputSignature(int technique, int pass, const void** data, size_t* size) override
		{
			if (technique < 0 || technique >= (int)Programs.size() || pass < 0 || pass >= (int)Programs[technique].size()) return false;
			*data = &Programs[technique][pass].Signature;
			*size = sizeof(GLInputSignature);
			return true;
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
				{
					UnitTextures[s->Unit + arrayIndex] = id;
					UnitViews[s->Unit + arrayIndex] = nullptr;
				}
		}

		void Apply(int technique, int pass) override;
		void ApplyStates(PassProgram& pp);
	};

	class GLDevice : public Rhi::Device
	{
	public:
		GLContext::Handle Ctx;
		bool OwnsContext = false;
		GfxDevice* SinkDevice = nullptr;     // 엔진 본 장치일 때: pass 상태를 이 Gfx 컨텍스트로 (OMGet… 이 맞게)
		GfxContext* SinkContext = nullptr;
		GLuint Fbo = 0, ClearFbo = 0;
		GLenum Mode = GL_TRIANGLES;
		GLInputLayout* Layout = nullptr;
		struct VB { GLuint Id = 0; uint32_t Stride = 0, Offset = 0; };
		VB Vbs[16];
		GLuint Ib = 0;
		bool Index32 = true;
		GLuint CurrentProgram = 0;

		~GLDevice() override
		{
			if (::wglGetCurrentContext())
			{
				if (Fbo) glDeleteFramebuffers(1, &Fbo);
				if (ClearFbo) glDeleteFramebuffers(1, &ClearFbo);
			}
			if (OwnsContext)
				GLContext::Destroy(Ctx);
		}

		// 숨은 창 + 자기 컨텍스트 (검사·도구용)
		bool Init(std::string& error)
		{
			if (!GLContext::Create(nullptr, Ctx, error))
				return false;
			OwnsContext = true;
			glCreateFramebuffers(1, &Fbo);
			glCreateFramebuffers(1, &ClearFbo);
			return true;
		}

		// 지금 현재인 컨텍스트 (엔진 본 창, Gfx GL 장치가 만든 것). pass 상태는 Gfx 컨텍스트로 보낸다
		bool InitOnCurrent(GfxDevice* sinkDevice, GfxContext* sinkContext, std::string& error)
		{
			if (!::wglGetCurrentContext()) { error = "no current OpenGL context"; return false; }
			SinkDevice = sinkDevice;
			SinkContext = sinkContext;
			glCreateFramebuffers(1, &Fbo);
			glCreateFramebuffers(1, &ClearFbo);
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
			GLState::ApplyDefaults();   // D3D11 기본 상태 (GLState.cpp)
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
			GLState::BindVertexArray(Layout->Vao);
			return true;
		}

		void Draw(uint32_t vertexCount, uint32_t startVertex) override
		{
			if (!BindGeometry()) return;
			glDrawArraysInstancedBaseInstance(Mode, startVertex, vertexCount, 1, 0);
		}

		void DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex) override
		{
			DrawIndexedInstanced(indexCount, 1, startIndex, baseVertex, 0);
		}

		void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance) override
		{
			if (!BindGeometry()) return;
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
				pp.Signature.Inputs = &pg.VertexInputs;
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
							GLState::InvalidateBindings();
							pp.Program = 0;
						}
						else
						{
							// SPIRV-Cross 는 SV_InstanceID 를 gl_InstanceID + SPIRV_Cross_BaseInstance 로 만든다.
							// D3D 의 SV_InstanceID 는 StartInstanceLocation 을 더하지 않으므로 늘 0
							const GLint loc = glGetUniformLocation(pp.Program, "SPIRV_Cross_BaseInstance");
							if (loc >= 0) glProgramUniform1i(pp.Program, loc, 0);
						}
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
				e->SamplerObjects[s.Sampler] = GLState::CreateSampler(st != e->Src.Fx.States.end() ? GLState::FxSampler(st->second) : GLState::DefaultSampler());
			}
		}
		e->UnitTextures.assign(units, 0);
		e->UnitSamplers.assign(units, 0);
		e->UnitViews.assign(units, nullptr);
		for (const auto& [n, s] : e->Src.Samplers)
			for (int i = 0; i < s.Count; ++i)
				e->UnitSamplers[s.Unit + i] = s.Sampler == "nosampler" ? 0 : e->SamplerObjects[s.Sampler];
		return e;
	}

	void GLEffect::Apply(int technique, int pass)
	{
		if (technique < 0 || technique >= (int)Programs.size() || pass < 0 || pass >= (int)Programs[technique].size())
			return;
		PassProgram& pp = Programs[technique][pass];
		if (!pp.Program)
		{
			if (Reported.insert(TechniqueNames[technique] + "/" + std::to_string(pass)).second)
				EditorLog::Write("OpenGL", "pass %s/%d is not available: %s", TechniqueNames[technique].c_str(), pass, pp.Error.c_str());
			Device->CurrentProgram = 0;
			GLState::UseProgram(0);   // 앞 pass 의 프로그램으로 잘못 그리지 않게
			return;
		}
		GLState::UseProgram(pp.Program);
		Device->CurrentProgram = pp.Program;
		for (Block& b : Blocks)
		{
			if (b.Dirty)
			{
				if (b.DirtyHi > b.DirtyLo && b.DirtyHi <= b.Cpu.size())
					glNamedBufferSubData(b.Ubo, b.DirtyLo, b.DirtyHi - b.DirtyLo, b.Cpu.data() + b.DirtyLo);   // 바뀐 범위만
				else
					glNamedBufferSubData(b.Ubo, 0, b.Cpu.size(), b.Cpu.data());
				b.Dirty = false;
			}
			GLState::BindUniformBuffer(b.Binding, b.Ubo);
		}
		// 묶기 캐시: 앞 그리기와 같은 텍스처·샘플러면 GL 을 부르지 않는다 (GLState)
		for (size_t u = 0; u < UnitTextures.size(); ++u)
		{
			GLState::BindTextureUnit((GLuint)u, UnitTextures[u]);
			GLState::BindSampler((GLuint)u, UnitTextures[u] ? UnitSamplers[u] : 0);   // 빈 유닛 + 비교 샘플러 = 드라이버 경고
		}
		ApplyStates(pp);
	}

	// pass 가 정한 상태만 바꾼다 (Effects11 과 같이 정하지 않은 상태는 그대로). 뜻은 D3D11 설명 그대로 (GLState)
	void GLEffect::ApplyStates(PassProgram& pp)
	{
		const FxParser::Pass& p = *pp.Fx;
		auto find = [&](const std::string& name) -> const FxParser::StateBlock* {
			if (name.empty()) return nullptr;
			auto it = Src.Fx.States.find(name);
			return it != Src.Fx.States.end() ? &it->second : nullptr;
		};
		const FxParser::StateBlock* rs = find(p.RasterizerState);
		const FxParser::StateBlock* bs = find(p.BlendState);
		const FxParser::StateBlock* ds = find(p.DepthStencilState);
		if (Device->SinkContext)
		{
			// 엔진 본 장치: Gfx 상태 객체로 Gfx 컨텍스트에 (렌더러의 OMGet…/RSGet… 저장·복원이 맞도록)
			if (!pp.StatesMade)
			{
				pp.StatesMade = true;
				if (rs) { const D3D11_RASTERIZER_DESC d = GLState::FxRasterizer(*rs); Device->SinkDevice->CreateRasterizerState(&d, pp.Rs.GetAddressOf()); }
				if (bs) { const D3D11_BLEND_DESC d = GLState::FxBlend(*bs); Device->SinkDevice->CreateBlendState(&d, pp.Bs.GetAddressOf()); }
				if (ds) { const D3D11_DEPTH_STENCIL_DESC d = GLState::FxDepthStencil(*ds); Device->SinkDevice->CreateDepthStencilState(&d, pp.Ds.GetAddressOf()); }
			}
			if (pp.Rs) Device->SinkContext->RSSetState(pp.Rs.Get());
			if (pp.Bs) Device->SinkContext->OMSetBlendState(pp.Bs.Get(), p.BlendFactor, p.SampleMask);
			if (pp.Ds) Device->SinkContext->OMSetDepthStencilState(pp.Ds.Get(), (UINT)p.StencilRef);
			return;
		}
		if (rs) GLState::ApplyRasterizer(GLState::FxRasterizer(*rs));
		if (bs) GLState::ApplyBlend(GLState::FxBlend(*bs), p.BlendFactor, p.SampleMask);
		if (ds) GLState::ApplyDepthStencil(GLState::FxDepthStencil(*ds), (UINT)p.StencilRef);
	}
}

std::unique_ptr<Rhi::Device> CreateGLRhiDevice(std::string& error)
{
	auto d = std::make_unique<GLDevice>();
	if (!d->Init(error))
		return nullptr;
	return d;
}

// 지금 현재인 GL 컨텍스트 위의 RHI 장치 (엔진 본 창). pass 상태는 sink(Gfx GL 컨텍스트)로
std::unique_ptr<Rhi::Device> CreateGLRhiDeviceOnCurrent(GfxDevice* sinkDevice, GfxContext* sinkContext, std::string& error)
{
	auto d = std::make_unique<GLDevice>();
	if (!d->InitOnCurrent(sinkDevice, sinkContext, error))
		return nullptr;
	return d;
}
