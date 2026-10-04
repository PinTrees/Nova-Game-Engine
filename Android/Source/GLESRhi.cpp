#include "pch.h"
#include "Rhi.h"
#include "ShaderCross.h"
#include "ShaderCrossJson.h"
#include "FxStates.h"
#include "GLESState.h"
#include "GLShared.h"
#include "GfxGLES.h"
#include "Gfx.h"
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#include <fstream>
#include "AndroidEngine.h"

bool NovaReadAsset(const std::string& path, std::string& out);   // AndroidMain.cpp

// RHI 의 OpenGL ES 3.2 구현 (안드로이드). 데스크톱 GL 구현(GLRhi.cpp)과 같은 규칙을 바인딩 방식(DSA 없음)으로.
//  - 셰이더: PC 가 미리 만든 GLSL ES 3.20 (nova android shaders → APK assets/Shaders/<이름>.json, ShaderCross::CompileEffectGles).
//    휴대폰에는 변환기(DXC)가 없다. 바인딩 · 이름 규칙은 데스크톱 GL 과 같다
//  - 좌표: 셰이더의 -fvk-invert-y 로 텍스처 행 0 = D3D 의 위 (데스크톱 GL 과 같음). glClipControl 이 없어 깊이 0..1 → -1..1 은
//    셰이더가 바꾼다 (fixup_clipspace) — 저장되는 깊이 값은 D3D 와 같다. D3D 앞면(시계) = GL_CCW
namespace
{
	constexpr int kGlesShaderVersion = 1;   // nova android shaders 가 쓰는 JSON 버전 (AndroidTools.cpp 와 같게)

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

	std::vector<double> Numbers(const std::string& text)
	{
		std::vector<double> out;
		const char* p = text.c_str();
		while (*p)
		{
			if ((*p >= '0' && *p <= '9') || ((*p == '-' || *p == '+' || *p == '.') && (isdigit((unsigned char)p[1]) || p[1] == '.')))
			{
				if (p > text.c_str() && (isalpha((unsigned char)p[-1]) || p[-1] == '_')) { ++p; continue; }
				char* end = nullptr;
				out.push_back(strtod(p, &end));
				p = end;
				while (*p == 'f' || *p == 'F' || *p == 'u' || *p == 'U') ++p;
				continue;
			}
			if (strncasecmp(p, "true", 4) == 0 && (p == text.c_str() || !isalnum((unsigned char)p[-1]))) { out.push_back(1); p += 4; continue; }
			if (strncasecmp(p, "false", 5) == 0 && (p == text.c_str() || !isalnum((unsigned char)p[-1]))) { out.push_back(0); p += 5; continue; }
			++p;
		}
		return out;
	}

	using GLESState::ApplyRasterizer;
	using GLESState::ApplyBlend;
	using GLESState::ApplyDepthStencil;
	using GLESState::CreateSampler;
	using GLESState::ApplyDefaults;

	class ESBuffer : public Rhi::Buffer
	{
	public:
		explicit ESBuffer(const Rhi::BufferDesc& d) { _desc = d; }
		~ESBuffer() override { if (Id) glDeleteBuffers(1, &Id); }
		GLuint Id = 0;
	};

	class ESTexture : public Rhi::Texture
	{
	public:
		explicit ESTexture(const Rhi::TextureDesc& d) { _desc = d; }
		~ESTexture() override { if (Id) glDeleteTextures(1, &Id); }
		GLuint Id = 0;
		GLenum Target = GL_TEXTURE_2D;
	};

	// 정점 배치: 데스크톱 GL 과 같이 의미 → location. ES 3.1 의 정점 속성 바인딩 (glVertexAttribFormat · glBindVertexBuffer)
	class ESInputLayout : public Rhi::InputLayout
	{
	public:
		~ESInputLayout() override { if (Vao) glDeleteVertexArrays(1, &Vao); }
		GLuint Vao = 0;
	};

	class ESDevice;

	class ESEffect : public Rhi::Effect
	{
	public:
		struct Block { std::string Name; int Binding = 0; std::vector<uint8_t> Cpu; GLuint Ubo = 0; bool Dirty = true; const ShaderCross::UniformBlock* Info = nullptr; };
		struct Var
		{
			int BlockIndex = -1;
			const ShaderCross::UniformBlock::Member* Member = nullptr;
			std::vector<const ShaderCross::SamplerBinding*> Samplers;
			int Ssbo = -1;    // (RW)StructuredBuffer · (RW)ByteAddressBuffer → SSBO 바인딩
			int Image = -1;   // RWTexture → image 유닛
		};
		// compute 자원 (오클루전 컬링): 바인딩마다 지금 넣은 버퍼 · 이미지 (Effects11 처럼 묶인 동안 뷰를 잡아 둔다)
		struct SsboBind { GLuint Buffer = 0; GLintptr Offset = 0; GLsizeiptr Size = 0; ComPtr<GfxObject> Hold; };
		struct ImageBind { GLuint Texture = 0; GLint Level = 0; GLenum Format = 0; ComPtr<GfxObject> Hold; };
		std::vector<SsboBind> Ssbos;
		std::vector<ImageBind> Images;
		struct PassProgram
		{
			GLuint Program = 0;
			std::string Error;
			const FxParser::Pass* Fx = nullptr;
			int SrcIndex = -1;       // Src.Passes 의 번호
			bool Built = false;      // 처음 Apply 할 때 컴파일 · 링크 (안 쓰는 technique 은 만들지 않는다 — 시작 시간)
			bool StatesMade = false;
			GLInputSignature Signature;   // Gfx 층 CreateInputLayout 이 받는 의미 → location 표
			ComPtr<GfxRasterizerState> SinkRs;   // Gfx 장치에 붙은 효과: pass 상태를 Gfx 컨텍스트로
			ComPtr<GfxBlendState> SinkBs;
			ComPtr<GfxDepthStencilState> SinkDs;
			D3D11_RASTERIZER_DESC Rs;
			D3D11_BLEND_DESC Bs;
			D3D11_DEPTH_STENCIL_DESC Ds;
			bool HasRs = false, HasBs = false, HasDs = false;
		};

