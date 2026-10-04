#pragma once
#include "VkLoader.h"
#include "VkMap.h"
#include "GfxVk.h"
#include "GfxVkShared.h"
#include <functional>

// Gfx Vulkan 구현 내부 (GfxVkDevice.cpp · GfxVkContext.cpp 만 쓴다)
//  - 제출: 타임라인 세마포어 하나. 제출마다 값 +1 (Serial). 자원 · 링 · 디스크립터 풀 · 명령 풀은
//    "마지막으로 쓴 제출 값" 이 끝난 뒤에 다시 쓰거나 없앤다 (프레임 수와 상관없음)
//  - 기록: 본 명령 버퍼 하나 + 업로드 명령 버퍼 (새 자원의 처음 데이터 — 본 버퍼보다 먼저 제출)
//  - 이미지 배치(layout)는 서브리소스마다 CPU 가 기록 순서대로 따라간다. 렌더링(동적 렌더링)은 그리기 때 늦게 시작하고
//    타깃이 바뀌거나 복사 · 지우기 · 배치 바꾸기가 필요하면 끝낸다
//  - 동기화: 이미지는 서브리소스마다 "쓴 뒤 아직 장벽 없음" (Written) 을 따라가 배치가 바뀌거나 같은 배치로 다시 쓰고 읽을 때만 장벽.
//    버퍼 복사 쓰기는 앞뒤로 전역 메모리 장벽 (드물다). 제출 끝에 호스트 읽기 장벽 하나
namespace GfxVkImpl
{
	class Dev;
	class Ctx;
	using GfxVkShared::RingLoc;

	uint64_t NextId();   // 캐시 키용 고유 번호 (핸들 값은 없앤 뒤 다시 쓰일 수 있어 쓰지 않는다)

	// ---- 메모리: 형식마다 큰 블록 (장치 64 MB · 호스트 16 MB) 안을 나눠 쓴다. 큰 자원은 따로
	struct Allocation
	{
		VkDeviceMemory Memory = VK_NULL_HANDLE;
		VkDeviceSize Offset = 0, Size = 0;
		uint8_t* Mapped = nullptr;   // 호스트에서 보이면 Offset 위치 포인터
		int Block = -1;              // -1 = 따로 할당
		uint32_t Type = 0;
	};

	class Allocator
	{
	public:
		void Init(VkDevice device, VkPhysicalDevice phys);
		void Shutdown();
		// linear = 버퍼 (이미지와 같은 블록에 두지 않는다 — bufferImageGranularity 를 따지지 않으려고)
		bool Allocate(const VkMemoryRequirements& req, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, bool linear, Allocation& out);
		void Free(Allocation& a);
		UINT64 LiveBytes() const { return _live; }
		UINT64 BlockBytes() const { return _blockBytes; }
		const VkPhysicalDeviceMemoryProperties& Props() const { return _props; }

	private:
		struct Block
		{
			VkDeviceMemory Memory = VK_NULL_HANDLE;
			VkDeviceSize Size = 0;
			uint8_t* Mapped = nullptr;
			uint32_t Type = 0;
			bool Linear = false;
			std::map<VkDeviceSize, VkDeviceSize> Free;   // 빈 칸: 시작 → 크기
		};
		int FindType(uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const;
		VkDevice _device = VK_NULL_HANDLE;
		VkPhysicalDeviceMemoryProperties _props = {};
		std::vector<Block> _blocks;
		UINT64 _live = 0, _blockBytes = 0;
	};

	// ---- 링: 호스트에서 보이는 큰 버퍼 조각들. 꽉 차면 지금 기록 값으로 은퇴, 그 값이 끝나면 다시 쓴다
	struct RingChunk
	{
		VkBuffer Buffer = VK_NULL_HANDLE;
		Allocation Mem;
		VkDeviceSize Size = 0, Used = 0;
		uint64_t Retire = 0;       // 이 값의 제출이 끝나야 다시 쓴다
		uint64_t Generation = 0;   // 다시 쓸 때마다 +1 (RingLoc 가 아직 유효한지)
		uint64_t Id = 0;
	};

