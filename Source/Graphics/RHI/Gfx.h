#pragma once
#include <d3d11.h>
#include <atomic>

// Gfx — 엔진 렌더러가 쓰는 그래픽 API 층 (D3D11 과 같은 모양).
//  엔진 코드는 ID3D11Xxx 대신 GfxXxx 를 쓴다. 메서드 이름·인자·설명 구조체(D3D11_*_DESC)는 D3D11 과 같아서
//  기존 렌더러 코드를 거의 그대로 두고 API 를 바꿀 수 있다.
//  - DirectX 11 구현(GfxDx11.cpp): 진짜 D3D11 객체를 감싸 그대로 넘긴다 (Native() = ID3D11 객체)
//  - OpenGL 구현(GfxGL.cpp): 같은 뜻을 GL 4.5 로 (Native() = nullptr)
//  - Vulkan 구현(Vulkan/GfxVk*.cpp): 같은 뜻을 Vulkan 1.3 으로 (Native() = nullptr, Api() = Vulkan)
//  - 셰이더 효과는 RhiFx.h (FxEffect …), 새 코드에서 쓰기 쉬운 장치 API 는 Rhi.h
//  - COM 처럼 AddRef/Release/QueryInterface 가 있어 ComPtr<GfxXxx> 를 그대로 쓴다. 만드는 함수는 참조 1 로 돌려준다
// 객체를 만든 구현 (Native() == nullptr 만으로는 GL 과 Vulkan 을 가를 수 없다)
enum class GfxApi { DirectX11, OpenGL, Vulkan };

class __declspec(uuid("4E6F7661-0001-4A00-8000-000000000001")) GfxObject : public IUnknown
{
public:
	virtual ~GfxObject() = default;
	ULONG STDMETHODCALLTYPE AddRef() override { return ++_refs; }
	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG n = --_refs;
		if (n == 0) delete this;
		return n;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
	{
		if (!out) return E_POINTER;
		if (Is(riid))
		{
			AddRef();
			*out = this;
			return S_OK;
		}
		*out = nullptr;
		return E_NOINTERFACE;
	}
	virtual void* Native() const { return nullptr; }   // DirectX 11 = ID3D11 객체, 그 밖 = nullptr
	virtual GfxApi Api() const { return Native() ? GfxApi::DirectX11 : GfxApi::OpenGL; }   // Vulkan 객체가 덮어쓴다

protected:
	virtual bool Is(REFIID riid) const { return riid == __uuidof(GfxObject) || riid == __uuidof(IUnknown); }

private:
	std::atomic<ULONG> _refs{ 1 };
};

#define NOVA_GFX_IS(Self, Base) \
protected: bool Is(REFIID riid) const override { return riid == __uuidof(Self) || Base::Is(riid); } public:

class __declspec(uuid("4E6F7661-0001-4A00-8000-000000000002")) GfxResource : public GfxObject
{
	NOVA_GFX_IS(GfxResource, GfxObject)
	virtual void GetType(D3D11_RESOURCE_DIMENSION* type) const = 0;
};

class __declspec(uuid("4E6F7661-0001-4A00-8000-000000000003")) GfxBuffer : public GfxResource
{
	NOVA_GFX_IS(GfxBuffer, GfxResource)
	void GetType(D3D11_RESOURCE_DIMENSION* type) const override { *type = D3D11_RESOURCE_DIMENSION_BUFFER; }
	virtual void GetDesc(D3D11_BUFFER_DESC* desc) const = 0;
};

class __declspec(uuid("4E6F7661-0001-4A00-8000-000000000004")) GfxTexture1D : public GfxResource
{
	NOVA_GFX_IS(GfxTexture1D, GfxResource)
	void GetType(D3D11_RESOURCE_DIMENSION* type) const override { *type = D3D11_RESOURCE_DIMENSION_TEXTURE1D; }
	virtual void GetDesc(D3D11_TEXTURE1D_DESC* desc) const = 0;
};

class __declspec(uuid("4E6F7661-0001-4A00-8000-000000000005")) GfxTexture2D : public GfxResource
{
	NOVA_GFX_IS(GfxTexture2D, GfxResource)
	void GetType(D3D11_RESOURCE_DIMENSION* type) const override { *type = D3D11_RESOURCE_DIMENSION_TEXTURE2D; }
	virtual void GetDesc(D3D11_TEXTURE2D_DESC* desc) const = 0;
};

class __declspec(uuid("4E6F7661-0001-4A00-8000-000000000006")) GfxTexture3D : public GfxResource
{
	NOVA_GFX_IS(GfxTexture3D, GfxResource)
	void GetType(D3D11_RESOURCE_DIMENSION* type) const override { *type = D3D11_RESOURCE_DIMENSION_TEXTURE3D; }
	virtual void GetDesc(D3D11_TEXTURE3D_DESC* desc) const = 0;
};

