#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include "GfxD3D12.h"
#include "GfxD3D12Shared.h"

// Gfx DirectX 12 구현 내부 (GfxD3D12*.cpp 만 쓴다). Vulkan 백엔드와 같은 생각:
//  - 제출: 펜스 하나, 제출마다 값 +1. 명령 할당기 · 업로드 링 · 디스크립터 링 · 늦은 삭제는 "마지막으로 쓴 제출 값" 이 끝난 뒤에
//  - 기록: 직접 명령 목록 하나 (그리기 · 복사 · 디스패치 모두). 새 자원의 처음 데이터도 같은 목록 (그 자원을 쓰는 명령보다 먼저)
//  - 상태: 텍스처는 서브리소스마다 D3D12_RESOURCE_STATES 를 CPU 가 따라가 장벽을 모아 낸다 (늘 명시적 — 승격 · 감쇠 없음).
//    버퍼는 제출이 끝나면 COMMON 으로 감쇠한다 → 새 명령 목록 (Epoch) 의 첫 사용은 장벽 없이 승격, 그 뒤만 명시적 장벽
//  - 디스크립터: 뷰는 CPU 전용 힙에 한 번 만들고, 그리기 때 셰이더에서 보이는 링 힙으로 복사 (단계마다 표 하나 + 샘플러 표)
//  - 상수 · DYNAMIC 버퍼 = 업로드 힙 링 (Map 마다 새 자리 — Vulkan 과 같은 CPU 사본 방식)
namespace GfxD3D12Impl
{
	class Dev;
	class Ctx;
	using GfxD3D12Shared::RingLoc;

	uint64_t NextId();

