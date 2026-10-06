#pragma once
#include "GfxWgpu.h"
#include "ShaderCross.h"
#include <webgpu/webgpu.h>
#include <functional>
#include <map>
#include <unordered_map>

#ifndef DXGI_ERROR_WAS_STILL_DRAWING
#define DXGI_ERROR_WAS_STILL_DRAWING ((HRESULT)0x887A000AL)   // Map 이 아직 끝나지 않았다 (DO_NOT_WAIT 와 같은 뜻)
#endif

// Gfx WebGPU 구현 내부 (GfxWgpuDevice.cpp · GfxWgpuContext.cpp · WgpuRhi.cpp)
//  - 효과 하나 = 바인딩 번호 공간 하나 (ShaderCross::EffectSpirv 의 바인딩 — 모든 pass 공통). 값 표는 바인딩 번호로 (원소 = 바인딩, WGSL 에는 텍스처 배열이 없다)
//  - pass 하나 = Program (단계별 셰이더 모듈 + 쓰는 바인딩의 종류). 바인드 그룹 배치는 Program 이 쓰는 바인딩만 —
//    깊이 형식 뷰를 보통 텍스처 칸에 묶으면 그 칸은 unfilterable-float, 같이 쓰는 샘플러는 non-filtering (변형마다 배치 · 파이프라인)
//  - 상수 (cbuffer) = 링의 자리 + 동적 오프셋
namespace GfxWgpuImpl
{
	class Dev;
	class Ctx;

	// 진단 (nova_web_stats — 검사가 페이지에서 읽는다): 지난 프레임의 수
	struct Stats
	{
		uint32_t Draws = 0, Dispatches = 0, Passes = 0, Clears = 0;
		uint32_t SkipNoProgram = 0, SkipNoTarget = 0, SkipNoPipeline = 0, SkipNoIndex = 0;
		uint32_t NewPipelines = 0, NewGroups = 0;
		uint32_t SkipNoVs = 0, SkipDynIndex = 0, Calls = 0;
	};
	inline Stats FrameStats, LastStats;

	uint64_t NextId();
	uint64_t HashBytes(const void* data, size_t size, uint64_t seed = 1469598103934665603ull);

	// 링 버퍼 자리 (프레임 안에서 한 번 쓰는 데이터)
	struct RingLoc
	{
		WGPUBuffer Buffer = nullptr;
		uint64_t BufferId = 0;
		uint64_t Offset = 0;
		uint64_t Frame = 0;   // 쓴 프레임 (다른 프레임이면 자리가 다시 쓰였다)
	};

	template <class Iface>
	class Obj : public Iface
	{
	public:
		explicit Obj(Dev* dev);
		~Obj() override;
		GfxApi Api() const override { return GfxApi::WebGPU; }
		Dev* D;
	};

	struct TexInfo
	{
		WGPUTexture Handle = nullptr;
		WGPUTextureFormat Format = WGPUTextureFormat_Undefined;
		DXGI_FORMAT Dxgi = DXGI_FORMAT_UNKNOWN;
		WGPUTextureDimension Dim = WGPUTextureDimension_2D;
		UINT Width = 1, Height = 1, Depth = 1, Layers = 1, Mips = 1, Samples = 1;
		bool Cube = false;
		D3D11_USAGE Usage = D3D11_USAGE_DEFAULT;
		UINT Bind = 0, CpuAccess = 0, Misc = 0;
		uint64_t Id = 0;
		uint64_t LastUse = 0;         // 마지막으로 쓴 인코더 번호 (UpdateSubresource 앞에서 제출할지)
		// STAGING: 읽기용 버퍼 (서브리소스마다 256 바이트 정렬 행)
		WGPUBuffer Staging = nullptr;
		std::vector<uint64_t> SubOffset;
		std::vector<uint32_t> SubRowPitch;
		int MapState = 0;             // 0 없음, 1 mapAsync 중, 2 읽을 수 있음
		uint64_t StagingBytes = 0;
		// DYNAMIC: Map(WRITE_DISCARD) 의 CPU 사본 (Unmap 때 writeTexture)
		std::map<UINT, std::vector<uint8_t>> Pending;
		UINT MipW(UINT m) const { return (std::max)(1u, Width >> m); }
		UINT MipH(UINT m) const { return (std::max)(1u, Height >> m); }
		UINT MipD(UINT m) const { return (std::max)(1u, Depth >> m); }
	};