class __declspec(uuid("4E6F7661-0001-4A00-8000-000000000007")) GfxView : public GfxObject
{
	NOVA_GFX_IS(GfxView, GfxObject)
	virtual void GetResource(GfxResource** resource) const = 0;   // 참조 +1
};

class __declspec(uuid("4E6F7661-0001-4A00-8000-000000000008")) GfxShaderResourceView : public GfxView
{
	NOVA_GFX_IS(GfxShaderResourceView, GfxView)
	virtual void GetDesc(D3D11_SHADER_RESOURCE_VIEW_DESC* desc) const = 0;
};

class __declspec(uuid("4E6F7661-0001-4A00-8000-000000000009")) GfxRenderTargetView : public GfxView
{
	NOVA_GFX_IS(GfxRenderTargetView, GfxView)
	virtual void GetDesc(D3D11_RENDER_TARGET_VIEW_DESC* desc) const = 0;
};

class __declspec(uuid("4E6F7661-0001-4A00-8000-00000000000A")) GfxDepthStencilView : public GfxView
{
	NOVA_GFX_IS(GfxDepthStencilView, GfxView)
	virtual void GetDesc(D3D11_DEPTH_STENCIL_VIEW_DESC* desc) const = 0;
};

class __declspec(uuid("4E6F7661-0001-4A00-8000-00000000000B")) GfxUnorderedAccessView : public GfxView
{
	NOVA_GFX_IS(GfxUnorderedAccessView, GfxView)
	virtual void GetDesc(D3D11_UNORDERED_ACCESS_VIEW_DESC* desc) const = 0;
};

class __declspec(uuid("4E6F7661-0001-4A00-8000-00000000000C")) GfxInputLayout : public GfxObject
{
	NOVA_GFX_IS(GfxInputLayout, GfxObject)
};

class __declspec(uuid("4E6F7661-0001-4A00-8000-00000000000D")) GfxRasterizerState : public GfxObject
{
	NOVA_GFX_IS(GfxRasterizerState, GfxObject)
	virtual void GetDesc(D3D11_RASTERIZER_DESC* desc) const = 0;
};

class __declspec(uuid("4E6F7661-0001-4A00-8000-00000000000E")) GfxBlendState : public GfxObject
{
	NOVA_GFX_IS(GfxBlendState, GfxObject)
	virtual void GetDesc(D3D11_BLEND_DESC* desc) const = 0;
};

class __declspec(uuid("4E6F7661-0001-4A00-8000-00000000000F")) GfxDepthStencilState : public GfxObject
{
	NOVA_GFX_IS(GfxDepthStencilState, GfxObject)
	virtual void GetDesc(D3D11_DEPTH_STENCIL_DESC* desc) const = 0;
};

class __declspec(uuid("4E6F7661-0001-4A00-8000-000000000010")) GfxSamplerState : public GfxObject
{
	NOVA_GFX_IS(GfxSamplerState, GfxObject)
	virtual void GetDesc(D3D11_SAMPLER_DESC* desc) const = 0;
};

class __declspec(uuid("4E6F7661-0001-4A00-8000-000000000011")) GfxQuery : public GfxObject
{
	NOVA_GFX_IS(GfxQuery, GfxObject)
	virtual void GetDesc(D3D11_QUERY_DESC* desc) const = 0;
};

class GfxContext;

class __declspec(uuid("4E6F7661-0001-4A00-8000-000000000012")) GfxDevice : public GfxObject
{
	NOVA_GFX_IS(GfxDevice, GfxObject)
	virtual HRESULT CreateBuffer(const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxBuffer** out) = 0;
	virtual HRESULT CreateTexture1D(const D3D11_TEXTURE1D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture1D** out) = 0;
	virtual HRESULT CreateTexture2D(const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture2D** out) = 0;
	virtual HRESULT CreateTexture3D(const D3D11_TEXTURE3D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture3D** out) = 0;
	virtual HRESULT CreateShaderResourceView(GfxResource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc, GfxShaderResourceView** out) = 0;
	virtual HRESULT CreateRenderTargetView(GfxResource* resource, const D3D11_RENDER_TARGET_VIEW_DESC* desc, GfxRenderTargetView** out) = 0;
	virtual HRESULT CreateDepthStencilView(GfxResource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, GfxDepthStencilView** out) = 0;
	virtual HRESULT CreateUnorderedAccessView(GfxResource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc, GfxUnorderedAccessView** out) = 0;
	// signature = FxPass::GetDesc 의 pIAInputSignature (D3D11 = VS 입력 서명, GL = 의미 → location 표)
	virtual HRESULT CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* elements, UINT count, const void* signature, SIZE_T signatureSize, GfxInputLayout** out) = 0;
	virtual HRESULT CreateRasterizerState(const D3D11_RASTERIZER_DESC* desc, GfxRasterizerState** out) = 0;
	virtual HRESULT CreateBlendState(const D3D11_BLEND_DESC* desc, GfxBlendState** out) = 0;
	virtual HRESULT CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC* desc, GfxDepthStencilState** out) = 0;
	virtual HRESULT CreateSamplerState(const D3D11_SAMPLER_DESC* desc, GfxSamplerState** out) = 0;
	virtual HRESULT CreateQuery(const D3D11_QUERY_DESC* desc, GfxQuery** out) = 0;
	virtual void GetImmediateContext(GfxContext** out) = 0;   // 참조 +1
	virtual HRESULT GetDeviceRemovedReason() = 0;
};