	// ---- CPU 전용 디스크립터 (뷰 · 샘플러 · RTV · DSV): 힙 덩어리 + 빈 칸 목록
	class CpuDescriptors
	{
	public:
		void Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT perHeap);
		D3D12_CPU_DESCRIPTOR_HANDLE Alloc();
		void Free(D3D12_CPU_DESCRIPTOR_HANDLE h) { if (h.ptr) _free.push_back(h); }
		UINT Increment() const { return _inc; }
	private:
		ID3D12Device* _device = nullptr;
		D3D12_DESCRIPTOR_HEAP_TYPE _type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		UINT _perHeap = 1024, _inc = 0, _used = 0;
		std::vector<ComPtr<ID3D12DescriptorHeap>> _heaps;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> _free;
	};

	// ---- 셰이더에서 보이는 링 힙: 앞에서부터 쓰고, 제출 값이 끝난 칸을 다시 쓴다
	class GpuRing
	{
	public:
		bool Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT size);
		// count 칸을 이어서. 모자라면 false (부른 쪽이 제출 · 기다린 뒤 다시)
		bool Alloc(UINT count, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu);
		// 제출할 때: 지금까지 쓴 칸은 그래픽 값 serial · 컴퓨트 값 computeSerial 이 모두 끝나면 빈다
		void Mark(uint64_t serial, uint64_t computeSerial) { if (_marks.empty() || _marks.back().Head != _head) _marks.push_back({ serial, computeSerial, _head }); }
		void Retire(uint64_t completed, uint64_t computeCompleted);
		ID3D12DescriptorHeap* Heap() const { return _heap.Get(); }
		UINT Increment() const { return _inc; }
		uint64_t Wraps = 0;
	private:
		ComPtr<ID3D12DescriptorHeap> _heap;
		UINT _size = 0, _inc = 0, _head = 0, _tail = 0;   // [tail, head) = GPU 가 아직 쓸 수 있는 칸 (고리)
		bool _full = false;
		struct MarkEntry { uint64_t Serial, ComputeSerial; UINT Head; };
		std::deque<MarkEntry> _marks;                     // (제출 값, 컴퓨트 값, 그때의 head)
		D3D12_CPU_DESCRIPTOR_HANDLE _cpu0 = {};
		D3D12_GPU_DESCRIPTOR_HANDLE _gpu0 = {};
	};

	// ---- 업로드 링 조각 (UPLOAD 힙 버퍼, 늘 매핑)
	struct UploadChunk
	{
		ComPtr<ID3D12Resource> Res;
		uint8_t* Cpu = nullptr;
		uint64_t Gpu = 0;
		UINT64 Size = 0, Used = 0;
		uint64_t Retire = 0, RetireCompute = 0;
		uint64_t Generation = 0;
	};
	struct UploadLoc
	{
		ID3D12Resource* Res = nullptr;
		UINT64 Offset = 0;
		uint8_t* Cpu = nullptr;
		uint64_t Gpu = 0;
		const void* Chunk = nullptr;
		uint64_t Generation = 0;
	};

	template <class Iface>
	class Obj : public Iface
	{
	public:
		explicit Obj(Dev* dev);
		~Obj() override;
		GfxApi Api() const override { return GfxApi::DirectX12; }
		Dev* D;
	};

	// ---- 자원 (버퍼 · 텍스처 공통 상태)
	struct Resource
	{
		ComPtr<ID3D12Resource> Res;
		bool Buffer = false;
		bool Fixed = false;            // UPLOAD 힙: 상태가 GENERIC_READ 로 고정 (장벽 없음)
		std::vector<D3D12_RESOURCE_STATES> States;   // 텍스처: 서브리소스 (Sub) 마다, 버퍼: 하나
		uint64_t StateEpoch = 0;       // 버퍼: 이 명령 목록에서 상태를 정했는지 (아니면 COMMON — 감쇠)
		UINT Mips = 1, Layers = 1, Planes = 1;
		uint64_t LastUse = 0;          // CPU 가 읽고 쓰는 자원 (STAGING): 마지막으로 GPU 명령이 쓴 제출 값
		UINT64 Bytes = 0;
		UINT Sub(UINT mip, UINT layer, UINT plane = 0) const { return mip + layer * Mips + plane * Mips * Layers; }
	};

	class Buf : public Obj<GfxBuffer>
	{
	public:
		using Obj::Obj;
		~Buf() override;
		void GetType(D3D11_RESOURCE_DIMENSION* t) const override { *t = D3D11_RESOURCE_DIMENSION_BUFFER; }
		void GetDesc(D3D11_BUFFER_DESC* d) const override { *d = Desc; }
		D3D11_BUFFER_DESC Desc = {};
		Resource R;                     // DEFAULT · STAGING (CPU 쓰기 · 읽기 = CUSTOM 힙, 늘 매핑)
		uint8_t* Mapped = nullptr;      // STAGING
		std::vector<uint8_t> Shadow;    // DYNAMIC: CPU 사본 (Unmap · 첫 사용 때 링으로)
		UploadLoc Loc;                  // DYNAMIC: 지금 링 자리
		bool HasLoc = false;
		uint64_t Id = 0;
	};

	struct Image
	{
		Resource R;
		D3D12_RESOURCE_DIMENSION Dim = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		UINT Width = 1, Height = 1, Depth = 1, Samples = 1;
		bool Cube = false;
		D3D11_USAGE Usage = D3D11_USAGE_DEFAULT;
		UINT CpuAccess = 0, Bind = 0;
		DXGI_FORMAT Dxgi = DXGI_FORMAT_UNKNOWN;   // 만든 형식 (D3D11 설명의 형식, TYPELESS 일 수 있다)
		bool DepthFormat = false;
		// STAGING: CPU 쪽 버퍼 (서브리소스마다 배치 — GetCopyableFootprints)
		ComPtr<ID3D12Resource> Staging;
		uint8_t* StagingCpu = nullptr;
		std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> Footprints;
		std::vector<UINT> Rows;
		std::vector<UINT64> RowBytes;
		// DYNAMIC Map(WRITE_DISCARD) 중인 서브리소스 → 업로드 자리 (Unmap 때 복사)
		std::map<UINT, std::pair<UploadLoc, D3D12_PLACED_SUBRESOURCE_FOOTPRINT>> Pending;
		uint64_t Id = 0;
		UINT MipW(UINT m) const { return (std::max)(1u, Width >> m); }
		UINT MipH(UINT m) const { return (std::max)(1u, Height >> m); }
		UINT MipD(UINT m) const { return (std::max)(1u, Depth >> m); }
	};

	class Tex1D : public Obj<GfxTexture1D>
	{
	public:
		using Obj::Obj;
		~Tex1D() override;
		void GetType(D3D11_RESOURCE_DIMENSION* t) const override { *t = D3D11_RESOURCE_DIMENSION_TEXTURE1D; }
		void GetDesc(D3D11_TEXTURE1D_DESC* d) const override { *d = Desc; }
		D3D11_TEXTURE1D_DESC Desc = {};
		Image I;
	};
	class Tex2D : public Obj<GfxTexture2D>
	{
	public:
		using Obj::Obj;
		~Tex2D() override;
		void GetType(D3D11_RESOURCE_DIMENSION* t) const override { *t = D3D11_RESOURCE_DIMENSION_TEXTURE2D; }
		void GetDesc(D3D11_TEXTURE2D_DESC* d) const override { *d = Desc; }
		D3D11_TEXTURE2D_DESC Desc = {};
		Image I;
	};
	class Tex3D : public Obj<GfxTexture3D>
	{
	public:
		using Obj::Obj;
		~Tex3D() override;
		void GetType(D3D11_RESOURCE_DIMENSION* t) const override { *t = D3D11_RESOURCE_DIMENSION_TEXTURE3D; }
		void GetDesc(D3D11_TEXTURE3D_DESC* d) const override { *d = Desc; }
		D3D11_TEXTURE3D_DESC Desc = {};
		Image I;
	};

	Image* ImageOf(GfxResource* r);
	Buf* BufOf(GfxResource* r);

	// 뷰: CPU 전용 디스크립터 하나 + 서브리소스 범위 (장벽용)
	struct ViewInfo
	{
		ComPtr<GfxResource> Res;
		Image* Img = nullptr;
		Buf* Buffer = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE Cpu = {};   // DYNAMIC 버퍼 SRV 는 0 (그리기 때 링 자리로 만든다)
		UINT BaseMip = 0, Mips = 1, BaseLayer = 0, Layers = 1;
		bool ReadOnly = false;                  // DSV: 깊이 · 스텐실 읽기 전용
		DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
		D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc = {};   // DYNAMIC 버퍼 SRV (FirstElement 는 링 자리 기준으로 다시)
		UINT64 ByteOffset = 0;                  // 버퍼 뷰: 첫 바이트
		UINT ElementBytes = 0;                  // 버퍼 뷰: 원소 크기 (DYNAMIC 버퍼 SRV 의 FirstElement 를 링 자리로)
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
		int Heap = 0;   // 0 뷰 (CBV_SRV_UAV), 1 RTV, 2 DSV
	};
	using Srv = View<GfxShaderResourceView, D3D11_SHADER_RESOURCE_VIEW_DESC>;
	using Rtv = View<GfxRenderTargetView, D3D11_RENDER_TARGET_VIEW_DESC>;
	using Dsv = View<GfxDepthStencilView, D3D11_DEPTH_STENCIL_VIEW_DESC>;
	using Uav = View<GfxUnorderedAccessView, D3D11_UNORDERED_ACCESS_VIEW_DESC>;

	class InputLayout : public Obj<GfxInputLayout>
	{
	public:
		using Obj::Obj;
		std::deque<std::string> Names;   // 의미 이름 (Elements 가 가리킨다 — deque 는 늘려도 자리가 그대로)
		std::vector<D3D12_INPUT_ELEMENT_DESC> Elements;
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
		D3D12_CPU_DESCRIPTOR_HANDLE Cpu = {};
		uint64_t Id = 0;
	};

	class Query : public Obj<GfxQuery>
	{
	public:
		using Obj::Obj;
		~Query() override;
		void GetDesc(D3D11_QUERY_DESC* d) const override { *d = Dsc; }
		D3D11_QUERY_DESC Dsc = {};
		ComPtr<ID3D12QueryHeap> Heap;
		D3D12_QUERY_TYPE Type = D3D12_QUERY_TYPE_TIMESTAMP;
		ComPtr<ID3D12Resource> Readback;   // 결과 (리드백 힙, 늘 매핑)
		uint8_t* ReadbackCpu = nullptr;
		Resource Predicate;                // 예측 쿼리: 결과를 복사한 기본 힙 버퍼 (SetPredication)
		uint64_t Serial = 0;               // End 를 기록한 제출 값 (0 = 아직)
		bool Open = false, Recorded = false;
	};

	// 프로그램 (pass 하나): 루트 시그니처 + 단계별 DXIL + 칸 표
	class Program : public Obj<GfxObject>
	{
	public:
		using Obj::Obj;
		~Program() override;
		struct Stage
		{
			GfxD3D12Shared::StageType Type = GfxD3D12Shared::StageType::Vertex;
			std::vector<uint8_t> Dxil;
			std::vector<GfxD3D12Shared::StageSlot> Resources;   // 표 순서: CBV 0..c-1, SRV 0..s-1, UAV 0..u-1 (레지스터 번호 = 칸, 빈 칸은 Element -1)
			std::vector<GfxD3D12Shared::StageSlot> Samplers;    // 샘플러 0..n-1
			int ResourceParam = -1, SamplerParam = -1;          // 루트 매개변수 번호
			UINT CbvCount = 0, SrvCount = 0, UavCount = 0;
		};
		std::vector<Stage> Stages;
		ComPtr<ID3D12RootSignature> Root;
		ComPtr<ID3D12PipelineState> Compute;   // compute pass (그리기 상태와 상관없어 만들 때)
		bool IsCompute = false;
		bool Tessellation = false;
		bool HasUav = false;
		uint32_t PixelOutputs = 0;
		std::string Name;
		uint64_t Id = 0;
	};

	// PSO 키 (memcmp 로 비교하므로 0 으로 채운 뒤 쓴다)
	struct PipelineKey
	{
		uint64_t Program = 0, Layout = 0;
		uint64_t Raster = 0, Blend = 0, Depth = 0;
		DXGI_FORMAT Colors[8] = {};
		DXGI_FORMAT DepthFormat = DXGI_FORMAT_UNKNOWN;
		uint32_t SampleMask = 0xFFFFFFFF;
		uint8_t ColorCount = 0, Samples = 1, TopologyType = 0, Cut = 0;   // Cut: 0 없음, 1 0xFFFF, 2 0xFFFFFFFF
		uint8_t ReadOnlyDepth = 0, Pad[3] = {};
		bool operator==(const PipelineKey& o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
	};
	struct PipelineKeyHash { size_t operator()(const PipelineKey& k) const; };

	// ---- 장치
	class Dev : public GfxDevice
	{
	public:
		~Dev() override;
		GfxApi Api() const override { return GfxApi::DirectX12; }

		ComPtr<IDXGIFactory4> Factory;
		ComPtr<IDXGIAdapter1> Adapter;
		ComPtr<ID3D12Device> Device;
		ComPtr<ID3D12InfoQueue> Info;
		ComPtr<ID3D12CommandQueue> Queue;
		ComPtr<ID3D12Fence> Fence;
		HANDLE FenceEvent = nullptr;
		std::string Name;
		HWND Window = nullptr;
		UINT64 TimestampFrequency = 1;
		bool Debug = false;
		bool Lost = false;
		uint64_t DebugMessages = 0;

		// ---- 제출
		uint64_t Submitted = 0;    // 마지막으로 제출한 값
		uint64_t Completed = 0;
		uint64_t Recording() const { return Submitted + 1; }
		struct CmdSlot { ComPtr<ID3D12CommandAllocator> Alloc; uint64_t Retire = 0; };
		std::vector<CmdSlot> FreeAllocs, InFlightAllocs;
		CmdSlot CurrentAlloc;
		ComPtr<ID3D12GraphicsCommandList> List;
		bool ListOpen = false;
		uint64_t EpochCounter = 1;
		uint64_t Epoch = 1;        // 그래픽 명령 목록마다 새 값 (컨텍스트가 묶은 상태를 다시 묶고, 버퍼 상태가 COMMON 으로)
		uint32_t DrawsSinceSubmit = 0;

		// ---- 비동기 컴퓨트 (두 번째 큐): BeginAsyncCompute ~ End 사이의 기록은 컴퓨트 목록으로
		ComPtr<ID3D12CommandQueue> ComputeQueue;
		ComPtr<ID3D12Fence> ComputeFence;
		HANDLE ComputeEvent = nullptr;
		ComPtr<ID3D12GraphicsCommandList> ComputeList;
		CmdSlot ComputeAlloc;
		std::vector<CmdSlot> FreeComputeAllocs, InFlightComputeAllocs;
		uint64_t ComputeSubmitted = 0, ComputeCompleted = 0;
		uint64_t ComputePending = 0;   // 그래픽 큐가 아직 기다리지 않은 컴퓨트 값
		uint64_t ComputeEpoch = 0;
		bool AsyncOpen = false;
		bool AsyncEnabled = true;      // NOVA_D3D12_ASYNC=0 이면 끔 (같은 큐에서)
		uint64_t AsyncSubmits = 0;     // 지금까지 컴퓨트 큐로 보낸 횟수 (CLI · 검사)
		uint64_t CurEpoch() const { return AsyncOpen ? ComputeEpoch : Epoch; }
		// 지금까지 기록한 것을 덮는 컴퓨트 값 (늦은 삭제 · 링 재사용이 컴퓨트도 기다리게)
		uint64_t ComputeCover() const { return ComputeSubmitted + (AsyncOpen ? 1 : 0); }
		void OpenCompute();
		void SubmitCompute();          // 그래픽 목록 제출 → 컴퓨트 큐가 그것을 기다린 뒤 컴퓨트 목록 실행
		void WaitCompute(uint64_t serial);

		// ---- 디스크립터
		CpuDescriptors ViewHeap, RtvHeap, DsvHeap, SamplerHeap;
		GpuRing ViewRing;
		ComPtr<ID3D12DescriptorHeap> SamplerGpu;   // 샘플러 표 캐시 (가득 차면 기다린 뒤 비운다)
		UINT SamplerGpuSize = 0, SamplerGpuUsed = 0, SamplerInc = 0;
		std::unordered_map<uint64_t, std::pair<std::vector<uint64_t>, D3D12_GPU_DESCRIPTOR_HANDLE>> SamplerTables;
		D3D12_CPU_DESCRIPTOR_HANDLE NullSrv[12] = {}, NullUav[12] = {}, NullRtv = {}, DefaultSampler = {}, DefaultCompare = {};
		ComPtr<ID3D12Resource> ZeroCb;   // 값이 없는 cbuffer 칸 (64 KB 0, UPLOAD 힙)

		// ---- 업로드 링
		std::vector<std::unique_ptr<UploadChunk>> Chunks;
		UploadChunk* Current = nullptr;
		std::deque<UploadChunk*> Retired;

		// ---- 늦은 삭제 (그래픽 값 · 컴퓨트 값이 모두 끝난 뒤)
		struct DeferredFn { uint64_t Serial, ComputeSerial; std::function<void()> Fn; };
		std::deque<DeferredFn> Deferred;

		// ---- 파이프라인 · 간접 그리기 서명 · 밉 만들기
		std::unordered_map<PipelineKey, ComPtr<ID3D12PipelineState>, PipelineKeyHash> Pipelines;
		ComPtr<ID3D12CommandSignature> DrawSig, DrawIndexedSig;
		ComPtr<ID3D12RootSignature> MipRoot;
		std::map<std::pair<DXGI_FORMAT, int>, ComPtr<ID3D12PipelineState>> MipPipelines;   // (형식, 1 2D 배열 · 2 3D)
		std::vector<uint8_t> MipVs, MipPs, MipPs3;

		// ---- 창 (스왑체인)
		struct Swap
		{
			HWND Wnd = nullptr;
			ComPtr<IDXGISwapChain3> Chain;
			std::vector<ComPtr<ID3D12Resource>> Buffers;
			UINT Width = 0, Height = 0;
			bool Failed = false;
		};
		std::map<HWND, std::unique_ptr<Swap>> Swaps;
		std::deque<uint64_t> Frames;   // 본 창에 표시한 프레임의 제출 값 (2 프레임 넘게 앞서 가지 않게)
		bool AllowTearing = false;
		Swap* SwapFor(HWND wnd, UINT width, UINT height);
		void PresentTo(Swap& s, Tex2D* source, int width, int height, int interval, bool pace);
		void ReleaseWindow(HWND wnd);
		void DestroyAllSwapchains();

		Ctx* Immediate = nullptr;
		std::set<std::string> Reported;
		uint64_t Objects = 0;

		bool Init(HWND window, std::string& error);
		void Once(const std::string& key, const char* fmt, const char* arg = "");
		void DrainMessages();   // 디버그 층 메시지 → Editor.log

		// 기록 · 제출 · 대기
		ID3D12GraphicsCommandList* Cmd() { return AsyncOpen ? ComputeList.Get() : GraphicsCmd(); }   // 지금 기록하는 목록
		ID3D12GraphicsCommandList* GraphicsCmd();   // 그래픽 목록 (열려 있지 않으면 연다 — 힙도 묶는다)
		void Submit(bool wait);
		void Poll();
		void WaitSerial(uint64_t serial);
		bool IsDone(uint64_t serial) { if (serial <= Completed) return true; Poll(); return serial <= Completed; }
		void Defer(std::function<void()> fn) { Deferred.push_back({ Recording(), ComputeCover(), std::move(fn) }); }
		template <class T> void DeferRelease(ComPtr<T> r) { if (r) Defer([r]() mutable { r.Reset(); }); }
		void CheckRemoved(HRESULT hr, const char* what);

		// 업로드 링 (align 은 2 의 거듭제곱이 아니어도 된다 — 구조 버퍼 간격)
		bool Upload(UINT64 size, UINT64 align, UploadLoc& loc);
		bool LocCurrent(const UploadLoc& loc) const { return Current && loc.Chunk == Current && loc.Generation == Current->Generation; }
		// 셰이더에서 보이는 칸 (모자라면 제출 · 기다림 — 그때 명령 목록이 바뀐다)
		bool AllocViews(UINT count, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu);
		bool SamplerTable(const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>& samplers, const std::vector<uint64_t>& key, D3D12_GPU_DESCRIPTOR_HANDLE& out);

		// 자원
		HRESULT MakeTexture(Image& img, const D3D11_SUBRESOURCE_DATA* data, const char* what);
		void UploadImage(Image& img, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch);
		bool GetPipeline(const PipelineKey& key, Program* program, InputLayout* layout, const D3D11_RASTERIZER_DESC& rs, const D3D11_BLEND_DESC& bs,
			const D3D11_DEPTH_STENCIL_DESC& ds, ID3D12PipelineState** out);
		void ForgetProgram(uint64_t id);
		ID3D12PipelineState* MipPipeline(DXGI_FORMAT format, bool volume);

		// ---- GfxDevice
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
		HRESULT GetDeviceRemovedReason() override { return Device ? Device->GetDeviceRemovedReason() : DXGI_ERROR_DEVICE_REMOVED; }

	private:
		void CreateNullDescriptors();
		bool CreateMipShaders(std::string& error);
	};

	template <class Iface>
	Obj<Iface>::Obj(Dev* dev) : D(dev) { D->AddRef(); ++D->Objects; }
	template <class Iface>
	Obj<Iface>::~Obj() { --D->Objects; D->GfxDevice::Release(); }

	// ---- 컨텍스트
	class Ctx : public GfxContext
	{
	public:
		~Ctx() override;
		GfxApi Api() const override { return GfxApi::DirectX12; }
		ComPtr<Dev> D;

		// D3D 상태 (Get… 이 돌려줄 것)
		ComPtr<GfxInputLayout> Layout;
		D3D11_PRIMITIVE_TOPOLOGY Topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		struct VB { ComPtr<GfxBuffer> Buf; UINT Stride = 0, Offset = 0; };
		VB Vbs[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
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
		std::vector<GfxD3D12Shared::BindingValue> Values;
		std::vector<ComPtr<GfxObject>> Held;

		// 기록 상태 (Epoch 가 바뀌면 다시 묶는다)
		uint64_t BoundEpoch = ~0ull;
		ID3D12RootSignature* BoundRoot = nullptr;
		ID3D12RootSignature* BoundComputeRoot = nullptr;   // 그래픽 · 컴퓨트 루트는 따로 묶인다 (PSO 는 하나)
		ID3D12PipelineState* BoundPipeline = nullptr;
		uint64_t TargetsSerial = 1, BoundTargets = 0;
		bool DynamicDirty = true;
		D3D12_PRIMITIVE_TOPOLOGY BoundTopo = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		D3D12_VERTEX_BUFFER_VIEW BoundVbs[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
		D3D12_INDEX_BUFFER_VIEW BoundIb = {};
		PipelineKey LastKey = {};
		ID3D12PipelineState* LastPipe = nullptr;
		std::vector<D3D12_RESOURCE_BARRIER> Barriers;
		bool UavDirty = false;             // 앞 디스패치 · 그리기가 UAV 에 썼다 → 다음 그리기 · 디스패치 앞에 UAV 장벽
		bool SavedUavDirty = false;        // 비동기 컴퓨트 동안 그래픽 쪽 값
		std::vector<D3D12_RESOURCE_BARRIER> PreBarriers;   // 비동기 컴퓨트: 그래픽 전용 상태에서 나오는 장벽 (그래픽 목록 — 컴퓨트 큐가 기다리는 제출에)
		ComPtr<GfxQuery> Predicate;
		bool Predicated = false;           // 지금 명령 목록에 SetPredication 이 켜져 있다

		// ---- 내부
		void Transition(Resource& r, UINT baseMip, UINT mips, UINT baseLayer, UINT layers, D3D12_RESOURCE_STATES state);
		void TransitionAll(Resource& r, D3D12_RESOURCE_STATES state) { Transition(r, 0, r.Mips, 0, r.Layers, state); }
		void FlushBarriers();
		void UavBarrier();
		bool PrepareDraw(bool indexed);
		bool BuildTables(std::vector<std::pair<int, D3D12_GPU_DESCRIPTOR_HANDLE>>& tables, bool& usesUav);
		void Invalidate() { BoundEpoch = ~0ull; }
		void ResolveBuffer(GfxResource* gb, D3D12_GPU_VIRTUAL_ADDRESS& gpu, Resource*& res);
		bool IsBoundTarget(const Image* img, UINT baseMip, UINT mips, UINT baseLayer, UINT layers) const;
		void ApplyPredicate(ID3D12GraphicsCommandList* cl, bool on);
		void AfterSubmit();
		void CopyImageRegion(Image& dst, UINT dstSub, UINT x, UINT y, UINT z, Image& src, UINT srcSub, const D3D11_BOX* box);

		// ---- GfxContext
		void IASetInputLayout(GfxInputLayout* l) override { Layout = l; }
		void IAGetInputLayout(GfxInputLayout** l) override { *l = Layout.Get(); if (*l) (*l)->AddRef(); }
		void IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY t) override { Topo = t; }
		void IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY* t) override { *t = Topo; }
		void IASetVertexBuffers(UINT start, UINT count, GfxBuffer* const* buffers, const UINT* strides, const UINT* offsets) override;
		void IAGetVertexBuffers(UINT start, UINT count, GfxBuffer** buffers, UINT* strides, UINT* offsets) override;
		void IASetIndexBuffer(GfxBuffer* b, DXGI_FORMAT f, UINT o) override { Ib = b; IbFormat = f; IbOffset = o; }
		void IAGetIndexBuffer(GfxBuffer** b, DXGI_FORMAT* f, UINT* o) override;
		void SOSetTargets(UINT count, GfxBuffer* const* buffers, const UINT* offsets) override;
		void OMSetRenderTargets(UINT count, GfxRenderTargetView* const* rtvs, GfxDepthStencilView* dsv) override;
		void OMGetRenderTargets(UINT count, GfxRenderTargetView** rtvs, GfxDepthStencilView** dsv) override;
		void OMSetDepthStencilState(GfxDepthStencilState* s, UINT ref) override;
		void OMGetDepthStencilState(GfxDepthStencilState** s, UINT* ref) override;
		void OMSetBlendState(GfxBlendState* s, const FLOAT f[4], UINT mask) override;
		void OMGetBlendState(GfxBlendState** s, FLOAT f[4], UINT* mask) override;
		void RSSetState(GfxRasterizerState* s) override { Rs = s; DynamicDirty = true; }
		void RSGetState(GfxRasterizerState** s) override { *s = Rs.Get(); if (*s) (*s)->AddRef(); }
		void RSSetViewports(UINT count, const D3D11_VIEWPORT* v) override;
		void RSGetViewports(UINT* count, D3D11_VIEWPORT* v) override;
		void RSSetScissorRects(UINT count, const D3D11_RECT* r) override;
		void RSGetScissorRects(UINT* count, D3D11_RECT* r) override;
		void PSSetShaderResources(UINT, UINT count, GfxShaderResourceView* const* views) override { OutsideViews("PS", count, views); }
		void VSSetShaderResources(UINT, UINT count, GfxShaderResourceView* const* views) override { OutsideViews("VS", count, views); }
		void CSSetShaderResources(UINT, UINT count, GfxShaderResourceView* const* views) override { OutsideViews("CS", count, views); }
		void CSSetUnorderedAccessViews(UINT, UINT, GfxUnorderedAccessView* const*, const UINT*) override {}
		void CSSetShader(void*, void*, UINT) override {}
		bool SupportsGpuDriven() const override { return true; }
		bool SupportsAsyncCompute() const override { return D->ComputeQueue && D->AsyncEnabled; }
		void BeginAsyncCompute() override;
		void EndAsyncCompute() override;
		void WaitAsyncCompute() override;
		void FinishAsync() { EndAsyncCompute(); WaitAsyncCompute(); }
		void FlushPreBarriers();
		bool DrawIndexedInstancedIndirect(GfxBuffer* args, UINT offset) override;
		bool DrawInstancedIndirect(GfxBuffer* args, UINT offset) override;
		bool SetPredication(GfxQuery* predicate, BOOL value) override;
		bool ClearUnorderedAccessViewUint(GfxUnorderedAccessView* uav, const UINT values[4]) override;
		void Draw(UINT vertexCount, UINT startVertex) override { DrawInstanced(vertexCount, 1, startVertex, 0); }
		void DrawIndexed(UINT indexCount, UINT startIndex, INT baseVertex) override { DrawIndexedInstanced(indexCount, 1, startIndex, baseVertex, 0); }
		void DrawInstanced(UINT vertexCountPerInstance, UINT instanceCount, UINT startVertex, UINT startInstance) override;
		void DrawIndexedInstanced(UINT indexCountPerInstance, UINT instanceCount, UINT startIndex, INT baseVertex, UINT startInstance) override;
		void DrawAuto() override;
		void Dispatch(UINT x, UINT y, UINT z) override;
		void ClearRenderTargetView(GfxRenderTargetView* rtv, const FLOAT color[4]) override;
		void ClearDepthStencilView(GfxDepthStencilView* dsv, UINT flags, FLOAT depth, UINT8 stencil) override;
		HRESULT Map(GfxResource* resource, UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped) override;
		void Unmap(GfxResource* resource, UINT subresource) override;
		void UpdateSubresource(GfxResource* resource, UINT subresource, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch) override;
		void CopyResource(GfxResource* dst, GfxResource* src) override;
		void CopySubresourceRegion(GfxResource* dst, UINT dstSub, UINT x, UINT y, UINT z, GfxResource* src, UINT srcSub, const D3D11_BOX* box) override;
		void GenerateMips(GfxShaderResourceView* srv) override;
		void Begin(GfxQuery* query) override;
		void End(GfxQuery* query) override;
		HRESULT GetData(GfxQuery* query, void* data, UINT size, UINT flags) override;
		void Flush() override;
		void ClearState() override;

	private:
		void OutsideViews(const char* stage, UINT count, GfxShaderResourceView* const* views);
	};

	uint64_t HashBytes(const void* data, size_t size, uint64_t seed = 1469598103934665603ull);
	uint64_t HashRasterizer(const D3D11_RASTERIZER_DESC& d);
	uint64_t HashBlend(const D3D11_BLEND_DESC& d);
	uint64_t HashDepthStencil(const D3D11_DEPTH_STENCIL_DESC& d);
	D3D11_RASTERIZER_DESC DefaultRasterizer();
	D3D11_BLEND_DESC DefaultBlend();
	D3D11_DEPTH_STENCIL_DESC DefaultDepthStencil();
	bool IsBlockCompressed(DXGI_FORMAT f);
	UINT PlaneCount(DXGI_FORMAT f);         // 깊이 + 스텐실 = 2
	bool IsSrgb(DXGI_FORMAT f);
	bool IsIntegerFormat(DXGI_FORMAT f);
	bool HasStencil(DXGI_FORMAT f);
	UINT64 AlignUp(UINT64 v, UINT64 a);
	// 깊이 텍스처 읽기 (SRV) = 깊이 읽기 + 셰이더 자원 (읽기 전용 DSV 와 같이 묶여도 장벽이 없게)
	constexpr D3D12_RESOURCE_STATES kDepthRead = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	constexpr D3D12_RESOURCE_STATES kShaderRead = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}
