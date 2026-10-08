#include "pch.h"
#include "GfxVkInternal.h"
#include "GLShared.h"   // GLInputSignature: FxPass::GetDesc 의 입력 서명 (의미 → location) — GL 과 같은 구조체를 쓴다

// Gfx Vulkan 장치: 메모리 · 링 · 제출 · 자원 만들기. 컨텍스트(그리기 · 복사)는 GfxVkContext.cpp
namespace GfxVkImpl
{
	namespace
	{
		std::atomic<uint64_t> s_NextId{ 1 };
		constexpr VkDeviceSize kDeviceBlock = 64ull << 20;
		constexpr VkDeviceSize kHostBlock = 16ull << 20;
		constexpr VkDeviceSize kRingChunk = 8ull << 20;
		VkDeviceSize AlignUp(VkDeviceSize v, VkDeviceSize a) { return a > 1 ? (v + a - 1) / a * a : v; }

		bool Check(VkResult r, const char* what, std::string* error = nullptr)
		{
			if (r == VK_SUCCESS) return true;
			const std::string msg = std::string(what) + " failed: " + VkLoader::ResultName(r);
			if (error) *error = msg;
			EditorLog::Write("Vulkan", "%s", msg.c_str());
			return false;
		}
	}

	uint64_t NextId() { return s_NextId++; }

