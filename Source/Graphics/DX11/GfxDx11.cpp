#include "pch.h"
#include "Gfx.h"
#include "GfxGL.h"
#include "GfxVk.h"
#include "GfxD3D12.h"
#include <mutex>
#include <dxgi1_4.h>   // IDXGIAdapter3 (VRAM 예산)
#include <d3d11_4.h>   // ID3D11Multithread (렌더 스레드)
#include <set>
#include "RenderThread.h"

// Gfx 의 DirectX 11 구현: 진짜 D3D11 객체를 감싸 그대로 넘긴다.
//  - 감싸개는 D3D 객체의 private data 에 자기 주소를 적어 둔다 → 같은 D3D 객체는 늘 같은 감싸개
//    (OMGetRenderTargets 처럼 D3D 가 돌려준 객체, Effects11·ImGui 가 직접 만든 객체도 감쌀 수 있다)
//  - 감싸개는 D3D 객체를 강하게 잡고, 감싸개가 사라지면 private data 를 지운다
namespace
{
	// {4E6F7661-0002-4A00-8000-475846575250}
	const GUID kWrapperGuid = { 0x4e6f7661, 0x0002, 0x4a00, { 0x80, 0x00, 0x47, 0x58, 0x46, 0x57, 0x52, 0x50 } };
	std::recursive_mutex s_WrapMutex;

	template <class N>
	GfxObject* FindWrapper(N* native)
	{
		void* p = nullptr;
		UINT size = sizeof(p);
		if (native && SUCCEEDED(native->GetPrivateData(kWrapperGuid, &size, &p)) && size == sizeof(p))
			return static_cast<GfxObject*>(p);
		return nullptr;
	}

	// 감싸개 공통: Iface = Gfx 인터페이스, N = D3D11 인터페이스
	template <class Iface, class N>
	class DxWrap : public Iface
	{
	public:
		explicit DxWrap(N* native) : D(native)
		{
			GfxObject* self = this;
			D->SetPrivateData(kWrapperGuid, sizeof(self), &self);
		}
		~DxWrap() override
		{
			std::lock_guard<std::recursive_mutex> lock(s_WrapMutex);
			if (FindWrapper(D.Get()) == static_cast<const GfxObject*>(this))
				D->SetPrivateData(kWrapperGuid, 0, nullptr);
		}
		void* Native() const override { return D.Get(); }
		ComPtr<N> D;
	};

	class DxBuffer : public DxWrap<GfxBuffer, ID3D11Buffer>
	{
	public:
		using DxWrap::DxWrap;
		void GetDesc(D3D11_BUFFER_DESC* d) const override { D->GetDesc(d); }
	};
	class DxTexture1D : public DxWrap<GfxTexture1D, ID3D11Texture1D>
	{
	public:
		using DxWrap::DxWrap;
		void GetDesc(D3D11_TEXTURE1D_DESC* d) const override { D->GetDesc(d); }
	};
	class DxTexture2D : public DxWrap<GfxTexture2D, ID3D11Texture2D>
	{
	public:
		using DxWrap::DxWrap;
		void GetDesc(D3D11_TEXTURE2D_DESC* d) const override { D->GetDesc(d); }
	};
	class DxTexture3D : public DxWrap<GfxTexture3D, ID3D11Texture3D>
	{
	public:
		using DxWrap::DxWrap;
		void GetDesc(D3D11_TEXTURE3D_DESC* d) const override { D->GetDesc(d); }
	};

	GfxResource* WrapResource(ID3D11Resource* r);

	template <class Iface, class N, class Desc>
	class DxView : public DxWrap<Iface, N>
	{
	public:
		using DxWrap<Iface, N>::DxWrap;
		void GetDesc(Desc* d) const override { this->D->GetDesc(d); }
		void GetResource(GfxResource** out) const override
		{
			ComPtr<ID3D11Resource> r;
			this->D->GetResource(r.GetAddressOf());
			*out = WrapResource(r.Get());
		}
	};
	using DxSrv = DxView<GfxShaderResourceView, ID3D11ShaderResourceView, D3D11_SHADER_RESOURCE_VIEW_DESC>;
	using DxRtv = DxView<GfxRenderTargetView, ID3D11RenderTargetView, D3D11_RENDER_TARGET_VIEW_DESC>;
	using DxDsv = DxView<GfxDepthStencilView, ID3D11DepthStencilView, D3D11_DEPTH_STENCIL_VIEW_DESC>;
	using DxUav = DxView<GfxUnorderedAccessView, ID3D11UnorderedAccessView, D3D11_UNORDERED_ACCESS_VIEW_DESC>;

	class DxInputLayout : public DxWrap<GfxInputLayout, ID3D11InputLayout> { public: using DxWrap::DxWrap; };

	template <class Iface, class N, class Desc>
	class DxState : public DxWrap<Iface, N>
	{
	public:
		using DxWrap<Iface, N>::DxWrap;
		void GetDesc(Desc* d) const override { this->D->GetDesc(d); }
	};
	using DxRasterizer = DxState<GfxRasterizerState, ID3D11RasterizerState, D3D11_RASTERIZER_DESC>;
	using DxBlend = DxState<GfxBlendState, ID3D11BlendState, D3D11_BLEND_DESC>;
	using DxDepthStencil = DxState<GfxDepthStencilState, ID3D11DepthStencilState, D3D11_DEPTH_STENCIL_DESC>;
	using DxSampler = DxState<GfxSamplerState, ID3D11SamplerState, D3D11_SAMPLER_DESC>;
	using DxQuery = DxState<GfxQuery, ID3D11Query, D3D11_QUERY_DESC>;