	class Buf : public Obj<GfxBuffer>
	{
	public:
		using Obj::Obj;
		~Buf() override;
		void GetDesc(D3D11_BUFFER_DESC* d) const override { *d = Desc; }
		D3D11_BUFFER_DESC Desc = {};
		WGPUBuffer Handle = nullptr;    // DYNAMIC 은 없다 (링에서 그린다)
		uint64_t Size = 0;              // 4 바이트 정렬
		uint64_t Id = 0;
		std::vector<uint8_t> Shadow;    // DYNAMIC: Map 의 CPU 사본
		RingLoc Loc;                    // DYNAMIC: 지금 데이터의 링 자리
		bool HasLoc = false;
		uint64_t LastUse = 0;
		int MapState = 0;               // STAGING: 0 없음, 1 mapAsync 중, 2 읽을 수 있음
	};

	class Tex1D : public Obj<GfxTexture1D> { public: using Obj::Obj; ~Tex1D() override; void GetDesc(D3D11_TEXTURE1D_DESC* d) const override { *d = Desc; } D3D11_TEXTURE1D_DESC Desc = {}; TexInfo I; };
	class Tex2D : public Obj<GfxTexture2D> { public: using Obj::Obj; ~Tex2D() override; void GetDesc(D3D11_TEXTURE2D_DESC* d) const override { *d = Desc; } D3D11_TEXTURE2D_DESC Desc = {}; TexInfo I; };
	class Tex3D : public Obj<GfxTexture3D> { public: using Obj::Obj; ~Tex3D() override; void GetDesc(D3D11_TEXTURE3D_DESC* d) const override { *d = Desc; } D3D11_TEXTURE3D_DESC Desc = {}; TexInfo I; };

	TexInfo* TexOf(GfxResource* r);
	Buf* BufOf(GfxResource* r);

	struct ViewInfo
	{
		ComPtr<GfxResource> Res;
		TexInfo* Tex = nullptr;
		Buf* Buffer = nullptr;            // 버퍼 뷰 (구조 · raw 버퍼 SRV · UAV → 스토리지 버퍼)
		WGPUTextureView View = nullptr;
		WGPUTextureViewDimension Dim = WGPUTextureViewDimension_2D;
		WGPUTextureFormat Format = WGPUTextureFormat_Undefined;
		WGPUTextureAspect Aspect = WGPUTextureAspect_All;
		UINT BaseMip = 0, Mips = 1, BaseLayer = 0, Layers = 1;
		uint64_t BufOffset = 0, BufSize = 0;
		bool DepthFormat = false;         // 깊이 형식 (보통 텍스처 칸이면 unfilterable-float)
		uint64_t Id = 0;
	};

	template <class Iface, class Desc>
	class View : public Obj<Iface>
	{
	public:
		using Obj<Iface>::Obj;
		~View() override;
		void GetDesc(Desc* d) const override { *d = Dsc; }
		void GetResource(GfxResource** out) const override
		{
			*out = V.Res.Get();
			if (*out) (*out)->AddRef();
		}
		Desc Dsc = {};
		ViewInfo V;
	};
	using Srv = View<GfxShaderResourceView, D3D11_SHADER_RESOURCE_VIEW_DESC>;
	using Rtv = View<GfxRenderTargetView, D3D11_RENDER_TARGET_VIEW_DESC>;
	using Dsv = View<GfxDepthStencilView, D3D11_DEPTH_STENCIL_VIEW_DESC>;
	using Uav = View<GfxUnorderedAccessView, D3D11_UNORDERED_ACCESS_VIEW_DESC>;
	ViewInfo* ViewOf(GfxView* v);

