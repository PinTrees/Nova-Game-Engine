#include "pch.h"
#include "Rhi.h"
#include "Application.h"
#include "ShaderCache.h"

// RHI 의 DirectX 11 구현: 에디터가 만든 장치·컨텍스트를 그대로 쓰고, 효과는 Effects11 (지금 렌더러와 같은 경로)
namespace
{
	DXGI_FORMAT TexFormat(Rhi::Format f)
	{
		switch (f)
		{
		case Rhi::Format::RGBA8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
		case Rhi::Format::RGBA16_Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case Rhi::Format::RGBA32_Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case Rhi::Format::R32_Float: return DXGI_FORMAT_R32_FLOAT;
		case Rhi::Format::RG16_Float: return DXGI_FORMAT_R16G16_FLOAT;
		case Rhi::Format::D32_Float: return DXGI_FORMAT_R32_TYPELESS;
		case Rhi::Format::D24_UNorm_S8_UInt: return DXGI_FORMAT_R24G8_TYPELESS;
		default: return DXGI_FORMAT_UNKNOWN;
		}
	}

	DXGI_FORMAT SrvFormat(Rhi::Format f)
	{
		if (f == Rhi::Format::D32_Float) return DXGI_FORMAT_R32_FLOAT;
		if (f == Rhi::Format::D24_UNorm_S8_UInt) return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		return TexFormat(f);
	}

	DXGI_FORMAT DsvFormat(Rhi::Format f)
	{
		return f == Rhi::Format::D32_Float ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_D24_UNORM_S8_UINT;
	}

	DXGI_FORMAT VertexFormatDxgi(Rhi::VertexFormat f)
	{
		switch (f)
		{
		case Rhi::VertexFormat::Float1: return DXGI_FORMAT_R32_FLOAT;
		case Rhi::VertexFormat::Float2: return DXGI_FORMAT_R32G32_FLOAT;
		case Rhi::VertexFormat::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
		case Rhi::VertexFormat::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case Rhi::VertexFormat::UByte4_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
		case Rhi::VertexFormat::UInt4: return DXGI_FORMAT_R32G32B32A32_UINT;
		}
		return DXGI_FORMAT_UNKNOWN;
	}

	class DxBuffer : public Rhi::Buffer
	{
	public:
		DxBuffer(const Rhi::BufferDesc& d) { _desc = d; }
		ComPtr<ID3D11Buffer> Buf;
	};

	class DxTexture : public Rhi::Texture
	{
	public:
		DxTexture(const Rhi::TextureDesc& d) { _desc = d; }
		ComPtr<ID3D11Texture2D> Tex;
		ComPtr<ID3D11ShaderResourceView> Srv;
		ComPtr<ID3D11RenderTargetView> Rtv;
		ComPtr<ID3D11DepthStencilView> Dsv;
		std::vector<ComPtr<ID3D11DepthStencilView>> DsvSlices;   // 배열 깊이: 조각마다
	};

	class DxInputLayout : public Rhi::InputLayout
	{
	public:
		ComPtr<ID3D11InputLayout> Layout;
	};

	class DxEffect : public Rhi::Effect
	{
	public:
		ComPtr<ID3DX11Effect> Fx;
		ID3D11DeviceContext* Ctx = nullptr;
		std::vector<ID3DX11EffectTechnique*> Techniques;
		std::vector<std::string> TechniqueNames;
		std::vector<ID3DX11EffectVariable*> Vars;
		std::map<std::string, Rhi::VarId> VarIds;

		int FindTechnique(const std::string& name) const override
		{
			for (size_t i = 0; i < TechniqueNames.size(); ++i)
				if (TechniqueNames[i] == name) return (int)i;
			return -1;
		}

		int PassCount(int technique) const override
		{
			if (technique < 0 || technique >= (int)Techniques.size()) return 0;
			D3DX11_TECHNIQUE_DESC d;
			Techniques[technique]->GetDesc(&d);
			return (int)d.Passes;
		}

		Rhi::VarId FindVariable(const std::string& name) override
		{
			auto it = VarIds.find(name);
			if (it != VarIds.end()) return it->second;
			ID3DX11EffectVariable* v = Fx->GetVariableByName(name.c_str());
			if (!v || !v->IsValid()) return -1;
			Vars.push_back(v);
			return VarIds[name] = (int)Vars.size() - 1;
		}