	// D3D 객체 → 감싸개 (있으면 그것, 없으면 새로). 참조 +1
	template <class W, class N>
	W* Wrap(N* native)
	{
		if (!native) return nullptr;
		std::lock_guard<std::recursive_mutex> lock(s_WrapMutex);
		if (GfxObject* w = FindWrapper(native))
		{
			w->AddRef();
			return static_cast<W*>(w);
		}
		return new W(native);   // 참조 1
	}

	// 새로 만든 D3D 객체 → 감싸개 (만든 함수가 준 참조는 감싸개가 넘겨받는다)
	// 주의: native 는 주소로 받는다 — 값으로 받으면 인자 계산 순서(MSVC 는 오른쪽부터)에 따라
	//       Create 호출 전의 nullptr 이 넘어와 만든 객체가 새고 실패로 보인다 (2026-10-02 VRAM 이 바닥나 PC 가 멈춘 원인)
	template <class W, class N, class Out>
	HRESULT Adopt(HRESULT hr, N** native, Out** out)
	{
		N* n = *native;
		if (!out)
			return hr;   // out 없이 부르면 D3D 는 설명만 검사한다 (S_FALSE)
		if (FAILED(hr) || !n)
		{
			*out = nullptr;
			if (n) n->Release();
			return FAILED(hr) ? hr : E_FAIL;
		}
		*out = Wrap<W>(n);
		n->Release();
		return hr;
	}

	GfxResource* WrapResource(ID3D11Resource* r)
	{
		if (!r) return nullptr;
		D3D11_RESOURCE_DIMENSION dim;
		r->GetType(&dim);
		switch (dim)
		{
		case D3D11_RESOURCE_DIMENSION_BUFFER: return Wrap<DxBuffer>(static_cast<ID3D11Buffer*>(r));
		case D3D11_RESOURCE_DIMENSION_TEXTURE1D: return Wrap<DxTexture1D>(static_cast<ID3D11Texture1D*>(r));
		case D3D11_RESOURCE_DIMENSION_TEXTURE2D: return Wrap<DxTexture2D>(static_cast<ID3D11Texture2D*>(r));
		case D3D11_RESOURCE_DIMENSION_TEXTURE3D: return Wrap<DxTexture3D>(static_cast<ID3D11Texture3D*>(r));
		default: return nullptr;
		}
	}

	template <class N, class G>
	N* Nat(G* g) { return g ? static_cast<N*>(g->Native()) : nullptr; }

	ID3D11Resource* NatRes(GfxResource* r) { return r ? static_cast<ID3D11Resource*>(r->Native()) : nullptr; }

