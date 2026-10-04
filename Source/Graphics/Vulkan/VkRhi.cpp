#include "pch.h"
#include "Rhi.h"
#include "GfxVk.h"
#include "GfxVkShared.h"
#include "ShaderCross.h"
#include "FxStates.h"   // .fx 상태 블록 → D3D11 설명 (API 와 상관없는 변환)
#include "GLShared.h"   // GLInputSignature (의미 → location)

// RHI 의 Vulkan 구현: Gfx Vulkan 장치 · 컨텍스트 위의 얇은 층.
//  - 효과: ShaderCross::CompileEffectSpirv 로 .fx → pass 마다 단계별 SPIR-V (자원 = 집합 0, 이름마다 고정 바인딩).
//    cbuffer = CPU 사본 → Apply 때 링에 (바뀌었거나 링 위치가 사라졌을 때만), 텍스처 = 바인딩 원소, 샘플러 = .fx 의 상태로 만든 Gfx 샘플러.
//    (RW)StructuredBuffer · (RW)ByteAddressBuffer = 스토리지 버퍼 (버퍼 SRV · UAV), RWTexture = 스토리지 이미지 (compute — 오클루전 컬링)
//    pass 상태는 Gfx 상태 객체로 컨텍스트에 (OMGet… 저장 · 복원이 맞도록 — GL 의 sink 와 같음)
//  - 그 밖의 Rhi::Device 함수는 Gfx 호출 그대로 (Rhi 텍스처 = Gfx 텍스처 + 뷰)
namespace
{
	using namespace GfxVkShared;

	std::string Upper(std::string s)
	{
		for (char& c : s) c = (char)toupper((unsigned char)c);
		return s;
	}