class __declspec(uuid("4E6F7661-0001-4A00-8000-000000000013")) GfxContext : public GfxObject
{
	NOVA_GFX_IS(GfxContext, GfxObject)
	// ---- 입력
	virtual void IASetInputLayout(GfxInputLayout* layout) = 0;
	virtual void IAGetInputLayout(GfxInputLayout** layout) = 0;
	virtual void IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY topology) = 0;
	virtual void IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY* topology) = 0;
	virtual void IASetVertexBuffers(UINT start, UINT count, GfxBuffer* const* buffers, const UINT* strides, const UINT* offsets) = 0;
	virtual void IAGetVertexBuffers(UINT start, UINT count, GfxBuffer** buffers, UINT* strides, UINT* offsets) = 0;
	virtual void IASetIndexBuffer(GfxBuffer* buffer, DXGI_FORMAT format, UINT offset) = 0;
	virtual void IAGetIndexBuffer(GfxBuffer** buffer, DXGI_FORMAT* format, UINT* offset) = 0;
	virtual void SOSetTargets(UINT count, GfxBuffer* const* buffers, const UINT* offsets) = 0;

	// ---- 출력·래스터
	virtual void OMSetRenderTargets(UINT count, GfxRenderTargetView* const* rtvs, GfxDepthStencilView* dsv) = 0;
	virtual void OMGetRenderTargets(UINT count, GfxRenderTargetView** rtvs, GfxDepthStencilView** dsv) = 0;
	virtual void OMSetDepthStencilState(GfxDepthStencilState* state, UINT stencilRef) = 0;
	virtual void OMGetDepthStencilState(GfxDepthStencilState** state, UINT* stencilRef) = 0;
	virtual void OMSetBlendState(GfxBlendState* state, const FLOAT factor[4], UINT sampleMask) = 0;
	virtual void OMGetBlendState(GfxBlendState** state, FLOAT factor[4], UINT* sampleMask) = 0;
	virtual void RSSetState(GfxRasterizerState* state) = 0;
	virtual void RSGetState(GfxRasterizerState** state) = 0;
	virtual void RSSetViewports(UINT count, const D3D11_VIEWPORT* viewports) = 0;
	virtual void RSGetViewports(UINT* count, D3D11_VIEWPORT* viewports) = 0;
	virtual void RSSetScissorRects(UINT count, const D3D11_RECT* rects) = 0;
	virtual void RSGetScissorRects(UINT* count, D3D11_RECT* rects) = 0;

	// ---- 셰이더 자원 (효과 밖에서 직접 묶을 때)
	virtual void PSSetShaderResources(UINT start, UINT count, GfxShaderResourceView* const* views) = 0;
	virtual void VSSetShaderResources(UINT start, UINT count, GfxShaderResourceView* const* views) = 0;
	virtual void CSSetShaderResources(UINT start, UINT count, GfxShaderResourceView* const* views) = 0;
	virtual void CSSetUnorderedAccessViews(UINT start, UINT count, GfxUnorderedAccessView* const* views, const UINT* initialCounts) = 0;
	virtual void CSSetShader(void* shader, void* classInstances, UINT count) = 0;   // 효과 밖 compute 셰이더 풀기 (nullptr) 용
	// 테셀레이션 (Hull · Domain) 셰이더 풀기 — 테셀레이션 그리기 뒤. DX11 의 Effects11 은 그 단계를 정하지 않는 패스가 풀지 않는다
	//  (OpenGL · Vulkan · GLES 는 패스마다 프로그램 · 파이프라인이 따로라 할 일이 없다)
	virtual void ClearTessellationShaders() {}

	// ---- 그리기
	virtual void Draw(UINT vertexCount, UINT startVertex) = 0;
	virtual void DrawIndexed(UINT indexCount, UINT startIndex, INT baseVertex) = 0;
	virtual void DrawInstanced(UINT vertexCountPerInstance, UINT instanceCount, UINT startVertex, UINT startInstance) = 0;
	virtual void DrawIndexedInstanced(UINT indexCountPerInstance, UINT instanceCount, UINT startIndex, INT baseVertex, UINT startInstance) = 0;
	virtual void DrawAuto() = 0;
	virtual void Dispatch(UINT x, UINT y, UINT z) = 0;

	// ---- 자원
	virtual void ClearRenderTargetView(GfxRenderTargetView* rtv, const FLOAT color[4]) = 0;
	virtual void ClearDepthStencilView(GfxDepthStencilView* dsv, UINT flags, FLOAT depth, UINT8 stencil) = 0;
	virtual HRESULT Map(GfxResource* resource, UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped) = 0;
	virtual void Unmap(GfxResource* resource, UINT subresource) = 0;
	virtual void UpdateSubresource(GfxResource* resource, UINT subresource, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch) = 0;
	virtual void CopyResource(GfxResource* dst, GfxResource* src) = 0;
	virtual void CopySubresourceRegion(GfxResource* dst, UINT dstSub, UINT x, UINT y, UINT z, GfxResource* src, UINT srcSub, const D3D11_BOX* box) = 0;
	virtual void GenerateMips(GfxShaderResourceView* srv) = 0;

	// ---- 쿼리·기타
	virtual void Begin(GfxQuery* query) = 0;
	virtual void End(GfxQuery* query) = 0;
	virtual HRESULT GetData(GfxQuery* query, void* data, UINT size, UINT flags) = 0;
	virtual void Flush() = 0;
	virtual void ClearState() = 0;

	// ---- GPU 가 정하는 그리기 (오클루전 컬링): 인자 = D3D 배치 (Indexed 5 · Instanced 4 uint). 구현하지 않으면 false → 호출한 쪽이 CPU 목록으로
	//  SupportsGpuDriven = compute (구조 · raw 버퍼 SRV · UAV, 텍스처 UAV) + 아래 함수를 모두 지원한다
	virtual bool SupportsGpuDriven() const { return false; }
	virtual bool DrawIndexedInstancedIndirect(GfxBuffer* args, UINT offset) { (void)args; (void)offset; return false; }
	virtual bool DrawInstancedIndirect(GfxBuffer* args, UINT offset) { (void)args; (void)offset; return false; }
	// D3D11_QUERY_OCCLUSION_PREDICATE 쿼리 결과로 그리기를 건너뛴다 (value = FALSE: 결과가 FALSE 면 건너뜀). nullptr = 끔
	virtual bool SetPredication(GfxQuery* predicate, BOOL value) { (void)predicate; (void)value; return false; }
	// 조건부 렌더링이 없는 API (OpenGL ES): 버퍼의 uint (0 / 1 — compute 가 썼다) 로 그리기를 건너뛴다. 인스턴스 하나의 그리기만
	//  (구현은 그리기를 간접 그리기로 바꿔 그 값을 InstanceCount 자리에 복사한다). nullptr = 끔
	virtual bool SetPredicationBuffer(GfxBuffer* buffer, UINT offset) { (void)buffer; (void)offset; return false; }
	virtual bool ClearUnorderedAccessViewUint(GfxUnorderedAccessView* uav, const UINT values[4]) { (void)uav; (void)values; return false; }
};