	// ---- VRAM 예산 가드: 큰 자원을 만들기 전에 이 프로세스의 VRAM 사용량(DXGI)을 보고, 예산을 넘으면 만들지 않는다.
	//  엔진이 실패를 매 프레임 다시 만드는 버그가 있어도 VRAM 이 바닥나 PC 가 멈추지 않게 (2026-10-02 사고)
	class VramGuard
	{
	public:
		bool Allow(ID3D11Device* device, uint64_t bytes, const char* what)
		{
			if (bytes < (4ull << 20))
				return true;   // 작은 자원은 보지 않는다 (질의 비용)
			if (!_tried)
			{
				_tried = true;
				ComPtr<IDXGIDevice> dxgi;
				ComPtr<IDXGIAdapter> adapter;
				if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(dxgi.GetAddressOf()))) && SUCCEEDED(dxgi->GetAdapter(adapter.GetAddressOf())))
					adapter.As(&_adapter);
			}
			if (!_adapter)
				return true;
			DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
			if (FAILED(_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)) || info.Budget == 0)
				return true;
			if (_testBudget < 0)
			{
				// 검사용: NOVA_GFX_VRAM_BUDGET_MB 로 예산을 일부러 줄여 가드가 막는지 확인
				char buf[32] = {};
				_testBudget = ::GetEnvironmentVariableA("NOVA_GFX_VRAM_BUDGET_MB", buf, sizeof(buf)) ? (int64_t)atoll(buf) << 20 : 0;
			}
			if (_testBudget > 0)
				info.Budget = (std::min<UINT64>)(info.Budget, (UINT64)_testBudget);
			if (info.CurrentUsage + bytes <= info.Budget / 100 * 95)
				return true;
			const int n = ++_refused;
			if (n <= 20 || n % 1000 == 0)
				EditorLog::Write("Gfx", "VRAM budget: refused %s of %.1f MB (in use %.0f MB / budget %.0f MB, refused %d so far)", what, bytes / 1048576.0,
					info.CurrentUsage / 1048576.0, info.Budget / 1048576.0, n);
			return false;
		}

	private:
		bool _tried = false;
		int64_t _testBudget = -1;
		ComPtr<IDXGIAdapter3> _adapter;
		std::atomic<int> _refused{ 0 };
	};

	uint64_t TextureBytes(DXGI_FORMAT format, UINT w, UINT h, UINT depthOrArray, UINT mips, UINT samples)
	{
		const uint64_t bpp = (std::max<size_t>)(DirectX::BitsPerPixel(format), 8);
		uint64_t bytes = 0;
		for (UINT m = 0; m < (std::max)(mips, 1u); ++m)
			bytes += (uint64_t)(std::max)(1u, w >> m) * (std::max)(1u, h >> m) * bpp / 8;
		if (mips == 0)   // 0 = 전체 밉 → 대략 4/3
			bytes = bytes * 4 / 3;
		return bytes * (std::max)(depthOrArray, 1u) * (std::max)(samples, 1u);
	}

	class DxContext;

	class DxDevice : public DxWrap<GfxDevice, ID3D11Device>
	{
	public:
		using DxWrap::DxWrap;
		VramGuard Guard;

		HRESULT CreateBuffer(const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxBuffer** out) override
		{
			if (out && desc && !Guard.Allow(D.Get(), desc->ByteWidth, "buffer")) { *out = nullptr; return E_OUTOFMEMORY; }
			ID3D11Buffer* n = nullptr;
			return Adopt<DxBuffer>(D->CreateBuffer(desc, data, out ? &n : nullptr), &n, out);
		}
		HRESULT CreateTexture1D(const D3D11_TEXTURE1D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture1D** out) override
		{
			if (out && desc && !Guard.Allow(D.Get(), TextureBytes(desc->Format, desc->Width, 1, desc->ArraySize, desc->MipLevels, 1), "texture1D")) { *out = nullptr; return E_OUTOFMEMORY; }
			ID3D11Texture1D* n = nullptr;
			return Adopt<DxTexture1D>(D->CreateTexture1D(desc, data, out ? &n : nullptr), &n, out);
		}
		HRESULT CreateTexture2D(const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture2D** out) override
		{
			if (out && desc && !Guard.Allow(D.Get(), TextureBytes(desc->Format, desc->Width, desc->Height, desc->ArraySize, desc->MipLevels, desc->SampleDesc.Count), "texture2D")) { *out = nullptr; return E_OUTOFMEMORY; }
			ID3D11Texture2D* n = nullptr;
			return Adopt<DxTexture2D>(D->CreateTexture2D(desc, data, out ? &n : nullptr), &n, out);
		}
		HRESULT CreateTexture3D(const D3D11_TEXTURE3D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture3D** out) override
		{
			if (out && desc && !Guard.Allow(D.Get(), TextureBytes(desc->Format, desc->Width, desc->Height, desc->Depth, desc->MipLevels, 1), "texture3D")) { *out = nullptr; return E_OUTOFMEMORY; }
			ID3D11Texture3D* n = nullptr;
			return Adopt<DxTexture3D>(D->CreateTexture3D(desc, data, out ? &n : nullptr), &n, out);
		}
		HRESULT CreateShaderResourceView(GfxResource* r, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc, GfxShaderResourceView** out) override
		{
			ID3D11ShaderResourceView* n = nullptr;
			return Adopt<DxSrv>(D->CreateShaderResourceView(NatRes(r), desc, out ? &n : nullptr), &n, out);
		}
		HRESULT CreateRenderTargetView(GfxResource* r, const D3D11_RENDER_TARGET_VIEW_DESC* desc, GfxRenderTargetView** out) override
		{
			ID3D11RenderTargetView* n = nullptr;
			return Adopt<DxRtv>(D->CreateRenderTargetView(NatRes(r), desc, out ? &n : nullptr), &n, out);
		}
		HRESULT CreateDepthStencilView(GfxResource* r, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, GfxDepthStencilView** out) override
		{
			ID3D11DepthStencilView* n = nullptr;
			return Adopt<DxDsv>(D->CreateDepthStencilView(NatRes(r), desc, out ? &n : nullptr), &n, out);
		}
		HRESULT CreateUnorderedAccessView(GfxResource* r, const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc, GfxUnorderedAccessView** out) override
		{
			ID3D11UnorderedAccessView* n = nullptr;
			return Adopt<DxUav>(D->CreateUnorderedAccessView(NatRes(r), desc, out ? &n : nullptr), &n, out);
		}
		HRESULT CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* elements, UINT count, const void* signature, SIZE_T signatureSize, GfxInputLayout** out) override
		{
			ID3D11InputLayout* n = nullptr;
			return Adopt<DxInputLayout>(D->CreateInputLayout(elements, count, signature, signatureSize, out ? &n : nullptr), &n, out);
		}
		HRESULT CreateRasterizerState(const D3D11_RASTERIZER_DESC* desc, GfxRasterizerState** out) override
		{
			ID3D11RasterizerState* n = nullptr;
			return Adopt<DxRasterizer>(D->CreateRasterizerState(desc, out ? &n : nullptr), &n, out);
		}
		HRESULT CreateBlendState(const D3D11_BLEND_DESC* desc, GfxBlendState** out) override
		{
			ID3D11BlendState* n = nullptr;
			return Adopt<DxBlend>(D->CreateBlendState(desc, out ? &n : nullptr), &n, out);
		}
		HRESULT CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC* desc, GfxDepthStencilState** out) override
		{
			ID3D11DepthStencilState* n = nullptr;
			return Adopt<DxDepthStencil>(D->CreateDepthStencilState(desc, out ? &n : nullptr), &n, out);
		}
		HRESULT CreateSamplerState(const D3D11_SAMPLER_DESC* desc, GfxSamplerState** out) override
		{
			ID3D11SamplerState* n = nullptr;
			return Adopt<DxSampler>(D->CreateSamplerState(desc, out ? &n : nullptr), &n, out);
		}
		HRESULT CreateQuery(const D3D11_QUERY_DESC* desc, GfxQuery** out) override
		{
			ID3D11Query* n = nullptr;
			// 예측 쿼리는 CreatePredicate 로 (SetPredication 은 ID3D11Predicate 만 받는다 — ID3D11Query 의 하위)
			if (desc && (desc->Query == D3D11_QUERY_OCCLUSION_PREDICATE || desc->Query == D3D11_QUERY_SO_OVERFLOW_PREDICATE))
			{
				ID3D11Predicate* p = nullptr;
				const HRESULT hr = D->CreatePredicate(desc, out ? &p : nullptr);
				n = p;
				return Adopt<DxQuery>(hr, &n, out);
			}
			return Adopt<DxQuery>(D->CreateQuery(desc, out ? &n : nullptr), &n, out);
		}
		void GetImmediateContext(GfxContext** out) override;
		HRESULT GetDeviceRemovedReason() override { return D->GetDeviceRemovedReason(); }
	};

	class DxContext : public DxWrap<GfxContext, ID3D11DeviceContext>
	{
	public:
		using DxWrap::DxWrap;

		// 렌더 스레드 (RenderThread.h): D = 기록하는 deferred context, Imm = 진짜 immediate (렌더 스레드가 실행).
		//  읽기 (Map 이 DISCARD 가 아님 · GetData) 만 Imm 으로 — Map 은 Sync 뒤에, GetData 는 끝난 쿼리를 넘긴 뒤 (ID3D11Multithread 보호)
		ComPtr<ID3D11DeviceContext> Imm;
		bool Deferred = false;
		bool DriverCommandLists = true;   // 아니면 deferred 의 UpdateSubresource (box) 에 알려진 어긋남이 있다 — 고쳐 넘긴다
		std::set<std::pair<ID3D11Resource*, UINT>> ImmMapped;   // Imm 에서 Map 한 것 (Unmap 도 Imm)
		std::set<ID3D11Query*> EndedInSegment;                  // 아직 넘기지 않은 목록에서 End 한 쿼리
		std::unordered_map<ID3D11Resource*, uint64_t> CopySerial;   // 복사로 쓴 자원 → 그 명령 목록 번호 (DO_NOT_WAIT 읽기)

		template <class N, class G>
		static void Natives(UINT count, G* const* in, N** out)
		{
			for (UINT i = 0; i < count; ++i)
				out[i] = in ? Nat<N>(in[i]) : nullptr;
		}

		void IASetInputLayout(GfxInputLayout* l) override { D->IASetInputLayout(Nat<ID3D11InputLayout>(l)); }
		void IAGetInputLayout(GfxInputLayout** l) override
		{
			ComPtr<ID3D11InputLayout> n;
			D->IAGetInputLayout(n.GetAddressOf());
			*l = Wrap<DxInputLayout>(n.Get());
		}
		void IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY t) override { D->IASetPrimitiveTopology(t); }
		void IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY* t) override { D->IAGetPrimitiveTopology(t); }
		void IASetVertexBuffers(UINT start, UINT count, GfxBuffer* const* buffers, const UINT* strides, const UINT* offsets) override
		{
			ID3D11Buffer* n[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
			Natives(count, buffers, n);
			D->IASetVertexBuffers(start, count, n, strides, offsets);
		}
		void IAGetVertexBuffers(UINT start, UINT count, GfxBuffer** buffers, UINT* strides, UINT* offsets) override
		{
			ID3D11Buffer* n[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
			D->IAGetVertexBuffers(start, count, n, strides, offsets);
			for (UINT i = 0; i < count; ++i)
			{
				if (buffers) buffers[i] = Wrap<DxBuffer>(n[i]);
				if (n[i]) n[i]->Release();
			}
		}
		void IASetIndexBuffer(GfxBuffer* b, DXGI_FORMAT f, UINT o) override { D->IASetIndexBuffer(Nat<ID3D11Buffer>(b), f, o); }
		void IAGetIndexBuffer(GfxBuffer** b, DXGI_FORMAT* f, UINT* o) override
		{
			ComPtr<ID3D11Buffer> n;
			D->IAGetIndexBuffer(n.GetAddressOf(), f, o);
			if (b) *b = Wrap<DxBuffer>(n.Get());
		}
		void SOSetTargets(UINT count, GfxBuffer* const* buffers, const UINT* offsets) override
		{
			ID3D11Buffer* n[D3D11_SO_BUFFER_SLOT_COUNT] = {};
			Natives(count, buffers, n);
			D->SOSetTargets(count, n, offsets);
		}

		void OMSetRenderTargets(UINT count, GfxRenderTargetView* const* rtvs, GfxDepthStencilView* dsv) override
		{
			ID3D11RenderTargetView* n[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
			Natives(count, rtvs, n);
			D->OMSetRenderTargets(count, n, Nat<ID3D11DepthStencilView>(dsv));
		}
		void OMGetRenderTargets(UINT count, GfxRenderTargetView** rtvs, GfxDepthStencilView** dsv) override
		{
			ID3D11RenderTargetView* n[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
			ID3D11DepthStencilView* d = nullptr;
			D->OMGetRenderTargets(count, rtvs ? n : nullptr, dsv ? &d : nullptr);
			for (UINT i = 0; rtvs && i < count; ++i)
			{
				rtvs[i] = Wrap<DxRtv>(n[i]);
				if (n[i]) n[i]->Release();
			}
			if (dsv)
			{
				*dsv = Wrap<DxDsv>(d);
				if (d) d->Release();
			}
		}
		void OMSetDepthStencilState(GfxDepthStencilState* s, UINT ref) override { D->OMSetDepthStencilState(Nat<ID3D11DepthStencilState>(s), ref); }
		void OMGetDepthStencilState(GfxDepthStencilState** s, UINT* ref) override
		{
			ComPtr<ID3D11DepthStencilState> n;
			D->OMGetDepthStencilState(n.GetAddressOf(), ref);
			if (s) *s = Wrap<DxDepthStencil>(n.Get());
		}
		void OMSetBlendState(GfxBlendState* s, const FLOAT f[4], UINT mask) override { D->OMSetBlendState(Nat<ID3D11BlendState>(s), f, mask); }
		void OMGetBlendState(GfxBlendState** s, FLOAT f[4], UINT* mask) override
		{
			ComPtr<ID3D11BlendState> n;
			D->OMGetBlendState(n.GetAddressOf(), f, mask);
			if (s) *s = Wrap<DxBlend>(n.Get());
		}
		void RSSetState(GfxRasterizerState* s) override { D->RSSetState(Nat<ID3D11RasterizerState>(s)); }
		void RSGetState(GfxRasterizerState** s) override
		{
			ComPtr<ID3D11RasterizerState> n;
			D->RSGetState(n.GetAddressOf());
			*s = Wrap<DxRasterizer>(n.Get());
		}
		void RSSetViewports(UINT count, const D3D11_VIEWPORT* v) override { D->RSSetViewports(count, v); }
		void RSGetViewports(UINT* count, D3D11_VIEWPORT* v) override { D->RSGetViewports(count, v); }
		void RSSetScissorRects(UINT count, const D3D11_RECT* r) override { D->RSSetScissorRects(count, r); }
		void RSGetScissorRects(UINT* count, D3D11_RECT* r) override { D->RSGetScissorRects(count, r); }

		void PSSetShaderResources(UINT start, UINT count, GfxShaderResourceView* const* views) override
		{
			ID3D11ShaderResourceView* n[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
			Natives(count, views, n);
			D->PSSetShaderResources(start, count, n);
		}
		void VSSetShaderResources(UINT start, UINT count, GfxShaderResourceView* const* views) override
		{
			ID3D11ShaderResourceView* n[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
			Natives(count, views, n);
			D->VSSetShaderResources(start, count, n);
		}
		void CSSetShaderResources(UINT start, UINT count, GfxShaderResourceView* const* views) override
		{
			ID3D11ShaderResourceView* n[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
			Natives(count, views, n);
			D->CSSetShaderResources(start, count, n);
		}
		void CSSetUnorderedAccessViews(UINT start, UINT count, GfxUnorderedAccessView* const* views, const UINT* initialCounts) override
		{
			ID3D11UnorderedAccessView* n[D3D11_1_UAV_SLOT_COUNT] = {};
			Natives(count, views, n);
			D->CSSetUnorderedAccessViews(start, count, n, initialCounts);
		}
		void CSSetShader(void* shader, void*, UINT) override { D->CSSetShader(static_cast<ID3D11ComputeShader*>(shader), nullptr, 0); }
		void ClearTessellationShaders() override { D->HSSetShader(nullptr, nullptr, 0); D->DSSetShader(nullptr, nullptr, 0); }

		void Draw(UINT c, UINT s) override { D->Draw(c, s); }
		void DrawIndexed(UINT c, UINT s, INT b) override { D->DrawIndexed(c, s, b); }
		void DrawInstanced(UINT c, UINT n, UINT s, UINT si) override { D->DrawInstanced(c, n, s, si); }
		void DrawIndexedInstanced(UINT c, UINT n, UINT s, INT b, UINT si) override { D->DrawIndexedInstanced(c, n, s, b, si); }
		void DrawAuto() override { D->DrawAuto(); }
		void Dispatch(UINT x, UINT y, UINT z) override { D->Dispatch(x, y, z); }

		void ClearRenderTargetView(GfxRenderTargetView* rtv, const FLOAT c[4]) override { D->ClearRenderTargetView(Nat<ID3D11RenderTargetView>(rtv), c); }
		void ClearDepthStencilView(GfxDepthStencilView* dsv, UINT flags, FLOAT depth, UINT8 stencil) override { D->ClearDepthStencilView(Nat<ID3D11DepthStencilView>(dsv), flags, depth, stencil); }
		HRESULT Map(GfxResource* r, UINT sub, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* m) override
		{
			if (Deferred && type == D3D11_MAP_READ && (flags & D3D11_MAP_FLAG_DO_NOT_WAIT))
			{
				// 지연을 견디는 읽기 (오클루전 · VFX 통계의 스테이징 고리): 복사한 목록을 렌더 스레드가 이미 실행했으면 바로 immediate 에서,
				//  아니면 '아직 그리는 중' — 기다리지 않는다 (Sync 하면 프레임마다 겹침이 깨진다)
				auto it = CopySerial.find(NatRes(r));
				if (it != CopySerial.end())
				{
					if (it->second > RenderThread::CompletedSerial())
						return DXGI_ERROR_WAS_STILL_DRAWING;
					CopySerial.erase(it);
				}
				const HRESULT hr = Imm->Map(NatRes(r), sub, type, flags, m);
				if (SUCCEEDED(hr))
					ImmMapped.insert({ NatRes(r), sub });
				return hr;
			}
			if (Deferred && type != D3D11_MAP_WRITE_DISCARD)
			{
				// 읽기 · 스테이징 쓰기: 기록한 것을 다 실행시킨 뒤 메인이 immediate 에서 (deferred 는 DISCARD 만 된다)
				static int s_Logged = 0;
				if (s_Logged < 12)
				{
					// 어떤 자원이 프레임마다 기다리게 하는지 (렌더 스레드의 겹침을 깬다) — Editor.log
					++s_Logged;
					D3D11_RESOURCE_DIMENSION dim;
					NatRes(r)->GetType(&dim);
					UINT a = 0, b = 0;
					if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) { D3D11_BUFFER_DESC d; static_cast<ID3D11Buffer*>(NatRes(r))->GetDesc(&d); a = d.ByteWidth; b = d.Usage; }
					else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) { D3D11_TEXTURE2D_DESC d; static_cast<ID3D11Texture2D*>(NatRes(r))->GetDesc(&d); a = d.Width; b = d.Height; }
					char name[64] = {};
					UINT len = sizeof(name) - 1;
					NatRes(r)->GetPrivateData(WKPDID_D3DDebugObjectName, &len, name);
					EditorLog::Write("RenderThread", "sync map: type %d, %s %u x %u, name '%s'", (int)type, dim == D3D11_RESOURCE_DIMENSION_BUFFER ? "buffer bytes/usage" : "texture", a, b, name);
				}
				RenderThread::Sync();
				const HRESULT hr = Imm->Map(NatRes(r), sub, type, flags, m);
				if (SUCCEEDED(hr))
					ImmMapped.insert({ NatRes(r), sub });
				return hr;
			}
			return D->Map(NatRes(r), sub, type, flags, m);
		}
		void Unmap(GfxResource* r, UINT sub) override
		{
			if (Deferred && ImmMapped.erase({ NatRes(r), sub }))
			{
				Imm->Unmap(NatRes(r), sub);
				return;
			}
			D->Unmap(NatRes(r), sub);
		}
		void UpdateSubresource(GfxResource* r, UINT sub, const D3D11_BOX* box, const void* data, UINT row, UINT depth) override
		{
			if (Deferred && box && data && !DriverCommandLists)
			{
				// 드라이버가 명령 목록을 직접 지원하지 않으면 런타임이 box 의 시작을 한 번 더 더한다 — 그만큼 앞으로 (Microsoft 문서의 우회)
				ID3D11Resource* res = NatRes(r);
				D3D11_RESOURCE_DIMENSION dim;
				res->GetType(&dim);
				size_t elem = 1;
				bool blocks = false;
				if (dim != D3D11_RESOURCE_DIMENSION_BUFFER)
				{
					DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
					if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) { D3D11_TEXTURE2D_DESC d; static_cast<ID3D11Texture2D*>(res)->GetDesc(&d); fmt = d.Format; }
					else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D) { D3D11_TEXTURE3D_DESC d; static_cast<ID3D11Texture3D*>(res)->GetDesc(&d); fmt = d.Format; }
					else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE1D) { D3D11_TEXTURE1D_DESC d; static_cast<ID3D11Texture1D*>(res)->GetDesc(&d); fmt = d.Format; }
					blocks = DirectX::IsCompressed(fmt);
					elem = blocks ? (DirectX::BitsPerPixel(fmt) * 16) / 8 : (std::max)(size_t(1), (size_t)(DirectX::BitsPerPixel(fmt) / 8));
				}
				const UINT left = blocks ? box->left / 4 : box->left;
				const UINT top = blocks ? box->top / 4 : box->top;
				data = static_cast<const uint8_t*>(data) - (size_t)box->front * depth - (size_t)top * row - (size_t)left * elem;
			}
			D->UpdateSubresource(NatRes(r), sub, box, data, row, depth);
		}
		void CopyResource(GfxResource* dst, GfxResource* src) override
		{
			D->CopyResource(NatRes(dst), NatRes(src));
			if (Deferred)
				CopySerial[NatRes(dst)] = RenderThread::RecordingSerial();
		}
		void CopySubresourceRegion(GfxResource* dst, UINT dstSub, UINT x, UINT y, UINT z, GfxResource* src, UINT srcSub, const D3D11_BOX* box) override
		{
			D->CopySubresourceRegion(NatRes(dst), dstSub, x, y, z, NatRes(src), srcSub, box);
			if (Deferred)
				CopySerial[NatRes(dst)] = RenderThread::RecordingSerial();
		}
		void GenerateMips(GfxShaderResourceView* srv) override { D->GenerateMips(Nat<ID3D11ShaderResourceView>(srv)); }

		void Begin(GfxQuery* q) override { D->Begin(Nat<ID3D11Query>(q)); }
		void End(GfxQuery* q) override
		{
			D->End(Nat<ID3D11Query>(q));
			if (Deferred)
				EndedInSegment.insert(Nat<ID3D11Query>(q));
		}
		HRESULT GetData(GfxQuery* q, void* data, UINT size, UINT flags) override
		{
			if (!Deferred)
				return D->GetData(Nat<ID3D11Query>(q), data, size, flags);
			// 결과는 immediate 에서 (렌더 스레드와 같이 써도 ID3D11Multithread 가 지킨다). 아직 넘기지 않은 목록에서 끝낸 쿼리:
			//  DONOTFLUSH (기다리지 않는 확인) 면 '아직' — 넘기지 않는다 (프레임 목록이 쪼개지면 GPU 프레임 쿼리가 깨진다), 기다리는 확인이면 넘긴다
			if (EndedInSegment.count(Nat<ID3D11Query>(q)))
			{
				if (flags & D3D11_ASYNC_GETDATA_DONOTFLUSH)
					return S_FALSE;
				RenderThread::Flush();
			}
			return Imm->GetData(Nat<ID3D11Query>(q), data, size, flags);
		}
		void Flush() override
		{
			if (Deferred)
				RenderThread::Flush();
			else
				D->Flush();
		}
		void ClearState() override { D->ClearState(); }

		bool SupportsGpuDriven() const override
		{
			ComPtr<ID3D11Device> dev;
			D->GetDevice(dev.GetAddressOf());
			return dev && dev->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0;
		}
		bool DrawIndexedInstancedIndirect(GfxBuffer* args, UINT offset) override { D->DrawIndexedInstancedIndirect(Nat<ID3D11Buffer>(args), offset); return true; }
		bool DrawInstancedIndirect(GfxBuffer* args, UINT offset) override { D->DrawInstancedIndirect(Nat<ID3D11Buffer>(args), offset); return true; }
		bool SetPredication(GfxQuery* predicate, BOOL value) override
		{
			D->SetPredication(static_cast<ID3D11Predicate*>(Nat<ID3D11Query>(predicate)), value);   // CreateQuery 가 예측 쿼리는 CreatePredicate 로 만들었다
			return true;
		}
		bool ClearUnorderedAccessViewUint(GfxUnorderedAccessView* uav, const UINT values[4]) override
		{
			D->ClearUnorderedAccessViewUint(Nat<ID3D11UnorderedAccessView>(uav), values);
			return true;
		}
	};

	DxContext* AsDx(GfxContext* ctx)
	{
		return ctx && ctx->Api() == GfxApi::DirectX11 && ctx->Native() ? static_cast<DxContext*>(ctx) : nullptr;
	}

	void DxDevice::GetImmediateContext(GfxContext** out)
	{
		ComPtr<ID3D11DeviceContext> n;
		D->GetImmediateContext(n.GetAddressOf());
		*out = Wrap<DxContext>(n.Get());
	}

	ComPtr<GfxDevice> s_Device;
	ComPtr<GfxContext> s_Context;
}