		int TechniqueCount() const override { return (int)Techniques.size(); }
		std::string TechniqueName(int technique) const override { return technique >= 0 && technique < (int)TechniqueNames.size() ? TechniqueNames[technique] : std::string(); }

		void SetRaw(Rhi::VarId var, const void* data, uint32_t bytes, uint32_t offset) override
		{
			if (var >= 0) Vars[var]->SetRawValue(data, offset, bytes);
		}
		void GetVector(Rhi::VarId var, float out[4]) override { if (var >= 0) Vars[var]->AsVector()->GetFloatVector(out); }
		void SetFloat(Rhi::VarId var, float v) override { if (var >= 0) Vars[var]->AsScalar()->SetFloat(v); }
		void SetInt(Rhi::VarId var, int v) override { if (var >= 0) Vars[var]->AsScalar()->SetInt(v); }
		void SetBool(Rhi::VarId var, bool v) override { if (var >= 0) Vars[var]->AsScalar()->SetBool(v); }
		void SetVector(Rhi::VarId var, const float v[4]) override { if (var >= 0) Vars[var]->AsVector()->SetFloatVector(v); }
		void SetFloatArray(Rhi::VarId var, const float* v, uint32_t first, uint32_t count) override { if (var >= 0) Vars[var]->AsScalar()->SetFloatArray(v, first, count); }
		void SetVectorArray(Rhi::VarId var, const float* v, uint32_t first, uint32_t count) override { if (var >= 0) Vars[var]->AsVector()->SetFloatVectorArray(v, first, count); }

		void SetView(Rhi::VarId var, GfxShaderResourceView* view, uint32_t arrayIndex) override
		{
			SetNativeTexture(var, view ? static_cast<ID3D11ShaderResourceView*>(view->Native()) : nullptr, arrayIndex);
		}

		void SetNativeTexture(Rhi::VarId var, ID3D11ShaderResourceView* srv, uint32_t arrayIndex)
		{
			if (var < 0) return;
			D3DX11_EFFECT_TYPE_DESC td;
			Vars[var]->GetType()->GetDesc(&td);
			if (td.Elements > 0)
				Vars[var]->AsShaderResource()->SetResourceArray(&srv, arrayIndex, 1);
			else
				Vars[var]->AsShaderResource()->SetResource(srv);
		}

		void SetUav(Rhi::VarId var, GfxUnorderedAccessView* uav) override
		{
			if (var >= 0) Vars[var]->AsUnorderedAccessView()->SetUnorderedAccessView(uav ? static_cast<ID3D11UnorderedAccessView*>(uav->Native()) : nullptr);
		}

		bool NativeInputSignature(int technique, int pass, const void** data, size_t* size) override
		{
			if (technique < 0 || technique >= (int)Techniques.size()) return false;
			D3DX11_PASS_DESC pd;
			if (FAILED(Techniques[technique]->GetPassByIndex(pass)->GetDesc(&pd))) return false;
			*data = pd.pIAInputSignature;
			*size = pd.IAInputSignatureSize;
			return true;
		}

		void SetMatrix(Rhi::VarId var, const float m[16]) override
		{
			if (var >= 0) Vars[var]->AsMatrix()->SetMatrix(m);
		}

		void SetMatrixArray(Rhi::VarId var, const float* m, uint32_t first, uint32_t count) override
		{
			if (var >= 0) Vars[var]->AsMatrix()->SetMatrixArray(m, first, count);
		}

		void SetTexture(Rhi::VarId var, Rhi::Texture* texture, uint32_t arrayIndex) override
		{
			SetNativeTexture(var, texture ? static_cast<DxTexture*>(texture)->Srv.Get() : nullptr, arrayIndex);
		}

		void Apply(int technique, int pass) override
		{
			if (technique >= 0 && technique < (int)Techniques.size())
				Techniques[technique]->GetPassByIndex(pass)->Apply(0, Ctx);
		}
	};