	// ---- Vulkan 객체 공통: 장치를 잡는다, Api() = Vulkan
	template <class Iface>
	class Obj : public Iface
	{
	public:
		explicit Obj(Dev* dev);
		~Obj() override;
		GfxApi Api() const override { return GfxApi::Vulkan; }
		Dev* D;
	};

	// 이미지의 배치 · "쓴 뒤 장벽 없음" 이 바뀔 때마다 +1 (Transition · MarkWritten). 그리기가 앞 그리기의 디스크립터 집합을
	//  그대로 쓸 수 있는지 (읽는 이미지의 배치를 다시 볼 필요가 없는지) 판단한다 — 렌더 스레드 하나
	inline uint64_t ImageStateSerial = 0;

	struct Image
	{
		VkImage Handle = VK_NULL_HANDLE;
		Allocation Mem;
		VkMap::Format Fmt;
		VkFormat Format = VK_FORMAT_UNDEFINED;   // 실제 이미지 형식 (D24S8 을 못 쓰면 D32S8 로 바뀐다)
		DXGI_FORMAT Dxgi = DXGI_FORMAT_UNKNOWN;
		VkImageType Type = VK_IMAGE_TYPE_2D;
		UINT Width = 1, Height = 1, Depth = 1, Layers = 1, Mips = 1, Samples = 1;
		bool Cube = false;
		D3D11_USAGE Usage = D3D11_USAGE_DEFAULT;
		UINT CpuAccess = 0, Bind = 0;
		std::vector<VkImageLayout> Layouts;      // 서브리소스 (layer * Mips + mip) 마다
		std::vector<uint8_t> Written;            // 서브리소스마다: 마지막 장벽 뒤에 GPU 가 썼다 (같은 배치로 다시 쓸 때 장벽)
		// STAGING 텍스처 = 버퍼 (서브리소스마다 빽빽한 행)
		VkBuffer Staging = VK_NULL_HANDLE;
		std::vector<VkDeviceSize> SubOffset;
		uint64_t LastUse = 0;
		// DYNAMIC 텍스처의 Map(WRITE_DISCARD) 중인 서브리소스 → 링 위치
		std::map<UINT, std::pair<RingLoc, uint8_t*>> Pending;
		uint64_t Id = 0;
		UINT64 Bytes = 0;

		UINT MipW(UINT m) const { return (std::max)(1u, Width >> m); }
		UINT MipH(UINT m) const { return (std::max)(1u, Height >> m); }
		UINT MipD(UINT m) const { return (std::max)(1u, Depth >> m); }
		VkImageLayout& LayoutOf(UINT mip, UINT layer) { return Layouts[layer * Mips + mip]; }
		void MarkWritten(UINT baseMip, UINT mips, UINT baseLayer, UINT layers)
		{
			++ImageStateSerial;
			for (UINT l = baseLayer; l < (std::min)(baseLayer + layers, Layers); ++l)
				for (UINT m = baseMip; m < (std::min)(baseMip + mips, Mips); ++m)
					Written[l * Mips + m] = 1;
		}
	};

	class Buf : public Obj<GfxBuffer>
	{
	public:
		using Obj::Obj;
		~Buf() override;
		void GetDesc(D3D11_BUFFER_DESC* d) const override { *d = Desc; }
		D3D11_BUFFER_DESC Desc = {};
		VkBuffer Buffer = VK_NULL_HANDLE;   // DYNAMIC 은 없다 (링에서 그린다)
		Allocation Mem;
		uint64_t Id = 0;
		// DYNAMIC: Map(WRITE_DISCARD) = 이 CPU 사본, Unmap 때 링으로 (GL 과 같은 방식). 다음 기록에서 링 위치가 사라졌으면 사본에서 다시
		std::vector<uint8_t> Shadow;
		RingLoc Loc;
		bool HasLoc = false;
		// STAGING: Map(READ) 은 이 값의 제출이 끝날 때까지 기다린다
		uint64_t LastUse = 0;
	};