	uint64_t HashBytes(const void* data, size_t size, uint64_t seed)
	{
		uint64_t h = seed;
		const uint8_t* p = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < size; ++i)
		{
			h ^= p[i];
			h *= 1099511628211ull;
		}
		return h;
	}

	size_t PipelineKeyHash::operator()(const PipelineKey& k) const { return (size_t)HashBytes(&k, sizeof(k)); }

	// 상태 설명 해시: 필드마다 (구조체 빈칸은 값이 정해지지 않는다)
	uint64_t HashRasterizer(const D3D11_RASTERIZER_DESC& d)
	{
		const int v[] = { (int)d.FillMode, (int)d.CullMode, d.FrontCounterClockwise, d.DepthBias, d.DepthClipEnable, d.ScissorEnable, d.MultisampleEnable, d.AntialiasedLineEnable };
		const float f[] = { d.DepthBiasClamp, d.SlopeScaledDepthBias };
		return HashBytes(f, sizeof(f), HashBytes(v, sizeof(v)));
	}

	uint64_t HashBlend(const D3D11_BLEND_DESC& d)
	{
		std::vector<int> v = { d.AlphaToCoverageEnable, d.IndependentBlendEnable };
		for (const auto& r : d.RenderTarget)
		{
			v.push_back(r.BlendEnable);
			v.push_back(r.SrcBlend); v.push_back(r.DestBlend); v.push_back(r.BlendOp);
			v.push_back(r.SrcBlendAlpha); v.push_back(r.DestBlendAlpha); v.push_back(r.BlendOpAlpha);
			v.push_back(r.RenderTargetWriteMask);
		}
		return HashBytes(v.data(), v.size() * sizeof(int));
	}

	uint64_t HashDepthStencil(const D3D11_DEPTH_STENCIL_DESC& d)
	{
		const int v[] = { d.DepthEnable, (int)d.DepthWriteMask, (int)d.DepthFunc, d.StencilEnable, d.StencilReadMask, d.StencilWriteMask,
			d.FrontFace.StencilFailOp, d.FrontFace.StencilDepthFailOp, d.FrontFace.StencilPassOp, d.FrontFace.StencilFunc,
			d.BackFace.StencilFailOp, d.BackFace.StencilDepthFailOp, d.BackFace.StencilPassOp, d.BackFace.StencilFunc };
		return HashBytes(v, sizeof(v));
	}

	D3D11_RASTERIZER_DESC DefaultRasterizer()
	{
		D3D11_RASTERIZER_DESC d = {};
		d.FillMode = D3D11_FILL_SOLID;
		d.CullMode = D3D11_CULL_BACK;
		d.DepthClipEnable = TRUE;
		return d;
	}

	D3D11_BLEND_DESC DefaultBlend()
	{
		D3D11_BLEND_DESC d = {};
		for (auto& r : d.RenderTarget)
		{
			r.SrcBlend = r.SrcBlendAlpha = D3D11_BLEND_ONE;
			r.DestBlend = r.DestBlendAlpha = D3D11_BLEND_ZERO;
			r.BlendOp = r.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			r.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		}
		return d;
	}

	D3D11_DEPTH_STENCIL_DESC DefaultDepthStencil()
	{
		D3D11_DEPTH_STENCIL_DESC d = {};
		d.DepthEnable = TRUE;
		d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		d.DepthFunc = D3D11_COMPARISON_LESS;
		d.StencilReadMask = d.StencilWriteMask = 0xFF;
		d.FrontFace = d.BackFace = { D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS };
		return d;
	}

	// ============================================================ 메모리
	void Allocator::Init(VkDevice device, VkPhysicalDevice phys)
	{
		_device = device;
		vkGetPhysicalDeviceMemoryProperties(phys, &_props);
	}

	void Allocator::Shutdown()
	{
		for (Block& b : _blocks)
			if (b.Memory) vkFreeMemory(_device, b.Memory, nullptr);
		_blocks.clear();
	}

	int Allocator::FindType(uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const
	{
		int best = -1;
		for (uint32_t i = 0; i < _props.memoryTypeCount; ++i)
		{
			if (!(bits & (1u << i))) continue;
			const VkMemoryPropertyFlags f = _props.memoryTypes[i].propertyFlags;
			if ((f & required) != required) continue;
			if ((f & preferred) == preferred) return (int)i;
			if (best < 0) best = (int)i;
		}
		return best;
	}

	bool Allocator::Allocate(const VkMemoryRequirements& req, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, bool linear, Allocation& out)
	{
		out = {};
		const int type = FindType(req.memoryTypeBits, required, preferred | required);
		if (type < 0) return false;
		const bool host = (_props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
		const VkDeviceSize blockSize = host ? kHostBlock : kDeviceBlock;
		auto mapMemory = [&](VkDeviceMemory m) -> uint8_t* {
			void* p = nullptr;
			return host && vkMapMemory(_device, m, 0, VK_WHOLE_SIZE, 0, &p) == VK_SUCCESS ? static_cast<uint8_t*>(p) : nullptr;
		};
		if (req.size >= blockSize / 2)
		{
			// 큰 자원: 따로
			VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			ai.allocationSize = req.size;
			ai.memoryTypeIndex = (uint32_t)type;
			if (vkAllocateMemory(_device, &ai, nullptr, &out.Memory) != VK_SUCCESS) return false;
			out.Size = req.size;
			out.Type = (uint32_t)type;
			out.Mapped = mapMemory(out.Memory);
			_live += req.size;
			_blockBytes += req.size;
			return true;
		}
		for (int pass = 0; pass < 2; ++pass)
		{
			for (size_t bi = 0; bi < _blocks.size(); ++bi)
			{
				Block& b = _blocks[bi];
				if (!b.Memory || b.Type != (uint32_t)type || b.Linear != linear) continue;
				for (auto it = b.Free.begin(); it != b.Free.end(); ++it)
				{
					const VkDeviceSize start = it->first, size = it->second;
					const VkDeviceSize at = AlignUp(start, req.alignment);
					if (at + req.size > start + size) continue;
					b.Free.erase(it);
					if (at > start) b.Free[start] = at - start;
					if (at + req.size < start + size) b.Free[at + req.size] = start + size - (at + req.size);
					out.Memory = b.Memory;
					out.Offset = at;
					out.Size = req.size;
					out.Block = (int)bi;
					out.Type = (uint32_t)type;
					out.Mapped = b.Mapped ? b.Mapped + at : nullptr;
					_live += req.size;
					return true;
				}
			}
			if (pass == 1) break;
			// 새 블록
			Block nb;
			VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			ai.allocationSize = blockSize;
			ai.memoryTypeIndex = (uint32_t)type;
			if (vkAllocateMemory(_device, &ai, nullptr, &nb.Memory) != VK_SUCCESS) return false;
			nb.Size = blockSize;
			nb.Type = (uint32_t)type;
			nb.Linear = linear;
			nb.Mapped = mapMemory(nb.Memory);
			nb.Free[0] = blockSize;
			_blockBytes += blockSize;
			bool placed = false;
			for (Block& slot : _blocks)
				if (!slot.Memory) { slot = std::move(nb); placed = true; break; }
			if (!placed) _blocks.push_back(std::move(nb));
		}
		return false;
	}

	void Allocator::Free(Allocation& a)
	{
		if (!a.Memory) return;
		_live -= (std::min)(_live, (UINT64)a.Size);
		if (a.Block < 0)
		{
			vkFreeMemory(_device, a.Memory, nullptr);
			_blockBytes -= (std::min)(_blockBytes, (UINT64)a.Size);
			a = {};
			return;
		}
		Block& b = _blocks[a.Block];
		VkDeviceSize start = a.Offset, size = a.Size;
		auto next = b.Free.lower_bound(start);
		if (next != b.Free.end() && start + size == next->first)
		{
			size += next->second;
			next = b.Free.erase(next);
		}
		if (next != b.Free.begin())
		{
			auto prev = std::prev(next);
			if (prev->first + prev->second == start)
			{
				start = prev->first;
				size += prev->second;
				b.Free.erase(prev);
			}
		}
		b.Free[start] = size;
		a = {};
	}

	// ============================================================ 장치
	Dev::~Dev()
	{
		if (Device)
		{
			vkDeviceWaitIdle(Device);
			DestroyAllSwapchains();
			Completed = Submitted;
			ComputeCompleted = ComputeSubmitted;
			for (auto& d : Deferred) d.Fn();
			Deferred.clear();
			for (auto& [k, p] : Pipelines) if (p) vkDestroyPipeline(Device, p, nullptr);
			Pipelines.clear();
			if (DummySampler) vkDestroySampler(Device, DummySampler, nullptr);
			if (DummyCompare) vkDestroySampler(Device, DummyCompare, nullptr);
			for (auto& [k, d] : Dummies)
			{
				vkDestroyImageView(Device, d.View, nullptr);
				vkDestroyImage(Device, d.Image, nullptr);
				Mem.Free(d.Mem);
			}
			if (DummyBuffer)
			{
				vkDestroyBuffer(Device, DummyBuffer, nullptr);
				Mem.Free(DummyBufferMem);
			}
			for (auto& c : Chunks)
			{
				vkDestroyBuffer(Device, c->Buffer, nullptr);
				Mem.Free(c->Mem);
			}
			auto destroyCmd = [&](CmdSlot& s) { if (s.Pool) vkDestroyCommandPool(Device, s.Pool, nullptr); };
			destroyCmd(Main);
			if (UploadOpen) destroyCmd(Upload);   // 닫힌 Upload 는 이미 InFlight/Free 목록에 있다
			for (auto& s : FreeCmds) destroyCmd(s);
			for (auto& s : InFlightCmds) destroyCmd(s);
			if (AsyncOpen) destroyCmd(Compute);
			for (auto& s : FreeComputeCmds) destroyCmd(s);
			for (auto& s : InFlightComputeCmds) destroyCmd(s);
			if (ComputeTimeline) vkDestroySemaphore(Device, ComputeTimeline, nullptr);
			if (CurrentPool) vkDestroyDescriptorPool(Device, CurrentPool, nullptr);
			for (auto& p : FreePools) vkDestroyDescriptorPool(Device, p.Handle, nullptr);
			for (auto& p : RetiredPools) vkDestroyDescriptorPool(Device, p.Handle, nullptr);
			if (PipelineCache) vkDestroyPipelineCache(Device, PipelineCache, nullptr);
			if (Timeline) vkDestroySemaphore(Device, Timeline, nullptr);
			Mem.Shutdown();
			vkDestroyDevice(Device, nullptr);
		}
		DestroyAllSwapchains();   // 장치를 못 만들었어도 창 표면은 있을 수 있다
		if (Loader)
			VkLoader::Release();
	}

	void Dev::Once(const std::string& key, const char* fmt, const char* arg)
	{
		if (Reported.insert(key).second)
			EditorLog::Write("Vulkan", fmt, arg);
	}

	bool Dev::Init(HWND window, std::string& error)
	{
		if (!VkLoader::Acquire(error)) return false;
		Loader = true;
		Window = window;
		VkInstance inst = VkLoader::Instance();
		uint32_t n = 0;
		vkEnumeratePhysicalDevices(inst, &n, nullptr);
		std::vector<VkPhysicalDevice> devices(n);
		vkEnumeratePhysicalDevices(inst, &n, devices.data());
		// 고르기: 1.3 + 그래픽 큐, 외장 > 내장 > 그 밖 (NOVA_VK_DEVICE = 이름 일부로 고정)
		char want[128] = {};
		::GetEnvironmentVariableA("NOVA_VK_DEVICE", want, sizeof(want));
		int bestScore = -1;
		std::string rejected;
		for (VkPhysicalDevice pd : devices)
		{
			VkPhysicalDeviceProperties p;
			vkGetPhysicalDeviceProperties(pd, &p);
			if (p.apiVersion < VK_API_VERSION_1_3)
			{
				rejected += std::string(p.deviceName) + " (Vulkan " + std::to_string(VK_API_VERSION_MAJOR(p.apiVersion)) + "." + std::to_string(VK_API_VERSION_MINOR(p.apiVersion)) + ") ";
				continue;
			}
			int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
			if (want[0] && strstr(p.deviceName, want)) score += 10;
			if (score > bestScore)
			{
				bestScore = score;
				Phys = pd;
				Props = p;
			}
		}
		if (!Phys)
		{
			error = "no Vulkan 1.3 GPU" + (rejected.empty() ? std::string() : ": " + rejected);
			return false;
		}
		Name = Props.deviceName;

		// 큐: 그래픽 (전송 · 표시도 같은 큐)
		vkGetPhysicalDeviceQueueFamilyProperties(Phys, &n, nullptr);
		std::vector<VkQueueFamilyProperties> families(n);
		vkGetPhysicalDeviceQueueFamilyProperties(Phys, &n, families.data());
		QueueFamily = ~0u;
		for (uint32_t i = 0; i < n; ++i)
			if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { QueueFamily = i; break; }
		if (QueueFamily == ~0u) { error = "no graphics queue"; return false; }

		// 기능: 꼭 필요한 것 (동적 렌더링 · synchronization2 · 타임라인 세마포어) + 있으면 켜는 것
		VkPhysicalDeviceVulkan13Features f13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		VkPhysicalDeviceVulkan11Features f11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
		VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		f2.pNext = &f11;
		f11.pNext = &f12;
		f12.pNext = &f13;
		vkGetPhysicalDeviceFeatures2(Phys, &f2);
		if (!f13.dynamicRendering || !f13.synchronization2 || !f12.timelineSemaphore)
		{
			error = std::string(Props.deviceName) + ": dynamicRendering / synchronization2 / timelineSemaphore not supported";
			return false;
		}
		const VkPhysicalDeviceFeatures& has = f2.features;
		VkPhysicalDeviceFeatures& on = Features;
		on = {};
#define NOVA_VK_FEATURE(name) on.name = has.name;
		NOVA_VK_FEATURE(samplerAnisotropy) NOVA_VK_FEATURE(geometryShader) NOVA_VK_FEATURE(tessellationShader) NOVA_VK_FEATURE(independentBlend)
		NOVA_VK_FEATURE(dualSrcBlend) NOVA_VK_FEATURE(depthClamp) NOVA_VK_FEATURE(depthBiasClamp) NOVA_VK_FEATURE(fillModeNonSolid)
		NOVA_VK_FEATURE(multiViewport) NOVA_VK_FEATURE(imageCubeArray) NOVA_VK_FEATURE(textureCompressionBC) NOVA_VK_FEATURE(shaderClipDistance)
		NOVA_VK_FEATURE(shaderImageGatherExtended) NOVA_VK_FEATURE(fragmentStoresAndAtomics) NOVA_VK_FEATURE(sampleRateShading)
		NOVA_VK_FEATURE(fullDrawIndexUint32) NOVA_VK_FEATURE(occlusionQueryPrecise) NOVA_VK_FEATURE(shaderStorageImageExtendedFormats)
		NOVA_VK_FEATURE(vertexPipelineStoresAndAtomics) NOVA_VK_FEATURE(largePoints) NOVA_VK_FEATURE(wideLines)
		NOVA_VK_FEATURE(drawIndirectFirstInstance)   // 오클루전 컬링의 간접 그리기 (묶음마다 첫 인스턴스)
		NOVA_VK_FEATURE(pipelineStatisticsQuery)     // 프로파일러 구간의 삼각형 · 픽셀 셰이더 수
#undef NOVA_VK_FEATURE
		VkPhysicalDeviceVulkan13Features e13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		VkPhysicalDeviceVulkan12Features e12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		VkPhysicalDeviceVulkan11Features e11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
		e13.dynamicRendering = VK_TRUE;
		e13.synchronization2 = VK_TRUE;
		e13.maintenance4 = f13.maintenance4;
		e12.timelineSemaphore = VK_TRUE;
		e12.hostQueryReset = f12.hostQueryReset;
		e12.samplerMirrorClampToEdge = f12.samplerMirrorClampToEdge;
		e12.scalarBlockLayout = f12.scalarBlockLayout;
		e12.separateDepthStencilLayouts = f12.separateDepthStencilLayouts;
		e11.shaderDrawParameters = f11.shaderDrawParameters;   // SV_InstanceID (BaseInstance 빼기)
		MirrorClamp = f12.samplerMirrorClampToEdge == VK_TRUE;
		VkPhysicalDeviceFeatures2 e2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		e2.features = on;
		e2.pNext = &e11;
		e11.pNext = &e12;
		e12.pNext = &e13;

		vkEnumerateDeviceExtensionProperties(Phys, nullptr, &n, nullptr);
		std::vector<VkExtensionProperties> exts(n);
		vkEnumerateDeviceExtensionProperties(Phys, nullptr, &n, exts.data());
		std::vector<const char*> enable;
		bool conditional = false;
		for (const auto& e : exts)
		{
			if (strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0 && VkLoader::HasSurfaceExtensions())
				enable.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
			if (strcmp(e.extensionName, VK_EXT_CONDITIONAL_RENDERING_EXTENSION_NAME) == 0)
				conditional = true;
		}
		// 조건부 렌더링 (D3D 의 SetPredication — 가려진 Skinned Mesh Renderer 를 GPU 가 건너뛴다). 없으면 예측 없이 그린다
		VkPhysicalDeviceConditionalRenderingFeaturesEXT cr = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT };
		if (conditional && vkCmdBeginConditionalRenderingEXT && vkCmdEndConditionalRenderingEXT)
		{
			VkPhysicalDeviceFeatures2 q = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			q.pNext = &cr;
			vkGetPhysicalDeviceFeatures2(Phys, &q);
			if (cr.conditionalRendering)
			{
				cr.inheritedConditionalRendering = VK_FALSE;
				cr.pNext = nullptr;
				e13.pNext = &cr;
				enable.push_back(VK_EXT_CONDITIONAL_RENDERING_EXTENSION_NAME);
				ConditionalRendering = true;
			}
		}

		const float priority[2] = { 1.0f, 1.0f };
		VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		qi.queueFamilyIndex = QueueFamily;
		// 비동기 컴퓨트: 그래픽 패밀리의 두 번째 큐 (같은 패밀리 — 자원 소유권을 옮길 필요가 없다). 하나뿐이면 같은 큐에서
		qi.queueCount = families[QueueFamily].queueCount >= 2 ? 2 : 1;
		qi.pQueuePriorities = priority;
		VkDeviceCreateInfo di = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		di.pNext = &e2;
		di.queueCreateInfoCount = 1;
		di.pQueueCreateInfos = &qi;
		di.enabledExtensionCount = (uint32_t)enable.size();
		di.ppEnabledExtensionNames = enable.data();
		if (Window && !SwapFor(Window, error))
			return false;
		if (!Check(vkCreateDevice(Phys, &di, nullptr, &Device), "vkCreateDevice", &error))
		{
			Device = VK_NULL_HANDLE;
			return false;
		}
		vkGetDeviceQueue(Device, QueueFamily, 0, &Queue);
		if (qi.queueCount >= 2)
			vkGetDeviceQueue(Device, QueueFamily, 1, &ComputeQueue);
		{
			char a[8] = {};
			::GetEnvironmentVariableA("NOVA_VK_ASYNC", a, sizeof(a));
			AsyncEnabled = a[0] != '0';
		}
		Mem.Init(Device, Phys);

		VkSemaphoreTypeCreateInfo st = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
		st.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		VkSemaphoreCreateInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		si.pNext = &st;
		if (!Check(vkCreateSemaphore(Device, &si, nullptr, &Timeline), "vkCreateSemaphore(timeline)", &error)) return false;
		if (ComputeQueue && vkCreateSemaphore(Device, &si, nullptr, &ComputeTimeline) != VK_SUCCESS)
		{
			ComputeQueue = VK_NULL_HANDLE;
			ComputeTimeline = VK_NULL_HANDLE;
		}
		VkPipelineCacheCreateInfo pc = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
		vkCreatePipelineCache(Device, &pc, nullptr, &PipelineCache);

		// D24S8 깊이 타깃 (AMD 등은 없다 → D32S8)
		VkFormatProperties fp;
		vkGetPhysicalDeviceFormatProperties(Phys, VK_FORMAT_D24_UNORM_S8_UINT, &fp);
		D24S8 = (fp.optimalTilingFeatures & (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) ==
			(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

		Main = TakeCmd();
		BeginCmd(Main);

		// 빈 샘플러 칸 (장치를 잡는 Gfx 객체가 아니라 날 핸들 — 순환 참조 없이)
		D3D11_SAMPLER_DESC sd = {};
		sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.MaxLOD = D3D11_FLOAT32_MAX;
		VkSamplerCreateInfo sci = VkMap::Sampler(sd, 1.0f, false);
		vkCreateSampler(Device, &sci, nullptr, &DummySampler);
		sd.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		sd.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
		sci = VkMap::Sampler(sd, 1.0f, false);
		vkCreateSampler(Device, &sci, nullptr, &DummyCompare);
		DummySamplerId = NextId();
		DummyCompareId = NextId();

		EditorLog::Write("Vulkan", "async compute: %s", !ComputeQueue ? "no second queue in the graphics family" : AsyncEnabled ? "on (second graphics-family queue)" : "off (NOVA_VK_ASYNC=0)");
		EditorLog::Write("Vulkan", "device: %s (Vulkan %u.%u.%u, driver %u), validation %s, D24S8 %s, swapchain %s", Props.deviceName,
			VK_API_VERSION_MAJOR(Props.apiVersion), VK_API_VERSION_MINOR(Props.apiVersion), VK_API_VERSION_PATCH(Props.apiVersion), Props.driverVersion,
			VkLoader::ValidationEnabled() ? "on" : "off", D24S8 ? "yes" : "no (D32S8)", enable.empty() ? "no" : "yes");
		return true;
	}

	// ============================================================ 명령 · 제출
	Dev::CmdSlot Dev::TakeCmd()
	{
		Poll();
		for (size_t i = 0; i < FreeCmds.size(); ++i)
		{
			CmdSlot s = FreeCmds[i];
			FreeCmds.erase(FreeCmds.begin() + i);
			vkResetCommandPool(Device, s.Pool, 0);
			return s;
		}
		CmdSlot s;
		VkCommandPoolCreateInfo pi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		pi.queueFamilyIndex = QueueFamily;
		vkCreateCommandPool(Device, &pi, nullptr, &s.Pool);
		VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		ai.commandPool = s.Pool;
		ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		ai.commandBufferCount = 1;
		vkAllocateCommandBuffers(Device, &ai, &s.Cb);
		return s;
	}

	void Dev::BeginCmd(CmdSlot& slot)
	{
		VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(slot.Cb, &bi);
	}

	VkCommandBuffer Dev::UploadCmd()
	{
		if (!UploadOpen)
		{
			Upload = TakeCmd();
			BeginCmd(Upload);
			UploadOpen = true;
		}
		return Upload.Cb;
	}

	void Dev::Submit(bool wait, VkSemaphore waitSemaphore, VkSemaphore signalSemaphore)
	{
		if (Lost) return;
		// 비동기 컴퓨트 중의 제출 (쿼리 읽기 · 기다림 등): 컴퓨트 블록을 먼저 닫는다 (아래의 장벽 · 통계 쿼리가 그래픽 명령 버퍼로 가게)
		if (AsyncOpen && Immediate)
			Immediate->EndAsyncCompute();
		if (Immediate)
		{
			Immediate->EndRendering();
			Immediate->PauseStats();
			Immediate->FlushBarrier();
		}
		{
			// 제출 끝: GPU 쓰기 → 호스트 읽기 (Map(READ) · 텍스처 읽기는 이 제출을 기다린 뒤 읽는다)
			VkMemoryBarrier2 hb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
			hb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			hb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
			hb.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
			hb.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
			VkDependencyInfo hd = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
			hd.memoryBarrierCount = 1;
			hd.pMemoryBarriers = &hb;
			vkCmdPipelineBarrier2(Main.Cb, &hd);
		}
		VkCommandBufferSubmitInfo cbs[2] = {};
		uint32_t count = 0;
		if (UploadOpen)
		{
			// 업로드 뒤 장벽: 같은 제출의 본 명령이 새 자원을 읽는다
			VkMemoryBarrier2 mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
			mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			mb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
			mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
			VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
			dep.memoryBarrierCount = 1;
			dep.pMemoryBarriers = &mb;
			vkCmdPipelineBarrier2(Upload.Cb, &dep);
			vkEndCommandBuffer(Upload.Cb);
			cbs[count].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
			cbs[count++].commandBuffer = Upload.Cb;
		}
		vkEndCommandBuffer(Main.Cb);
		cbs[count].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		cbs[count++].commandBuffer = Main.Cb;
		const uint64_t serial = Recording();
		VkSemaphoreSubmitInfo signal[2] = { { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO }, { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO } };
		signal[0].semaphore = Timeline;
		signal[0].value = serial;
		signal[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		signal[1].semaphore = signalSemaphore;   // 표시 (스왑체인)
		signal[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		VkSemaphoreSubmitInfo waitInfo[2] = { { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO }, { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO } };
		uint32_t waits = 0;
		if (waitSemaphore)
		{
			waitInfo[waits].semaphore = waitSemaphore;      // 스왑체인 이미지 받기
			waitInfo[waits++].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		}
		if (GraphicsWaitCompute)
		{
			waitInfo[waits].semaphore = ComputeTimeline;    // 비동기 컴퓨트 결과 (WaitAsyncCompute)
			waitInfo[waits].value = GraphicsWaitCompute;
			waitInfo[waits++].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			GraphicsWaitCompute = 0;
		}
		VkSubmitInfo2 si = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
		si.commandBufferInfoCount = count;
		si.pCommandBufferInfos = cbs;
		si.signalSemaphoreInfoCount = signalSemaphore ? 2 : 1;
		si.pSignalSemaphoreInfos = signal;
		si.waitSemaphoreInfoCount = waits;
		si.pWaitSemaphoreInfos = waitInfo;
		const VkResult r = vkQueueSubmit2(Queue, 1, &si, VK_NULL_HANDLE);
		if (r != VK_SUCCESS)
		{
			Check(r, "vkQueueSubmit2");
			if (r == VK_ERROR_DEVICE_LOST) Lost = true;
		}
		Submitted = serial;
		Main.Retire = serial;
		InFlightCmds.push_back(Main);
		if (UploadOpen)
		{
			Upload.Retire = serial;
			InFlightCmds.push_back(Upload);
			UploadOpen = false;
		}
		++Epoch;
		DrawsSinceSubmit = 0;
		Main = TakeCmd();
		BeginCmd(Main);
		if (Immediate) Immediate->AfterSubmit();
		if (wait) WaitSerial(serial);
		else Poll();
	}

	void Dev::Poll()
	{
		if (!Device) return;
		uint64_t value = 0;
		if (vkGetSemaphoreCounterValue(Device, Timeline, &value) == VK_SUCCESS)
			Completed = (std::max)(Completed, value);
		if (ComputeTimeline && vkGetSemaphoreCounterValue(Device, ComputeTimeline, &value) == VK_SUCCESS)
			ComputeCompleted = (std::max)(ComputeCompleted, value);
		for (size_t i = 0; i < InFlightComputeCmds.size();)
		{
			if (InFlightComputeCmds[i].Retire <= ComputeCompleted)
			{
				FreeComputeCmds.push_back(InFlightComputeCmds[i]);
				InFlightComputeCmds.erase(InFlightComputeCmds.begin() + i);
			}
			else ++i;
		}
		for (size_t i = 0; i < InFlightCmds.size();)
		{
			if (InFlightCmds[i].Retire <= Completed)
			{
				FreeCmds.push_back(InFlightCmds[i]);
				InFlightCmds.erase(InFlightCmds.begin() + i);
			}
			else ++i;
		}
		for (size_t i = 0; i < RetiredPools.size();)
		{
			if (RetiredPools[i].Retire <= Completed && RetiredPools[i].RetireCompute <= ComputeCompleted)
			{
				vkResetDescriptorPool(Device, RetiredPools[i].Handle, 0);
				FreePools.push_back(RetiredPools[i]);
				RetiredPools.erase(RetiredPools.begin() + i);
			}
			else ++i;
		}
		while (!Deferred.empty() && Deferred.front().Serial <= Completed && Deferred.front().ComputeSerial <= ComputeCompleted)
		{
			auto fn = std::move(Deferred.front().Fn);
			Deferred.pop_front();
			fn();
		}
	}

	void Dev::WaitSerial(uint64_t serial)
	{
		if (Lost || serial <= Completed) return;
		if (serial > Submitted) Submit(false);
		VkSemaphoreWaitInfo wi = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
		wi.semaphoreCount = 1;
		wi.pSemaphores = &Timeline;
		wi.pValues = &serial;
		// 10 초 넘게 끝나지 않으면 GPU 가 멈춘 것으로 본다 (기다리다 PC 가 굳지 않게)
		const VkResult r = vkWaitSemaphores(Device, &wi, 10ull * 1000 * 1000 * 1000);
		if (r == VK_TIMEOUT)
		{
			EditorLog::Write("Vulkan", "GPU did not finish submit %llu in 10 s - treated as device lost", (unsigned long long)serial);
			Lost = true;
		}
		else if (r != VK_SUCCESS)
		{
			Check(r, "vkWaitSemaphores");
			if (r == VK_ERROR_DEVICE_LOST) Lost = true;
		}
		Poll();
	}

	void Dev::WaitCompute(uint64_t serial)
	{
		if (Lost || !ComputeTimeline || serial == 0 || serial <= ComputeCompleted) return;
		VkSemaphoreWaitInfo wi = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
		wi.semaphoreCount = 1;
		wi.pSemaphores = &ComputeTimeline;
		wi.pValues = &serial;
		const VkResult r = vkWaitSemaphores(Device, &wi, 10ull * 1000 * 1000 * 1000);
		if (r == VK_TIMEOUT)
		{
			EditorLog::Write("Vulkan", "compute queue did not finish submit %llu in 10 s - treated as device lost", (unsigned long long)serial);
			Lost = true;
		}
		else if (r != VK_SUCCESS && r == VK_ERROR_DEVICE_LOST)
			Lost = true;
		Poll();
	}

	void Dev::OpenCompute()
	{
		Poll();
		if (!FreeComputeCmds.empty())
		{
			Compute = FreeComputeCmds.back();
			FreeComputeCmds.pop_back();
			vkResetCommandPool(Device, Compute.Pool, 0);
		}
		else
			Compute = TakeCmd();   // 같은 패밀리라 그래픽 풀과 같은 모양 (다시 쓸 때는 컴퓨트 목록으로)
		BeginCmd(Compute);
		AsyncOpen = true;
	}

	void Dev::SubmitCompute()
	{
		// 1) 그래픽 (컴퓨트가 읽을 것을 만든 일) 을 먼저 — 컴퓨트 큐가 이 값을 기다린다
		AsyncOpen = false;
		ComputeUnsubmitted = true;   // 이 그래픽 제출의 링 · 풀은 곧 보낼 컴퓨트 값까지 기다리게
		Submit(false);
		const uint64_t graphics = Submitted;
		// 2) 컴퓨트 명령 버퍼
		vkEndCommandBuffer(Compute.Cb);
		const uint64_t serial = ComputeSubmitted + 1;
		VkCommandBufferSubmitInfo cbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
		cbi.commandBuffer = Compute.Cb;
		VkSemaphoreSubmitInfo wait = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
		wait.semaphore = Timeline;
		wait.value = graphics;
		wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		VkSemaphoreSubmitInfo signal = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
		signal.semaphore = ComputeTimeline;
		signal.value = serial;
		signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		VkSubmitInfo2 si = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
		si.commandBufferInfoCount = 1;
		si.pCommandBufferInfos = &cbi;
		si.waitSemaphoreInfoCount = 1;
		si.pWaitSemaphoreInfos = &wait;
		si.signalSemaphoreInfoCount = 1;
		si.pSignalSemaphoreInfos = &signal;
		const VkResult r = Lost ? VK_ERROR_DEVICE_LOST : vkQueueSubmit2(ComputeQueue, 1, &si, VK_NULL_HANDLE);
		if (r != VK_SUCCESS)
		{
			Check(r, "vkQueueSubmit2(compute)");
			if (r == VK_ERROR_DEVICE_LOST) Lost = true;
		}
		else
			++AsyncSubmits;
		ComputeSubmitted = serial;
		ComputePending = serial;
		ComputeUnsubmitted = false;
		Compute.Retire = serial;
		InFlightComputeCmds.push_back(Compute);
		Compute = CmdSlot();
	}

	// ============================================================ 링
	uint8_t* Dev::RingAlloc(VkDeviceSize size, VkDeviceSize align, RingLoc& loc)
	{
		size = (std::max<VkDeviceSize>)(size, 4);
		if (Current)
		{
			const VkDeviceSize at = AlignUp(Current->Used, align);
			if (at + size <= Current->Size)
			{
				Current->Used = at + size;
				loc = { Current->Buffer, Current->Id, (uint32_t)at, Current, Current->Generation };
				return Current->Mem.Mapped + at;
			}
			Current->Retire = Recording();
			Current->RetireCompute = ComputeCover();
			Retired.push_back(Current);
			Current = nullptr;
		}
		Poll();
		for (auto it = Retired.begin(); it != Retired.end(); ++it)
			if ((*it)->Retire <= Completed && (*it)->RetireCompute <= ComputeCompleted && (*it)->Size >= size)
			{
				Current = *it;
				Retired.erase(it);
				Current->Used = 0;
				++Current->Generation;
				break;
			}
		if (!Current)
		{
			auto c = std::make_unique<RingChunk>();
			c->Size = (std::max)(kRingChunk, AlignUp(size, 1ull << 20));
			VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
			bi.size = c->Size;
			// 스토리지 = DYNAMIC 구조 버퍼 (오클루전 컬링의 후보 목록), 조건부 렌더링 = 쿼리 결과를 복사한 값
			bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			if (ConditionalRendering) bi.usage |= VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT;
			if (!Check(vkCreateBuffer(Device, &bi, nullptr, &c->Buffer), "vkCreateBuffer(ring)")) return nullptr;
			VkMemoryRequirements req;
			vkGetBufferMemoryRequirements(Device, c->Buffer, &req);
			if (!Mem.Allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, true, c->Mem) || !c->Mem.Mapped)
			{
				EditorLog::Write("Vulkan", "ring chunk allocation of %.1f MB failed", c->Size / 1048576.0);
				vkDestroyBuffer(Device, c->Buffer, nullptr);
				return nullptr;
			}
			vkBindBufferMemory(Device, c->Buffer, c->Mem.Memory, c->Mem.Offset);
			c->Id = NextId();
			Current = c.get();
			Chunks.push_back(std::move(c));
			if (Chunks.size() == 16 || Chunks.size() == 64)
				EditorLog::Write("Vulkan", "ring grew to %zu chunks", Chunks.size());
		}
		Current->Used = size;
		loc = { Current->Buffer, Current->Id, 0, Current, Current->Generation };
		return Current->Mem.Mapped;
	}

	bool Dev::RingCurrent(const RingLoc& loc) const
	{
		return Current && loc.Chunk == Current && loc.Generation == Current->Generation;
	}

	VkDescriptorSet Dev::AllocateSet(VkDescriptorSetLayout layout)
	{
		for (int attempt = 0; attempt < 2; ++attempt)
		{
			if (!CurrentPool)
			{
				Poll();
				if (!FreePools.empty())
				{
					CurrentPool = FreePools.back().Handle;
					FreePools.pop_back();
				}
				else
				{
					const VkDescriptorPoolSize sizes[] = {
						{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 8192 }, { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096 },
						{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 32768 }, { VK_DESCRIPTOR_TYPE_SAMPLER, 16384 },
						{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8192 }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2048 } };
					VkDescriptorPoolCreateInfo pi = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
					pi.maxSets = 2048;
					pi.poolSizeCount = _countof(sizes);
					pi.pPoolSizes = sizes;
					if (!Check(vkCreateDescriptorPool(Device, &pi, nullptr, &CurrentPool), "vkCreateDescriptorPool")) return VK_NULL_HANDLE;
				}
				++PoolGeneration;
			}
			VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			ai.descriptorPool = CurrentPool;
			ai.descriptorSetCount = 1;
			ai.pSetLayouts = &layout;
			VkDescriptorSet set = VK_NULL_HANDLE;
			if (vkAllocateDescriptorSets(Device, &ai, &set) == VK_SUCCESS)
				return set;
			RetiredPools.push_back({ CurrentPool, Recording(), ComputeCover() });
			CurrentPool = VK_NULL_HANDLE;
		}
		Once("set-alloc", "%s", "descriptor set allocation failed");
		return VK_NULL_HANDLE;
	}

	// ============================================================ 자원
	Image* ImageOf(GfxResource* r)
	{
		if (!r || r->Api() != GfxApi::Vulkan) return nullptr;
		D3D11_RESOURCE_DIMENSION dim;
		r->GetType(&dim);
		switch (dim)
		{
		case D3D11_RESOURCE_DIMENSION_TEXTURE1D: return &static_cast<Tex1D*>(r)->I;
		case D3D11_RESOURCE_DIMENSION_TEXTURE2D: return &static_cast<Tex2D*>(r)->I;
		case D3D11_RESOURCE_DIMENSION_TEXTURE3D: return &static_cast<Tex3D*>(r)->I;
		default: return nullptr;
		}
	}

	Buf* BufOf(GfxResource* r)
	{
		if (!r || r->Api() != GfxApi::Vulkan) return nullptr;
		D3D11_RESOURCE_DIMENSION dim;
		r->GetType(&dim);
		return dim == D3D11_RESOURCE_DIMENSION_BUFFER ? static_cast<Buf*>(r) : nullptr;
	}

	Buf::~Buf()
	{
		Dev* d = D;
		if (Buffer)
		{
			VkBuffer b = Buffer;
			Allocation m = Mem;
			d->Defer([d, b, m]() mutable { vkDestroyBuffer(d->Device, b, nullptr); d->Mem.Free(m); });
		}
	}

	void DestroyImage(Dev* d, Image& img)
	{
		VkImage h = img.Handle;
		VkBuffer s = img.Staging;
		Allocation m = img.Mem;
		if (!h && !s) return;
		d->Defer([d, h, s, m]() mutable {
			if (h) vkDestroyImage(d->Device, h, nullptr);
			if (s) vkDestroyBuffer(d->Device, s, nullptr);
			d->Mem.Free(m);
		});
	}
	Tex1D::~Tex1D() { DestroyImage(D, I); }
	Tex2D::~Tex2D() { DestroyImage(D, I); }
	Tex3D::~Tex3D() { DestroyImage(D, I); }

	template <class Iface, class Desc>
	View<Iface, Desc>::~View()
	{
		if (V.View)
		{
			Dev* d = this->D;
			VkImageView v = V.View;
			d->Defer([d, v]() { vkDestroyImageView(d->Device, v, nullptr); });
		}
	}

	Sampler::~Sampler()
	{
		if (Handle)
		{
			Dev* d = D;
			VkSampler s = Handle;
			d->Defer([d, s]() { vkDestroySampler(d->Device, s, nullptr); });
		}
	}

	Query::~Query()
	{
		if (Pool)
		{
			Dev* d = D;
			VkQueryPool p = Pool;
			d->Defer([d, p]() { vkDestroyQueryPool(d->Device, p, nullptr); });
		}
	}

	BindingLayout::~BindingLayout()
	{
		Dev* d = D;
		VkDescriptorSetLayout s = SetLayout;
		VkPipelineLayout p = PipelineLayout;
		d->Defer([d, s, p]() {
			if (p) vkDestroyPipelineLayout(d->Device, p, nullptr);
			if (s) vkDestroyDescriptorSetLayout(d->Device, s, nullptr);
		});
	}

	Program::~Program()
	{
		Dev* d = D;
		d->ForgetProgram(Id);
		std::vector<VkShaderModule> modules;
		for (const auto& s : Stages) modules.push_back(s.Module);
		VkPipeline compute = Compute;
		d->Defer([d, modules, compute]() {
			for (VkShaderModule m : modules) vkDestroyShaderModule(d->Device, m, nullptr);
			if (compute) vkDestroyPipeline(d->Device, compute, nullptr);
		});
	}

	namespace
	{
		UINT FullMips(UINT w, UINT h, UINT d)
		{
			UINT m = 1, s = (std::max)((std::max)(w, h), d);
			while (s > 1) { s >>= 1; ++m; }
			return m;
		}

		bool IsDepthLayoutFormat(const Image& img) { return img.Fmt.Depth; }

		VkImageLayout ReadLayout(const Image& img)
		{
			return IsDepthLayoutFormat(img) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		VkImageMemoryBarrier2 ImageBarrier(const Image& img, VkImageLayout from, VkImageLayout to, UINT mip, UINT mips, UINT layer, UINT layers)
		{
			VkImageMemoryBarrier2 b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
			b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			b.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_WRITE_BIT;
			b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
			b.oldLayout = from;
			b.newLayout = to;
			b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.image = img.Handle;
			b.subresourceRange = { img.Fmt.Aspect, mip, mips, layer, layers };
			return b;
		}

		void RecordBarriers(VkCommandBuffer cb, const std::vector<VkImageMemoryBarrier2>& images)
		{
			if (images.empty()) return;
			VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
			dep.imageMemoryBarrierCount = (uint32_t)images.size();
			dep.pImageMemoryBarriers = images.data();
			vkCmdPipelineBarrier2(cb, &dep);
		}
	}

	HRESULT Dev::MakeImage(Image& img, const D3D11_SUBRESOURCE_DATA* data, const char* what)
	{
		if (img.Fmt.Vk == VK_FORMAT_UNDEFINED)
		{
			char buf[64];
			snprintf(buf, sizeof(buf), "%d", (int)img.Dxgi);
			Once(std::string("fmt:") + buf, "unsupported texture format DXGI %s", buf);
			return E_INVALIDARG;
		}
		img.Format = FixFormat(img.Fmt.Vk);
		img.Id = NextId();
		if (img.Usage == D3D11_USAGE_STAGING)
		{
			// CPU 읽기 · 쓰기 텍스처 = 버퍼 (서브리소스마다 빽빽한 행, 16 바이트 정렬)
			VkDeviceSize size = 0;
			img.SubOffset.clear();
			for (UINT layer = 0; layer < img.Layers; ++layer)
				for (UINT m = 0; m < img.Mips; ++m)
				{
					size = AlignUp(size, 16);
					img.SubOffset.push_back(size);
					size += VkMap::SliceBytes(img.Fmt, img.MipW(m), img.MipH(m)) * img.MipD(m);
				}
			VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
			bi.size = (std::max<VkDeviceSize>)(size, 16);
			bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			if (!Check(vkCreateBuffer(Device, &bi, nullptr, &img.Staging), "vkCreateBuffer(staging texture)")) return E_FAIL;
			VkMemoryRequirements req;
			vkGetBufferMemoryRequirements(Device, img.Staging, &req);
			if (!Mem.Allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT, true, img.Mem))
			{
				vkDestroyBuffer(Device, img.Staging, nullptr);
				img.Staging = VK_NULL_HANDLE;
				return E_OUTOFMEMORY;
			}
			vkBindBufferMemory(Device, img.Staging, img.Mem.Memory, img.Mem.Offset);
			img.Bytes = size;
			if (data)
				for (UINT sub = 0; sub < img.Layers * img.Mips; ++sub)
				{
					const D3D11_SUBRESOURCE_DATA& d = data[sub];
					if (!d.pSysMem) continue;
					const UINT m = sub % img.Mips;
					const UINT64 row = VkMap::RowBytes(img.Fmt, img.MipW(m));
					const UINT rows = img.Fmt.Compressed ? (std::max)(1u, (img.MipH(m) + 3) / 4) : img.MipH(m);
					uint8_t* dst = img.Mem.Mapped + img.SubOffset[sub];
					for (UINT z = 0; z < img.MipD(m); ++z)
						for (UINT r = 0; r < rows; ++r)
							memcpy(dst + (z * rows + r) * row, static_cast<const uint8_t*>(d.pSysMem) + (size_t)z * d.SysMemSlicePitch + (size_t)r * d.SysMemPitch, (size_t)row);
				}
			return S_OK;
		}

		VkImageCreateInfo ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		ci.imageType = img.Type;
		ci.format = img.Format;
		ci.extent = { img.Width, img.Height, img.Depth };
		ci.mipLevels = img.Mips;
		ci.arrayLayers = img.Layers;
		ci.samples = (VkSampleCountFlagBits)img.Samples;
		ci.tiling = VK_IMAGE_TILING_OPTIMAL;
		ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		if (img.Bind & D3D11_BIND_RENDER_TARGET) ci.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		if (img.Bind & D3D11_BIND_UNORDERED_ACCESS) ci.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
		if (img.Fmt.Depth) ci.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		if (img.Cube) ci.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		if (img.Fmt.Typeless && !img.Fmt.Depth) ci.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
		if (img.Type == VK_IMAGE_TYPE_3D && (img.Bind & D3D11_BIND_RENDER_TARGET)) ci.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
		ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkResult r = vkCreateImage(Device, &ci, nullptr, &img.Handle);
		if (r != VK_SUCCESS)
		{
			Check(r, what);
			img.Handle = VK_NULL_HANDLE;
			return r == VK_ERROR_OUT_OF_DEVICE_MEMORY ? E_OUTOFMEMORY : E_FAIL;
		}
		VkMemoryRequirements req;
		vkGetImageMemoryRequirements(Device, img.Handle, &req);
		if (!Mem.Allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, false, img.Mem))
		{
			EditorLog::Write("Vulkan", "%s: out of device memory (%.1f MB)", what, req.size / 1048576.0);
			vkDestroyImage(Device, img.Handle, nullptr);
			img.Handle = VK_NULL_HANDLE;
			return E_OUTOFMEMORY;
		}
		vkBindImageMemory(Device, img.Handle, img.Mem.Memory, img.Mem.Offset);
		img.Bytes = req.size;
		img.Layouts.assign((size_t)img.Layers * img.Mips, VK_IMAGE_LAYOUT_UNDEFINED);
		img.Written.assign((size_t)img.Layers * img.Mips, 0);
		if (data)
		{
			// 처음 데이터: 업로드 명령 (본 명령보다 먼저 제출) → 모두 셰이더 읽기 배치로
			VkCommandBuffer cb = UploadCmd();
			RecordBarriers(cb, { ImageBarrier(img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, img.Mips, 0, img.Layers) });
			for (UINT sub = 0; sub < img.Layers * img.Mips; ++sub)
				if (data[sub].pSysMem)
					UploadImage(cb, img, sub, nullptr, data[sub].pSysMem, data[sub].SysMemPitch, data[sub].SysMemSlicePitch);
			const VkImageLayout final = ReadLayout(img);
			RecordBarriers(cb, { ImageBarrier(img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, final, 0, img.Mips, 0, img.Layers) });
			std::fill(img.Layouts.begin(), img.Layouts.end(), final);
		}
		return S_OK;
	}

	void Dev::UploadImage(VkCommandBuffer cb, Image& img, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch)
	{
		const UINT m = sub % img.Mips, layer = sub / img.Mips;
		const UINT x = box ? box->left : 0, y = box ? box->top : 0, z = box ? box->front : 0;
		const UINT w = box ? box->right - box->left : img.MipW(m);
		const UINT h = box ? box->bottom - box->top : img.MipH(m);
		const UINT d = box ? box->back - box->front : img.MipD(m);
		if (!w || !h || !d) return;
		const UINT64 row = VkMap::RowBytes(img.Fmt, w);
		const UINT rows = img.Fmt.Compressed ? (std::max)(1u, (h + 3) / 4) : h;
		if (!rowPitch) rowPitch = (UINT)row;
		if (!depthPitch) depthPitch = rowPitch * rows;
		RingLoc loc;
		uint8_t* dst = RingAlloc(row * rows * d, (std::max<VkDeviceSize>)(16, img.Fmt.Bytes), loc);
		if (!dst) return;
		for (UINT zz = 0; zz < d; ++zz)
			for (UINT r = 0; r < rows; ++r)
				memcpy(dst + ((size_t)zz * rows + r) * row, static_cast<const uint8_t*>(data) + (size_t)zz * depthPitch + (size_t)r * rowPitch, (size_t)row);
		VkBufferImageCopy c = {};
		c.bufferOffset = loc.Offset;
		c.imageSubresource.aspectMask = img.Fmt.Depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		c.imageSubresource.mipLevel = m;
		c.imageSubresource.baseArrayLayer = img.Type == VK_IMAGE_TYPE_3D ? 0 : layer;
		c.imageSubresource.layerCount = 1;
		c.imageOffset = { (int32_t)x, (int32_t)y, (int32_t)(img.Type == VK_IMAGE_TYPE_3D ? z : 0) };
		c.imageExtent = { w, h, img.Type == VK_IMAGE_TYPE_3D ? d : 1 };
		vkCmdCopyBufferToImage(cb, loc.Buffer, img.Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
	}

	VkImageView Dev::MakeView(Image& img, VkImageViewType type, VkFormat format, VkImageAspectFlags aspect, UINT baseMip, UINT mips, UINT baseLayer, UINT layers,
		const VkComponentMapping* swizzle)
	{
		VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		vi.image = img.Handle;
		vi.viewType = type;
		vi.format = format;
		if (swizzle) vi.components = *swizzle;
		vi.subresourceRange = { aspect, baseMip, mips, baseLayer, layers };
		VkImageView v = VK_NULL_HANDLE;
		Check(vkCreateImageView(Device, &vi, nullptr, &v), "vkCreateImageView");
		return v;
	}

	// 빈 스토리지 칸: 셰이더가 쓰지 않는 바인딩 (Effects11 처럼 pass 가 쓰는 자원만 묶는다)
	VkBuffer Dev::DummyStorageBuffer()
	{
		if (DummyBuffer) return DummyBuffer;
		VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bi.size = 256;
		bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		if (vkCreateBuffer(Device, &bi, nullptr, &DummyBuffer) != VK_SUCCESS) { DummyBuffer = VK_NULL_HANDLE; return VK_NULL_HANDLE; }
		VkMemoryRequirements req;
		vkGetBufferMemoryRequirements(Device, DummyBuffer, &req);
		if (!Mem.Allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, true, DummyBufferMem))
		{
			vkDestroyBuffer(Device, DummyBuffer, nullptr);
			DummyBuffer = VK_NULL_HANDLE;
			return VK_NULL_HANDLE;
		}
		vkBindBufferMemory(Device, DummyBuffer, DummyBufferMem.Memory, DummyBufferMem.Offset);
		vkCmdFillBuffer(UploadCmd(), DummyBuffer, 0, VK_WHOLE_SIZE, 0);
		return DummyBuffer;
	}

	const Dev::Dummy& Dev::DummyStorageImage()
	{
		const int key = 1 << 10;   // DummyImage 의 키 (7 비트) 와 겹치지 않게
		auto it = Dummies.find(key);
		if (it != Dummies.end()) return it->second;
		Dummy& d = Dummies[key];
		Image img;
		img.Fmt = VkMap::FromDxgi(DXGI_FORMAT_R32_FLOAT, false);
		img.Format = img.Fmt.Vk;
		VkImageCreateInfo ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		ci.imageType = VK_IMAGE_TYPE_2D;
		ci.format = img.Format;
		ci.extent = { 1, 1, 1 };
		ci.mipLevels = 1;
		ci.arrayLayers = 1;
		ci.samples = VK_SAMPLE_COUNT_1_BIT;
		ci.tiling = VK_IMAGE_TILING_OPTIMAL;
		ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		if (vkCreateImage(Device, &ci, nullptr, &d.Image) != VK_SUCCESS) return d;
		VkMemoryRequirements req;
		vkGetImageMemoryRequirements(Device, d.Image, &req);
		Mem.Allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, false, d.Mem);
		vkBindImageMemory(Device, d.Image, d.Mem.Memory, d.Mem.Offset);
		img.Handle = d.Image;
		d.View = MakeView(img, VK_IMAGE_VIEW_TYPE_2D, img.Format, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
		d.Layout = VK_IMAGE_LAYOUT_GENERAL;
		d.Id = NextId();
		RecordBarriers(UploadCmd(), { ImageBarrier(img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, 1, 0, 1) });
		return d;
	}

	const Dev::Dummy& Dev::DummyImage(const GfxVkShared::BindingDesc& b)
	{
		const int dim = b.Dim == 0 || b.Dim == 2 || b.Dim == 3 ? b.Dim : 1;
		const bool arrayed = b.Arrayed && dim != 2 && (dim != 3 || Features.imageCubeArray);
		const int key = dim | (arrayed ? 16 : 0) | (b.Depth ? 32 : 0) | (b.Integer ? 64 : 0);
		auto it = Dummies.find(key);
		if (it != Dummies.end()) return it->second;
		Dummy& d = Dummies[key];
		Image img;
		img.Fmt = VkMap::FromDxgi(b.Depth ? DXGI_FORMAT_D32_FLOAT : b.Integer ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R8G8B8A8_UNORM, b.Depth);
		img.Format = img.Fmt.Vk;
		img.Type = dim == 0 ? VK_IMAGE_TYPE_1D : dim == 2 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
		img.Layers = dim == 3 ? 6 : 1;
		VkImageCreateInfo ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		ci.imageType = img.Type;
		ci.format = img.Format;
		ci.extent = { 1, 1, 1 };
		ci.mipLevels = 1;
		ci.arrayLayers = img.Layers;
		ci.samples = VK_SAMPLE_COUNT_1_BIT;
		ci.tiling = VK_IMAGE_TILING_OPTIMAL;
		ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		if (b.Depth) ci.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;   // DEPTH_STENCIL_READ_ONLY 배치에 필요
		if (dim == 3) ci.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		if (vkCreateImage(Device, &ci, nullptr, &d.Image) != VK_SUCCESS) return d;
		VkMemoryRequirements req;
		vkGetImageMemoryRequirements(Device, d.Image, &req);
		Mem.Allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, false, d.Mem);
		vkBindImageMemory(Device, d.Image, d.Mem.Memory, d.Mem.Offset);
		img.Handle = d.Image;
		const VkImageViewType type = dim == 0 ? (arrayed ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D)
			: dim == 2 ? VK_IMAGE_VIEW_TYPE_3D
			: dim == 3 ? (arrayed ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE)
			: (arrayed ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
		d.View = MakeView(img, type, img.Format, b.Depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, img.Layers);
		d.Layout = ReadLayout(img);
		d.Id = NextId();
		// 값: 색 = 0 (D3D 의 빈 SRV 와 같음), 깊이 = 1 (그림자 없음)
		VkCommandBuffer cb = UploadCmd();
		RecordBarriers(cb, { ImageBarrier(img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 1, 0, img.Layers) });
		const VkImageSubresourceRange range = { img.Fmt.Aspect, 0, 1, 0, img.Layers };
		if (b.Depth)
		{
			const VkClearDepthStencilValue v = { 1.0f, 0 };
			vkCmdClearDepthStencilImage(cb, d.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
		}
		else
		{
			const VkClearColorValue v = {};
			vkCmdClearColorImage(cb, d.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
		}
		RecordBarriers(cb, { ImageBarrier(img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, d.Layout, 0, 1, 0, img.Layers) });
		return d;
	}

	// ============================================================ 만들기 (GfxDevice)
	HRESULT Dev::CreateBuffer(const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxBuffer** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		if (!desc || desc->ByteWidth == 0) return E_INVALIDARG;
		auto* b = new Buf(this);
		b->Desc = *desc;
		b->Id = NextId();
		if (desc->Usage == D3D11_USAGE_DYNAMIC)
		{
			// 링에서 그린다 (Map 마다 새 자리). 처음 데이터는 사본에 두고 첫 사용 때 링으로
			b->Shadow.assign(desc->ByteWidth, 0);
			if (data && data->pSysMem) memcpy(b->Shadow.data(), data->pSysMem, desc->ByteWidth);
			*out = b;
			return S_OK;
		}
		VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bi.size = AlignUp(desc->ByteWidth, 4);
		bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		if (desc->BindFlags & D3D11_BIND_VERTEX_BUFFER) bi.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		if (desc->BindFlags & D3D11_BIND_INDEX_BUFFER) bi.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		if (desc->BindFlags & D3D11_BIND_CONSTANT_BUFFER) bi.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		if (desc->BindFlags & (D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE)) bi.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		if (desc->MiscFlags & D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS) bi.usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		if (!Check(vkCreateBuffer(Device, &bi, nullptr, &b->Buffer), "vkCreateBuffer"))
		{
			b->Buffer = VK_NULL_HANDLE;
			b->Release();
			return E_FAIL;
		}
		VkMemoryRequirements req;
		vkGetBufferMemoryRequirements(Device, b->Buffer, &req);
		const bool staging = desc->Usage == D3D11_USAGE_STAGING;
		const bool ok = staging
			? Mem.Allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				(desc->CPUAccessFlags & D3D11_CPU_ACCESS_READ) ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT : 0, true, b->Mem)
			: Mem.Allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, true, b->Mem);
		if (!ok)
		{
			EditorLog::Write("Vulkan", "buffer of %.1f MB: out of memory", desc->ByteWidth / 1048576.0);
			b->Release();
			return E_OUTOFMEMORY;
		}
		vkBindBufferMemory(Device, b->Buffer, b->Mem.Memory, b->Mem.Offset);
		if (data && data->pSysMem)
		{
			if (staging) memcpy(b->Mem.Mapped, data->pSysMem, desc->ByteWidth);
			else
			{
				RingLoc loc;
				uint8_t* p = RingAlloc(desc->ByteWidth, 16, loc);
				if (p)
				{
					memcpy(p, data->pSysMem, desc->ByteWidth);
					const VkBufferCopy c = { loc.Offset, 0, desc->ByteWidth };
					vkCmdCopyBuffer(UploadCmd(), loc.Buffer, b->Buffer, 1, &c);
				}
			}
		}
		*out = b;
		return S_OK;
	}

	HRESULT Dev::CreateTexture1D(const D3D11_TEXTURE1D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture1D** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		auto* t = new Tex1D(this);
		t->Desc = *desc;
		Image& i = t->I;
		i.Dxgi = desc->Format;
		i.Fmt = VkMap::FromDxgi(desc->Format, (desc->BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0);
		i.Type = VK_IMAGE_TYPE_1D;
		i.Width = desc->Width;
		i.Layers = (std::max)(1u, desc->ArraySize);
		i.Mips = desc->MipLevels ? desc->MipLevels : FullMips(desc->Width, 1, 1);
		t->Desc.MipLevels = i.Mips;
		i.Usage = desc->Usage;
		i.CpuAccess = desc->CPUAccessFlags;
		i.Bind = desc->BindFlags;
		const HRESULT hr = MakeImage(i, data, "texture1D");
		if (FAILED(hr)) { t->Release(); return hr; }
		*out = t;
		return S_OK;
	}

	HRESULT Dev::CreateTexture2D(const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture2D** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		auto* t = new Tex2D(this);
		t->Desc = *desc;
		Image& i = t->I;
		i.Dxgi = desc->Format;
		i.Fmt = VkMap::FromDxgi(desc->Format, (desc->BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0);
		i.Type = VK_IMAGE_TYPE_2D;
		i.Width = desc->Width;
		i.Height = desc->Height;
		i.Layers = (std::max)(1u, desc->ArraySize);
		i.Mips = desc->MipLevels ? desc->MipLevels : FullMips(desc->Width, desc->Height, 1);
		t->Desc.MipLevels = i.Mips;
		i.Samples = (std::max)(1u, desc->SampleDesc.Count);
		i.Cube = (desc->MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0 && i.Layers >= 6;
		i.Usage = desc->Usage;
		i.CpuAccess = desc->CPUAccessFlags;
		i.Bind = desc->BindFlags;
		const HRESULT hr = MakeImage(i, data, "texture2D");
		if (FAILED(hr)) { t->Release(); return hr; }
		*out = t;
		return S_OK;
	}

	HRESULT Dev::CreateTexture3D(const D3D11_TEXTURE3D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture3D** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		auto* t = new Tex3D(this);
		t->Desc = *desc;
		Image& i = t->I;
		i.Dxgi = desc->Format;
		i.Fmt = VkMap::FromDxgi(desc->Format, false);
		i.Type = VK_IMAGE_TYPE_3D;
		i.Width = desc->Width;
		i.Height = desc->Height;
		i.Depth = desc->Depth;
		i.Mips = desc->MipLevels ? desc->MipLevels : FullMips(desc->Width, desc->Height, desc->Depth);
		t->Desc.MipLevels = i.Mips;
		i.Usage = desc->Usage;
		i.CpuAccess = desc->CPUAccessFlags;
		i.Bind = desc->BindFlags;
		const HRESULT hr = MakeImage(i, data, "texture3D");
		if (FAILED(hr)) { t->Release(); return hr; }
		*out = t;
		return S_OK;
	}

	HRESULT Dev::CreateShaderResourceView(GfxResource* r, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc, GfxShaderResourceView** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		if (Buf* b = BufOf(r))
		{
			// 구조 · raw 버퍼 SRV → 스토리지 버퍼 (셰이더는 읽기만). 형식 버퍼 (Buffer<float4> — texel buffer) 는 아직
			VkDeviceSize first = 0, count = 0, stride = b->Desc.StructureByteStride;
			if (desc && desc->ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX && (desc->BufferEx.Flags & D3D11_BUFFEREX_SRV_FLAG_RAW))
			{
				stride = 4;
				first = desc->BufferEx.FirstElement;
				count = desc->BufferEx.NumElements;
			}
			else if (desc && desc->ViewDimension == D3D11_SRV_DIMENSION_BUFFER && (b->Desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) && stride)
			{
				first = desc->Buffer.FirstElement;
				count = desc->Buffer.NumElements;
			}
			else
			{
				Once("srv-buffer", "%s", "typed buffer shader resource views are not supported (structured / raw only)");
				return E_NOTIMPL;
			}
			if (!stride || !count || (first + count) * stride > b->Desc.ByteWidth) return E_INVALIDARG;
			auto* v = new Srv(this);
			if (desc) v->Dsc = *desc;
			v->V.Res = r;
			v->V.Buffer = true;
			v->V.BufOffset = first * stride;
			v->V.BufSize = count * stride;
			v->V.Id = NextId();
			*out = v;
			return S_OK;
		}
		Image* t = ImageOf(r);
		if (!t || !t->Handle)
		{
			Once("srv-staging", "%s", "staging texture shader resource views are not supported");
			return E_NOTIMPL;
		}
		D3D11_SHADER_RESOURCE_VIEW_DESC d = {};
		if (desc) d = *desc;
		else
		{
			d.Format = t->Dxgi;
			if (t->Type == VK_IMAGE_TYPE_1D) { d.ViewDimension = t->Layers > 1 ? D3D11_SRV_DIMENSION_TEXTURE1DARRAY : D3D11_SRV_DIMENSION_TEXTURE1D; d.Texture1DArray.MipLevels = t->Mips; d.Texture1DArray.ArraySize = t->Layers; }
			else if (t->Type == VK_IMAGE_TYPE_3D) { d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D; d.Texture3D.MipLevels = t->Mips; }
			else if (t->Samples > 1) d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
			else if (t->Cube && t->Layers == 6) { d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE; d.TextureCube.MipLevels = t->Mips; }
			else if (t->Cube) { d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY; d.TextureCubeArray.MipLevels = t->Mips; d.TextureCubeArray.NumCubes = t->Layers / 6; }
			else if (t->Layers > 1) { d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY; d.Texture2DArray.MipLevels = t->Mips; d.Texture2DArray.ArraySize = t->Layers; }
			else { d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; d.Texture2D.MipLevels = t->Mips; }
		}
		auto mips = [&](UINT most, UINT count) { return count == (UINT)-1 ? t->Mips - (std::min)(most, t->Mips) : count; };
		VkImageViewType type;
		UINT minLevel = 0, numLevels = 1, minLayer = 0, numLayers = 1;
		switch (d.ViewDimension)
		{
		case D3D11_SRV_DIMENSION_TEXTURE1D: type = VK_IMAGE_VIEW_TYPE_1D; minLevel = d.Texture1D.MostDetailedMip; numLevels = mips(minLevel, d.Texture1D.MipLevels); break;
		case D3D11_SRV_DIMENSION_TEXTURE1DARRAY: type = VK_IMAGE_VIEW_TYPE_1D_ARRAY; minLevel = d.Texture1DArray.MostDetailedMip; numLevels = mips(minLevel, d.Texture1DArray.MipLevels);
			minLayer = d.Texture1DArray.FirstArraySlice; numLayers = d.Texture1DArray.ArraySize; break;
		case D3D11_SRV_DIMENSION_TEXTURE2D: type = VK_IMAGE_VIEW_TYPE_2D; minLevel = d.Texture2D.MostDetailedMip; numLevels = mips(minLevel, d.Texture2D.MipLevels); break;
		case D3D11_SRV_DIMENSION_TEXTURE2DARRAY: type = VK_IMAGE_VIEW_TYPE_2D_ARRAY; minLevel = d.Texture2DArray.MostDetailedMip; numLevels = mips(minLevel, d.Texture2DArray.MipLevels);
			minLayer = d.Texture2DArray.FirstArraySlice; numLayers = d.Texture2DArray.ArraySize; break;
		case D3D11_SRV_DIMENSION_TEXTURECUBE: type = VK_IMAGE_VIEW_TYPE_CUBE; minLevel = d.TextureCube.MostDetailedMip; numLevels = mips(minLevel, d.TextureCube.MipLevels); numLayers = 6; break;
		case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY: type = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY; minLevel = d.TextureCubeArray.MostDetailedMip; numLevels = mips(minLevel, d.TextureCubeArray.MipLevels);
			minLayer = d.TextureCubeArray.First2DArrayFace; numLayers = d.TextureCubeArray.NumCubes * 6; break;
		case D3D11_SRV_DIMENSION_TEXTURE3D: type = VK_IMAGE_VIEW_TYPE_3D; minLevel = d.Texture3D.MostDetailedMip; numLevels = mips(minLevel, d.Texture3D.MipLevels); break;
		case D3D11_SRV_DIMENSION_TEXTURE2DMS: type = VK_IMAGE_VIEW_TYPE_2D; break;
		case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY: type = VK_IMAGE_VIEW_TYPE_2D_ARRAY; minLayer = d.Texture2DMSArray.FirstArraySlice; numLayers = d.Texture2DMSArray.ArraySize; break;
		default:
			Once("srv-dim", "%s", "unsupported shader resource view dimension");
			return E_NOTIMPL;
		}
		numLevels = (std::min)(numLevels, t->Mips - (std::min)(minLevel, t->Mips));
		numLayers = (std::min)(numLayers, t->Layers - (std::min)(minLayer, t->Layers));
		if (!numLevels || !numLayers) return E_INVALIDARG;
		auto* v = new Srv(this);
		v->Dsc = d;
		v->V.Res = r;
		v->V.Img = t;
		v->V.Type = type;
		v->V.BaseMip = minLevel;
		v->V.Mips = numLevels;
		v->V.BaseLayer = minLayer;
		v->V.Layers = numLayers;
		VkComponentMapping swizzle = {};
		if (t->Fmt.Depth)
		{
			// 깊이 텍스처: 뷰 형식 = 이미지 형식, 면 하나 (R24_UNORM_X8 · R32_FLOAT = 깊이, X24_G8 · X32_G8 = 스텐실)
			v->V.Format = t->Format;
			v->V.Aspect = (d.Format == DXGI_FORMAT_X24_TYPELESS_G8_UINT || d.Format == DXGI_FORMAT_X32_TYPELESS_G8X24_UINT) ? VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		else
		{
			const VkMap::Format vf = VkMap::FromDxgi(d.Format == DXGI_FORMAT_UNKNOWN ? t->Dxgi : d.Format, false);
			v->V.Format = vf.Vk != VK_FORMAT_UNDEFINED ? vf.Vk : t->Format;
			v->V.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
			swizzle = vf.Swizzle;
		}
		v->V.View = MakeView(*t, type, v->V.Format, v->V.Aspect, minLevel, numLevels, minLayer, numLayers, &swizzle);
		v->V.Id = NextId();
		if (!v->V.View) { v->Release(); return E_FAIL; }
		*out = v;
		return S_OK;
	}

	HRESULT Dev::CreateRenderTargetView(GfxResource* r, const D3D11_RENDER_TARGET_VIEW_DESC* desc, GfxRenderTargetView** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		Image* t = ImageOf(r);
		if (!t || !t->Handle || t->Fmt.Depth) return E_INVALIDARG;
		auto* v = new Rtv(this);
		UINT mip = 0, first = 0, count = t->Type == VK_IMAGE_TYPE_3D ? t->Depth : t->Layers;
		if (desc)
		{
			v->Dsc = *desc;
			switch (desc->ViewDimension)
			{
			case D3D11_RTV_DIMENSION_TEXTURE1D: mip = desc->Texture1D.MipSlice; count = 1; break;
			case D3D11_RTV_DIMENSION_TEXTURE1DARRAY: mip = desc->Texture1DArray.MipSlice; first = desc->Texture1DArray.FirstArraySlice; count = desc->Texture1DArray.ArraySize; break;
			case D3D11_RTV_DIMENSION_TEXTURE2D: mip = desc->Texture2D.MipSlice; count = 1; break;
			case D3D11_RTV_DIMENSION_TEXTURE2DARRAY: mip = desc->Texture2DArray.MipSlice; first = desc->Texture2DArray.FirstArraySlice; count = desc->Texture2DArray.ArraySize; break;
			case D3D11_RTV_DIMENSION_TEXTURE2DMS: count = 1; break;
			case D3D11_RTV_DIMENSION_TEXTURE3D: mip = desc->Texture3D.MipSlice; first = desc->Texture3D.FirstWSlice; count = desc->Texture3D.WSize; break;
			default: break;
			}
		}
		else
		{
			v->Dsc.Format = t->Dxgi;
			v->Dsc.ViewDimension = t->Layers > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY : D3D11_RTV_DIMENSION_TEXTURE2D;
			v->Dsc.Texture2DArray.ArraySize = t->Layers;
		}
		const UINT total = t->Type == VK_IMAGE_TYPE_3D ? (std::max)(1u, t->MipD(mip)) : t->Layers;
		first = (std::min)(first, total - 1);
		count = (std::min)(count == (UINT)-1 ? total : count, total - first);
		const VkMap::Format vf = VkMap::FromDxgi(v->Dsc.Format == DXGI_FORMAT_UNKNOWN ? t->Dxgi : v->Dsc.Format, false);
		v->V.Res = r;
		v->V.Img = t;
		v->V.Format = vf.Vk != VK_FORMAT_UNDEFINED ? vf.Vk : t->Format;
		v->V.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		v->V.BaseMip = mip;
		v->V.Mips = 1;
		v->V.BaseLayer = t->Type == VK_IMAGE_TYPE_3D ? 0 : first;
		v->V.Layers = t->Type == VK_IMAGE_TYPE_3D ? 1 : (std::max)(1u, count);
		v->V.Type = v->V.Layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : (t->Type == VK_IMAGE_TYPE_1D ? VK_IMAGE_VIEW_TYPE_1D : VK_IMAGE_VIEW_TYPE_2D);
		if (t->Type == VK_IMAGE_TYPE_3D)
		{
			// 3D 의 깊이 조각들 = 2D 배열 뷰 (2D_ARRAY_COMPATIBLE)
			v->V.Type = count > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
			v->V.View = MakeView(*t, v->V.Type, v->V.Format, VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, first, (std::max)(1u, count));
		}
		else
			v->V.View = MakeView(*t, v->V.Type, v->V.Format, VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, v->V.BaseLayer, v->V.Layers);
		v->V.Id = NextId();
		if (!v->V.View) { v->Release(); return E_FAIL; }
		*out = v;
		return S_OK;
	}

	HRESULT Dev::CreateDepthStencilView(GfxResource* r, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, GfxDepthStencilView** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		Image* t = ImageOf(r);
		if (!t || !t->Handle || !t->Fmt.Depth) return E_INVALIDARG;
		auto* v = new Dsv(this);
		UINT mip = 0, first = 0, count = t->Layers;
		if (desc)
		{
			v->Dsc = *desc;
			switch (desc->ViewDimension)
			{
			case D3D11_DSV_DIMENSION_TEXTURE1D: mip = desc->Texture1D.MipSlice; count = 1; break;
			case D3D11_DSV_DIMENSION_TEXTURE1DARRAY: mip = desc->Texture1DArray.MipSlice; first = desc->Texture1DArray.FirstArraySlice; count = desc->Texture1DArray.ArraySize; break;
			case D3D11_DSV_DIMENSION_TEXTURE2D: mip = desc->Texture2D.MipSlice; count = 1; break;
			case D3D11_DSV_DIMENSION_TEXTURE2DARRAY: mip = desc->Texture2DArray.MipSlice; first = desc->Texture2DArray.FirstArraySlice; count = desc->Texture2DArray.ArraySize; break;
			case D3D11_DSV_DIMENSION_TEXTURE2DMS: count = 1; break;
			default: break;
			}
		}
		else
		{
			v->Dsc.Format = t->Dxgi;
			v->Dsc.ViewDimension = t->Layers > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_DSV_DIMENSION_TEXTURE2D;
			v->Dsc.Texture2DArray.ArraySize = t->Layers;
		}
		first = (std::min)(first, t->Layers - 1);
		count = (std::min)(count == (UINT)-1 ? t->Layers : (std::max)(1u, count), t->Layers - first);
		v->V.Res = r;
		v->V.Img = t;
		v->V.Format = t->Format;
		v->V.Aspect = t->Fmt.Aspect;
		v->V.BaseMip = mip;
		v->V.Mips = 1;
		v->V.BaseLayer = first;
		v->V.Layers = count;
		v->V.ReadOnlyDepth = (v->Dsc.Flags & (D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL)) != 0;
		v->V.Type = count > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : (t->Type == VK_IMAGE_TYPE_1D ? VK_IMAGE_VIEW_TYPE_1D : VK_IMAGE_VIEW_TYPE_2D);
		v->V.View = MakeView(*t, v->V.Type, v->V.Format, v->V.Aspect, mip, 1, first, count);
		v->V.Id = NextId();
		if (!v->V.View) { v->Release(); return E_FAIL; }
		*out = v;
		return S_OK;
	}

	HRESULT Dev::CreateUnorderedAccessView(GfxResource* r, const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc, GfxUnorderedAccessView** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		if (Buf* b = BufOf(r))
		{
			// 구조 · raw 버퍼 UAV → 스토리지 버퍼 (append · counter 는 없다)
			if (!b->Buffer || !(b->Desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)) return E_INVALIDARG;
			if (!desc || desc->ViewDimension != D3D11_UAV_DIMENSION_BUFFER ||
				(desc->Buffer.Flags & (D3D11_BUFFER_UAV_FLAG_APPEND | D3D11_BUFFER_UAV_FLAG_COUNTER)))
			{
				Once("uav-buffer", "%s", "only structured / raw buffer UAVs without append or counter are supported");
				return E_NOTIMPL;
			}
			const VkDeviceSize stride = (desc->Buffer.Flags & D3D11_BUFFER_UAV_FLAG_RAW) ? 4 : b->Desc.StructureByteStride;
			const VkDeviceSize first = desc->Buffer.FirstElement, count = desc->Buffer.NumElements;
			if (!stride || !count || (first + count) * stride > b->Desc.ByteWidth) return E_INVALIDARG;
			auto* v = new Uav(this);
			v->Dsc = *desc;
			v->V.Res = r;
			v->V.Buffer = true;
			v->V.BufOffset = first * stride;
			v->V.BufSize = count * stride;
			v->V.Id = NextId();
			*out = v;
			return S_OK;
		}
		// 텍스처 UAV → 스토리지 이미지 (밉 하나, 2D · 2D 배열)
		Image* t = ImageOf(r);
		if (!t || !t->Handle || !(t->Bind & D3D11_BIND_UNORDERED_ACCESS) || t->Fmt.Depth) return E_INVALIDARG;
		UINT mip = 0, first = 0, count = 1;
		VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
		const D3D11_UAV_DIMENSION dim = desc ? desc->ViewDimension : (t->Layers > 1 ? D3D11_UAV_DIMENSION_TEXTURE2DARRAY : D3D11_UAV_DIMENSION_TEXTURE2D);
		switch (dim)
		{
		case D3D11_UAV_DIMENSION_TEXTURE2D: mip = desc ? desc->Texture2D.MipSlice : 0; break;
		case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
			mip = desc ? desc->Texture2DArray.MipSlice : 0;
			first = desc ? desc->Texture2DArray.FirstArraySlice : 0;
			count = desc ? desc->Texture2DArray.ArraySize : t->Layers;
			type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			break;
		default:
			Once("uav-dim", "%s", "only 2D / 2D array texture UAVs are supported");
			return E_NOTIMPL;
		}
		if (t->Type != VK_IMAGE_TYPE_2D || mip >= t->Mips || first >= t->Layers) return E_INVALIDARG;
		count = (std::min)(count == (UINT)-1 ? t->Layers : (std::max)(1u, count), t->Layers - first);
		auto* v = new Uav(this);
		if (desc) v->Dsc = *desc;
		else { v->Dsc.Format = t->Dxgi; v->Dsc.ViewDimension = dim; }
		const VkMap::Format vf = VkMap::FromDxgi(v->Dsc.Format == DXGI_FORMAT_UNKNOWN ? t->Dxgi : v->Dsc.Format, false);
		v->V.Res = r;
		v->V.Img = t;
		v->V.Type = type;
		v->V.Format = vf.Vk != VK_FORMAT_UNDEFINED ? vf.Vk : t->Format;
		v->V.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		v->V.BaseMip = mip;
		v->V.Mips = 1;
		v->V.BaseLayer = first;
		v->V.Layers = count;
		v->V.View = MakeView(*t, type, v->V.Format, VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, first, count);
		v->V.Id = NextId();
		if (!v->V.View) { v->Release(); return E_FAIL; }
		*out = v;
		return S_OK;
	}

	HRESULT Dev::CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* elements, UINT count, const void* signature, SIZE_T signatureSize, GfxInputLayout** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		const auto* sig = static_cast<const GLInputSignature*>(signature);
		if (!sig || signatureSize != sizeof(GLInputSignature) || sig->Magic != GLInputSignature::kMagic || !sig->Inputs)
		{
			Once("layout-sig", "%s", "CreateInputLayout needs a pass signature from FxPass::GetDesc");
			return E_INVALIDARG;
		}
		auto upper = [](std::string s) { for (char& c : s) c = (char)toupper((unsigned char)c); return s; };
		auto* l = new InputLayout(this);
		l->Id = NextId();
		UINT offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
		std::set<std::string> provided;
		for (UINT i = 0; i < count; ++i)
		{
			const D3D11_INPUT_ELEMENT_DESC& e = elements[i];
			const VkMap::Format f = VkMap::FromDxgi(e.Format, false);
			if (f.Vk == VK_FORMAT_UNDEFINED || f.Compressed || e.InputSlot >= D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
			{
				Once(std::string("vfmt:") + e.SemanticName, "unsupported vertex format for %s", e.SemanticName);
				l->Release();
				return E_INVALIDARG;
			}
			InputLayout::Element el;
			el.Base = upper(e.SemanticName);
			el.Index = e.SemanticIndex;
			el.Semantic = el.Base + std::to_string(e.SemanticIndex);
			el.Slot = e.InputSlot;
			el.Format = f.Vk;
			el.Offset = e.AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT ? offsets[e.InputSlot] : e.AlignedByteOffset;
			offsets[e.InputSlot] = el.Offset + f.Bytes;
			provided.insert(el.Semantic);
			l->PerInstance[e.InputSlot] = e.InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA;
			if (e.InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA && e.InstanceDataStepRate > 1)
				Once("step-rate", "%s", "instance data step rate > 1 is not supported (treated as 1)");
			l->SlotMask |= 1u << e.InputSlot;
			l->Elements.push_back(el);
		}
		for (const auto& [s, loc] : *sig->Inputs)
		{
			if (provided.count(s) || s.rfind("SV_", 0) == 0) continue;
			// 행렬 입력 (WORLD0 → WORLD0..3): 첫 원소가 있으면 된다
			Once("missing:" + s, "vertex input %s is not provided by the layout", s.c_str());
			l->Release();
			return E_INVALIDARG;
		}
		*out = l;
		return S_OK;
	}

	HRESULT Dev::CreateRasterizerState(const D3D11_RASTERIZER_DESC* desc, GfxRasterizerState** out)
	{
		auto* s = new Rasterizer(this);
		s->Dsc = *desc;
		s->Hash = HashRasterizer(*desc);
		*out = s;
		return S_OK;
	}

	HRESULT Dev::CreateBlendState(const D3D11_BLEND_DESC* desc, GfxBlendState** out)
	{
		auto* s = new Blend(this);
		s->Dsc = *desc;
		s->Hash = HashBlend(*desc);
		*out = s;
		return S_OK;
	}

	HRESULT Dev::CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC* desc, GfxDepthStencilState** out)
	{
		auto* s = new DepthStencil(this);
		s->Dsc = *desc;
		s->Hash = HashDepthStencil(*desc);
		*out = s;
		return S_OK;
	}

	HRESULT Dev::CreateSamplerState(const D3D11_SAMPLER_DESC* desc, GfxSamplerState** out)
	{
		*out = nullptr;
		auto* s = new Sampler(this);
		s->Dsc = *desc;
		const VkSamplerCreateInfo ci = VkMap::Sampler(*desc, Features.samplerAnisotropy ? Props.limits.maxSamplerAnisotropy : 1.0f, MirrorClamp);
		if (!Check(vkCreateSampler(Device, &ci, nullptr, &s->Handle), "vkCreateSampler"))
		{
			s->Handle = VK_NULL_HANDLE;
			s->Release();
			return E_FAIL;
		}
		s->Id = NextId();
		*out = s;
		return S_OK;
	}

	HRESULT Dev::CreateQuery(const D3D11_QUERY_DESC* desc, GfxQuery** out)
	{
		auto* q = new Query(this);
		q->Dsc = *desc;
		const bool occlusion = desc->Query == D3D11_QUERY_OCCLUSION || desc->Query == D3D11_QUERY_OCCLUSION_PREDICATE;
		if (desc->Query == D3D11_QUERY_PIPELINE_STATISTICS && Features.pipelineStatisticsQuery)
		{
			// 자르기 뒤 삼각형 (D3D 의 CPrimitives) · 픽셀 셰이더 실행 — 결과는 비트 순서 (CLIPPING_PRIMITIVES 0x40, FRAGMENT 0x80)
			VkQueryPoolCreateInfo ci = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
			ci.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
			ci.queryCount = Query::kStatSegs;
			ci.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT | VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
			if (vkCreateQueryPool(Device, &ci, nullptr, &q->Pool) != VK_SUCCESS) q->Pool = VK_NULL_HANDLE;
			else vkResetQueryPool(Device, q->Pool, 0, ci.queryCount);
		}
		else if (desc->Query == D3D11_QUERY_TIMESTAMP || desc->Query == D3D11_QUERY_TIMESTAMP_DISJOINT || occlusion)
		{
			// 오클루전 = 칸 고리 (Query::kSlots), 타임스탬프 = 칸 하나
			VkQueryPoolCreateInfo ci = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
			ci.queryType = occlusion ? VK_QUERY_TYPE_OCCLUSION : VK_QUERY_TYPE_TIMESTAMP;
			ci.queryCount = occlusion ? Query::kSlots : 1;
			if (vkCreateQueryPool(Device, &ci, nullptr, &q->Pool) != VK_SUCCESS) q->Pool = VK_NULL_HANDLE;
			else vkResetQueryPool(Device, q->Pool, 0, ci.queryCount);
		}
		*out = q;
		return S_OK;
	}

	void Dev::GetImmediateContext(GfxContext** out)
	{
		if (!Immediate)
		{
			auto* c = new Ctx();
			c->D = this;
			Immediate = c;
			*out = c;
			return;
		}
		Immediate->AddRef();
		*out = Immediate;
	}

	// ============================================================ 파이프라인
	namespace
	{
		bool IsIntegerFormat(VkFormat f)
		{
			switch (f)
			{
			case VK_FORMAT_R8_UINT: case VK_FORMAT_R8_SINT: case VK_FORMAT_R8G8_UINT: case VK_FORMAT_R8G8_SINT:
			case VK_FORMAT_R8G8B8A8_UINT: case VK_FORMAT_R8G8B8A8_SINT: case VK_FORMAT_R16_UINT: case VK_FORMAT_R16_SINT:
			case VK_FORMAT_R16G16_UINT: case VK_FORMAT_R16G16_SINT: case VK_FORMAT_R16G16B16A16_UINT: case VK_FORMAT_R16G16B16A16_SINT:
			case VK_FORMAT_R32_UINT: case VK_FORMAT_R32_SINT: case VK_FORMAT_R32G32_UINT: case VK_FORMAT_R32G32_SINT:
			case VK_FORMAT_R32G32B32_UINT: case VK_FORMAT_R32G32B32_SINT: case VK_FORMAT_R32G32B32A32_UINT: case VK_FORMAT_R32G32B32A32_SINT:
			case VK_FORMAT_A2B10G10R10_UINT_PACK32:
				return true;
			default: return false;
			}
		}
		bool HasStencil(VkFormat f) { return f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT || f == VK_FORMAT_D16_UNORM_S8_UINT || f == VK_FORMAT_S8_UINT; }
		bool HasDepth(VkFormat f) { return f != VK_FORMAT_UNDEFINED && f != VK_FORMAT_S8_UINT; }
		bool IsDualSource(D3D11_BLEND b) { return b == D3D11_BLEND_SRC1_COLOR || b == D3D11_BLEND_INV_SRC1_COLOR || b == D3D11_BLEND_SRC1_ALPHA || b == D3D11_BLEND_INV_SRC1_ALPHA; }
	}

	VkPipeline Dev::GetPipeline(const PipelineKey& key, Program* program, InputLayout* layout, const D3D11_RASTERIZER_DESC& rs, const D3D11_BLEND_DESC& bs,
		const D3D11_DEPTH_STENCIL_DESC& ds)
	{
		auto it = Pipelines.find(key);
		if (it != Pipelines.end()) return it->second;
		const auto t0 = std::chrono::steady_clock::now();

		std::vector<VkPipelineShaderStageCreateInfo> stages;
		for (const auto& s : program->Stages)
		{
			VkPipelineShaderStageCreateInfo si = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			si.stage = s.Flag;
			si.module = s.Module;
			si.pName = s.Entry.c_str();
			stages.push_back(si);
		}

		// 정점 입력: 배치의 의미 → 이 프로그램의 location (D3D 와 같이 셰이더가 쓰지 않는 요소는 버린다)
		std::vector<VkVertexInputBindingDescription> bindings;
		std::vector<VkVertexInputAttributeDescription> attributes;
		auto location = [&](const std::string& sem) -> int {
			for (const auto& [s, l] : program->VertexInputs)
				if (s == sem) return l;
			return -1;
		};
		if (layout)
		{
			for (UINT slot = 0; slot < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++slot)
				if (layout->SlotMask & (1u << slot))
					bindings.push_back({ slot, 0, layout->PerInstance[slot] ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX });
			for (const auto& e : layout->Elements)
			{
				int loc = location(e.Semantic);
				if (loc < 0 && e.Index > 0)
				{
					const int first = location(e.Base + "0");
					if (first >= 0) loc = first + (int)e.Index;   // 행렬 입력 (WORLD0..3)
				}
				if (loc < 0) continue;
				attributes.push_back({ (uint32_t)loc, e.Slot, e.Format, e.Offset });
			}
		}
		for (const auto& [s, l] : program->VertexInputs)
		{
			bool found = false;
			for (const auto& a : attributes)
				if (a.location == (uint32_t)l) found = true;
			if (!found && s.rfind("SV_", 0) != 0)
				Once("vin:" + program->Name + ":" + s, "%s", (program->Name + ": vertex input " + s + " is not in the input layout").c_str());
		}
		VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		vi.vertexBindingDescriptionCount = (uint32_t)bindings.size();
		vi.pVertexBindingDescriptions = bindings.data();
		vi.vertexAttributeDescriptionCount = (uint32_t)attributes.size();
		vi.pVertexAttributeDescriptions = attributes.data();

		VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		ia.topology = (VkPrimitiveTopology)key.Topology;
		ia.primitiveRestartEnable = key.Strip ? VK_TRUE : VK_FALSE;
		VkPipelineTessellationStateCreateInfo ts = { VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO };
		ts.patchControlPoints = (std::max<uint32_t>)(1, key.PatchPoints);
		VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		vp.viewportCount = vp.scissorCount = (std::max<uint32_t>)(1, key.Viewports);

		VkPipelineRasterizationStateCreateInfo rz = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		rz.depthClampEnable = !rs.DepthClipEnable && Features.depthClamp ? VK_TRUE : VK_FALSE;
		rz.polygonMode = rs.FillMode == D3D11_FILL_WIREFRAME && Features.fillModeNonSolid ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
		rz.cullMode = VkMap::Cull(rs.CullMode);
		rz.frontFace = rs.FrontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
		rz.depthBiasEnable = (rs.DepthBias != 0 || rs.SlopeScaledDepthBias != 0.0f) ? VK_TRUE : VK_FALSE;
		rz.depthBiasConstantFactor = (float)rs.DepthBias;
		rz.depthBiasClamp = Features.depthBiasClamp ? rs.DepthBiasClamp : 0.0f;
		rz.depthBiasSlopeFactor = rs.SlopeScaledDepthBias;
		rz.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		ms.rasterizationSamples = (VkSampleCountFlagBits)(std::max<uint32_t>)(1, key.Samples);
		ms.pSampleMask = &key.SampleMask;
		ms.alphaToCoverageEnable = bs.AlphaToCoverageEnable ? VK_TRUE : VK_FALSE;

		VkPipelineDepthStencilStateCreateInfo dz = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		dz.depthTestEnable = ds.DepthEnable ? VK_TRUE : VK_FALSE;
		dz.depthWriteEnable = ds.DepthEnable && ds.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL && !key.ReadOnlyDepth ? VK_TRUE : VK_FALSE;
		dz.depthCompareOp = VkMap::Compare(ds.DepthFunc);
		dz.stencilTestEnable = ds.StencilEnable && HasStencil(key.DepthFormat) ? VK_TRUE : VK_FALSE;
		auto face = [&](const D3D11_DEPTH_STENCILOP_DESC& f) {
			VkStencilOpState s = {};
			s.failOp = VkMap::StencilOp(f.StencilFailOp);
			s.passOp = VkMap::StencilOp(f.StencilPassOp);
			s.depthFailOp = VkMap::StencilOp(f.StencilDepthFailOp);
			s.compareOp = VkMap::Compare(f.StencilFunc);
			s.compareMask = ds.StencilReadMask;
			s.writeMask = key.ReadOnlyDepth ? 0 : ds.StencilWriteMask;
			return s;
		};
		dz.front = face(ds.FrontFace);
		dz.back = face(ds.BackFace);
		dz.minDepthBounds = 0.0f;
		dz.maxDepthBounds = 1.0f;

		VkPipelineColorBlendAttachmentState att[8] = {};
		for (uint32_t i = 0; i < key.ColorCount; ++i)
		{
			const D3D11_RENDER_TARGET_BLEND_DESC& b = bs.RenderTarget[bs.IndependentBlendEnable ? i : 0];
			VkPipelineColorBlendAttachmentState& a = att[i];
			const bool used = key.Colors[i] != VK_FORMAT_UNDEFINED && (program->PixelOutputs & (1u << i));
			const bool dual = IsDualSource(b.SrcBlend) || IsDualSource(b.DestBlend) || IsDualSource(b.SrcBlendAlpha) || IsDualSource(b.DestBlendAlpha);
			a.blendEnable = used && b.BlendEnable && !IsIntegerFormat(key.Colors[i]) && (!dual || Features.dualSrcBlend) ? VK_TRUE : VK_FALSE;
			a.srcColorBlendFactor = VkMap::BlendFactor(b.SrcBlend, false);
			a.dstColorBlendFactor = VkMap::BlendFactor(b.DestBlend, false);
			a.colorBlendOp = VkMap::BlendOp(b.BlendOp);
			a.srcAlphaBlendFactor = VkMap::BlendFactor(b.SrcBlendAlpha, true);
			a.dstAlphaBlendFactor = VkMap::BlendFactor(b.DestBlendAlpha, true);
			a.alphaBlendOp = VkMap::BlendOp(b.BlendOpAlpha);
			// 픽셀 셰이더가 쓰지 않는 타깃은 그대로 둔다 (Vulkan 은 쓰지 않은 출력의 값이 정해지지 않음)
			a.colorWriteMask = used ? (VkColorComponentFlags)(b.RenderTargetWriteMask & 0xF) : 0;
		}
		VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		cb.attachmentCount = key.ColorCount;
		cb.pAttachments = att;

		std::vector<VkDynamicState> dyn = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS, VK_DYNAMIC_STATE_STENCIL_REFERENCE };
		if (!bindings.empty()) dyn.push_back(VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE);
		VkPipelineDynamicStateCreateInfo dy = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dy.dynamicStateCount = (uint32_t)dyn.size();
		dy.pDynamicStates = dyn.data();

		VkPipelineRenderingCreateInfo ri = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		ri.colorAttachmentCount = key.ColorCount;
		ri.pColorAttachmentFormats = key.Colors;
		ri.depthAttachmentFormat = HasDepth(key.DepthFormat) ? key.DepthFormat : VK_FORMAT_UNDEFINED;
		ri.stencilAttachmentFormat = HasStencil(key.DepthFormat) ? key.DepthFormat : VK_FORMAT_UNDEFINED;

		VkGraphicsPipelineCreateInfo pi = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pi.pNext = &ri;
		pi.stageCount = (uint32_t)stages.size();
		pi.pStages = stages.data();
		pi.pVertexInputState = &vi;
		pi.pInputAssemblyState = &ia;
		pi.pTessellationState = program->Tessellation ? &ts : nullptr;
		pi.pViewportState = &vp;
		pi.pRasterizationState = &rz;
		pi.pMultisampleState = &ms;
		pi.pDepthStencilState = &dz;
		pi.pColorBlendState = &cb;
		pi.pDynamicState = &dy;
		pi.layout = program->Layout->PipelineLayout;
		VkPipeline p = VK_NULL_HANDLE;
		const VkResult r = vkCreateGraphicsPipelines(Device, PipelineCache, 1, &pi, nullptr, &p);
		if (r != VK_SUCCESS)
		{
			Once("pipe:" + program->Name, "%s", (program->Name + ": vkCreateGraphicsPipelines failed: " + VkLoader::ResultName(r)).c_str());
			p = VK_NULL_HANDLE;
		}
		Pipelines[key] = p;   // 실패도 기억 (다시 만들지 않는다)
		const double ms2 = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		if (ms2 > 50.0)
			EditorLog::Write("Vulkan", "pipeline %s took %.0f ms (%zu pipelines)", program->Name.c_str(), ms2, Pipelines.size());
		return p;
	}

	void Dev::ForgetProgram(uint64_t id)
	{
		for (auto it = Pipelines.begin(); it != Pipelines.end();)
		{
			if (it->first.Program == id)
			{
				VkPipeline p = it->second;
				if (p) Defer([this, p]() { vkDestroyPipeline(Device, p, nullptr); });
				it = Pipelines.erase(it);
			}
			else ++it;
		}
	}
}

// ============================================================ 효과 쪽 (GfxVkShared)
namespace GfxVkShared
{
	using namespace GfxVkImpl;

	HRESULT CreateBindingLayout(GfxDevice* device, const BindingDesc* bindings, uint32_t count, GfxObject** out, std::string& error)
	{
		*out = nullptr;
		auto* d = static_cast<Dev*>(device);
		auto* l = new BindingLayout(d);
		l->Bindings.assign(bindings, bindings + count);
		uint32_t ubos = 0;
		for (uint32_t i = 0; i < count; ++i)
		{
			if (bindings[i].Type == BindingType::UniformBuffer) ubos += bindings[i].Count;
			if (bindings[i].Type == BindingType::StorageBuffer) l->HasStorage = true;
		}
		l->DynamicUbo = ubos <= d->Props.limits.maxDescriptorSetUniformBuffersDynamic;
		std::vector<VkDescriptorSetLayoutBinding> lb;
		for (uint32_t i = 0; i < count; ++i)
		{
			l->Offset.push_back(l->Elements);
			l->Elements += bindings[i].Count;
			VkDescriptorSetLayoutBinding b = {};
			b.binding = i;
			b.descriptorCount = bindings[i].Count;
			b.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT;
			switch (bindings[i].Type)
			{
			case BindingType::UniformBuffer: b.descriptorType = l->DynamicUbo ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; break;
			case BindingType::SampledImage: b.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; break;
			case BindingType::Sampler: b.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER; break;
			case BindingType::StorageBuffer: b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; break;
			case BindingType::StorageImage: b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; break;
			default: continue;   // 지원하지 않는 자원 (그것을 쓰는 pass 는 효과가 막는다)
			}
			lb.push_back(b);
		}
		VkDescriptorSetLayoutCreateInfo ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		ci.bindingCount = (uint32_t)lb.size();
		ci.pBindings = lb.data();
		VkResult r = vkCreateDescriptorSetLayout(d->Device, &ci, nullptr, &l->SetLayout);
		if (r == VK_SUCCESS)
		{
			VkPipelineLayoutCreateInfo pi = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
			pi.setLayoutCount = 1;
			pi.pSetLayouts = &l->SetLayout;
			r = vkCreatePipelineLayout(d->Device, &pi, nullptr, &l->PipelineLayout);
		}
		if (r != VK_SUCCESS)
		{
			error = std::string("descriptor set layout: ") + VkLoader::ResultName(r);
			l->Release();
			return E_FAIL;
		}
		l->Id = NextId();
		*out = l;
		return S_OK;
	}

	uint32_t ElementCount(GfxObject* layout) { return static_cast<BindingLayout*>(layout)->Elements; }
	uint32_t ElementOffset(GfxObject* layout, uint32_t binding)
	{
		auto* l = static_cast<BindingLayout*>(layout);
		return binding < l->Offset.size() ? l->Offset[binding] : 0;
	}

	HRESULT CreateProgram(GfxDevice* device, GfxObject* layout, const StageCode* stages, uint32_t count,
		const std::vector<std::pair<std::string, int>>& vertexInputs, uint32_t pixelOutputs, const std::vector<int>& usedBindings,
		const std::string& name, GfxObject** out, std::string& error)
	{
		*out = nullptr;
		auto* d = static_cast<Dev*>(device);
		auto* p = new Program(d);
		p->Layout = static_cast<BindingLayout*>(layout);
		p->Used.assign(p->Layout->Bindings.size(), false);
		for (int b : usedBindings)
			if (b >= 0 && b < (int)p->Used.size()) p->Used[b] = true;
		p->VertexInputs = vertexInputs;
		p->PixelOutputs = pixelOutputs;
		p->Name = name;
		p->Id = NextId();
		for (uint32_t i = 0; i < count; ++i)
		{
			const StageCode& s = stages[i];
			if ((s.Stage == VK_SHADER_STAGE_GEOMETRY_BIT && !d->Features.geometryShader) ||
				((s.Stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT || s.Stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) && !d->Features.tessellationShader))
			{
				error = "geometry / tessellation shaders are not supported by this GPU";
				p->Release();
				return E_NOTIMPL;
			}
			if (s.Stage == VK_SHADER_STAGE_COMPUTE_BIT && count != 1)
			{
				error = "a compute pass must have only the compute shader";
				p->Release();
				return E_INVALIDARG;
			}
			VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
			ci.codeSize = s.Code->size() * 4;
			ci.pCode = s.Code->data();
			VkShaderModule m = VK_NULL_HANDLE;
			const VkResult r = vkCreateShaderModule(d->Device, &ci, nullptr, &m);
			if (r != VK_SUCCESS)
			{
				error = std::string("vkCreateShaderModule: ") + VkLoader::ResultName(r);
				p->Release();
				return E_FAIL;
			}
			p->Stages.push_back({ s.Stage, m, s.Entry });
			if (s.Stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT || s.Stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
				p->Tessellation = true;
			if (s.Stage == VK_SHADER_STAGE_COMPUTE_BIT)
			{
				// compute 파이프라인 = 셰이더 + 배치뿐 (그리기 상태 · 타깃과 상관없다) → 지금 만든다
				VkComputePipelineCreateInfo pi = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
				pi.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
				pi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
				pi.stage.module = m;
				pi.stage.pName = p->Stages.back().Entry.c_str();
				pi.layout = p->Layout->PipelineLayout;
				const VkResult cr = vkCreateComputePipelines(d->Device, d->PipelineCache, 1, &pi, nullptr, &p->Compute);
				if (cr != VK_SUCCESS)
				{
					p->Compute = VK_NULL_HANDLE;
					error = std::string("vkCreateComputePipelines: ") + VkLoader::ResultName(cr);
					p->Release();
					return E_FAIL;
				}
			}
		}
		*out = p;
		return S_OK;
	}

	bool WriteConstants(GfxDevice* device, const void* data, uint32_t size, RingLoc& loc)
	{
		auto* d = static_cast<Dev*>(device);
		uint8_t* p = d->RingAlloc((size + 15) / 16 * 16, (std::max<VkDeviceSize>)(16, d->Props.limits.minUniformBufferOffsetAlignment), loc);
		if (!p) return false;
		memcpy(p, data, size);
		return true;
	}

	bool IsCurrent(GfxDevice* device, const RingLoc& loc)
	{
		return static_cast<Dev*>(device)->RingCurrent(loc);
	}
}

// ============================================================ 공개 함수
std::unique_ptr<Rhi::Device> CreateVkRhiDevice(GfxDevice* device, GfxContext* context, std::string& error);   // VkRhi.cpp

namespace GfxVk
{
	using namespace GfxVkImpl;

	bool CreateDevice(HWND window, GfxDevice** device, GfxContext** context, std::string& error)
	{
		*device = nullptr;
		*context = nullptr;
		auto* d = new Dev();
		if (!d->Init(window, error))
		{
			EditorLog::Write("Vulkan", "device creation failed: %s", error.c_str());
			d->GfxDevice::Release();
			return false;
		}
		auto* c = new Ctx();
		c->D = d;            // 컨텍스트가 장치를 잡는다
		d->Immediate = c;    // 장치는 약하게
		*device = d;
		*context = c;
		return true;
	}

	std::unique_ptr<Rhi::Device> CreateRhiDevice(GfxDevice* device, GfxContext* context, std::string& error)
	{
		return CreateVkRhiDevice(device, context, error);
	}

	bool IsVulkan(const GfxObject* object) { return object && object->Api() == GfxApi::Vulkan; }

	void Present(GfxDevice* device, GfxTexture2D* backBuffer, int windowWidth, int windowHeight, int syncInterval)
	{
		auto* d = static_cast<Dev*>(device);
		std::string error;
		Dev::Swap* s = d->Window ? d->SwapFor(d->Window, error) : nullptr;
		Tex2D* t = backBuffer && backBuffer->Api() == GfxApi::Vulkan ? static_cast<Tex2D*>(backBuffer) : nullptr;
		if (s) d->PresentFrame(*s, t, windowWidth, windowHeight, syncInterval, true);
		else d->Submit(false);
	}

	bool PresentWindow(GfxDevice* device, HWND window, GfxTexture2D* texture, int width, int height, int syncInterval)
	{
		auto* d = static_cast<Dev*>(device);
		std::string error;
		Dev::Swap* s = d->SwapFor(window, error);
		if (!s)
		{
			d->Once("window-swap:" + error, "%s", ("window swapchain: " + error).c_str());
			return false;
		}
		d->PresentFrame(*s, texture && texture->Api() == GfxApi::Vulkan ? static_cast<Tex2D*>(texture) : nullptr, width, height, syncInterval, false);
		return true;
	}

	void ReleaseWindow(GfxDevice* device, HWND window)
	{
		static_cast<Dev*>(device)->ReleaseWindow(window);
	}

	void WaitIdle(GfxDevice* device)
	{
		auto* d = static_cast<Dev*>(device);
		if (d->Immediate) d->Immediate->FinishAsync();
		d->Submit(true);
		d->WaitCompute(d->ComputeSubmitted);
	}

	bool IsFormatSupported(DXGI_FORMAT format)
	{
		return VkMap::FromDxgi(format, false).Vk != VK_FORMAT_UNDEFINED;
	}

	std::string Description(GfxDevice* device)
	{
		auto* d = static_cast<Dev*>(device);
		char buf[256];
		snprintf(buf, sizeof(buf), "Vulkan %u.%u.%u (%s, driver %u)", VK_API_VERSION_MAJOR(d->Props.apiVersion), VK_API_VERSION_MINOR(d->Props.apiVersion),
			VK_API_VERSION_PATCH(d->Props.apiVersion), d->Props.deviceName, d->Props.driverVersion);
		return buf;
	}

	HRESULT CreateTextureFromImages(GfxDevice* device, const DirectX::Image* images, size_t count, const DirectX::TexMetadata& meta,
		D3D11_USAGE usage, UINT bindFlags, UINT cpuAccess, UINT miscFlags, GfxResource** out)
	{
		*out = nullptr;
		std::vector<D3D11_SUBRESOURCE_DATA> data;
		if (meta.dimension == DirectX::TEX_DIMENSION_TEXTURE3D)
		{
			size_t index = 0;
			for (size_t m = 0; m < meta.mipLevels; ++m)
			{
				if (index >= count) return E_INVALIDARG;
				const DirectX::Image& img = images[index];
				data.push_back({ img.pixels, (UINT)img.rowPitch, (UINT)img.slicePitch });
				index += (std::max<size_t>)(1, meta.depth >> m);
			}
		}
		else
		{
			const size_t subs = meta.arraySize * meta.mipLevels;
			if (count < subs) return E_INVALIDARG;
			for (size_t i = 0; i < subs; ++i)
				data.push_back({ images[i].pixels, (UINT)images[i].rowPitch, (UINT)images[i].slicePitch });
		}
		switch (meta.dimension)
		{
		case DirectX::TEX_DIMENSION_TEXTURE1D:
		{
			D3D11_TEXTURE1D_DESC d = { (UINT)meta.width, (UINT)meta.mipLevels, (UINT)meta.arraySize, meta.format, usage, bindFlags, cpuAccess, miscFlags };
			GfxTexture1D* t = nullptr;
			const HRESULT hr = device->CreateTexture1D(&d, data.data(), &t);
			*out = t;
			return hr;
		}
		case DirectX::TEX_DIMENSION_TEXTURE3D:
		{
			D3D11_TEXTURE3D_DESC d = { (UINT)meta.width, (UINT)meta.height, (UINT)meta.depth, (UINT)meta.mipLevels, meta.format, usage, bindFlags, cpuAccess, miscFlags };
			GfxTexture3D* t = nullptr;
			const HRESULT hr = device->CreateTexture3D(&d, data.data(), &t);
			*out = t;
			return hr;
		}
		default:
		{
			D3D11_TEXTURE2D_DESC d = {};
			d.Width = (UINT)meta.width;
			d.Height = (UINT)meta.height;
			d.MipLevels = (UINT)meta.mipLevels;
			d.ArraySize = (UINT)meta.arraySize;
			d.Format = meta.format;
			d.SampleDesc.Count = 1;
			d.Usage = usage;
			d.BindFlags = bindFlags;
			d.CPUAccessFlags = cpuAccess;
			d.MiscFlags = miscFlags | (meta.IsCubemap() ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0);
			GfxTexture2D* t = nullptr;
			const HRESULT hr = device->CreateTexture2D(&d, data.data(), &t);
			*out = t;
			return hr;
		}
		}
	}
}