	class Dx11Device : public Rhi::Device
	{
	public:
		ID3D11Device* Dev = nullptr;
		ID3D11DeviceContext* Ctx = nullptr;
		D3D11_PRIMITIVE_TOPOLOGY Topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		GraphicsAPI GetAPI() const override { return GraphicsAPI::DirectX11; }

		std::string Description() const override
		{
			ComPtr<IDXGIDevice> dxgi;
			ComPtr<IDXGIAdapter> adapter;
			DXGI_ADAPTER_DESC ad = {};
			if (SUCCEEDED(Dev->QueryInterface(IID_PPV_ARGS(&dxgi))) && SUCCEEDED(dxgi->GetAdapter(&adapter)))
				adapter->GetDesc(&ad);
			return "Direct3D 11 (" + wstring_to_string(ad.Description) + ")";
		}

		std::unique_ptr<Rhi::Buffer> CreateBuffer(const Rhi::BufferDesc& desc, const void* initialData) override
		{
			auto b = std::make_unique<DxBuffer>(desc);
			D3D11_BUFFER_DESC bd = {};
			bd.ByteWidth = (desc.Bind & Rhi::BindConstant) ? (desc.Size + 15) / 16 * 16 : desc.Size;
			bd.Usage = desc.Dynamic ? D3D11_USAGE_DEFAULT : (initialData ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT);
			bd.BindFlags = ((desc.Bind & Rhi::BindVertex) ? D3D11_BIND_VERTEX_BUFFER : 0) | ((desc.Bind & Rhi::BindIndex) ? D3D11_BIND_INDEX_BUFFER : 0) |
				((desc.Bind & Rhi::BindConstant) ? D3D11_BIND_CONSTANT_BUFFER : 0);
			D3D11_SUBRESOURCE_DATA sd = { initialData, 0, 0 };
			if (FAILED(Dev->CreateBuffer(&bd, initialData ? &sd : nullptr, b->Buf.GetAddressOf())))
				return nullptr;
			return b;
		}

		void UpdateBuffer(Rhi::Buffer* buffer, const void* data, uint32_t bytes) override
		{
			auto* b = static_cast<DxBuffer*>(buffer);
			D3D11_BOX box = { 0, 0, 0, bytes, 1, 1 };
			Ctx->UpdateSubresource(b->Buf.Get(), 0, (b->Desc().Bind & Rhi::BindConstant) ? nullptr : &box, data, 0, 0);
		}

		std::unique_ptr<Rhi::Texture> CreateTexture(const Rhi::TextureDesc& desc, const Rhi::SubresourceData* initialData) override
		{
			auto t = std::make_unique<DxTexture>(desc);
			const bool depth = Rhi::IsDepth(desc.Format);
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = desc.Width;
			td.Height = desc.Height;
			td.MipLevels = desc.MipLevels;
			td.ArraySize = desc.Type == Rhi::TextureType::Cube ? 6 : desc.ArraySize;
			td.Format = TexFormat(desc.Format);
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | (desc.RenderTarget ? (depth ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET) : 0);
			td.MiscFlags = desc.Type == Rhi::TextureType::Cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
			std::vector<D3D11_SUBRESOURCE_DATA> sd;
			if (initialData)
				for (uint32_t i = 0; i < td.ArraySize * td.MipLevels; ++i)
					sd.push_back({ initialData[i].Data, initialData[i].RowPitch, 0 });
			if (FAILED(Dev->CreateTexture2D(&td, initialData ? sd.data() : nullptr, t->Tex.GetAddressOf())))
				return nullptr;
			D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
			sv.Format = SrvFormat(desc.Format);
			if (desc.Type == Rhi::TextureType::Cube)
			{
				sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
				sv.TextureCube.MipLevels = desc.MipLevels;
			}
			else if (desc.Type == Rhi::TextureType::Tex2DArray)
			{
				sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				sv.Texture2DArray.MipLevels = desc.MipLevels;
				sv.Texture2DArray.ArraySize = desc.ArraySize;
			}
			else
			{
				sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				sv.Texture2D.MipLevels = desc.MipLevels;
			}
			Dev->CreateShaderResourceView(t->Tex.Get(), &sv, t->Srv.GetAddressOf());
			if (desc.RenderTarget && depth)
			{
				D3D11_DEPTH_STENCIL_VIEW_DESC dv = {};
				dv.Format = DsvFormat(desc.Format);
				if (desc.Type == Rhi::TextureType::Tex2D)
				{
					dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
					Dev->CreateDepthStencilView(t->Tex.Get(), &dv, t->Dsv.GetAddressOf());
				}
				else
				{
					dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
					dv.Texture2DArray.ArraySize = 1;
					for (UINT s = 0; s < td.ArraySize; ++s)
					{
						dv.Texture2DArray.FirstArraySlice = s;
						ComPtr<ID3D11DepthStencilView> v;
						Dev->CreateDepthStencilView(t->Tex.Get(), &dv, v.GetAddressOf());
						t->DsvSlices.push_back(v);
					}
					t->Dsv = t->DsvSlices[0];
				}
			}
			else if (desc.RenderTarget)
				Dev->CreateRenderTargetView(t->Tex.Get(), nullptr, t->Rtv.GetAddressOf());
			return t;
		}