	class Tex1D : public Obj<GfxTexture1D>
	{
	public:
		using Obj::Obj;
		~Tex1D() override;
		void GetDesc(D3D11_TEXTURE1D_DESC* d) const override { *d = Desc; }
		D3D11_TEXTURE1D_DESC Desc = {};
		Image I;
	};
	class Tex2D : public Obj<GfxTexture2D>
	{
	public:
		using Obj::Obj;
		~Tex2D() override;
		void GetDesc(D3D11_TEXTURE2D_DESC* d) const override { *d = Desc; }
		D3D11_TEXTURE2D_DESC Desc = {};
		Image I;
	};
	class Tex3D : public Obj<GfxTexture3D>
	{
	public:
		using Obj::Obj;
		~Tex3D() override;
		void GetDesc(D3D11_TEXTURE3D_DESC* d) const override { *d = Desc; }
		D3D11_TEXTURE3D_DESC Desc = {};
		Image I;
	};

	Image* ImageOf(GfxResource* r);
	Buf* BufOf(GfxResource* r);

	struct ViewInfo
	{
		ComPtr<GfxResource> Res;
		Image* Img = nullptr;
		VkImageView View = VK_NULL_HANDLE;
		VkImageViewType Type = VK_IMAGE_VIEW_TYPE_2D;
		VkFormat Format = VK_FORMAT_UNDEFINED;
		VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		UINT BaseMip = 0, Mips = 1, BaseLayer = 0, Layers = 1;
		bool ReadOnlyDepth = false;   // DSV 의 READ_ONLY 깃발
		// 버퍼 뷰 (구조 · raw 버퍼 SRV · UAV → 스토리지 버퍼): Res 안의 바이트 범위
		bool Buffer = false;
		VkDeviceSize BufOffset = 0, BufSize = 0;
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

	class InputLayout : public Obj<GfxInputLayout>
	{
	public:
		using Obj::Obj;
		struct Element
		{
			std::string Semantic;   // 대문자 + 번호 (TEXCOORD0)
			std::string Base;       // 대문자 (TEXCOORD)
			UINT Index = 0;
			UINT Slot = 0;
			VkFormat Format = VK_FORMAT_UNDEFINED;
			UINT Offset = 0;
		};
		std::vector<Element> Elements;
		UINT SlotMask = 0;
		bool PerInstance[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
		uint64_t Id = 0;
	};

	template <class Iface, class Desc>
	class StateObj : public Obj<Iface>
	{
	public:
		using Obj<Iface>::Obj;
		void GetDesc(Desc* d) const override { *d = Dsc; }
		Desc Dsc = {};
		uint64_t Hash = 0;   // 설명의 해시 (파이프라인 키 — 같은 값의 다른 객체도 같은 파이프라인)
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
		VkSampler Handle = VK_NULL_HANDLE;
		uint64_t Id = 0;
	};