	class InputLayout : public Obj<GfxInputLayout>
	{
	public:
		using Obj::Obj;
		struct Element
		{
			std::string Semantic;   // 대문자 + 번호 (TEXCOORD0)
			UINT Slot = 0;
			WGPUVertexFormat Format = WGPUVertexFormat_Undefined;
			UINT Offset = 0;
		};
		std::vector<Element> Elements;
		bool PerInstance[16] = {};
		uint64_t Id = 0;
	};

	template <class Iface, class Desc>
	class StateObj : public Obj<Iface>
	{
	public:
		using Obj<Iface>::Obj;
		void GetDesc(Desc* d) const override { *d = Dsc; }
		Desc Dsc = {};
		uint64_t Hash = 0;
	};
	using Rasterizer = StateObj<GfxRasterizerState, D3D11_RASTERIZER_DESC>;
	using Blend = StateObj<GfxBlendState, D3D11_BLEND_DESC>;
	using DepthStencil = StateObj<GfxDepthStencilState, D3D11_DEPTH_STENCIL_DESC>;

	class Sampler : public Obj<GfxSamplerState>
	{
	public:
		using Obj::Obj;
		~Sampler() override;
		void GetDesc(D3D11_SAMPLER_DESC* d) const override { *d = Dsc; }
		D3D11_SAMPLER_DESC Dsc = {};
		WGPUSampler Handle = nullptr;
		WGPUSampler Nearest = nullptr;   // non-filtering 칸용 (깊이 텍스처와 같이 쓰일 때 — 늦게 만든다)
		bool Comparison = false;
		uint64_t Id = 0;
	};

	class Query : public Obj<GfxQuery>
	{
	public:
		using Obj::Obj;
		void GetDesc(D3D11_QUERY_DESC* d) const override { *d = Dsc; }
		D3D11_QUERY_DESC Dsc = {};
	};

	// 프로그램이 쓰는 바인딩 하나 (Tint 반사)
	struct ProgramBinding
	{
		int Binding = 0;
		enum class Kind { Uniform, Storage, ReadOnlyStorage, Texture, DepthTexture, Sampler, ComparisonSampler, StorageTexture, Unsupported } Type = Kind::Unsupported;
		WGPUTextureViewDimension Dim = WGPUTextureViewDimension_2D;
		WGPUTextureSampleType Sample = WGPUTextureSampleType_Float;
		WGPUStorageTextureAccess Access = WGPUStorageTextureAccess_WriteOnly;
		WGPUTextureFormat StorageFormat = WGPUTextureFormat_Undefined;
		WGPUShaderStageFlags Visibility = 0;
	};

	class Program : public Obj<GfxObject>
	{
	public:
		using Obj::Obj;
		~Program() override;
		struct Stage { WGPUShaderStage Flag; WGPUShaderModule Module = nullptr; std::string Entry; };
		std::vector<Stage> Stages;
		std::vector<ProgramBinding> Bindings;          // 바인딩 번호 순
		std::vector<std::pair<int, int>> SamplerPairs; // (텍스처, 샘플러)
		std::vector<std::pair<std::string, int>> VertexInputs;
		uint32_t PixelOutputs = 0;
		bool Compute = false;
		bool UsesInstanceIndex = false;                // SV_InstanceID → 시작 인스턴스를 정점 버퍼 오프셋으로
		int DynamicUniforms = 0;                       // 동적 오프셋 상수 칸 수 (바인딩 순)
		std::string Name;
		uint64_t Id = 0;
		// 변형 (unfilterable 텍스처 칸 마스크) → 배치
		struct Layout { WGPUBindGroupLayout Group = nullptr; WGPUPipelineLayout Pipeline = nullptr; WGPUComputePipeline ComputePipe = nullptr; };
		std::unordered_map<uint64_t, Layout> Layouts;
	};