namespace Gfx
{
	GfxDevice* Device() { return s_Device.Get(); }
	GfxContext* Context() { return s_Context.Get(); }
	void SetMain(GfxDevice* device, GfxContext* context)
	{
		s_Device = device;
		s_Context = context;
	}

	GfxObject* WrapD3D11(IUnknown* native)
	{
		if (!native) return nullptr;
		ComPtr<ID3D11Resource> res;
		if (SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(res.GetAddressOf())))) return WrapResource(res.Get());
#define NOVA_TRY(N, W) { ComPtr<N> p; if (SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(p.GetAddressOf())))) return Wrap<W>(p.Get()); }
		NOVA_TRY(ID3D11ShaderResourceView, DxSrv)
		NOVA_TRY(ID3D11RenderTargetView, DxRtv)
		NOVA_TRY(ID3D11DepthStencilView, DxDsv)
		NOVA_TRY(ID3D11UnorderedAccessView, DxUav)
		NOVA_TRY(ID3D11InputLayout, DxInputLayout)
		NOVA_TRY(ID3D11RasterizerState, DxRasterizer)
		NOVA_TRY(ID3D11BlendState, DxBlend)
		NOVA_TRY(ID3D11DepthStencilState, DxDepthStencil)
		NOVA_TRY(ID3D11SamplerState, DxSampler)
		NOVA_TRY(ID3D11Query, DxQuery)
		NOVA_TRY(ID3D11DeviceContext, DxContext)
		NOVA_TRY(ID3D11Device, DxDevice)