	class Query : public Obj<GfxQuery>
	{
	public:
		using Obj::Obj;
		~Query() override;
		void GetDesc(D3D11_QUERY_DESC* d) const override { *d = Dsc; }
		D3D11_QUERY_DESC Dsc = {};
		VkQueryPool Pool = VK_NULL_HANDLE;
		uint64_t Serial = 0;   // End 를 기록한 제출 값 (0 = 아직)
		// 오클루전 (예측) 쿼리: 칸 고리 — 매 프레임 다시 쓰는 쿼리가 GPU 에 남은 앞 결과를 기다리지 않게 (호스트 리셋은 그 칸이 끝난 뒤)
		static constexpr uint32_t kSlots = 4;
		uint32_t Slot = 0;                    // 지금 칸
		uint64_t SlotSerial[kSlots] = {};     // 칸마다 마지막으로 쓴 제출 값
		bool Recorded = false;                // 지금 칸에 vkCmdBeginQuery 를 기록했다 (그리기가 없었으면 false — 결과 = 보임)
		bool Broken = false;                  // 렌더링이 쿼리 중간에 끝나 결과가 모자랄 수 있다 (결과 = 보임)
		RingLoc PredLoc;                      // 조건부 렌더링 값 (쿼리 결과를 복사한 4 바이트)
		bool HasPredLoc = false;
		// 파이프라인 통계 (삼각형 · 픽셀 셰이더 수): Vulkan 은 같은 종류 쿼리를 겹쳐 열 수 없다 → 안쪽 구간이 열리면 끊고 닫히면 새 조각.
		//  결과 = 자기 조각 (Pool 의 칸 0 .. StatSegs-1) + 안쪽 쿼리들. 조각은 렌더링 밖에서만 시작 · 끝 (렌더 패스를 넘을 수 있게)
		static constexpr uint32_t kStatSegs = 32;
		uint32_t StatSegs = 0;
		bool StatOpen = false, StatOverflow = false;
		std::vector<ComPtr<GfxQuery>> Children;
	};

	// 효과 하나의 바인딩 배치
	class BindingLayout : public Obj<GfxObject>
	{
	public:
		using Obj::Obj;
		~BindingLayout() override;
		std::vector<GfxVkShared::BindingDesc> Bindings;
		bool HasStorage = false;               // 스토리지 버퍼 (DYNAMIC 이면 링 자리가 값 밖에서 바뀐다 → 집합을 다시 쓰지 않는다)
		std::vector<uint32_t> Offset;          // 바인딩 → 첫 원소 번호
		uint32_t Elements = 0;
		bool DynamicUbo = true;                // 상수 = UNIFORM_BUFFER_DYNAMIC (집합을 다시 쓰고 오프셋만 바꾼다)
		VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
		VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
		uint64_t Id = 0;
	};

	class Program : public Obj<GfxObject>
	{
	public:
		using Obj::Obj;
		~Program() override;
		ComPtr<BindingLayout> Layout;
		struct Stage { VkShaderStageFlagBits Flag; VkShaderModule Module; std::string Entry; };
		std::vector<Stage> Stages;
		std::vector<std::pair<std::string, int>> VertexInputs;
		uint32_t PixelOutputs = 0;
		std::vector<bool> Used;   // 바인딩마다: 이 pass 가 쓰나
		bool Tessellation = false;
		VkPipeline Compute = VK_NULL_HANDLE;   // compute pass (단계 하나 — 파이프라인이 상태와 상관없어 만들 때 바로)
		std::string Name;
		uint64_t Id = 0;
	};

	// 파이프라인 키 (memcmp 가능하게 빈칸 없이 0 으로 채운다)
	struct PipelineKey
	{
		uint64_t Program = 0, Layout = 0;
		uint64_t Raster = 0, Blend = 0, Depth = 0;
		VkFormat Colors[8] = {};
		VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
		uint32_t SampleMask = 0xFFFFFFFF;
		uint8_t ColorCount = 0, Samples = 1, Topology = 0, PatchPoints = 0;
		uint8_t ReadOnlyDepth = 0, Viewports = 1, Strip = 0, Pad = 0;
		uint32_t VertexStrideMask = 0;   // (자리) 정렬
		bool operator==(const PipelineKey& o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
	};
	struct PipelineKeyHash { size_t operator()(const PipelineKey& k) const; };

	// ---- 장치
	class Dev : public GfxDevice
	{
	public:
		~Dev() override;
		GfxApi Api() const override { return GfxApi::Vulkan; }