		ESDevice* Device = nullptr;
		ShaderCross::EffectGlsl Src;
		std::vector<std::string> TechniqueNames;
		std::vector<std::vector<PassProgram>> Programs;
		std::string Name;   // 효과 이름 (로그)
		std::vector<Block> Blocks;
		std::vector<Var> Vars;
		std::map<std::string, Rhi::VarId> VarIds;
		std::map<std::string, GLuint> SamplerObjects;
		std::vector<GLuint> UnitTextures, UnitSamplers;
		std::vector<ComPtr<GfxShaderResourceView>> UnitViews;   // Gfx 층 뷰 (Apply 때 GL 텍스처로)
		std::vector<GLenum> UnitTargets, UnitShadowTarget;
		std::set<std::string> Reported;

		~ESEffect() override
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
		int PassCount(int technique) const override { return technique >= 0 && technique < (int)Programs.size() ? (int)Programs[technique].size() : 0; }
		int TechniqueCount() const override { return (int)TechniqueNames.size(); }
		std::string TechniqueName(int technique) const override { return technique >= 0 && technique < (int)TechniqueNames.size() ? TechniqueNames[technique] : std::string(); }

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
			{
				if (auto b = Src.Buffers.find(name); b != Src.Buffers.end()) v.Ssbo = b->second;
				if (auto im = Src.Images.find(name); im != Src.Images.end()) v.Image = im->second;
			}
			if (!v.Member && v.Samplers.empty() && v.Ssbo < 0 && v.Image < 0) return -1;
			Vars.push_back(v);
			return VarIds[name] = (int)Vars.size() - 1;
		}

		void Write(const Var& v, uint32_t offset, const void* data, uint32_t bytes)
		{
			Block& b = Blocks[v.BlockIndex];
			if (offset >= (uint32_t)v.Member->Size) return;
			bytes = (std::min)(bytes, (uint32_t)v.Member->Size - offset);
			const uint32_t at = (uint32_t)v.Member->Offset + offset;
			if (at >= b.Cpu.size()) return;
			bytes = (std::min)(bytes, (uint32_t)b.Cpu.size() - at);
			if (memcmp(b.Cpu.data() + at, data, bytes) == 0) return;
			memcpy(b.Cpu.data() + at, data, bytes);
			b.Dirty = true;
		}

		const Var* Uniform(Rhi::VarId var) const { return var >= 0 && var < (int)Vars.size() && Vars[var].Member ? &Vars[var] : nullptr; }

		void WriteComponent(const Var& v, uint32_t offset, double value)
		{
			if (v.Member->Integer) { const int i = (int)value; Write(v, offset, &i, 4); }
			else { const float f = (float)value; Write(v, offset, &f, 4); }
		}