#undef NOVA_TRY
		return nullptr;
	}

	HRESULT CreateTexture(GfxDevice* device, const DirectX::Image* images, size_t count, const DirectX::TexMetadata& meta,
		D3D11_USAGE usage, UINT bindFlags, UINT cpuAccess, UINT miscFlags, GfxResource** out)
	{
		if (device->Api() == GfxApi::Vulkan)
			return GfxVk::CreateTextureFromImages(device, images, count, meta, usage, bindFlags, cpuAccess, miscFlags, out);
		if (device->Api() == GfxApi::DirectX12)
			return GfxD3D12::CreateTextureFromImages(device, images, count, meta, usage, bindFlags, cpuAccess, miscFlags, out);
		if (!device->Native())   // OpenGL 장치
			return GfxGL::CreateTextureFromImages(device, images, count, meta, usage, bindFlags, cpuAccess, miscFlags, out);
		ID3D11Resource* n = nullptr;
		HRESULT hr = DirectX::CreateTextureEx(static_cast<ID3D11Device*>(device->Native()), images, count, meta, usage, bindFlags, cpuAccess, miscFlags,
			false, &n);
		if (FAILED(hr) || !n) { *out = nullptr; return FAILED(hr) ? hr : E_FAIL; }
		*out = WrapResource(n);
		n->Release();
		return hr;
	}

	HRESULT CreateShaderResourceView(GfxDevice* device, const DirectX::Image* images, size_t count, const DirectX::TexMetadata& meta, GfxShaderResourceView** out)
	{
		if (!device->Native())   // OpenGL · Vulkan 장치: 텍스처 → 전체 뷰
		{
			ComPtr<GfxResource> tex;
			*out = nullptr;
			HRESULT hr = device->Api() == GfxApi::Vulkan
				? GfxVk::CreateTextureFromImages(device, images, count, meta, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, tex.GetAddressOf())
				: device->Api() == GfxApi::DirectX12
				? GfxD3D12::CreateTextureFromImages(device, images, count, meta, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, tex.GetAddressOf())
				: GfxGL::CreateTextureFromImages(device, images, count, meta, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, tex.GetAddressOf());
			if (FAILED(hr)) return hr;
			return device->CreateShaderResourceView(tex.Get(), nullptr, out);
		}
		ID3D11ShaderResourceView* n = nullptr;
		HRESULT hr = DirectX::CreateShaderResourceView(static_cast<ID3D11Device*>(device->Native()), images, count, meta, &n);
		return Adopt<DxSrv>(hr, &n, out);
	}

	HRESULT CaptureTexture(GfxContext* context, GfxResource* texture, DirectX::ScratchImage& out)
	{
		if (context->Api() == GfxApi::Vulkan)
			return GfxVk::CaptureTexture(context, texture, out);
		if (context->Api() == GfxApi::DirectX12)
			return GfxD3D12::CaptureTexture(context, texture, out);
		if (!context->Native())
			return GfxGL::CaptureTexture(context, texture, out);
		auto* ctx = static_cast<ID3D11DeviceContext*>(context->Native());
		if (DxContext* c = AsDx(context); c && c->Deferred)
		{
			// 렌더 스레드: 기록한 것을 다 실행시킨 뒤 immediate 에서 읽는다 (deferred 는 Map READ 를 못 한다)
			RenderThread::Sync();
			ctx = c->Imm.Get();
		}
		ComPtr<ID3D11Device> dev;
		ctx->GetDevice(dev.GetAddressOf());
		return DirectX::CaptureTexture(dev.Get(), ctx, NatRes(texture), out);
	}
}