		std::unique_ptr<Rhi::Effect> LoadEffect(const std::wstring& fxPath, std::string& error) override
		{
			ComPtr<ID3D10Blob> compiled, msgs;
			if (FAILED(ShaderCache::CompileEffect(fxPath, ShaderCache::DefaultFlags(), compiled, msgs)))
			{
				error = msgs ? std::string((const char*)msgs->GetBufferPointer(), msgs->GetBufferSize()) : "effect compile failed";
				return nullptr;
			}
			auto e = std::make_unique<DxEffect>();
			if (FAILED(::D3DX11CreateEffectFromMemory(compiled->GetBufferPointer(), compiled->GetBufferSize(), 0, Dev, e->Fx.GetAddressOf())))
			{
				error = "D3DX11CreateEffectFromMemory failed";
				return nullptr;
			}
			e->Ctx = Ctx;
			D3DX11_EFFECT_DESC ed;
			e->Fx->GetDesc(&ed);
			for (UINT i = 0; i < ed.Techniques; ++i)
			{
				ID3DX11EffectTechnique* t = e->Fx->GetTechniqueByIndex(i);
				D3DX11_TECHNIQUE_DESC td;
				t->GetDesc(&td);
				e->Techniques.push_back(t);
				e->TechniqueNames.push_back(td.Name ? td.Name : "");
			}
			return e;
		}

		std::unique_ptr<Rhi::InputLayout> CreateInputLayout(const Rhi::VertexElement* elements, uint32_t count, Rhi::Effect* effect, int technique, int pass, std::string& error) override
		{
			auto* fx = static_cast<DxEffect*>(effect);
			if (technique < 0 || technique >= (int)fx->Techniques.size())
			{
				error = "bad technique";
				return nullptr;
			}
			D3DX11_PASS_DESC pd;
			fx->Techniques[technique]->GetPassByIndex(pass)->GetDesc(&pd);
			std::vector<D3D11_INPUT_ELEMENT_DESC> descs;
			for (uint32_t i = 0; i < count; ++i)
			{
				const Rhi::VertexElement& e = elements[i];
				descs.push_back({ e.Semantic, e.SemanticIndex, VertexFormatDxgi(e.Format), e.Slot, e.Offset,
					e.PerInstance ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA, e.PerInstance ? 1u : 0u });
			}
			auto l = std::make_unique<DxInputLayout>();
			if (FAILED(Dev->CreateInputLayout(descs.data(), (UINT)descs.size(), pd.pIAInputSignature, pd.IAInputSignatureSize, l->Layout.GetAddressOf())))
			{
				error = "CreateInputLayout failed (vertex elements do not match the shader input)";
				return nullptr;
			}
			return l;
		}

		void ResetState() override
		{
			Ctx->OMSetDepthStencilState(nullptr, 0);
			Ctx->RSSetState(nullptr);
			const float zero[4] = { 0, 0, 0, 0 };
			Ctx->OMSetBlendState(nullptr, zero, 0xFFFFFFFF);
		}