		// ---- Vulkan
		VkPhysicalDevice Phys = VK_NULL_HANDLE;
		VkDevice Device = VK_NULL_HANDLE;
		VkQueue Queue = VK_NULL_HANDLE;
		uint32_t QueueFamily = 0;
		VkPhysicalDeviceProperties Props = {};
		VkPhysicalDeviceFeatures Features = {};   // 켠 것
		bool MirrorClamp = false;
		bool ConditionalRendering = false;         // VK_EXT_conditional_rendering (SetPredication)
		bool D24S8 = true;                         // VK_FORMAT_D24_UNORM_S8_UINT 를 깊이 타깃으로 쓸 수 있나 (아니면 D32S8)
		std::string Name;
		Allocator Mem;
		VkPipelineCache PipelineCache = VK_NULL_HANDLE;
		bool Loader = false;                       // VkLoader::Acquire 했나
		HWND Window = nullptr;

		// ---- 제출
		VkSemaphore Timeline = VK_NULL_HANDLE;
		uint64_t Submitted = 0;    // 마지막으로 제출한 값
		uint64_t Completed = 0;    // 끝난 값 (Poll 이 갱신)
		uint64_t Recording() const { return Submitted + 1; }   // 지금 기록 중인 명령이 받을 값
		struct CmdSlot { VkCommandPool Pool = VK_NULL_HANDLE; VkCommandBuffer Cb = VK_NULL_HANDLE; uint64_t Retire = 0; };
		std::vector<CmdSlot> FreeCmds, InFlightCmds;
		CmdSlot Main, Upload;
		bool UploadOpen = false;
		uint64_t Epoch = 0;        // 새 명령 버퍼마다 +1 (컨텍스트가 묶은 상태를 다시 묶는다)
		uint32_t DrawsSinceSubmit = 0;

		// ---- 링
		std::vector<std::unique_ptr<RingChunk>> Chunks;
		RingChunk* Current = nullptr;
		std::deque<RingChunk*> Retired;

		// ---- 디스크립터 풀 (꽉 차면 은퇴, 끝나면 리셋해 다시)
		struct Pool { VkDescriptorPool Handle = VK_NULL_HANDLE; uint64_t Retire = 0; };
		std::vector<Pool> FreePools, RetiredPools;
		VkDescriptorPool CurrentPool = VK_NULL_HANDLE;
		uint64_t PoolGeneration = 0;   // 풀이 바뀌면 +1 (집합 캐시를 비운다)

		// ---- 늦은 삭제: GPU 가 이 값까지 끝내면 지운다
		std::deque<std::pair<uint64_t, std::function<void()>>> Deferred;

		// ---- 파이프라인
		std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> Pipelines;

		// ---- 빈 칸 더미
		struct Dummy { VkImage Image = VK_NULL_HANDLE; Allocation Mem; VkImageView View = VK_NULL_HANDLE; VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED; uint64_t Id = 0; };
		std::map<int, Dummy> Dummies;   // 키 = Dim | Arrayed<<4 | Depth<<5 | Integer<<6
		VkSampler DummySampler = VK_NULL_HANDLE, DummyCompare = VK_NULL_HANDLE;   // 빈 샘플러 칸 (보통 · 비교)
		uint64_t DummySamplerId = 0, DummyCompareId = 0;
		VkBuffer DummyBuffer = VK_NULL_HANDLE;   // 빈 스토리지 버퍼 칸 (256 바이트, 0)
		Allocation DummyBufferMem;
		const Dummy& DummyStorageImage();       // 빈 스토리지 이미지 칸 (R32F 1x1, GENERAL)
		VkBuffer DummyStorageBuffer();