	// 효과의 바인딩 값 (바인딩 번호마다)
	struct BindingValue
	{
		GfxShaderResourceView* View = nullptr;
		GfxUnorderedAccessView* Uav = nullptr;
		GfxSamplerState* Sampler = nullptr;
		RingLoc Ubo;
		uint32_t Range = 0;
	};

	struct StageSource
	{
		FxParser::Stage Stage = FxParser::Stage::Vertex;
		const std::string* Wgsl = nullptr;
		std::string Entry;
		const std::vector<ShaderCross::WgslBinding>* Bindings = nullptr;
	};

	// WgpuRhi (효과) → 장치 · 컨텍스트
	HRESULT CreateProgram(GfxDevice* device, const StageSource* stages, uint32_t count, const std::vector<std::pair<std::string, int>>& vertexInputs,
		uint32_t pixelOutputs, const std::vector<std::pair<int, int>>& samplerPairs, const std::string& name, GfxObject** out, std::string& error);
	bool WriteConstants(GfxDevice* device, const void* data, uint32_t size, RingLoc& loc);
	bool IsCurrent(GfxDevice* device, const RingLoc& loc);
	void SetProgram(GfxContext* context, GfxObject* program, const BindingValue* values, uint32_t count);

	// 파이프라인 키 (memcmp 가능 — 빈칸 없이 0 으로)
	struct PipelineKey
	{
		uint64_t Program = 0, Variant = 0, Layout = 0;
		uint64_t Raster = 0, Blend = 0, Depth = 0;
		uint32_t Strides[8] = {};
		uint32_t Colors[8] = {};          // WGPUTextureFormat
		uint32_t DepthFormat = 0;
		uint32_t SampleMask = 0xFFFFFFFF;
		uint8_t ColorCount = 0, Samples = 1, Topology = 0, StripIndex = 0;
		uint32_t Pad = 0;
		bool operator==(const PipelineKey& o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
	};
	struct PipelineKeyHash { size_t operator()(const PipelineKey& k) const { return (size_t)HashBytes(&k, sizeof(k)); } };

	class Dev : public GfxDevice
	{
	public:
		~Dev() override;
		GfxApi Api() const override { return GfxApi::WebGPU; }

		WGPUInstance Instance = nullptr;
		WGPUDevice Device = nullptr;
		WGPUQueue Queue = nullptr;
		WGPUSurface Surface = nullptr;
		WGPUSwapChain Swap = nullptr;
		int SwapW = 0, SwapH = 0;
		WGPUTextureFormat SwapFormat = WGPUTextureFormat_BGRA8Unorm;
		std::string Name;
		std::set<std::string> Reported;
		uint64_t Resources = 0;
		Ctx* Immediate = nullptr;

		// ---- 명령: 인코더 하나 (제출하면 새로). 번호 = 자원이 이 인코더에서 쓰였는지
		WGPUCommandEncoder Encoder = nullptr;
		uint64_t EncoderSerial = 1;
		WGPUCommandEncoder Enc();
		void Submit();            // 열린 패스를 닫고 인코더를 제출
		uint64_t Frame = 1;       // Present 마다 +1 (링 자리 재사용)

		// ---- 링 (상수 · 동적 버퍼): 4 MB 덩어리, 프레임마다 처음부터
		struct Chunk { WGPUBuffer Buffer = nullptr; uint64_t Size = 0, Used = 0, Id = 0; };
		std::vector<Chunk> Chunks;
		size_t CurrentChunk = 0;
		bool RingWrite(const void* data, uint64_t size, uint64_t align, RingLoc& loc);
		void RingReset();

		// ---- 캐시
		std::unordered_map<PipelineKey, WGPURenderPipeline, PipelineKeyHash> Pipelines;
		std::unordered_map<uint64_t, WGPUBindGroup> BindGroups;   // 프레임마다 비운다
		// 빈 칸 더미 (보기 차원 · 표본 종류마다)
		std::map<int, WGPUTextureView> Dummies;
		WGPUSampler DummySampler = nullptr, DummyCompare = nullptr, DummyNearest = nullptr;
		WGPUBuffer DummyBuffer = nullptr;
		WGPUTextureView DummyView(WGPUTextureViewDimension dim, WGPUTextureSampleType sample);
		WGPUTextureView DummyStorage(WGPUTextureFormat format, WGPUTextureViewDimension dim);
		std::map<std::pair<int, int>, WGPUTextureView> StorageDummies;