	// "float4(1.0f, 0.5f, ...)" / "{ 0.05f, ... }" / "true" → 숫자들 (GLRhi 와 같은 규칙)
	std::vector<double> Numbers(const std::string& text)
	{
		std::vector<double> out;
		const char* p = text.c_str();
		while (*p)
		{
			if ((*p >= '0' && *p <= '9') || ((*p == '-' || *p == '+' || *p == '.') && (isdigit((unsigned char)p[1]) || p[1] == '.')))
			{
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

	VkShaderStageFlagBits StageFlag(FxParser::Stage s)
	{
		switch (s)
		{
		case FxParser::Stage::Hull: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
		case FxParser::Stage::Domain: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
		case FxParser::Stage::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
		case FxParser::Stage::Pixel: return VK_SHADER_STAGE_FRAGMENT_BIT;
		case FxParser::Stage::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
		default: return VK_SHADER_STAGE_VERTEX_BIT;
		}
	}

	DXGI_FORMAT DxgiOf(Rhi::Format f)
	{
		switch (f)
		{
		case Rhi::Format::RGBA16_Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case Rhi::Format::RGBA32_Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case Rhi::Format::R32_Float: return DXGI_FORMAT_R32_FLOAT;
		case Rhi::Format::RG16_Float: return DXGI_FORMAT_R16G16_FLOAT;
		case Rhi::Format::D32_Float: return DXGI_FORMAT_R32_TYPELESS;
		case Rhi::Format::D24_UNorm_S8_UInt: return DXGI_FORMAT_R24G8_TYPELESS;
		default: return DXGI_FORMAT_R8G8B8A8_UNORM;
		}
	}

	class VkRhiBuffer : public Rhi::Buffer
	{
	public:
		explicit VkRhiBuffer(const Rhi::BufferDesc& d) { _desc = d; }
		ComPtr<GfxBuffer> Buf;
	};

	class VkRhiTexture : public Rhi::Texture
	{
	public:
		explicit VkRhiTexture(const Rhi::TextureDesc& d) { _desc = d; }
		ComPtr<GfxTexture2D> Tex;
		ComPtr<GfxShaderResourceView> Srv;
		ComPtr<GfxRenderTargetView> Rtv;
		ComPtr<GfxDepthStencilView> Dsv;                         // 모든 조각
		std::map<uint32_t, ComPtr<GfxDepthStencilView>> SliceDsv;  // 조각 하나 (그림자 캐스케이드)
	};

	class VkRhiInputLayout : public Rhi::InputLayout
	{
	public:
		ComPtr<GfxInputLayout> Layout;
	};

	class VkRhiDevice;

	class VkEffect : public Rhi::Effect
	{
	public:
		struct Block { std::string Name; int Binding = 0; uint32_t Element = 0; std::vector<uint8_t> Cpu; bool Dirty = true; RingLoc Loc; bool HasLoc = false; const ShaderCross::UniformBlock* Info = nullptr; };
		struct Var
		{
			int BlockIndex = -1;                                   // cbuffer 멤버
			const ShaderCross::UniformBlock::Member* Member = nullptr;
			uint32_t Element = 0;                                  // 텍스처 · 스토리지: 첫 원소
			uint32_t Count = 0;                                    // 텍스처 배열 원소 수 (0 = 텍스처 아님)
			bool Storage = false;                                  // 스토리지 버퍼 · 이미지 (SetView = 버퍼 SRV, SetUav)
		};
		struct PassProgram
		{
			ComPtr<GfxObject> Program;
			std::string Error;
			const FxParser::Pass* Fx = nullptr;
			GLInputSignature Signature;
			bool StatesMade = false;
			ComPtr<GfxRasterizerState> Rs;
			ComPtr<GfxBlendState> Bs;
			ComPtr<GfxDepthStencilState> Ds;
		};

		VkRhiDevice* Device = nullptr;
		ShaderCross::EffectSpirv Src;
		std::vector<std::string> TechniqueNames;
		std::vector<std::vector<PassProgram>> Programs;
		std::vector<Block> Blocks;
		std::vector<Var> Vars;
		std::map<std::string, Rhi::VarId> VarIds;
		ComPtr<GfxObject> Layout;
		std::vector<BindingValue> Values;                    // 원소마다 (BindingLayout 순서)
		std::vector<ComPtr<GfxShaderResourceView>> Views;    // 원소마다 (텍스처 칸이 뷰를 잡는다)
		std::vector<ComPtr<GfxUnorderedAccessView>> Uavs;   // 원소마다 (스토리지 칸)
		std::vector<ComPtr<GfxSamplerState>> Samplers;
		std::set<std::string> Reported;
		std::string FileName;

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
			{
				auto r = Src.Resources.find(name);
				if (r != Src.Resources.end() && r->second.Type == ShaderCross::ResourceBinding::Kind::SampledImage)
				{
					v.Element = ElementOffset(Layout.Get(), (uint32_t)r->second.Binding);
					v.Count = (uint32_t)r->second.Count;
				}
				else if (r != Src.Resources.end() && (r->second.Type == ShaderCross::ResourceBinding::Kind::StorageBuffer ||
					r->second.Type == ShaderCross::ResourceBinding::Kind::StorageImage))
				{
					v.Element = ElementOffset(Layout.Get(), (uint32_t)r->second.Binding);
					v.Storage = true;
				}
			}
			if (!v.Member && !v.Count && !v.Storage)
				return -1;   // 셰이더가 쓰지 않아 지워진 변수
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

		void SetMatrix(Rhi::VarId var, const float m[16]) override { if (const Var* v = Uniform(var)) WriteMatrix(*v, 0, m); }

		void SetMatrixArray(Rhi::VarId var, const float* m, uint32_t first, uint32_t count) override
		{
			const Var* v = Uniform(var);
			if (!v) return;
			const uint32_t stride = v->Member->ArrayStride ? v->Member->ArrayStride : 64;
			for (uint32_t i = 0; i < count; ++i)
				WriteMatrix(*v, (first + i) * stride, m + i * 16);
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

		void SetView(Rhi::VarId var, GfxShaderResourceView* view, uint32_t arrayIndex) override
		{
			if (var >= 0 && var < (int)Vars.size() && Vars[var].Storage)
			{
				// 구조 · raw 버퍼 SRV → 스토리지 버퍼 (UAV 를 비운다 — 같은 칸에 하나만)
				const uint32_t e = Vars[var].Element;
				if (view && !GfxVk::IsVulkan(view)) view = nullptr;
				Views[e] = view;
				Values[e].View = view;
				Uavs[e] = nullptr;
				Values[e].Uav = nullptr;
				return;
			}
			if (var < 0 || var >= (int)Vars.size() || !Vars[var].Count || arrayIndex >= Vars[var].Count) return;
			if (view && !GfxVk::IsVulkan(view))
			{
				if (Reported.insert("non-vk-view").second)
					EditorLog::Write("Vulkan", "%s: a texture view that is not a Vulkan view was set - ignored", FileName.c_str());
				view = nullptr;
			}
			const uint32_t e = Vars[var].Element + arrayIndex;
			Views[e] = view;
			Values[e].View = view;
		}

		void SetTexture(Rhi::VarId var, Rhi::Texture* texture, uint32_t arrayIndex) override
		{
			SetView(var, texture ? static_cast<VkRhiTexture*>(texture)->Srv.Get() : nullptr, arrayIndex);
		}

		void SetUav(Rhi::VarId var, GfxUnorderedAccessView* uav) override
		{
			if (var < 0 || var >= (int)Vars.size() || !Vars[var].Storage) return;
			if (uav && !GfxVk::IsVulkan(uav)) uav = nullptr;
			const uint32_t e = Vars[var].Element;
			Uavs[e] = uav;
			Values[e].Uav = uav;
			Views[e] = nullptr;
			Values[e].View = nullptr;
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

	class VkRhiDevice : public Rhi::Device
	{
	public:
		ComPtr<GfxDevice> Dev;
		ComPtr<GfxContext> Ctx;
		Rhi::Topology Topo = Rhi::Topology::TriangleList;

		GraphicsAPI GetAPI() const override { return GraphicsAPI::Vulkan; }
		std::string Description() const override { return GfxVk::Description(Dev.Get()); }

		std::unique_ptr<Rhi::Buffer> CreateBuffer(const Rhi::BufferDesc& desc, const void* initialData) override
		{
			auto b = std::make_unique<VkRhiBuffer>(desc);
			D3D11_BUFFER_DESC bd = {};
			bd.ByteWidth = desc.Size;
			bd.Usage = desc.Dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
			bd.CPUAccessFlags = desc.Dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
			if (desc.Bind & Rhi::BindVertex) bd.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
			if (desc.Bind & Rhi::BindIndex) bd.BindFlags |= D3D11_BIND_INDEX_BUFFER;
			if (desc.Bind & Rhi::BindConstant) bd.BindFlags |= D3D11_BIND_CONSTANT_BUFFER;
			D3D11_SUBRESOURCE_DATA sd = { initialData };
			if (FAILED(Dev->CreateBuffer(&bd, initialData ? &sd : nullptr, b->Buf.GetAddressOf()))) return nullptr;
			return b;
		}

		void UpdateBuffer(Rhi::Buffer* buffer, const void* data, uint32_t bytes) override
		{
			auto* b = static_cast<VkRhiBuffer*>(buffer);
			if (b->Desc().Dynamic)
			{
				D3D11_MAPPED_SUBRESOURCE m;
				if (SUCCEEDED(Ctx->Map(b->Buf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
				{
					memcpy(m.pData, data, (std::min)(bytes, b->Desc().Size));
					Ctx->Unmap(b->Buf.Get(), 0);
				}
				return;
			}
			const D3D11_BOX box = { 0, 0, 0, (std::min)(bytes, b->Desc().Size), 1, 1 };
			Ctx->UpdateSubresource(b->Buf.Get(), 0, &box, data, 0, 0);
		}

		std::unique_ptr<Rhi::Texture> CreateTexture(const Rhi::TextureDesc& desc, const Rhi::SubresourceData* initialData) override
		{
			auto t = std::make_unique<VkRhiTexture>(desc);
			const bool depth = Rhi::IsDepth(desc.Format);
			const uint32_t slices = desc.Type == Rhi::TextureType::Cube ? 6 : (std::max)(1u, desc.ArraySize);
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = desc.Width;
			td.Height = desc.Height;
			td.MipLevels = desc.MipLevels;
			td.ArraySize = slices;
			td.Format = DxgiOf(desc.Format);
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | (desc.RenderTarget ? (depth ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET) : 0);
			if (depth) td.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
			td.MiscFlags = desc.Type == Rhi::TextureType::Cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
			std::vector<D3D11_SUBRESOURCE_DATA> sd;
			if (initialData)
				for (uint32_t i = 0; i < slices * desc.MipLevels; ++i)
				{
					const uint32_t m = i % desc.MipLevels;
					sd.push_back({ initialData[i].Data, initialData[i].RowPitch ? initialData[i].RowPitch : (std::max)(1u, desc.Width >> m) * Rhi::BytesPerPixel(desc.Format), 0 });
				}
			if (FAILED(Dev->CreateTexture2D(&td, initialData ? sd.data() : nullptr, t->Tex.GetAddressOf()))) return nullptr;
			D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
			sv.Format = desc.Format == Rhi::Format::D32_Float ? DXGI_FORMAT_R32_FLOAT : desc.Format == Rhi::Format::D24_UNorm_S8_UInt ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : td.Format;
			if (desc.Type == Rhi::TextureType::Cube) { sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE; sv.TextureCube.MipLevels = desc.MipLevels; }
			else if (desc.Type == Rhi::TextureType::Tex2DArray) { sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY; sv.Texture2DArray.MipLevels = desc.MipLevels; sv.Texture2DArray.ArraySize = slices; }
			else { sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = desc.MipLevels; }
			Dev->CreateShaderResourceView(t->Tex.Get(), &sv, t->Srv.GetAddressOf());
			if (depth)
			{
				D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
				dd.Format = desc.Format == Rhi::Format::D32_Float ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_D24_UNORM_S8_UINT;
				dd.ViewDimension = slices > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_DSV_DIMENSION_TEXTURE2D;
				dd.Texture2DArray.ArraySize = slices;
				Dev->CreateDepthStencilView(t->Tex.Get(), &dd, t->Dsv.GetAddressOf());
			}
			else if (desc.RenderTarget)
				Dev->CreateRenderTargetView(t->Tex.Get(), nullptr, t->Rtv.GetAddressOf());
			return t;
		}

		std::unique_ptr<Rhi::Effect> LoadEffect(const std::wstring& fxPath, std::string& error) override;

		std::unique_ptr<Rhi::InputLayout> CreateInputLayout(const Rhi::VertexElement* elements, uint32_t count, Rhi::Effect* effect, int technique, int pass, std::string& error) override
		{
			const void* sig = nullptr;
			size_t size = 0;
			if (!effect->NativeInputSignature(technique, pass, &sig, &size))
			{
				error = "bad technique/pass";
				return nullptr;
			}
			std::vector<D3D11_INPUT_ELEMENT_DESC> desc;
			for (uint32_t i = 0; i < count; ++i)
			{
				const Rhi::VertexElement& e = elements[i];
				static const DXGI_FORMAT formats[] = { DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT,
					DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32G32B32A32_UINT };
				desc.push_back({ e.Semantic, e.SemanticIndex, formats[(int)e.Format], e.Slot, e.Offset,
					e.PerInstance ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA, e.PerInstance ? 1u : 0u });
			}
			auto l = std::make_unique<VkRhiInputLayout>();
			if (FAILED(Dev->CreateInputLayout(desc.data(), (UINT)desc.size(), sig, size, l->Layout.GetAddressOf())))
			{
				error = "CreateInputLayout failed (see Editor.log)";
				return nullptr;
			}
			return l;
		}

		void ResetState() override
		{
			Ctx->RSSetState(nullptr);
			Ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
			Ctx->OMSetDepthStencilState(nullptr, 0);
		}

		void SetRenderTargets(Rhi::Texture* const* colors, uint32_t count, Rhi::Texture* depth, uint32_t depthSlice) override
		{
			GfxRenderTargetView* rtvs[8] = {};
			for (uint32_t i = 0; i < count && i < 8; ++i)
				rtvs[i] = colors[i] ? static_cast<VkRhiTexture*>(colors[i])->Rtv.Get() : nullptr;
			GfxDepthStencilView* dsv = nullptr;
			if (auto* d = static_cast<VkRhiTexture*>(depth))
			{
				const uint32_t slices = d->Desc().Type == Rhi::TextureType::Cube ? 6 : d->Desc().ArraySize;
				if (slices <= 1) dsv = d->Dsv.Get();
				else
				{
					ComPtr<GfxDepthStencilView>& v = d->SliceDsv[depthSlice];
					if (!v)
					{
						D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
						dd.Format = d->Desc().Format == Rhi::Format::D32_Float ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_D24_UNORM_S8_UINT;
						dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
						dd.Texture2DArray.FirstArraySlice = depthSlice;
						dd.Texture2DArray.ArraySize = 1;
						Dev->CreateDepthStencilView(d->Tex.Get(), &dd, v.GetAddressOf());
					}
					dsv = v.Get();
				}
			}
			Ctx->OMSetRenderTargets((std::min)(count, 8u), rtvs, dsv);
		}

		void SetViewport(float x, float y, float width, float height) override
		{
			const D3D11_VIEWPORT vp = { x, y, width, height, 0.0f, 1.0f };
			Ctx->RSSetViewports(1, &vp);
		}

		void ClearColor(Rhi::Texture* target, const float rgba[4]) override
		{
			if (auto* t = static_cast<VkRhiTexture*>(target); t && t->Rtv) Ctx->ClearRenderTargetView(t->Rtv.Get(), rgba);
		}

		void ClearDepth(Rhi::Texture* target, float depth) override
		{
			if (auto* t = static_cast<VkRhiTexture*>(target); t && t->Dsv) Ctx->ClearDepthStencilView(t->Dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth, 0);
		}

		void SetInputLayout(Rhi::InputLayout* layout) override { Ctx->IASetInputLayout(layout ? static_cast<VkRhiInputLayout*>(layout)->Layout.Get() : nullptr); }

		void SetVertexBuffer(uint32_t slot, Rhi::Buffer* buffer, uint32_t stride, uint32_t offset) override
		{
			GfxBuffer* b = buffer ? static_cast<VkRhiBuffer*>(buffer)->Buf.Get() : nullptr;
			Ctx->IASetVertexBuffers(slot, 1, &b, &stride, &offset);
		}

		void SetIndexBuffer(Rhi::Buffer* buffer, bool use32Bit) override
		{
			Ctx->IASetIndexBuffer(buffer ? static_cast<VkRhiBuffer*>(buffer)->Buf.Get() : nullptr, use32Bit ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
		}

		void SetTopology(Rhi::Topology topology) override
		{
			static const D3D11_PRIMITIVE_TOPOLOGY map[] = { D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
				D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST };
			Ctx->IASetPrimitiveTopology(map[(int)topology]);
		}

		void Draw(uint32_t vertexCount, uint32_t startVertex) override { Ctx->Draw(vertexCount, startVertex); }
		void DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex) override { Ctx->DrawIndexed(indexCount, startIndex, baseVertex); }
		void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance) override
		{
			Ctx->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance);
		}

		bool ReadPixels(Rhi::Texture* texture, std::vector<uint8_t>& rgba, std::string& error) override
		{
			auto* t = static_cast<VkRhiTexture*>(texture);
			DirectX::ScratchImage img;
			if (FAILED(GfxVk::CaptureTexture(Ctx.Get(), t->Tex.Get(), img)))
			{
				error = "capture failed";
				return false;
			}
			const DirectX::Image* i = img.GetImage(0, 0, 0);
			const uint32_t w = t->Desc().Width, h = t->Desc().Height;
			rgba.resize((size_t)w * h * 4);
			if (t->Desc().Format == Rhi::Format::RGBA8_UNorm)
			{
				for (uint32_t y = 0; y < h; ++y)
					memcpy(rgba.data() + (size_t)y * w * 4, i->pixels + y * i->rowPitch, (size_t)w * 4);
				return true;
			}
			if (Rhi::IsDepth(t->Desc().Format))
			{
				for (uint32_t y = 0; y < h; ++y)
					for (uint32_t x = 0; x < w; ++x)
					{
						uint32_t raw;
						memcpy(&raw, i->pixels + y * i->rowPitch + x * 4, 4);
						float d;
						if (t->Desc().Format == Rhi::Format::D32_Float) memcpy(&d, &raw, 4);
						else d = (raw & 0xFFFFFF) / 16777215.0f;
						const uint8_t v = (uint8_t)std::clamp(d * 255.0f + 0.5f, 0.0f, 255.0f);
						uint8_t* p = rgba.data() + ((size_t)y * w + x) * 4;
						p[0] = p[1] = p[2] = v;
						p[3] = 255;
					}
				return true;
			}
			DirectX::ScratchImage conv;
			if (FAILED(DirectX::Convert(*i, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, conv)))
			{
				error = "format conversion failed";
				return false;
			}
			const DirectX::Image* c = conv.GetImage(0, 0, 0);
			for (uint32_t y = 0; y < h; ++y)
				memcpy(rgba.data() + (size_t)y * w * 4, c->pixels + y * c->rowPitch, (size_t)w * 4);
			return true;
		}

		void Finish() override { GfxVk::WaitIdle(Dev.Get()); }
	};

	std::unique_ptr<Rhi::Effect> VkRhiDevice::LoadEffect(const std::wstring& fxPath, std::string& error)
	{
		auto e = std::make_unique<VkEffect>();
		e->Device = this;
		e->FileName = wstring_to_string(std::filesystem::path(fxPath).filename().wstring());
		if (!ShaderCross::CompileEffectSpirv(fxPath, e->Src) || !e->Src.Error.empty())
		{
			error = e->Src.Error.empty() ? "shader conversion failed" : e->Src.Error;
			return nullptr;
		}
		// 바인딩 배치 (효과 하나 = 집합 하나)
		std::vector<BindingDesc> bindings(e->Src.BindingCount);
		for (const auto& [name, b] : e->Src.Blocks)
			if (b.Binding >= 0 && b.Binding < e->Src.BindingCount)
				bindings[b.Binding].Type = BindingType::UniformBuffer;
		for (const auto& [name, r] : e->Src.Resources)
		{
			if (r.Binding < 0 || r.Binding >= e->Src.BindingCount) continue;
			BindingDesc& d = bindings[r.Binding];
			d.Count = (uint32_t)(std::max)(1, r.Count);
			d.Dim = r.Dim;
			d.Arrayed = r.Arrayed;
			d.Depth = r.Depth;
			d.Integer = r.Integer;
			d.Comparison = r.Comparison;
			switch (r.Type)
			{
			case ShaderCross::ResourceBinding::Kind::SampledImage: d.Type = BindingType::SampledImage; break;
			case ShaderCross::ResourceBinding::Kind::Sampler: d.Type = BindingType::Sampler; break;
			case ShaderCross::ResourceBinding::Kind::StorageBuffer: d.Type = BindingType::StorageBuffer; break;
			case ShaderCross::ResourceBinding::Kind::StorageImage: d.Type = BindingType::StorageImage; break;
			default: d.Type = BindingType::Unsupported; break;   // 형식 버퍼 (Buffer<T> — texel buffer)
			}
		}
		if (FAILED(CreateBindingLayout(Dev.Get(), bindings.data(), (uint32_t)bindings.size(), e->Layout.GetAddressOf(), error)))
			return nullptr;
		const uint32_t elements = ElementCount(e->Layout.Get());
		e->Values.assign(elements, {});
		e->Views.assign(elements, nullptr);
		e->Uavs.assign(elements, nullptr);

		// pass 마다 프로그램 (실패는 그 pass 만 못 쓴다)
		const std::string stem = wstring_to_string(std::filesystem::path(fxPath).stem().wstring());
		size_t index = 0;
		for (const FxParser::Technique& tech : e->Src.Fx.Techniques)
		{
			e->TechniqueNames.push_back(tech.Name);
			std::vector<VkEffect::PassProgram> passes;
			for (const FxParser::Pass& pass : tech.Passes)
			{
				const ShaderCross::PassSpirv& ps = e->Src.Passes[index++];
				VkEffect::PassProgram pp;
				pp.Fx = &pass;
				pp.Error = ps.Error;
				pp.Signature.Inputs = &ps.VertexInputs;
				if (pp.Error.empty())
					for (int b : ps.Bindings)
						if (b >= 0 && b < (int)bindings.size() && bindings[b].Type == BindingType::Unsupported)
							pp.Error = "uses typed buffers (Buffer<T> - not supported on Vulkan yet)";
				if (pp.Error.empty())
				{
					std::vector<StageCode> stages;
					for (const auto& s : ps.Stages)
						stages.push_back({ StageFlag(s.StageType), &s.Code, s.Entry });
					CreateProgram(Dev.Get(), e->Layout.Get(), stages.data(), (uint32_t)stages.size(), ps.VertexInputs, ps.PixelOutputs, ps.Bindings,
						stem + "/" + tech.Name + "/" + pass.Name, pp.Program.GetAddressOf(), pp.Error);
				}
				if (!pp.Error.empty())
					EditorLog::Write("Vulkan", "%s %s/%s: %s", e->FileName.c_str(), tech.Name.c_str(), pass.Name.c_str(), pp.Error.c_str());
				passes.push_back(std::move(pp));
			}
			e->Programs.push_back(std::move(passes));
		}
		// cbuffer: CPU 사본 (초기값 포함)
		for (const auto& [name, info] : e->Src.Blocks)
		{
			VkEffect::Block b;
			b.Name = name;
			b.Binding = info.Binding;
			b.Element = ElementOffset(e->Layout.Get(), (uint32_t)info.Binding);
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
			if (b.Cpu.empty()) b.Cpu.assign(16, 0);
			e->Blocks.push_back(std::move(b));
		}
		// 샘플러: .fx 의 샘플러 상태 → Gfx 샘플러 (원소마다 같은 것)
		for (const auto& [name, r] : e->Src.Resources)
		{
			if (r.Type != ShaderCross::ResourceBinding::Kind::Sampler) continue;
			auto st = e->Src.Fx.States.find(name);
			const D3D11_SAMPLER_DESC sd = st != e->Src.Fx.States.end() ? FxStates::Sampler(st->second) : FxStates::DefaultSampler();
			ComPtr<GfxSamplerState> s;
			if (FAILED(Dev->CreateSamplerState(&sd, s.GetAddressOf()))) continue;
			const uint32_t first = ElementOffset(e->Layout.Get(), (uint32_t)r.Binding);
			for (int i = 0; i < (std::max)(1, r.Count); ++i)
				e->Values[first + i].Sampler = s.Get();
			e->Samplers.push_back(s);
		}
		return e;
	}

	void VkEffect::Apply(int technique, int pass)
	{
		if (technique < 0 || technique >= (int)Programs.size() || pass < 0 || pass >= (int)Programs[technique].size())
			return;
		PassProgram& pp = Programs[technique][pass];
		GfxContext* ctx = Device->Ctx.Get();
		if (!pp.Program)
		{
			if (Reported.insert(TechniqueNames[technique] + "/" + std::to_string(pass)).second)
				EditorLog::Write("Vulkan", "%s pass %s/%d is not available: %s", FileName.c_str(), TechniqueNames[technique].c_str(), pass, pp.Error.c_str());
			SetProgram(ctx, nullptr, nullptr, 0);   // 앞 pass 로 잘못 그리지 않게
			return;
		}
		// cbuffer: 바뀌었거나 앞 링 위치를 이번 기록에서 쓸 수 없으면 새로 쓴다
		for (Block& b : Blocks)
		{
			if (b.Dirty || !b.HasLoc || !IsCurrent(Device->Dev.Get(), b.Loc))
			{
				b.HasLoc = WriteConstants(Device->Dev.Get(), b.Cpu.data(), (uint32_t)b.Cpu.size(), b.Loc);
				b.Dirty = false;
			}
			Values[b.Element].Ubo = b.Loc;
			Values[b.Element].Range = (uint32_t)b.Cpu.size();
		}
		// pass 상태 (정하지 않은 것은 그대로 = Effects11)
		const FxParser::Pass& p = *pp.Fx;
		if (!pp.StatesMade)
		{
			pp.StatesMade = true;
			auto find = [&](const std::string& name) -> const FxParser::StateBlock* {
				if (name.empty()) return nullptr;
				auto it = Src.Fx.States.find(name);
				return it != Src.Fx.States.end() ? &it->second : nullptr;
			};
			if (const auto* rs = find(p.RasterizerState)) { const D3D11_RASTERIZER_DESC d = FxStates::Rasterizer(*rs); Device->Dev->CreateRasterizerState(&d, pp.Rs.GetAddressOf()); }
			if (const auto* bs = find(p.BlendState)) { const D3D11_BLEND_DESC d = FxStates::Blend(*bs); Device->Dev->CreateBlendState(&d, pp.Bs.GetAddressOf()); }
			if (const auto* ds = find(p.DepthStencilState)) { const D3D11_DEPTH_STENCIL_DESC d = FxStates::DepthStencil(*ds); Device->Dev->CreateDepthStencilState(&d, pp.Ds.GetAddressOf()); }
		}
		if (pp.Rs) ctx->RSSetState(pp.Rs.Get());
		if (pp.Bs) ctx->OMSetBlendState(pp.Bs.Get(), p.BlendFactor, p.SampleMask);
		if (pp.Ds) ctx->OMSetDepthStencilState(pp.Ds.Get(), (UINT)p.StencilRef);
		SetProgram(ctx, pp.Program.Get(), Values.data(), (uint32_t)Values.size());
	}
}

// Gfx Vulkan 장치 · 컨텍스트 위의 RHI 장치 (GfxVk::CreateRhiDevice)
std::unique_ptr<Rhi::Device> CreateVkRhiDevice(GfxDevice* device, GfxContext* context, std::string& error)
{
	if (!GfxVk::IsVulkan(device) || !GfxVk::IsVulkan(context))
	{
		error = "not a Vulkan device";
		return nullptr;
	}
	auto d = std::make_unique<VkRhiDevice>();
	d->Dev = device;
	d->Ctx = context;
	return d;
}

// 화면 없는 Vulkan 장치 + RHI (Rhi::CreateDevice(Vulkan) — 검사용). RHI 장치가 Gfx 장치를 잡는다
std::unique_ptr<Rhi::Device> CreateVkRhiDeviceHeadless(std::string& error)
{
	ComPtr<GfxDevice> dev;
	ComPtr<GfxContext> ctx;
	if (!GfxVk::CreateDevice(nullptr, dev.GetAddressOf(), ctx.GetAddressOf(), error))
		return nullptr;
	return CreateVkRhiDevice(dev.Get(), ctx.Get(), error);
}