		void SetRenderTargets(Rhi::Texture* const* colors, uint32_t count, Rhi::Texture* depth, uint32_t depthSlice) override
		{
			ID3D11RenderTargetView* rtvs[8] = {};
			for (uint32_t i = 0; i < count && i < 8; ++i)
				rtvs[i] = colors[i] ? static_cast<DxTexture*>(colors[i])->Rtv.Get() : nullptr;
			ID3D11DepthStencilView* dsv = nullptr;
			if (auto* d = static_cast<DxTexture*>(depth))
				dsv = depthSlice < d->DsvSlices.size() ? d->DsvSlices[depthSlice].Get() : d->Dsv.Get();
			Ctx->OMSetRenderTargets(count, rtvs, dsv);
		}

		void SetViewport(float x, float y, float width, float height) override
		{
			D3D11_VIEWPORT vp = { x, y, width, height, 0.0f, 1.0f };
			Ctx->RSSetViewports(1, &vp);
		}

		void ClearColor(Rhi::Texture* target, const float rgba[4]) override
		{
			Ctx->ClearRenderTargetView(static_cast<DxTexture*>(target)->Rtv.Get(), rgba);
		}

		void ClearDepth(Rhi::Texture* target, float depth) override
		{
			auto* t = static_cast<DxTexture*>(target);
			if (t->DsvSlices.empty())
				Ctx->ClearDepthStencilView(t->Dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth, 0);
			for (auto& v : t->DsvSlices)
				Ctx->ClearDepthStencilView(v.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth, 0);
		}

		void SetInputLayout(Rhi::InputLayout* layout) override
		{
			Ctx->IASetInputLayout(layout ? static_cast<DxInputLayout*>(layout)->Layout.Get() : nullptr);
		}

		void SetVertexBuffer(uint32_t slot, Rhi::Buffer* buffer, uint32_t stride, uint32_t offset) override
		{
			ID3D11Buffer* b = buffer ? static_cast<DxBuffer*>(buffer)->Buf.Get() : nullptr;
			Ctx->IASetVertexBuffers(slot, 1, &b, &stride, &offset);
		}

		void SetIndexBuffer(Rhi::Buffer* buffer, bool use32Bit) override
		{
			Ctx->IASetIndexBuffer(buffer ? static_cast<DxBuffer*>(buffer)->Buf.Get() : nullptr, use32Bit ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
		}

		void SetTopology(Rhi::Topology topology) override
		{
			static const D3D11_PRIMITIVE_TOPOLOGY map[] = { D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
				D3D11_PRIMITIVE_TOPOLOGY_LINELIST, D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST };
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
			auto* t = static_cast<DxTexture*>(texture);
			DirectX::ScratchImage captured, converted;
			if (FAILED(DirectX::CaptureTexture(Dev, Ctx, t->Tex.Get(), captured)))
			{
				error = "capture failed";
				return false;
			}
			const DirectX::Image* img = captured.GetImage(0, 0, 0);
			if (img->format != DXGI_FORMAT_R8G8B8A8_UNORM)
			{
				if (FAILED(DirectX::Convert(*img, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted)))
				{
					error = "format conversion failed";
					return false;
				}
				img = converted.GetImage(0, 0, 0);
			}
			rgba.resize(img->width * img->height * 4);
			for (size_t y = 0; y < img->height; ++y)
				memcpy(rgba.data() + y * img->width * 4, img->pixels + y * img->rowPitch, img->width * 4);
			return true;
		}

		void Finish() override { Ctx->Flush(); }
	};
}

std::unique_ptr<Rhi::Device> Rhi::WrapD3D11(GfxDevice* device, GfxContext* context)
{
	auto d = std::make_unique<Dx11Device>();
	d->Dev = static_cast<ID3D11Device*>(device->Native());
	d->Ctx = static_cast<ID3D11DeviceContext*>(context->Native());
	return d;
}

std::unique_ptr<Rhi::Device> CreateDx11RhiDevice(std::string& error)
{
	auto d = std::make_unique<Dx11Device>();
	d->Dev = static_cast<ID3D11Device*>(Application::GetI()->GetDevice()->Native());
	d->Ctx = static_cast<ID3D11DeviceContext*>(Application::GetI()->GetDeviceContext()->Native());
	if (!d->Dev || !d->Ctx)
	{
		error = "no Direct3D 11 device";
		return nullptr;
	}
	return d;
}