		// ---- 블릿 (Present · GenerateMips): 형식마다 파이프라인
		WGPUShaderModule BlitModule = nullptr;
		WGPUBindGroupLayout BlitGroup = nullptr;
		WGPUPipelineLayout BlitLayout = nullptr;
		WGPUSampler BlitSampler = nullptr;
		std::map<WGPUTextureFormat, WGPURenderPipeline> BlitPipes;
		WGPURenderPipeline BlitPipeline(WGPUTextureFormat format);
		void Blit(WGPUTextureView src, WGPUTextureView dst, WGPUTextureFormat dstFormat);

		void Once(const std::string& key, const char* fmt, const char* arg = "");
		bool Init(std::string& error);

		HRESULT CreateBuffer(const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxBuffer** out) override;
		HRESULT CreateTexture1D(const D3D11_TEXTURE1D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture1D** out) override;
		HRESULT CreateTexture2D(const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture2D** out) override;
		HRESULT CreateTexture3D(const D3D11_TEXTURE3D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture3D** out) override;
		HRESULT CreateShaderResourceView(GfxResource* r, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc, GfxShaderResourceView** out) override;
		HRESULT CreateRenderTargetView(GfxResource* r, const D3D11_RENDER_TARGET_VIEW_DESC* desc, GfxRenderTargetView** out) override;
		HRESULT CreateDepthStencilView(GfxResource* r, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, GfxDepthStencilView** out) override;
		HRESULT CreateUnorderedAccessView(GfxResource* r, const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc, GfxUnorderedAccessView** out) override;
		HRESULT CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* elements, UINT count, const void* signature, SIZE_T signatureSize, GfxInputLayout** out) override;
		HRESULT CreateRasterizerState(const D3D11_RASTERIZER_DESC* desc, GfxRasterizerState** out) override;
		HRESULT CreateBlendState(const D3D11_BLEND_DESC* desc, GfxBlendState** out) override;
		HRESULT CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC* desc, GfxDepthStencilState** out) override;
		HRESULT CreateSamplerState(const D3D11_SAMPLER_DESC* desc, GfxSamplerState** out) override;
		HRESULT CreateQuery(const D3D11_QUERY_DESC* desc, GfxQuery** out) override;
		void GetImmediateContext(GfxContext** out) override;
		HRESULT GetDeviceRemovedReason() override { return S_OK; }