		void SetRaw(Rhi::VarId var, const void* data, uint32_t bytes, uint32_t offset) override { if (const Var* v = Uniform(var)) Write(*v, offset, data, bytes); }
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
			for (uint32_t i = 0; i < count; ++i) WriteComponent(*v, (first + i) * stride, values[i]);
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
			else Write(v, offset, m, 64);
		}

		void SetMatrix(Rhi::VarId var, const float m[16]) override { if (const Var* v = Uniform(var)) WriteMatrix(*v, 0, m); }

		void SetMatrixArray(Rhi::VarId var, const float* m, uint32_t first, uint32_t count) override
		{
			const Var* v = Uniform(var);
			if (!v) return;
			const uint32_t stride = v->Member->ArrayStride ? v->Member->ArrayStride : 64;
			for (uint32_t i = 0; i < count; ++i) WriteMatrix(*v, (first + i) * stride, m + i * 16);
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

		void SetTexture(Rhi::VarId var, Rhi::Texture* texture, uint32_t arrayIndex) override
		{
			if (var < 0 || var >= (int)Vars.size()) return;
			auto* t = static_cast<ESTexture*>(texture);
			for (const ShaderCross::SamplerBinding* s : Vars[var].Samplers)
				if ((int)arrayIndex < s->Count)
				{
					UnitTextures[s->Unit + arrayIndex] = t ? t->Id : 0;
					UnitTargets[s->Unit + arrayIndex] = t ? t->Target : 0;
					UnitViews[s->Unit + arrayIndex] = nullptr;
				}
		}

		void SetView(Rhi::VarId var, GfxShaderResourceView* view, uint32_t arrayIndex) override
		{
			if (var < 0 || var >= (int)Vars.size()) return;
			if (Vars[var].Ssbo >= 0 && Vars[var].Ssbo < (int)Ssbos.size())
			{
				// 구조 · raw 버퍼 SRV → SSBO (읽기만)
				unsigned b = 0, o = 0, n = 0;
				Ssbos[Vars[var].Ssbo] = view && GfxGL_BufferRange(view, b, o, n) ? SsboBind{ b, (GLintptr)o, (GLsizeiptr)n, ComPtr<GfxObject>(view) } : SsboBind();
				return;
			}
			for (const ShaderCross::SamplerBinding* s : Vars[var].Samplers)
				if ((int)arrayIndex < s->Count)
				{
					UnitViews[s->Unit + arrayIndex] = view;
					UnitTextures[s->Unit + arrayIndex] = 0;
				}
		}
		void SetUav(Rhi::VarId var, GfxUnorderedAccessView* uav) override
		{
			if (var < 0 || var >= (int)Vars.size()) return;
			unsigned a = 0, b = 0, c = 0;
			if (Vars[var].Ssbo >= 0 && Vars[var].Ssbo < (int)Ssbos.size())
				Ssbos[Vars[var].Ssbo] = uav && GfxGL_UavBuffer(uav, a, b, c) ? SsboBind{ a, (GLintptr)b, (GLsizeiptr)c, ComPtr<GfxObject>(uav) } : SsboBind();
			else if (Vars[var].Image >= 0 && Vars[var].Image < (int)Images.size())
				Images[Vars[var].Image] = uav && GfxGL_UavImage(uav, a, b, c) ? ImageBind{ a, (GLint)b, (GLenum)c, ComPtr<GfxObject>(uav) } : ImageBind();
		}
		bool NativeInputSignature(int technique, int pass, const void** data, size_t* size) override
		{
			if (technique < 0 || technique >= (int)Programs.size() || pass < 0 || pass >= (int)Programs[technique].size()) return false;
			*data = &Programs[technique][pass].Signature;
			*size = sizeof(GLInputSignature);
			return true;
		}
		void Apply(int technique, int pass) override;
	};

	class ESDevice : public Rhi::Device
	{
	public:
		GLuint Fbo = 0, ClearFbo = 0, ReadFbo = 0;
		GLenum Mode = GL_TRIANGLES;
		ESInputLayout* Layout = nullptr;
		struct VB { GLuint Id = 0; uint32_t Stride = 0, Offset = 0; };
		VB Vbs[16];
		GLuint Ib = 0;
		bool Index32 = true;
		GLuint CurrentProgram = 0;
		GLuint DummyShadow2D = 0, DummyShadowArray = 0, DummyShadowCube = 0, DummyCompare = 0, Point = 0;
		GfxDevice* SinkDevice = nullptr;     // Gfx 장치 위의 RHI: pass 상태를 이 Gfx 컨텍스트로 (읽기 전용 깊이 등 Gfx 가 아는 상태가 맞게)
		GfxContext* SinkContext = nullptr;

		ESDevice()
		{
			glGenFramebuffers(1, &Fbo);
			glGenFramebuffers(1, &ClearFbo);
			glGenFramebuffers(1, &ReadFbo);
			ApplyDefaults();
		}

		~ESDevice() override
		{
			GLuint f[] = { Fbo, ClearFbo, ReadFbo };
			glDeleteFramebuffers(3, f);
			for (GLuint* t : { &DummyShadow2D, &DummyShadowArray, &DummyShadowCube })
				if (*t) glDeleteTextures(1, t);
			if (DummyCompare) glDeleteSamplers(1, &DummyCompare);
			if (Point) glDeleteSamplers(1, &Point);
		}

		// 샘플러 없이 읽는 텍스처 (HLSL 의 Load · texelFetch) 칸: NEAREST — 텍스처 기본 필터 (LINEAR) 이면 float32 · 깊이 텍스처가
		//  ES 에서 불완전이라 texelFetch 가 0 을 읽는다 (texelFetch 는 필터를 쓰지 않으니 다른 텍스처는 그대로)
		GLuint PointSampler()
		{
			if (!Point)
			{
				glGenSamplers(1, &Point);
				glSamplerParameteri(Point, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glSamplerParameteri(Point, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			}
			return Point;
		}

		GLuint DummyShadow(GLenum target)
		{
			GLuint& t = target == GL_TEXTURE_2D_ARRAY ? DummyShadowArray : target == GL_TEXTURE_CUBE_MAP ? DummyShadowCube : DummyShadow2D;
			if (t) return t;
			const float one = 1.0f;
			glGenTextures(1, &t);
			glBindTexture(target, t);
			if (target == GL_TEXTURE_2D_ARRAY)
			{
				glTexStorage3D(target, 1, GL_DEPTH_COMPONENT32F, 1, 1, 1);
				glTexSubImage3D(target, 0, 0, 0, 0, 1, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &one);
			}
			else
			{
				glTexStorage2D(target, 1, GL_DEPTH_COMPONENT32F, 1, 1);
				if (target == GL_TEXTURE_CUBE_MAP)
					for (int face = 0; face < 6; ++face)
						glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &one);
				else
					glTexSubImage2D(target, 0, 0, 0, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &one);
			}
			return t;
		}

		GLuint DummyCompareSampler()
		{
			if (!DummyCompare)
			{
				glGenSamplers(1, &DummyCompare);
				glSamplerParameteri(DummyCompare, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
				glSamplerParameteri(DummyCompare, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
				glSamplerParameteri(DummyCompare, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			}
			return DummyCompare;
		}

		GraphicsAPI GetAPI() const override { return GraphicsAPI::OpenGL; }   // (GraphicsAPI 에 아직 GLES 항목이 없다)

		std::string Description() const override
		{
			auto s = [](GLenum e) { const GLubyte* v = glGetString(e); return v ? std::string((const char*)v) : std::string(); };
			return s(GL_VERSION) + " (" + s(GL_RENDERER) + ")";
		}

		std::unique_ptr<Rhi::Buffer> CreateBuffer(const Rhi::BufferDesc& desc, const void* initialData) override
		{
			auto b = std::make_unique<ESBuffer>(desc);
			glGenBuffers(1, &b->Id);
			glBindBuffer(GL_COPY_WRITE_BUFFER, b->Id);
			glBufferData(GL_COPY_WRITE_BUFFER, desc.Size, initialData, desc.Dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
			return b;
		}

		void UpdateBuffer(Rhi::Buffer* buffer, const void* data, uint32_t bytes) override
		{
			glBindBuffer(GL_COPY_WRITE_BUFFER, static_cast<ESBuffer*>(buffer)->Id);
			glBufferSubData(GL_COPY_WRITE_BUFFER, 0, bytes, data);
		}

		std::unique_ptr<Rhi::Texture> CreateTexture(const Rhi::TextureDesc& desc, const Rhi::SubresourceData* initialData) override
		{
			auto t = std::make_unique<ESTexture>(desc);
			const GLFormat f = FormatGL(desc.Format);
			const uint32_t slices = desc.Type == Rhi::TextureType::Cube ? 6 : desc.ArraySize;
			t->Target = desc.Type == Rhi::TextureType::Cube ? GL_TEXTURE_CUBE_MAP : desc.Type == Rhi::TextureType::Tex2DArray ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
			glGenTextures(1, &t->Id);
			glBindTexture(t->Target, t->Id);
			if (t->Target == GL_TEXTURE_2D_ARRAY)
				glTexStorage3D(t->Target, desc.MipLevels, f.Internal, desc.Width, desc.Height, slices);
			else
				glTexStorage2D(t->Target, desc.MipLevels, f.Internal, desc.Width, desc.Height);
			glTexParameteri(t->Target, GL_TEXTURE_MAX_LEVEL, (GLint)desc.MipLevels - 1);
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
						if (t->Target == GL_TEXTURE_2D) glTexSubImage2D(GL_TEXTURE_2D, m, 0, 0, w, h, f.Format, f.Type, d.Data);
						else if (t->Target == GL_TEXTURE_CUBE_MAP) glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + s, m, 0, 0, w, h, f.Format, f.Type, d.Data);
						else glTexSubImage3D(t->Target, m, 0, 0, s, w, h, 1, f.Format, f.Type, d.Data);
					}
				glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
			}
			return t;
		}

		std::unique_ptr<Rhi::Effect> LoadEffect(const std::wstring& fxPath, std::string& error) override;

		std::unique_ptr<Rhi::InputLayout> CreateInputLayout(const Rhi::VertexElement* elements, uint32_t count, Rhi::Effect* effect, int technique, int pass, std::string& error) override
		{
			auto* fx = static_cast<ESEffect*>(effect);
			if (technique < 0 || technique >= (int)fx->Programs.size() || pass < 0 || pass >= (int)fx->Programs[technique].size())
			{
				error = "bad technique/pass";
				return nullptr;
			}
			int index = 0;
			for (int t = 0; t < technique; ++t) index += (int)fx->Programs[t].size();
			const ShaderCross::PassGlsl* pg = &fx->Src.Passes[index + pass];
			auto l = std::make_unique<ESInputLayout>();
			glGenVertexArrays(1, &l->Vao);
			glBindVertexArray(l->Vao);
			std::set<std::string> provided;
			for (uint32_t i = 0; i < count; ++i)
			{
				const Rhi::VertexElement& e = elements[i];
				const std::string sem = Upper(e.Semantic) + std::to_string(e.SemanticIndex);
				provided.insert(sem);
				int loc = -1;
				for (const auto& [s, l2] : pg->VertexInputs)
					if (s == sem) loc = l2;
				if (loc < 0) continue;
				glEnableVertexAttribArray(loc);
				switch (e.Format)
				{
				case Rhi::VertexFormat::Float1: glVertexAttribFormat(loc, 1, GL_FLOAT, GL_FALSE, e.Offset); break;
				case Rhi::VertexFormat::Float2: glVertexAttribFormat(loc, 2, GL_FLOAT, GL_FALSE, e.Offset); break;
				case Rhi::VertexFormat::Float3: glVertexAttribFormat(loc, 3, GL_FLOAT, GL_FALSE, e.Offset); break;
				case Rhi::VertexFormat::Float4: glVertexAttribFormat(loc, 4, GL_FLOAT, GL_FALSE, e.Offset); break;
				case Rhi::VertexFormat::UByte4_UNorm: glVertexAttribFormat(loc, 4, GL_UNSIGNED_BYTE, GL_TRUE, e.Offset); break;
				case Rhi::VertexFormat::UInt4: glVertexAttribIFormat(loc, 4, GL_UNSIGNED_INT, e.Offset); break;
				}
				glVertexAttribBinding(loc, e.Slot);
				glVertexBindingDivisor(e.Slot, e.PerInstance ? 1 : 0);
			}
			glBindVertexArray(0);
			for (const auto& [s, loc] : pg->VertexInputs)
				if (!provided.count(s))
				{
					error = "vertex input " + s + " is not provided by the layout";
					return nullptr;
				}
			return l;
		}

		void ResetState() override { ApplyDefaults(); }

		void SetRenderTargets(Rhi::Texture* const* colors, uint32_t count, Rhi::Texture* depth, uint32_t depthSlice) override
		{
			glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
			GLenum bufs[8];
			for (uint32_t i = 0; i < 8; ++i)
			{
				auto* t = i < count && colors[i] ? static_cast<ESTexture*>(colors[i]) : nullptr;
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, t ? t->Id : 0, 0);
				bufs[i] = t ? GL_COLOR_ATTACHMENT0 + i : GL_NONE;
			}
			glDrawBuffers(8, bufs);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
			if (auto* d = static_cast<ESTexture*>(depth))
			{
				const GLenum att = d->Desc().Format == Rhi::Format::D24_UNorm_S8_UInt ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
				if (d->Target == GL_TEXTURE_2D) glFramebufferTexture2D(GL_FRAMEBUFFER, att, GL_TEXTURE_2D, d->Id, 0);
				else glFramebufferTextureLayer(GL_FRAMEBUFFER, att, d->Id, 0, depthSlice);
			}
		}

		void SetViewport(float x, float y, float width, float height) override
		{
			glViewport((GLint)x, (GLint)y, (GLsizei)width, (GLsizei)height);
			glDepthRangef(0.0f, 1.0f);
		}

		// D3D Clear 는 쓰기 마스크 · 가위와 상관없다 → 잠시 풀고 되돌린다
		template <class F>
		void ClearWith(F fn)
		{
			GLint prev = 0;
			glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev);
			GLboolean mask[4], depthMask;
			glGetBooleanv(GL_COLOR_WRITEMASK, mask);
			glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
			const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glDepthMask(GL_TRUE);
			glDisable(GL_SCISSOR_TEST);
			glBindFramebuffer(GL_FRAMEBUFFER, ClearFbo);
			fn();
			glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
			glColorMask(mask[0], mask[1], mask[2], mask[3]);
			glDepthMask(depthMask);
			if (scissor) glEnable(GL_SCISSOR_TEST);
		}

		void ClearColor(Rhi::Texture* target, const float rgba[4]) override
		{
			auto* t = static_cast<ESTexture*>(target);
			ClearWith([&]() {
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->Id, 0);
				const GLenum b = GL_COLOR_ATTACHMENT0;
				glDrawBuffers(1, &b);
				glClearBufferfv(GL_COLOR, 0, rgba);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
			});
		}

		void ClearDepth(Rhi::Texture* target, float depth) override
		{
			auto* t = static_cast<ESTexture*>(target);
			const bool stencil = t->Desc().Format == Rhi::Format::D24_UNorm_S8_UInt;
			const GLenum att = stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
			ClearWith([&]() {
				const GLenum none = GL_NONE;
				glDrawBuffers(1, &none);
				const uint32_t slices = t->Target == GL_TEXTURE_2D ? 1 : (t->Target == GL_TEXTURE_CUBE_MAP ? 6 : t->Desc().ArraySize);
				for (uint32_t s = 0; s < slices; ++s)   // 배열이면 모든 조각
				{
					if (t->Target == GL_TEXTURE_2D) glFramebufferTexture2D(GL_FRAMEBUFFER, att, GL_TEXTURE_2D, t->Id, 0);
					else glFramebufferTextureLayer(GL_FRAMEBUFFER, att, t->Id, 0, s);
					if (stencil) glClearBufferfi(GL_DEPTH_STENCIL, 0, depth, 0);
					else glClearBufferfv(GL_DEPTH, 0, &depth);
				}
				glFramebufferTexture2D(GL_FRAMEBUFFER, att, GL_TEXTURE_2D, 0, 0);
			});
		}

		void SetInputLayout(Rhi::InputLayout* layout) override { Layout = static_cast<ESInputLayout*>(layout); }
		void SetVertexBuffer(uint32_t slot, Rhi::Buffer* buffer, uint32_t stride, uint32_t offset) override
		{
			if (slot < 16) Vbs[slot] = { buffer ? static_cast<ESBuffer*>(buffer)->Id : 0, stride, offset };
		}
		void SetIndexBuffer(Rhi::Buffer* buffer, bool use32Bit) override
		{
			Ib = buffer ? static_cast<ESBuffer*>(buffer)->Id : 0;
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
			glBindVertexArray(Layout->Vao);
			for (GLuint i = 0; i < 16; ++i)
				if (Vbs[i].Id)
					glBindVertexBuffer(i, Vbs[i].Id, Vbs[i].Offset, Vbs[i].Stride);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, Ib);
			return true;
		}

		void Draw(uint32_t vertexCount, uint32_t startVertex) override
		{
			if (BindGeometry()) glDrawArrays(Mode, startVertex, vertexCount);
		}

		void DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex) override
		{
			DrawIndexedInstanced(indexCount, 1, startIndex, baseVertex, 0);
		}

		void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance) override
		{
			if (!BindGeometry()) return;
			if (startInstance)
				EditorLog::Write("GLES", "%s", "start instance is not supported on OpenGL ES (drawn from 0)");
			const size_t size = Index32 ? 4 : 2;
			glDrawElementsInstancedBaseVertex(Mode, indexCount, Index32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, (const void*)(startIndex * size), instanceCount, baseVertex);
		}

		bool ReadPixels(Rhi::Texture* texture, std::vector<uint8_t>& rgba, std::string& error) override
		{
			auto* t = static_cast<ESTexture*>(texture);
			const uint32_t w = t->Desc().Width, h = t->Desc().Height;
			if (Rhi::IsDepth(t->Desc().Format) || t->Desc().Format != Rhi::Format::RGBA8_UNorm)
			{
				error = "ReadPixels: only RGBA8 color textures on OpenGL ES";
				return false;
			}
			rgba.resize((size_t)w * h * 4);
			GLint prev = 0;
			glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, ReadFbo);
			glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->Id, 0);
			glReadBuffer(GL_COLOR_ATTACHMENT0);
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());   // 행 0 = 텍스처 행 0 = D3D 의 위
			glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev);
			if (glGetError() != GL_NO_ERROR) { error = "glReadPixels failed"; return false; }
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

	// ---- 프로그램 바이너리 캐시 (앱 파일 폴더의 glcache/): 같은 GPU 드라이버 · 같은 GLSL 이면 다음 실행부터 컴파일 · 링크 없이 (glProgramBinary)
	uint64_t Fnv(uint64_t h, const std::string& s)
	{
		for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
		return h;
	}

	int BinaryFormats()
	{
		static int n = -1;
		if (n < 0)
		{
			glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &n);
			EditorLog::Write("GLES", "program binary formats: %d%s", n, n > 0 ? " (shader cache on)" : " (no shader cache)");
		}
		return n;
	}

	std::string CachePath(uint64_t key)
	{
		char name[32];
		snprintf(name, sizeof(name), "%016llx.bin", (unsigned long long)key);
		return NovaAndroid::FilesDir() + "/glcache/" + name;
	}

	bool LoadBinary(GLuint program, uint64_t key)
	{
		std::ifstream in(CachePath(key), std::ios::binary);
		if (!in) return false;
		GLenum format = 0;
		in.read(reinterpret_cast<char*>(&format), sizeof(format));
		std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		if (data.empty()) return false;
		glProgramBinary(program, format, data.data(), (GLsizei)data.size());
		GLint ok = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &ok);
		return ok != 0;   // 드라이버가 바뀌었으면 실패 → 다시 컴파일
	}

	void SaveBinary(GLuint program, uint64_t key)
	{
		GLint len = 0;
		glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &len);
		if (len <= 0) return;
		std::vector<char> data((size_t)len);
		GLenum format = 0;
		glGetProgramBinary(program, len, nullptr, &format, data.data());
		if (glGetError() != GL_NO_ERROR) return;
		std::error_code ec;
		std::filesystem::create_directories(NovaAndroid::FilesDir() + "/glcache", ec);
		std::ofstream out(CachePath(key), std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(&format), sizeof(format));
		out.write(data.data(), (std::streamsize)data.size());
	}

	// 한 pass 의 프로그램: 캐시 → 없으면 단계마다 컴파일 + 링크 (+ 캐시에 저장)
	void BuildPass(const ShaderCross::EffectGlsl& src, const std::string& effectName, const std::string& techName, ESEffect::PassProgram& pp)
	{
		pp.Built = true;
		if (!pp.Error.empty() || pp.SrcIndex < 0)
			return;
		const ShaderCross::PassGlsl& pg = src.Passes[(size_t)pp.SrcIndex];
		uint64_t key = 14695981039346656037ull;
		key = Fnv(key, reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
		key = Fnv(key, reinterpret_cast<const char*>(glGetString(GL_VERSION)));
		for (const ShaderCross::StageGlsl& sg : pg.Stages)
			key = Fnv(Fnv(key, std::to_string((int)sg.StageType)), sg.Glsl);
		const bool cache = BinaryFormats() > 0;
		pp.Program = glCreateProgram();
		if (cache && LoadBinary(pp.Program, key))
		{
			const GLint loc = glGetUniformLocation(pp.Program, "SPIRV_Cross_BaseInstance");
			if (loc >= 0) { glUseProgram(pp.Program); glUniform1i(loc, 0); }
			return;
		}
		std::vector<GLuint> shaders;
		for (const ShaderCross::StageGlsl& sg : pg.Stages)
		{
			static const GLenum types[] = { GL_VERTEX_SHADER, GL_TESS_CONTROL_SHADER, GL_TESS_EVALUATION_SHADER, GL_GEOMETRY_SHADER, GL_FRAGMENT_SHADER, GL_COMPUTE_SHADER };
			std::string err;
			GLuint sh = CompileStage(types[(int)sg.StageType], sg.Glsl, err);
			if (!sh)
			{
				pp.Error = std::string(FxParser::StageName(sg.StageType)) + " " + sg.Entry + ": GLSL ES: " + err;
				break;
			}
			shaders.push_back(sh);
		}
		if (pp.Error.empty())
		{
			for (GLuint sh : shaders) glAttachShader(pp.Program, sh);
			if (cache) glProgramParameteri(pp.Program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
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
			}
			else
			{
				if (cache) SaveBinary(pp.Program, key);
				const GLint loc = glGetUniformLocation(pp.Program, "SPIRV_Cross_BaseInstance");
				if (loc >= 0) { glUseProgram(pp.Program); glUniform1i(loc, 0); }
			}
		}
		for (GLuint sh : shaders)
		{
			glDetachShader(pp.Program, sh);
			glDeleteShader(sh);
		}
		if (!pp.Error.empty())
		{
			glDeleteProgram(pp.Program);
			pp.Program = 0;
			EditorLog::Write("GLES", "%s %s: %s", effectName.c_str(), techName.c_str(), pp.Error.c_str());
		}
	}

	std::unique_ptr<Rhi::Effect> ESDevice::LoadEffect(const std::wstring& fxPath, std::string& error)
	{
		auto e = std::make_unique<ESEffect>();
		e->Device = this;
		e->Name = std::filesystem::path(wstring_to_string(fxPath)).stem().string();
		// 미리 변환한 GLSL ES (assets/Shaders/<이름>.json)
		const std::string stem = std::filesystem::path(wstring_to_string(fxPath)).stem().string();
		std::string text;
		if (!NovaReadAsset("Shaders/" + stem + ".json", text))
		{
			error = "shader not packaged: Shaders/" + stem + ".json (run nova android shaders)";
			return nullptr;
		}
		try
		{
			if (!ShaderCross::Json::FromJson(nlohmann::json::parse(text), e->Src, kGlesShaderVersion))
			{
				error = stem + ".json: version mismatch (re-export shaders)";
				return nullptr;
			}
		}
		catch (const std::exception& ex)
		{
			error = stem + ".json: " + ex.what();
			return nullptr;
		}
		size_t index = 0;
		for (const FxParser::Technique& tech : e->Src.Fx.Techniques)
		{
			e->TechniqueNames.push_back(tech.Name);
			std::vector<ESEffect::PassProgram> passes;
			for (const FxParser::Pass& pass : tech.Passes)
			{
				const ShaderCross::PassGlsl& pg = e->Src.Passes[index++];
				ESEffect::PassProgram pp;
				pp.Fx = &pass;
				pp.Signature.Inputs = &pg.VertexInputs;
				pp.Error = pg.Error;
				pp.SrcIndex = (int)index - 1;
				if (!pp.Error.empty())
					EditorLog::Write("GLES", "%s %s/%s: %s", stem.c_str(), tech.Name.c_str(), pass.Name.c_str(), pp.Error.c_str());
				passes.push_back(pp);
			}
			e->Programs.push_back(std::move(passes));
		}
		for (const auto& [name, info] : e->Src.Blocks)
		{
			ESEffect::Block b;
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
			glGenBuffers(1, &b.Ubo);
			glBindBuffer(GL_UNIFORM_BUFFER, b.Ubo);
			glBufferData(GL_UNIFORM_BUFFER, b.Cpu.size(), b.Cpu.data(), GL_DYNAMIC_DRAW);
			b.Dirty = false;
			e->Blocks.push_back(std::move(b));
		}
		int units = 0;
		for (const auto& [n, s] : e->Src.Samplers)
		{
			units = (std::max)(units, s.Unit + s.Count);
			if (s.Sampler != "nosampler" && !e->SamplerObjects.count(s.Sampler))
			{
				auto st = e->Src.Fx.States.find(s.Sampler);
				e->SamplerObjects[s.Sampler] = CreateSampler(st != e->Src.Fx.States.end() ? FxStates::Sampler(st->second) : FxStates::DefaultSampler());
			}
		}
		// compute 자원 바인딩 수 (ShaderCross 가 효과 안에서 이름마다 0, 1, 2 … 로 정했다)
		e->Ssbos.resize(e->Src.Buffers.size());
		e->Images.resize(e->Src.Images.size());
		e->UnitTextures.assign(units, 0);
		e->UnitViews.assign(units, nullptr);
		e->UnitSamplers.assign(units, 0);
		e->UnitTargets.assign(units, 0);
		e->UnitShadowTarget.assign(units, 0);
		for (const auto& [n, s] : e->Src.Samplers)
			for (int i = 0; i < s.Count; ++i)
				e->UnitSamplers[s.Unit + i] = s.Sampler == "nosampler" ? 0 : e->SamplerObjects[s.Sampler];
		// 그림자(비교) 샘플러 유닛: GLSL ES 선언 (uniform highp sampler2DArrayShadow 이름) 에서 종류를 읽는다
		//  (모든 단계의 'uniform [정밀도] sampler…Shadow 이름' 을 한 번 훑는다 — 샘플러 × pass 마다 std::regex 를 돌리면 큰 효과가 몇 초 걸렸다)
		std::map<std::string, GLenum> shadowDecl;
		for (const auto& pass : e->Src.Passes)
			for (const auto& stage : pass.Stages)
			{
				const std::string& g = stage.Glsl;
				for (size_t at = g.find("uniform "); at != std::string::npos; at = g.find("uniform ", at + 8))
				{
					std::istringstream line(g.substr(at, g.find(';', at) - at));
					std::string word, type, name;
					line >> word;   // uniform
					while (line >> word)
					{
						if (word.rfind("sampler", 0) == 0) { type = word; line >> name; break; }
						if (word != "highp" && word != "mediump" && word != "lowp") break;
					}
					if (type.size() > 6 && type.compare(type.size() - 6, 6, "Shadow") == 0 && !name.empty())
						shadowDecl[name] = type == "sampler2DArrayShadow" ? GL_TEXTURE_2D_ARRAY : type == "samplerCubeShadow" ? GL_TEXTURE_CUBE_MAP : type == "sampler2DShadow" ? GL_TEXTURE_2D : 0;
				}
			}
		for (const auto& [n, s] : e->Src.Samplers)
		{
			auto it = shadowDecl.find(n);
			const GLenum target = it != shadowDecl.end() ? it->second : 0;
			for (int i = 0; target && i < s.Count; ++i)
				e->UnitShadowTarget[s.Unit + i] = target;
		}
		EditorLog::Write("GLES", "effect %s: %d techniques, %zu blocks, %d texture units", stem.c_str(), (int)e->TechniqueNames.size(), e->Blocks.size(), units);
		return e;
	}

	void ESEffect::Apply(int technique, int pass)
	{
		if (technique < 0 || technique >= (int)Programs.size() || pass < 0 || pass >= (int)Programs[technique].size())
			return;
		PassProgram& pp = Programs[technique][pass];
		if (!pp.Built)
			BuildPass(Src, Name, TechniqueNames[technique] + "/" + std::to_string(pass), pp);
		if (!pp.Program)
		{
			if (Reported.insert(TechniqueNames[technique] + "/" + std::to_string(pass)).second)
				EditorLog::Write("GLES", "pass %s/%d is not available: %s", TechniqueNames[technique].c_str(), pass, pp.Error.c_str());
			Device->CurrentProgram = 0;
			glUseProgram(0);
			return;
		}
		glUseProgram(pp.Program);
		Device->CurrentProgram = pp.Program;
		for (Block& b : Blocks)
		{
			if (b.Dirty)
			{
				glBindBuffer(GL_UNIFORM_BUFFER, b.Ubo);
				glBufferSubData(GL_UNIFORM_BUFFER, 0, b.Cpu.size(), b.Cpu.data());
				b.Dirty = false;
			}
			glBindBufferBase(GL_UNIFORM_BUFFER, b.Binding, b.Ubo);
		}
		for (size_t u = 0; u < UnitTextures.size(); ++u)
		{
			glActiveTexture(GL_TEXTURE0 + (GLenum)u);
			GLuint tex = UnitTextures[u];
			GLenum target = UnitTargets[u];
			if (!tex && UnitViews[u])
			{
				unsigned int t = 0;
				tex = GfxGLES_ResolveView(UnitViews[u].Get(), &t);
				target = t;
				glActiveTexture(GL_TEXTURE0 + (GLenum)u);   // 사본 새로 고침이 유닛을 바꿨을 수 있다
			}
			if (!tex && UnitShadowTarget[u])
			{
				glBindTexture(UnitShadowTarget[u], Device->DummyShadow(UnitShadowTarget[u]));
				glBindSampler((GLuint)u, UnitSamplers[u] ? UnitSamplers[u] : Device->DummyCompareSampler());
				continue;
			}
			if (tex) glBindTexture(target, tex);
			else { glBindTexture(GL_TEXTURE_2D, 0); glBindTexture(GL_TEXTURE_2D_ARRAY, 0); glBindTexture(GL_TEXTURE_CUBE_MAP, 0); }
			glBindSampler((GLuint)u, tex ? (UnitSamplers[u] ? UnitSamplers[u] : Device->PointSampler()) : 0);
		}
		glActiveTexture(GL_TEXTURE0);
		// compute 자원: SSBO · image (D3D 의 SRV · UAV)
		for (size_t i = 0; i < Ssbos.size(); ++i)
		{
			const SsboBind& b = Ssbos[i];
			if (b.Buffer && b.Size > 0)
				glBindBufferRange(GL_SHADER_STORAGE_BUFFER, (GLuint)i, b.Buffer, b.Offset, b.Size);
			else
				glBindBufferBase(GL_SHADER_STORAGE_BUFFER, (GLuint)i, 0);
		}
		for (size_t i = 0; i < Images.size(); ++i)
		{
			const ImageBind& im = Images[i];
			if (im.Texture)
				glBindImageTexture((GLuint)i, im.Texture, im.Level, GL_FALSE, 0, GL_READ_WRITE, im.Format);
			else
				glBindImageTexture((GLuint)i, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
		}
		// pass 가 정한 상태만 (Effects11 과 같이)
		const FxParser::Pass& p = *pp.Fx;
		if (!pp.StatesMade)
		{
			pp.StatesMade = true;
			auto find = [&](const std::string& name) -> const FxParser::StateBlock* {
				if (name.empty()) return nullptr;
				auto it = Src.Fx.States.find(name);
				return it != Src.Fx.States.end() ? &it->second : nullptr;
			};
			if (const auto* rs = find(p.RasterizerState)) { pp.Rs = FxStates::Rasterizer(*rs); pp.HasRs = true; }
			if (const auto* bs = find(p.BlendState)) { pp.Bs = FxStates::Blend(*bs); pp.HasBs = true; }
			if (const auto* ds = find(p.DepthStencilState)) { pp.Ds = FxStates::DepthStencil(*ds); pp.HasDs = true; }
		}
		if (Device->SinkContext)
		{
			// Gfx 장치 위: 상태 객체로 Gfx 컨텍스트에 (OMGet… 이 맞고, 읽기 전용 깊이 DSV 를 Gfx 가 지킨다)
			if (pp.HasRs && !pp.SinkRs) Device->SinkDevice->CreateRasterizerState(&pp.Rs, pp.SinkRs.GetAddressOf());
			if (pp.HasBs && !pp.SinkBs) Device->SinkDevice->CreateBlendState(&pp.Bs, pp.SinkBs.GetAddressOf());
			if (pp.HasDs && !pp.SinkDs) Device->SinkDevice->CreateDepthStencilState(&pp.Ds, pp.SinkDs.GetAddressOf());
			if (pp.SinkRs) Device->SinkContext->RSSetState(pp.SinkRs.Get());
			if (pp.SinkBs) Device->SinkContext->OMSetBlendState(pp.SinkBs.Get(), p.BlendFactor, p.SampleMask);
			if (pp.SinkDs) Device->SinkContext->OMSetDepthStencilState(pp.SinkDs.Get(), (UINT)p.StencilRef);
			return;
		}
		if (pp.HasRs) ApplyRasterizer(pp.Rs);
		if (pp.HasBs) ApplyBlend(pp.Bs, p.BlendFactor, p.SampleMask);
		if (pp.HasDs) ApplyDepthStencil(pp.Ds, (UINT)p.StencilRef);
	}
}

// 지금 현재인 EGL 컨텍스트 (OpenGL ES 3.2) 위의 RHI 장치
std::unique_ptr<Rhi::Device> CreateGlesRhiDevice(std::string& error)
{
	const GLubyte* v = glGetString(GL_VERSION);
	if (!v) { error = "no current OpenGL ES context"; return nullptr; }
	return std::make_unique<ESDevice>();
}

// Gfx(GLES) 장치와 같은 컨텍스트 위의 RHI 장치 — pass 상태는 sink(Gfx 컨텍스트)로
std::unique_ptr<Rhi::Device> CreateGlesRhiDeviceOnGfx(GfxDevice* sinkDevice, GfxContext* sinkContext, std::string& error)
{
	const GLubyte* v = glGetString(GL_VERSION);
	if (!v) { error = "no current OpenGL ES context"; return nullptr; }
	auto d = std::make_unique<ESDevice>();
	d->SinkDevice = sinkDevice;
	d->SinkContext = sinkContext;
	return d;
}