namespace DirectX { struct Image; struct TexMetadata; class ScratchImage; }

namespace Gfx
{
	// 엔진 장치·컨텍스트 (App 이 그래픽 초기화 때 정함)
	GfxDevice* Device();
	GfxContext* Context();
	void SetMain(GfxDevice* device, GfxContext* context);

	// DirectX 11: 이미 있는 D3D11 객체를 감싼다 (같은 객체는 늘 같은 감싸개, 참조 +1). 다른 API 에서는 nullptr
	GfxObject* WrapD3D11(IUnknown* native);
	template <class T> T* WrapD3D11As(IUnknown* native) { return static_cast<T*>(WrapD3D11(native)); }

	// DirectXTex 이미지 → 텍스처 (밉·배열·큐브 그대로)
	HRESULT CreateTexture(GfxDevice* device, const DirectX::Image* images, size_t count, const DirectX::TexMetadata& meta,
		D3D11_USAGE usage, UINT bindFlags, UINT cpuAccess, UINT miscFlags, GfxResource** out);
	HRESULT CreateShaderResourceView(GfxDevice* device, const DirectX::Image* images, size_t count, const DirectX::TexMetadata& meta, GfxShaderResourceView** out);
	// 텍스처 → CPU 이미지 (스크린샷·검사)
	HRESULT CaptureTexture(GfxContext* context, GfxResource* texture, DirectX::ScratchImage& out);
}