		// ---- 창 (스왑체인 — GfxVkSwapchain.cpp). 본 창 + ImGui 가 만든 OS 창(뷰포트)마다 하나
		struct Swap
		{
			HWND Wnd = nullptr;
			VkSurfaceKHR Surface = VK_NULL_HANDLE;
			VkSwapchainKHR Chain = VK_NULL_HANDLE;
			VkFormat Format = VK_FORMAT_UNDEFINED;
			VkExtent2D Extent = {};
			VkPresentModeKHR Mode = VK_PRESENT_MODE_FIFO_KHR;
			int Interval = -1;
			std::vector<VkImage> Images;
			std::vector<VkSemaphore> Acquire;   // 고리 (이미지 수 + 1)
			std::vector<VkSemaphore> Done;      // 이미지마다 (표시가 기다린다)
			uint32_t AcquireIndex = 0;
			bool Failed = false;
		};
		std::map<HWND, std::unique_ptr<Swap>> Swaps;
		std::deque<uint64_t> Frames;            // 본 창에 표시한 프레임의 제출 값 (2 프레임 넘게 앞서 가지 않게)
		Swap* SwapFor(HWND wnd, std::string& error);   // 없으면 표면을 만든다
		bool CreateSurface(Swap& s, std::string& error);
		bool RecreateSwapchain(Swap& s, uint32_t width, uint32_t height, int interval);
		void DestroySwapchain(Swap& s, bool surfaceToo);
		void DestroyAllSwapchains();
		void ReleaseWindow(HWND wnd);
		void PresentFrame(Swap& s, class Tex2D* backBuffer, int width, int height, int interval, bool pace);

		Ctx* Immediate = nullptr;   // 약한 참조 (컨텍스트가 장치를 잡는다)
		std::set<std::string> Reported;
		uint64_t Resources = 0;

		bool Init(HWND window, std::string& error);
		void Once(const std::string& key, const char* fmt, const char* arg = "");

		// 제출 · 대기
		VkCommandBuffer Cmd() { return Main.Cb; }
		VkCommandBuffer UploadCmd();
		void Submit(bool wait, VkSemaphore waitSemaphore = VK_NULL_HANDLE, VkSemaphore signalSemaphore = VK_NULL_HANDLE);
		void Poll();
		void WaitSerial(uint64_t serial);
		bool IsDone(uint64_t serial) { if (serial <= Completed) return true; Poll(); return serial <= Completed; }
		void Defer(std::function<void()> fn) { Deferred.push_back({ Recording(), std::move(fn) }); }

		// 링
		uint8_t* RingAlloc(VkDeviceSize size, VkDeviceSize align, RingLoc& loc);
		bool RingCurrent(const RingLoc& loc) const;

		// 디스크립터
		VkDescriptorSet AllocateSet(VkDescriptorSetLayout layout);

		// 이미지 · 버퍼 만들기
		HRESULT MakeImage(Image& img, const D3D11_SUBRESOURCE_DATA* data, const char* what);
		void UploadImage(VkCommandBuffer cb, Image& img, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch);
		VkImageView MakeView(Image& img, VkImageViewType type, VkFormat format, VkImageAspectFlags aspect, UINT baseMip, UINT mips, UINT baseLayer, UINT layers,
			const VkComponentMapping* swizzle = nullptr);
		const Dummy& DummyImage(const GfxVkShared::BindingDesc& b);
		VkFormat FixFormat(VkFormat f) const { return (!D24S8 && f == VK_FORMAT_D24_UNORM_S8_UINT) ? VK_FORMAT_D32_SFLOAT_S8_UINT : f; }

		VkPipeline GetPipeline(const PipelineKey& key, Program* program, InputLayout* layout, const D3D11_RASTERIZER_DESC& rs, const D3D11_BLEND_DESC& bs,
			const D3D11_DEPTH_STENCIL_DESC& ds);
		void ForgetProgram(uint64_t id);

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
		HRESULT GetDeviceRemovedReason() override { return Lost ? DXGI_ERROR_DEVICE_REMOVED : S_OK; }
		bool Lost = false;

	private:
		void BeginCmd(CmdSlot& slot);
		CmdSlot TakeCmd();
	};

	template <class Iface>
	Obj<Iface>::Obj(Dev* dev) : D(dev) { D->AddRef(); ++D->Resources; }
	template <class Iface>
	Obj<Iface>::~Obj() { --D->Resources; D->GfxDevice::Release(); }

	// ---- 컨텍스트
	class Ctx : public GfxContext
	{
	public:
		~Ctx() override;
		GfxApi Api() const override { return GfxApi::Vulkan; }
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