// ImGui DX11 백엔드용 (imgui_impl_dx11.cpp 는 엔진 헤더를 넣지 않는다)
void* NovaGfx_NativeOf(void* gfxObject) { return gfxObject ? static_cast<GfxObject*>(gfxObject)->Native() : nullptr; }
void* NovaGfx_WrapD3D11(void* d3d11Object) { return Gfx::WrapD3D11(static_cast<IUnknown*>(d3d11Object)); }
void NovaGfx_Release(void* gfxObject) { if (gfxObject) static_cast<GfxObject*>(gfxObject)->Release(); }

// ---- 렌더 스레드 (RenderThread.cpp): DxContext 를 deferred context 로 바꾼다
namespace GfxDx11
{
	bool SetDeferred(GfxContext* ctx, bool on, std::string& error)
	{
		DxContext* c = AsDx(ctx);
		if (!c)
		{
			error = "not a DirectX 11 context";
			return false;
		}
		if (on == c->Deferred)
			return true;
		if (on)
		{
			ComPtr<ID3D11Device> dev;
			c->D->GetDevice(dev.GetAddressOf());
			ComPtr<ID3D11DeviceContext> deferred;
			const HRESULT hr = dev ? dev->CreateDeferredContext(0, deferred.GetAddressOf()) : E_FAIL;
			if (FAILED(hr))
			{
				char b[64];
				snprintf(b, sizeof(b), "CreateDeferredContext hr=0x%08X", (unsigned)hr);
				error = b;
				return false;
			}
			D3D11_FEATURE_DATA_THREADING th = {};
			dev->CheckFeatureSupport(D3D11_FEATURE_THREADING, &th, sizeof(th));
			c->DriverCommandLists = th.DriverCommandLists != FALSE;
			// 메인 (쿼리 결과) 과 렌더 스레드 (실행 · Present) 가 immediate 를 함께 쓴다 — 런타임이 호출마다 잠그게
			ComPtr<ID3D11Multithread> mt;
			if (SUCCEEDED(c->D.As(&mt)))
				mt->SetMultithreadProtected(TRUE);
			c->D->ClearState();
			c->Imm = c->D;
			c->D = deferred;
			c->Deferred = true;
			EditorLog::Write("RenderThread", "deferred context ready (driver command lists %s, concurrent creates %s)", th.DriverCommandLists ? "yes" : "no (runtime emulates)", th.DriverConcurrentCreates ? "yes" : "no");
			return true;
		}
		c->D = c->Imm;
		c->Imm.Reset();
		c->Deferred = false;
		c->ImmMapped.clear();
		c->EndedInSegment.clear();
		c->CopySerial.clear();
		return true;
	}

	ID3D11CommandList* FinishSegment(GfxContext* ctx)
	{
		DxContext* c = AsDx(ctx);
		if (!c || !c->Deferred)
			return nullptr;
		ID3D11CommandList* list = nullptr;
		// TRUE = 기록 쪽 상태를 그대로 이어 간다 (메인 코드는 렌더 타깃 · 상태가 프레임을 넘어 남는다고 본다)
		if (FAILED(c->D->FinishCommandList(TRUE, &list)))
			list = nullptr;
		c->EndedInSegment.clear();
		return list;
	}

	ID3D11DeviceContext* Immediate(GfxContext* ctx)
	{
		DxContext* c = AsDx(ctx);
		return c ? (c->Deferred ? c->Imm.Get() : c->D.Get()) : nullptr;
	}

	ID3D11DeviceContext* Recording(GfxContext* ctx)
	{
		DxContext* c = AsDx(ctx);
		return c ? c->D.Get() : nullptr;
	}
}