		HRESULT MakeTexture(TexInfo& t, const D3D11_SUBRESOURCE_DATA* data, const char* what);
		void UploadSub(TexInfo& t, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch);
		WGPUTextureView MakeView(TexInfo& t, WGPUTextureViewDimension dim, WGPUTextureFormat format, WGPUTextureAspect aspect, UINT baseMip, UINT mips, UINT baseLayer, UINT layers);
	};

	template <class Iface>
	Obj<Iface>::Obj(Dev* dev) : D(dev) { D->AddRef(); ++D->Resources; }
	template <class Iface>
	Obj<Iface>::~Obj() { --D->Resources; D->GfxDevice::Release(); }

	class Ctx : public GfxContext
	{
	public:
		~Ctx() override;
		GfxApi Api() const override { return GfxApi::WebGPU; }
		ComPtr<Dev> D;

		// D3D 상태
		ComPtr<GfxInputLayout> Layout;
		D3D11_PRIMITIVE_TOPOLOGY Topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		struct VB { ComPtr<GfxBuffer> Buf; UINT Stride = 0, Offset = 0; };
		VB Vbs[16];
		ComPtr<GfxBuffer> Ib;
		DXGI_FORMAT IbFormat = DXGI_FORMAT_R16_UINT;
		UINT IbOffset = 0;
		ComPtr<GfxRenderTargetView> Rtvs[8];
		UINT RtvCount = 0;
		ComPtr<GfxDepthStencilView> DsvView;
		ComPtr<GfxDepthStencilState> Ds;
		UINT StencilRef = 0;
		ComPtr<GfxBlendState> Bs;
		float BlendFactor[4] = { 1, 1, 1, 1 };
		UINT SampleMask = 0xFFFFFFFF;
		ComPtr<GfxRasterizerState> Rs;
		D3D11_VIEWPORT Viewports[16] = {};
		UINT ViewportCount = 0;
		D3D11_RECT Scissors[16] = {};
		UINT ScissorCount = 0;

		// 효과가 넣은 프로그램 · 값
		ComPtr<Program> Prog;
		std::vector<BindingValue> Values;
		std::vector<ComPtr<GfxObject>> Held;

		// 렌더 패스 (늦게 연다)
		WGPURenderPassEncoder Pass = nullptr;
		bool TargetsDirty = true;
		// 지우기 예약: 뷰 → 값 (패스를 열 때 loadOp clear, 또는 비어 있는 패스로)
		std::map<GfxView*, std::array<float, 4>> PendingColor;
		struct DepthClear { float Depth = 1.0f; UINT8 Stencil = 0; UINT Flags = 0; };
		std::map<GfxView*, DepthClear> PendingDepth;
		std::vector<ComPtr<GfxView>> PendingHeld;
		// 패스 안의 상태 (같으면 다시 묶지 않는다)
		WGPURenderPipeline BoundPipe = nullptr;
		WGPUBindGroup BoundGroup = nullptr;
		std::vector<uint32_t> BoundOffsets;
		bool DynamicDirty = true;
		WGPUBuffer BoundVb[16] = {};
		uint64_t BoundVbOffset[16] = {};
		WGPUBuffer BoundIb = nullptr;
		uint64_t BoundIbOffset = 0;

		void EndPass();
		bool BeginPass();
		void FlushClears();       // 예약된 지우기를 모두 실행 (패스 밖)
		bool PrepareDraw(bool indexed, UINT startInstance, uint64_t& instanceShiftBytes);
		WGPUBindGroup BuildGroup(Program& p, uint64_t variant, std::vector<uint32_t>& offsets, bool compute);
		uint64_t VariantOf(Program& p);
		void Touch(GfxResource* r);   // 이 인코더에서 썼다

		// ---- GfxContext
		void IASetInputLayout(GfxInputLayout* l) override { Layout = l; }
		void IAGetInputLayout(GfxInputLayout** l) override { *l = Layout.Get(); if (*l) (*l)->AddRef(); }
		void IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY t) override { Topo = t; }
		void IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY* t) override { *t = Topo; }
		void IASetVertexBuffers(UINT start, UINT count, GfxBuffer* const* buffers, const UINT* strides, const UINT* offsets) override;
		void IAGetVertexBuffers(UINT start, UINT count, GfxBuffer** buffers, UINT* strides, UINT* offsets) override;
		void IASetIndexBuffer(GfxBuffer* b, DXGI_FORMAT f, UINT o) override { Ib = b; IbFormat = f; IbOffset = o; }
		void IAGetIndexBuffer(GfxBuffer** b, DXGI_FORMAT* f, UINT* o) override;
		void SOSetTargets(UINT, GfxBuffer* const*, const UINT*) override {}
		void OMSetRenderTargets(UINT count, GfxRenderTargetView* const* rtvs, GfxDepthStencilView* dsv) override;
		void OMGetRenderTargets(UINT count, GfxRenderTargetView** rtvs, GfxDepthStencilView** dsv) override;
		void OMSetDepthStencilState(GfxDepthStencilState* s, UINT ref) override { Ds = s; StencilRef = ref; DynamicDirty = true; }
		void OMGetDepthStencilState(GfxDepthStencilState** s, UINT* ref) override { *s = Ds.Get(); if (*s) (*s)->AddRef(); if (ref) *ref = StencilRef; }
		void OMSetBlendState(GfxBlendState* s, const FLOAT f[4], UINT mask) override;
		void OMGetBlendState(GfxBlendState** s, FLOAT f[4], UINT* mask) override;
		void RSSetState(GfxRasterizerState* s) override { Rs = s; DynamicDirty = true; }
		void RSGetState(GfxRasterizerState** s) override { *s = Rs.Get(); if (*s) (*s)->AddRef(); }
		void RSSetViewports(UINT count, const D3D11_VIEWPORT* v) override;
		void RSGetViewports(UINT* count, D3D11_VIEWPORT* v) override;
		void RSSetScissorRects(UINT count, const D3D11_RECT* r) override;
		void RSGetScissorRects(UINT* count, D3D11_RECT* r) override;
		void PSSetShaderResources(UINT, UINT, GfxShaderResourceView* const*) override {}
		void VSSetShaderResources(UINT, UINT, GfxShaderResourceView* const*) override {}
		void CSSetShaderResources(UINT, UINT, GfxShaderResourceView* const*) override {}
		void CSSetUnorderedAccessViews(UINT, UINT, GfxUnorderedAccessView* const*, const UINT*) override {}
		void CSSetShader(void*, void*, UINT) override {}
		void Draw(UINT vertexCount, UINT startVertex) override { DrawInstanced(vertexCount, 1, startVertex, 0); }
		void DrawIndexed(UINT indexCount, UINT startIndex, INT baseVertex) override { DrawIndexedInstanced(indexCount, 1, startIndex, baseVertex, 0); }
		void DrawInstanced(UINT vertexCountPerInstance, UINT instanceCount, UINT startVertex, UINT startInstance) override;
		void DrawIndexedInstanced(UINT indexCountPerInstance, UINT instanceCount, UINT startIndex, INT baseVertex, UINT startInstance) override;
		void DrawAuto() override {}
		void Dispatch(UINT x, UINT y, UINT z) override;
		void ClearRenderTargetView(GfxRenderTargetView* rtv, const FLOAT color[4]) override;
		void ClearDepthStencilView(GfxDepthStencilView* dsv, UINT flags, FLOAT depth, UINT8 stencil) override;
		HRESULT Map(GfxResource* resource, UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped) override;
		void Unmap(GfxResource* resource, UINT subresource) override;
		void UpdateSubresource(GfxResource* resource, UINT subresource, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch) override;
		void CopyResource(GfxResource* dst, GfxResource* src) override;
		void CopySubresourceRegion(GfxResource* dst, UINT dstSub, UINT x, UINT y, UINT z, GfxResource* src, UINT srcSub, const D3D11_BOX* box) override;
		void GenerateMips(GfxShaderResourceView* srv) override;
		void Begin(GfxQuery*) override {}
		void End(GfxQuery*) override {}
		HRESULT GetData(GfxQuery* query, void* data, UINT size, UINT flags) override;
		void Flush() override;
		void ClearState() override;
	};

	// 형식 변환 (GfxWgpuDevice.cpp)
	WGPUTextureFormat TextureFormat(DXGI_FORMAT f, UINT bindFlags);   // TYPELESS 는 바인드로 (깊이 · 색)
	WGPUTextureFormat ViewFormat(DXGI_FORMAT f, WGPUTextureFormat textureFormat, WGPUTextureAspect& aspect);
	bool IsDepthFormat(WGPUTextureFormat f);
	bool IsCompressed(WGPUTextureFormat f);
	UINT BlockBytes(WGPUTextureFormat f, UINT& blockW, UINT& blockH);
	WGPUVertexFormat VertexFormat(DXGI_FORMAT f);
	WGPUCompareFunction Compare(D3D11_COMPARISON_FUNC f);
	D3D11_RASTERIZER_DESC DefaultRasterizer();
	D3D11_BLEND_DESC DefaultBlend();
	D3D11_DEPTH_STENCIL_DESC DefaultDepthStencil();
}