		// 효과가 넣은 프로그램 · 바인딩
		ComPtr<Program> Prog;
		std::vector<GfxVkShared::BindingValue> Values;
		std::vector<ComPtr<GfxObject>> Held;   // 값의 뷰 · 샘플러를 잡아 둔다

		// 기록 상태
		bool Rendering = false;
		VkRect2D RenderArea = {};
		bool NeedBarrier = false;    // 버퍼 복사 쓰기 뒤 전역 장벽 (다음 읽기 전에)
		uint64_t BoundEpoch = ~0ull;
		VkPipeline BoundPipeline = VK_NULL_HANDLE;
		VkDescriptorSet BoundSet = VK_NULL_HANDLE;
		std::vector<uint32_t> BoundOffsets;
		bool DynamicDirty = true;     // 뷰포트 · 가위 · 블렌드 상수 · 스텐실 기준
		// 디스크립터 집합 캐시 (지금 풀 안에서)
		uint64_t SetPoolGeneration = ~0ull;
		std::unordered_map<uint64_t, std::pair<std::vector<uint64_t>, VkDescriptorSet>> SetCache;
		std::vector<ViewInfo*> StorageImages;   // BuildSet 이 스토리지 이미지로 묶은 뷰 (디스패치 뒤 Written)
		// ---- 그리기마다의 CPU 비용 (같은 상태가 이어지는 그리기 — 인스턴싱 묶음 · 같은 재질):
		//  값이 바뀌지 않았고 (ValuesSerial) 이미지 배치 (ImageStateSerial) · 타깃 (TargetsSerial) · 풀이 그대로면 앞 집합을 그대로,
		//  파이프라인 키가 앞과 같으면 앞 파이프라인을, 정점 · 인덱스 버퍼가 같으면 다시 묶지 않는다
		uint64_t ValuesSerial = 1, TargetsSerial = 1;
		bool UboOffsetsDirty = false;   // 값이 상수 링 자리 (동적 UBO 오프셋) 만 바뀌었다 — 집합은 그대로, 오프셋만 다시
		struct LastSetInfo { Program* Prog = nullptr; uint64_t Values = 0, ImageState = 0, Pool = 0, Targets = 0; VkDescriptorSet Set = VK_NULL_HANDLE; std::vector<uint32_t> Offsets; } LastSet;
		PipelineKey LastKey = {};
		VkPipeline LastPipe = VK_NULL_HANDLE;
		struct BoundVb { VkBuffer Buffer = VK_NULL_HANDLE; VkDeviceSize Offset = 0, Stride = 0; } BoundVbs[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		VkBuffer BoundIb = VK_NULL_HANDLE;
		VkDeviceSize BoundIbOffset = 0;
		VkIndexType BoundIbType = VK_INDEX_TYPE_UINT16;
		// 오클루전 쿼리: Begin 이 렌더링 밖이면 다음 그리기가 렌더링을 시작한 뒤에 기록한다 (쿼리는 한 렌더링 안에서 시작 · 끝)
		ComPtr<GfxQuery> PendingQuery, ActiveQuery;
		std::vector<ComPtr<GfxQuery>> ToCopy;   // 끝난 예측 쿼리 → 조건부 렌더링 값으로 복사할 것 (첫 SetPredication 때 한꺼번에)
		ComPtr<GfxQuery> Predicate;             // SetPredication (그리기마다 조건부 렌더링으로 감싼다)
		std::vector<Query*> StatStack;          // 열린 파이프라인 통계 쿼리 (바깥 → 안)
		void StatSegBegin(Query* q);
		void StatSegEnd(Query* q);
		void PauseStats();    // 제출 앞: 열린 조각을 닫는다 (쿼리는 명령 버퍼를 넘을 수 없다)
		void ResumeStats();   // 새 명령 버퍼: 다시 연다

		// ---- 내부
		void EndRendering();
		void FlushBarrier();
		void Barrier(const std::vector<VkImageMemoryBarrier2>& images);
		// 서브리소스 범위를 newLayout 으로 (바뀌는 것만 장벽에 더한다)
		void Transition(Image& img, UINT baseMip, UINT mips, UINT baseLayer, UINT layers, VkImageLayout newLayout, std::vector<VkImageMemoryBarrier2>& out);
		void TransitionNow(Image& img, UINT baseMip, UINT mips, UINT baseLayer, UINT layers, VkImageLayout newLayout);
		void BeforeTransfer();   // 렌더링 끝 + 앞 버퍼 쓰기 장벽
		void BeforeBufferWrite();   // 렌더링 끝 + 앞 읽기 · 쓰기 모두 끝날 때까지 (버퍼 덮어쓰기)
		void AfterSubmit();
		bool PrepareDraw(bool indexed);
		void ResolveBuffer(GfxResource* b, VkBuffer& buffer, VkDeviceSize& offset);
		VkDescriptorSet BuildSet(std::vector<uint32_t>& dynamicOffsets, std::vector<VkImageMemoryBarrier2>& barriers, bool& ok);
		void BeginPendingQuery();   // 렌더링 안: 미룬 vkCmdBeginQuery
		void CopyPredicates();      // 렌더링 밖: 끝난 예측 쿼리 결과 → 링 (조건부 렌더링 값)
		bool BeginCondition();      // 그리기 앞: 예측이 있으면 조건부 렌더링 시작
		void EndCondition(bool begun);
		bool IsWritableTarget(const Image* img, UINT baseMip, UINT mips, UINT baseLayer, UINT layers) const;

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
		void RSSetState(GfxRasterizerState* s) override { Rs = s; DynamicDirty = true; }   // 가위 사용이 바뀔 수 있다
		void RSGetState(GfxRasterizerState** s) override { *s = Rs.Get(); if (*s) (*s)->AddRef(); }
		void RSSetViewports(UINT count, const D3D11_VIEWPORT* v) override;
		void RSGetViewports(UINT* count, D3D11_VIEWPORT* v) override;
		void RSSetScissorRects(UINT count, const D3D11_RECT* r) override;
		void RSGetScissorRects(UINT* count, D3D11_RECT* r) override;
		void PSSetShaderResources(UINT, UINT count, GfxShaderResourceView* const* views) override { OutsideViews("PS", count, views); }
		void VSSetShaderResources(UINT, UINT count, GfxShaderResourceView* const* views) override { OutsideViews("VS", count, views); }
		void CSSetShaderResources(UINT, UINT count, GfxShaderResourceView* const* views) override { OutsideViews("CS", count, views); }
		void CSSetUnorderedAccessViews(UINT, UINT, GfxUnorderedAccessView* const*, const UINT*) override {}
		bool SupportsGpuDriven() const override;
		bool DrawIndexedInstancedIndirect(GfxBuffer* args, UINT offset) override;
		bool DrawInstancedIndirect(GfxBuffer* args, UINT offset) override;
		bool SetPredication(GfxQuery* predicate, BOOL value) override;
		bool ClearUnorderedAccessViewUint(GfxUnorderedAccessView* uav, const UINT values[4]) override;
		void CSSetShader(void*, void*, UINT) override {}
		void Draw(UINT vertexCount, UINT startVertex) override;
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
		void CopyImageRegion(Image& dst, UINT dstSub, UINT x, UINT y, UINT z, Image& src, UINT srcSub, const D3D11_BOX* box);
	};

	uint64_t HashBytes(const void* data, size_t size, uint64_t seed = 1469598103934665603ull);
	uint64_t HashRasterizer(const D3D11_RASTERIZER_DESC& d);
	uint64_t HashBlend(const D3D11_BLEND_DESC& d);
	uint64_t HashDepthStencil(const D3D11_DEPTH_STENCIL_DESC& d);
	D3D11_RASTERIZER_DESC DefaultRasterizer();
	D3D11_BLEND_DESC DefaultBlend();
	D3D11_DEPTH_STENCIL_DESC DefaultDepthStencil();
}
