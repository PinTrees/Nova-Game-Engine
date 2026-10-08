#include "pch.h"
#include "GfxD3D12Internal.h"

// Gfx DirectX 12 장치: 제출 · 링 · 디스크립터 · 자원 만들기 · 파이프라인. 컨텍스트(그리기 · 복사)는 GfxD3D12Context.cpp, 창은 GfxD3D12Swapchain.cpp
namespace GfxD3D12Impl
{
	namespace
	{
		std::atomic<uint64_t> s_NextId{ 1 };
		constexpr UINT64 kUploadChunk = 16ull << 20;
		constexpr UINT kViewRingSize = 500000;
		constexpr UINT kSamplerGpuSize = 2048;

		bool Check(HRESULT hr, const char* what, std::string* error = nullptr)
		{
			if (SUCCEEDED(hr)) return true;
			char buf[256];
			snprintf(buf, sizeof(buf), "%s failed: hr=0x%08X", what, (unsigned)hr);
			if (error) *error = buf;
			EditorLog::Write("DX12", "%s", buf);
			return false;
		}

		D3D12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE type)
		{
			D3D12_HEAP_PROPERTIES p = {};
			p.Type = type;
			if (type == D3D12_HEAP_TYPE_CUSTOM)
			{
				// CPU 가 읽고 쓰는 STAGING (리드백과 같은 메모리지만 상태 제한이 없어 복사 원본도 된다)
				p.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
				p.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
			}
			return p;
		}

		D3D12_RESOURCE_DESC BufferDesc(UINT64 size, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
		{
			D3D12_RESOURCE_DESC d = {};
			d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			d.Width = (std::max<UINT64>)(size, 4);
			d.Height = 1;
			d.DepthOrArraySize = 1;
			d.MipLevels = 1;
			d.SampleDesc.Count = 1;
			d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			d.Flags = flags;
			return d;
		}

		UINT FullMips(UINT w, UINT h, UINT d)
		{
			UINT m = 1, s = (std::max)((std::max)(w, h), d);
			while (s > 1) { s >>= 1; ++m; }
			return m;
		}

		// TYPELESS 깊이 텍스처 → DSV 형식 (깊이 형식 그대로면 그대로)
		DXGI_FORMAT DepthViewFormat(DXGI_FORMAT f)
		{
			switch (f)
			{
			case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return DXGI_FORMAT_D24_UNORM_S8_UINT;
			case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
			case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_R32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
			case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_UNORM: return DXGI_FORMAT_D16_UNORM;
			default: return f;
			}
		}

		bool IsTypeless(DXGI_FORMAT f)
		{
			switch (f)
			{
			case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32_TYPELESS: case DXGI_FORMAT_R16G16B16A16_TYPELESS:
			case DXGI_FORMAT_R32G32_TYPELESS: case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R16G16_TYPELESS: case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_R24G8_TYPELESS:
			case DXGI_FORMAT_R8G8_TYPELESS: case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R8_TYPELESS: case DXGI_FORMAT_BC1_TYPELESS:
			case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC5_TYPELESS:
			case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8X8_TYPELESS: case DXGI_FORMAT_BC6H_TYPELESS: case DXGI_FORMAT_BC7_TYPELESS:
				return true;
			default: return false;
			}
		}

		D3D12_SHADER_VISIBILITY Visibility(GfxD3D12Shared::StageType s)
		{
			switch (s)
			{
			case GfxD3D12Shared::StageType::Vertex: return D3D12_SHADER_VISIBILITY_VERTEX;
			case GfxD3D12Shared::StageType::Hull: return D3D12_SHADER_VISIBILITY_HULL;
			case GfxD3D12Shared::StageType::Domain: return D3D12_SHADER_VISIBILITY_DOMAIN;
			case GfxD3D12Shared::StageType::Geometry: return D3D12_SHADER_VISIBILITY_GEOMETRY;
			case GfxD3D12Shared::StageType::Pixel: return D3D12_SHADER_VISIBILITY_PIXEL;
			default: return D3D12_SHADER_VISIBILITY_ALL;
			}
		}

		// 밉 만들기 셰이더 (전체 화면 삼각형 + 선형 축소). D3D12 는 DXBC (SM 5.x) 도 받는다
		const char* kMipShader = R"(
Texture2DArray gSrc : register(t0);
SamplerState gLinear : register(s0);
cbuffer cbMip : register(b0) { uint gSlice; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VS(uint id : SV_VertexID)
{
	VSOut o;
	o.uv = float2((id << 1) & 2, id & 2);
	o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
	return o;
}
float4 PS(VSOut i) : SV_Target { return gSrc.SampleLevel(gLinear, float3(i.uv, gSlice), 0); }
// 3D: 다음 밉의 깊이 조각 하나 = 앞 밉을 그 조각 가운데 w 에서 3 선형으로 (2 x 2 x 2 평균과 같다). gSlice = w 의 float 비트
Texture3D gSrc3 : register(t0);
float4 PS3(VSOut i) : SV_Target { return gSrc3.SampleLevel(gLinear, float3(i.uv, asfloat(gSlice)), 0); }
)";
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

	UINT64 AlignUp(UINT64 v, UINT64 a) { return a > 1 ? (v + a - 1) / a * a : v; }

	bool IsBlockCompressed(DXGI_FORMAT f) { return DirectX::IsCompressed(f); }

	UINT PlaneCount(DXGI_FORMAT f)
	{
		switch (f)
		{
		case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			return 2;
		default: return 1;
		}
	}

	bool HasStencil(DXGI_FORMAT f) { return PlaneCount(f) == 2; }

	bool IsSrgb(DXGI_FORMAT f) { return DirectX::IsSRGB(f); }

	bool IsIntegerFormat(DXGI_FORMAT f)
	{
		switch (f)
		{
		case DXGI_FORMAT_R8_UINT: case DXGI_FORMAT_R8_SINT: case DXGI_FORMAT_R8G8_UINT: case DXGI_FORMAT_R8G8_SINT:
		case DXGI_FORMAT_R8G8B8A8_UINT: case DXGI_FORMAT_R8G8B8A8_SINT: case DXGI_FORMAT_R16_UINT: case DXGI_FORMAT_R16_SINT:
		case DXGI_FORMAT_R16G16_UINT: case DXGI_FORMAT_R16G16_SINT: case DXGI_FORMAT_R16G16B16A16_UINT: case DXGI_FORMAT_R16G16B16A16_SINT:
		case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R32_SINT: case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R32G32_SINT:
		case DXGI_FORMAT_R32G32B32_UINT: case DXGI_FORMAT_R32G32B32_SINT: case DXGI_FORMAT_R32G32B32A32_UINT: case DXGI_FORMAT_R32G32B32A32_SINT:
		case DXGI_FORMAT_R10G10B10A2_UINT:
			return true;
		default: return false;
		}
	}

	// ============================================================ CPU 전용 디스크립터
	void CpuDescriptors::Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT perHeap)
	{
		_device = device;
		_type = type;
		_perHeap = perHeap;
		_inc = device->GetDescriptorHandleIncrementSize(type);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptors::Alloc()
	{
		if (!_free.empty())
		{
			const D3D12_CPU_DESCRIPTOR_HANDLE h = _free.back();
			_free.pop_back();
			return h;
		}
		if (_heaps.empty() || _used == _perHeap)
		{
			D3D12_DESCRIPTOR_HEAP_DESC hd = {};
			hd.Type = _type;
			hd.NumDescriptors = _perHeap;
			ComPtr<ID3D12DescriptorHeap> heap;
			if (FAILED(_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap))))
				return {};
			_heaps.push_back(heap);
			_used = 0;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE h = _heaps.back()->GetCPUDescriptorHandleForHeapStart();
		h.ptr += (SIZE_T)_used++ * _inc;
		return h;
	}

	// ============================================================ 셰이더에서 보이는 링
	bool GpuRing::Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT size)
	{
		D3D12_DESCRIPTOR_HEAP_DESC hd = {};
		hd.Type = type;
		hd.NumDescriptors = size;
		hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&_heap))))
			return false;
		_size = size;
		_inc = device->GetDescriptorHandleIncrementSize(type);
		_cpu0 = _heap->GetCPUDescriptorHandleForHeapStart();
		_gpu0 = _heap->GetGPUDescriptorHandleForHeapStart();
		return true;
	}

	bool GpuRing::Alloc(UINT count, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu)
	{
		if (count == 0 || count > _size || _full) return false;
		if (_head == _tail)
			_head = _tail = 0;   // 비었다 (_full 아님)
		UINT at;
		if (_head >= _tail)
		{
			if (_size - _head >= count) at = _head;
			else if (count <= _tail) { at = 0; ++Wraps; }   // 끝의 남은 칸은 버린다 (tail 이 지나가면 다시)
			else return false;
		}
		else
		{
			if (_tail - _head >= count) at = _head;
			else return false;
		}
		const UINT end = at + count;
		_head = end == _size ? 0 : end;
		if (_head == _tail) _full = true;
		cpu.ptr = _cpu0.ptr + (SIZE_T)at * _inc;
		gpu.ptr = _gpu0.ptr + (UINT64)at * _inc;
		return true;
	}

	void GpuRing::Retire(uint64_t completed, uint64_t computeCompleted)
	{
		while (!_marks.empty() && _marks.front().Serial <= completed && _marks.front().ComputeSerial <= computeCompleted)
		{
			_tail = _marks.front().Head;
			_marks.pop_front();
			_full = false;
		}
	}

	// ============================================================ 장치
	Dev::~Dev()
	{
		if (Device)
		{
			if (!Lost && Queue && Fence)
			{
				if (ListOpen) Submit(true);
				else WaitSerial(Submitted);
				WaitCompute(ComputeSubmitted);
			}
			Completed = Submitted;
			ComputeCompleted = ComputeSubmitted;
			while (!Deferred.empty())
			{
				auto fn = std::move(Deferred.front().Fn);
				Deferred.pop_front();
				fn();
			}
		}
		DestroyAllSwapchains();
		Pipelines.clear();
		MipPipelines.clear();
		Chunks.clear();
		if (FenceEvent) ::CloseHandle(FenceEvent);
		if (ComputeEvent) ::CloseHandle(ComputeEvent);
	}

	void Dev::Once(const std::string& key, const char* fmt, const char* arg)
	{
		if (Reported.insert(key).second)
			EditorLog::Write("DX12", fmt, arg);
	}

	void Dev::DrainMessages()
	{
		if (!Info) return;
		const UINT64 n = Info->GetNumStoredMessages();
		for (UINT64 i = 0; i < n; ++i)
		{
			SIZE_T len = 0;
			if (FAILED(Info->GetMessage(i, nullptr, &len)) || !len) continue;
			std::vector<uint8_t> buf(len);
			auto* m = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
			if (FAILED(Info->GetMessage(i, m, &len))) continue;
			if (m->Severity > D3D12_MESSAGE_SEVERITY_WARNING) continue;
			++DebugMessages;
			// 같은 메시지 번호는 3 번까지 (같은 실수가 그리기마다 쌓이지 않게)
			static std::map<int, int> s_Seen;
			if (++s_Seen[(int)m->ID] > 3) continue;
			EditorLog::Write("DX12", "[debug layer %s #%d] %.*s", m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR ? "error" : "warning", (int)m->ID,
				(int)(std::min<SIZE_T>)(m->DescriptionByteLength, 1500), m->pDescription);
		}
		Info->ClearStoredMessages();
	}

	void Dev::CheckRemoved(HRESULT hr, const char* what)
	{
		if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG)
		{
			if (!Lost)
				EditorLog::Write("DX12", "%s: device removed (reason 0x%08X)", what, Device ? (unsigned)Device->GetDeviceRemovedReason() : 0u);
			Lost = true;
		}
	}

	bool Dev::Init(HWND window, std::string& error)
	{
		Window = window;
		char env[16] = {};
		::GetEnvironmentVariableA("NOVA_D3D12_DEBUG", env, sizeof(env));
#ifdef _DEBUG
		const bool wantDebug = env[0] != '0';
#else
		const bool wantDebug = env[0] == '1';
#endif
		if (wantDebug)
		{
			ComPtr<ID3D12Debug> dbg;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
			{
				dbg->EnableDebugLayer();
				Debug = true;
			}
		}
		if (FAILED(CreateDXGIFactory2(Debug ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&Factory))) &&
			!Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&Factory)), "CreateDXGIFactory2", &error))
			return false;

		// 어댑터: 고성능 순서 (NOVA_D3D12_DEVICE = 이름 일부로 고정), 소프트웨어 (WARP) 는 빼고
		char want[128] = {};
		::GetEnvironmentVariableA("NOVA_D3D12_DEVICE", want, sizeof(want));
		ComPtr<IDXGIFactory6> f6;
		Factory.As(&f6);
		std::vector<ComPtr<IDXGIAdapter1>> adapters;
		for (UINT i = 0;; ++i)
		{
			ComPtr<IDXGIAdapter1> a;
			const HRESULT hr = f6 ? f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) : Factory->EnumAdapters1(i, &a);
			if (hr == DXGI_ERROR_NOT_FOUND) break;
			if (FAILED(hr)) continue;
			DXGI_ADAPTER_DESC1 d;
			a->GetDesc1(&d);
			if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
			adapters.push_back(a);
		}
		if (want[0])
			std::stable_partition(adapters.begin(), adapters.end(), [&](const ComPtr<IDXGIAdapter1>& a) {
				DXGI_ADAPTER_DESC1 d;
				a->GetDesc1(&d);
				return wstring_to_string(d.Description).find(want) != std::string::npos;
			});
		for (auto& a : adapters)
			if (SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device))))
			{
				Adapter = a;
				break;
			}
		if (!Device)
		{
			error = "no Direct3D 12 GPU (feature level 11_0)";
			return false;
		}
		{
			DXGI_ADAPTER_DESC1 d;
			Adapter->GetDesc1(&d);
			Name = wstring_to_string(d.Description);
		}
		if (Debug && SUCCEEDED(Device.As(&Info)))
		{
			// 성능 경고 (지우기 값이 만들 때와 다름) 는 뺀다
			//  · 3D 텍스처의 서로 다른 깊이 조각 (FirstWSlice) 을 RTV 여럿으로 같이 묶는 것 (APV 의 판 찍기) — D3D12 는 밉 전체를 서브리소스 하나로 보아
			//    겹친다고 하지만 쓰는 조각은 겹치지 않는다 (D3D11 과 같이 하드웨어는 조각마다 쓴다)
			D3D12_MESSAGE_ID deny[] = { D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE, D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
				(D3D12_MESSAGE_ID)728 };   // OMSetRenderTargets: RTV 가 겹친다 (#728)
			D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO, D3D12_MESSAGE_SEVERITY_MESSAGE };
			D3D12_INFO_QUEUE_FILTER filter = {};
			filter.DenyList.NumIDs = _countof(deny);
			filter.DenyList.pIDList = deny;
			filter.DenyList.NumSeverities = _countof(severities);
			filter.DenyList.pSeverityList = severities;
			Info->PushStorageFilter(&filter);
		}

		D3D12_COMMAND_QUEUE_DESC qd = {};
		qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		if (!Check(Device->CreateCommandQueue(&qd, IID_PPV_ARGS(&Queue)), "CreateCommandQueue", &error)) return false;
		if (!Check(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence)), "CreateFence", &error)) return false;
		FenceEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
		Queue->GetTimestampFrequency(&TimestampFrequency);
		{
			// 비동기 컴퓨트 큐 (없으면 같은 큐에서). NOVA_D3D12_ASYNC=0 = 쓰지 않음 (비교 · 문제 찾기)
			char a[8] = {};
			::GetEnvironmentVariableA("NOVA_D3D12_ASYNC", a, sizeof(a));
			AsyncEnabled = a[0] != '0';
			D3D12_COMMAND_QUEUE_DESC cd = {};
			cd.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
			if (FAILED(Device->CreateCommandQueue(&cd, IID_PPV_ARGS(&ComputeQueue))) ||
				FAILED(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ComputeFence))))
			{
				ComputeQueue = nullptr;
				ComputeFence = nullptr;
			}
			else
				ComputeEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
		}

		ComPtr<IDXGIFactory5> f5;
		if (SUCCEEDED(Factory.As(&f5)))
		{
			BOOL tearing = FALSE;
			if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))))
				AllowTearing = tearing == TRUE;
		}

		ViewHeap.Init(Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4096);
		RtvHeap.Init(Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 512);
		DsvHeap.Init(Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 256);
		SamplerHeap.Init(Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 256);
		if (!ViewRing.Init(Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kViewRingSize))
		{
			error = "shader-visible descriptor heap could not be created";
			return false;
		}
		{
			D3D12_DESCRIPTOR_HEAP_DESC hd = {};
			hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			hd.NumDescriptors = kSamplerGpuSize;
			hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			if (!Check(Device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&SamplerGpu)), "CreateDescriptorHeap(sampler)", &error)) return false;
			SamplerGpuSize = kSamplerGpuSize;
			SamplerInc = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		}
		{
			const D3D12_HEAP_PROPERTIES hp = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
			const D3D12_RESOURCE_DESC rd = BufferDesc(65536);
			if (!Check(Device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&ZeroCb)), "zero constant buffer", &error))
				return false;
			void* p = nullptr;
			const D3D12_RANGE none = { 0, 0 };
			if (SUCCEEDED(ZeroCb->Map(0, &none, &p))) { memset(p, 0, 65536); ZeroCb->Unmap(0, nullptr); }
		}
		CreateNullDescriptors();

		D3D12_INDIRECT_ARGUMENT_DESC arg = {};
		arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
		D3D12_COMMAND_SIGNATURE_DESC sd = {};
		sd.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
		sd.NumArgumentDescs = 1;
		sd.pArgumentDescs = &arg;
		Device->CreateCommandSignature(&sd, nullptr, IID_PPV_ARGS(&DrawSig));
		arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
		sd.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
		Device->CreateCommandSignature(&sd, nullptr, IID_PPV_ARGS(&DrawIndexedSig));

		std::string mipError;
		if (!CreateMipShaders(mipError))
			EditorLog::Write("DX12", "mip generation shaders: %s (GenerateMips disabled)", mipError.c_str());

		// 최대 기능 수준 (로그용)
		const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
		D3D12_FEATURE_DATA_FEATURE_LEVELS fl = { _countof(levels), levels, D3D_FEATURE_LEVEL_11_0 };
		Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl));
		D3D12_FEATURE_DATA_D3D12_OPTIONS o = {};
		Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof(o));
		EditorLog::Write("DX12", "device: %s (feature level %d_%d, resource binding tier %d), debug layer %s, tearing %s, async compute %s", Name.c_str(),
			(fl.MaxSupportedFeatureLevel >> 12) & 0xF, (fl.MaxSupportedFeatureLevel >> 8) & 0xF, (int)o.ResourceBindingTier, Debug ? "on" : "off", AllowTearing ? "yes" : "no",
			!ComputeQueue ? "no queue" : AsyncEnabled ? "on" : "off (NOVA_D3D12_ASYNC=0)");
		GraphicsCmd();
		return true;
	}

	void Dev::CreateNullDescriptors()
	{
		// 빈 칸 = 널 디스크립터 (D3D11 의 빈 SRV · UAV 와 같이 읽으면 0). 모양은 셰이더 선언 (D3D_SRV_DIMENSION) 에 맞춘다
		for (int dim = 0; dim < 12; ++dim)
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
			s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			s.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			switch (dim)
			{
			case D3D_SRV_DIMENSION_BUFFER: case D3D_SRV_DIMENSION_BUFFEREX: s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; s.Format = DXGI_FORMAT_R32_UINT; break;
			case D3D_SRV_DIMENSION_TEXTURE1D: s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D; s.Texture1D.MipLevels = 1; break;
			case D3D_SRV_DIMENSION_TEXTURE1DARRAY: s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY; s.Texture1DArray.MipLevels = 1; s.Texture1DArray.ArraySize = 1; break;
			case D3D_SRV_DIMENSION_TEXTURE2DARRAY: s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; s.Texture2DArray.MipLevels = 1; s.Texture2DArray.ArraySize = 1; break;
			case D3D_SRV_DIMENSION_TEXTURE2DMS: s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS; break;
			case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY: s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY; s.Texture2DMSArray.ArraySize = 1; break;
			case D3D_SRV_DIMENSION_TEXTURE3D: s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D; s.Texture3D.MipLevels = 1; break;
			case D3D_SRV_DIMENSION_TEXTURECUBE: s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; s.TextureCube.MipLevels = 1; break;
			case D3D_SRV_DIMENSION_TEXTURECUBEARRAY: s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY; s.TextureCubeArray.MipLevels = 1; s.TextureCubeArray.NumCubes = 1; break;
			default: s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; s.Texture2D.MipLevels = 1; break;
			}
			NullSrv[dim] = ViewHeap.Alloc();
			Device->CreateShaderResourceView(nullptr, &s, NullSrv[dim]);

			D3D12_UNORDERED_ACCESS_VIEW_DESC u = {};
			u.Format = DXGI_FORMAT_R32_FLOAT;
			switch (dim)
			{
			case D3D_SRV_DIMENSION_BUFFER: case D3D_SRV_DIMENSION_BUFFEREX: u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; u.Format = DXGI_FORMAT_R32_UINT; break;
			case D3D_SRV_DIMENSION_TEXTURE1D: u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D; break;
			case D3D_SRV_DIMENSION_TEXTURE1DARRAY: u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY; u.Texture1DArray.ArraySize = 1; break;
			case D3D_SRV_DIMENSION_TEXTURE2DARRAY: u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY; u.Texture2DArray.ArraySize = 1; break;
			case D3D_SRV_DIMENSION_TEXTURE3D: u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D; u.Texture3D.WSize = 1; break;
			default: u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; break;
			}
			NullUav[dim] = ViewHeap.Alloc();
			Device->CreateUnorderedAccessView(nullptr, nullptr, &u, NullUav[dim]);
		}
		// 빈 색 타깃 칸 (OMSetRenderTargets 의 가운데 빈 칸)
		D3D12_RENDER_TARGET_VIEW_DESC rv = {};
		rv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		NullRtv = RtvHeap.Alloc();
		Device->CreateRenderTargetView(nullptr, &rv, NullRtv);
		D3D12_SAMPLER_DESC sd = {};
		sd.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sd.MaxLOD = D3D12_FLOAT32_MAX;
		sd.MaxAnisotropy = 1;
		DefaultSampler = SamplerHeap.Alloc();
		Device->CreateSampler(&sd, DefaultSampler);
		sd.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		sd.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		DefaultCompare = SamplerHeap.Alloc();
		Device->CreateSampler(&sd, DefaultCompare);
	}

	bool Dev::CreateMipShaders(std::string& error)
	{
		ComPtr<ID3DBlob> vs, ps, ps3, err;
		if (FAILED(D3DCompile(kMipShader, strlen(kMipShader), "NovaMip", nullptr, nullptr, "VS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &err)) ||
			FAILED(D3DCompile(kMipShader, strlen(kMipShader), "NovaMip", nullptr, nullptr, "PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &err)) ||
			FAILED(D3DCompile(kMipShader, strlen(kMipShader), "NovaMip", nullptr, nullptr, "PS3", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps3, &err)))
		{
			error = err ? std::string((const char*)err->GetBufferPointer(), err->GetBufferSize()) : "D3DCompile failed";
			return false;
		}
		MipVs.assign((const uint8_t*)vs->GetBufferPointer(), (const uint8_t*)vs->GetBufferPointer() + vs->GetBufferSize());
		MipPs.assign((const uint8_t*)ps->GetBufferPointer(), (const uint8_t*)ps->GetBufferPointer() + ps->GetBufferSize());
		MipPs3.assign((const uint8_t*)ps3->GetBufferPointer(), (const uint8_t*)ps3->GetBufferPointer() + ps3->GetBufferSize());
		// 루트: 상수 1 개 (조각 번호) + SRV 표 + 고정 샘플러
		D3D12_DESCRIPTOR_RANGE range = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
		D3D12_ROOT_PARAMETER params[2] = {};
		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		params[0].Constants = { 0, 0, 1 };
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[1].DescriptorTable = { 1, &range };
		params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_STATIC_SAMPLER_DESC ss = {};
		ss.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		ss.MaxLOD = D3D12_FLOAT32_MAX;
		ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_ROOT_SIGNATURE_DESC rd = {};
		rd.NumParameters = 2;
		rd.pParameters = params;
		rd.NumStaticSamplers = 1;
		rd.pStaticSamplers = &ss;
		ComPtr<ID3DBlob> blob;
		err.Reset();
		if (FAILED(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
			FAILED(Device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&MipRoot))))
		{
			error = err ? std::string((const char*)err->GetBufferPointer(), err->GetBufferSize()) : "mip root signature failed";
			return false;
		}
		return true;
	}

	ID3D12PipelineState* Dev::MipPipeline(DXGI_FORMAT format, bool volume)
	{
		const auto key = std::make_pair(format, volume ? 2 : 1);
		auto it = MipPipelines.find(key);
		if (it != MipPipelines.end()) return it->second.Get();
		ComPtr<ID3D12PipelineState>& p = MipPipelines[key];
		if (!MipRoot) return nullptr;
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
		pd.pRootSignature = MipRoot.Get();
		pd.VS = { MipVs.data(), MipVs.size() };
		pd.PS = volume ? D3D12_SHADER_BYTECODE{ MipPs3.data(), MipPs3.size() } : D3D12_SHADER_BYTECODE{ MipPs.data(), MipPs.size() };
		pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		pd.SampleMask = 0xFFFFFFFF;
		pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		pd.RasterizerState.DepthClipEnable = TRUE;
		pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pd.NumRenderTargets = 1;
		pd.RTVFormats[0] = format;
		pd.SampleDesc.Count = 1;
		if (FAILED(Device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&p))))
		{
			char buf[32];
			snprintf(buf, sizeof(buf), "%d", (int)format);
			Once(std::string("mip-pso:") + buf, "mip generation pipeline for DXGI format %s failed", buf);
			p = nullptr;
		}
		return p.Get();
	}

	// ============================================================ 명령 · 제출
	ID3D12GraphicsCommandList* Dev::GraphicsCmd()
	{
		if (ListOpen) return List.Get();
		Poll();
		if (!FreeAllocs.empty())
		{
			CurrentAlloc = FreeAllocs.back();
			FreeAllocs.pop_back();
			CurrentAlloc.Alloc->Reset();
		}
		else
		{
			CurrentAlloc = CmdSlot();
			Check(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CurrentAlloc.Alloc)), "CreateCommandAllocator");
		}
		if (!List)
			Check(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, CurrentAlloc.Alloc.Get(), nullptr, IID_PPV_ARGS(&List)), "CreateCommandList");
		else if (FAILED(List->Reset(CurrentAlloc.Alloc.Get(), nullptr)))
		{
			Once("reset-fail", "%s", "command list Reset failed - creating a new command list");
			List = nullptr;
			Check(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, CurrentAlloc.Alloc.Get(), nullptr, IID_PPV_ARGS(&List)), "CreateCommandList");
		}
		ID3D12DescriptorHeap* heaps[] = { ViewRing.Heap(), SamplerGpu.Get() };
		List->SetDescriptorHeaps(2, heaps);
		ListOpen = true;
		return List.Get();
	}

	void Dev::Submit(bool wait)
	{
		ID3D12GraphicsCommandList* cl = GraphicsCmd();
		if (Immediate)
		{
			if (!AsyncOpen) Immediate->FlushBarriers();
			Immediate->FlushPreBarriers();
			if (Immediate->Predicated) Immediate->ApplyPredicate(cl, false);
		}
		const HRESULT hr = cl->Close();
		const uint64_t serial = Recording();
		if (SUCCEEDED(hr) && !Lost)
		{
			ID3D12CommandList* lists[] = { cl };
			Queue->ExecuteCommandLists(1, lists);
		}
		else if (FAILED(hr))
		{
			Once("close-fail", "%s", "command list Close failed - the commands of this submit were dropped (see the debug layer messages)");
			CheckRemoved(Device->GetDeviceRemovedReason(), "Close");
			List = nullptr;   // 오류 상태의 목록은 다시 쓰지 않는다 (다음 Cmd 가 새로 만든다)
		}
		CheckRemoved(Queue->Signal(Fence.Get(), serial), "Signal");
		Submitted = serial;
		ViewRing.Mark(serial, ComputeCover());
		CurrentAlloc.Retire = serial;
		InFlightAllocs.push_back(CurrentAlloc);
		CurrentAlloc = CmdSlot();
		ListOpen = false;
		Epoch = ++EpochCounter;
		DrawsSinceSubmit = 0;
		if (Immediate) Immediate->AfterSubmit();
		DrainMessages();
		if (wait) WaitSerial(serial);
		else Poll();
	}

	void Dev::Poll()
	{
		if (!Fence) return;
		const UINT64 v = Fence->GetCompletedValue();
		if (v == UINT64_MAX)
		{
			CheckRemoved(DXGI_ERROR_DEVICE_REMOVED, "fence");
			return;
		}
		Completed = (std::max)(Completed, (uint64_t)v);
		if (ComputeFence)
		{
			const UINT64 c = ComputeFence->GetCompletedValue();
			if (c != UINT64_MAX) ComputeCompleted = (std::max)(ComputeCompleted, (uint64_t)c);
		}
		for (size_t i = 0; i < InFlightAllocs.size();)
		{
			if (InFlightAllocs[i].Retire <= Completed)
			{
				FreeAllocs.push_back(InFlightAllocs[i]);
				InFlightAllocs.erase(InFlightAllocs.begin() + i);
			}
			else ++i;
		}
		for (size_t i = 0; i < InFlightComputeAllocs.size();)
		{
			if (InFlightComputeAllocs[i].Retire <= ComputeCompleted)
			{
				FreeComputeAllocs.push_back(InFlightComputeAllocs[i]);
				InFlightComputeAllocs.erase(InFlightComputeAllocs.begin() + i);
			}
			else ++i;
		}
		ViewRing.Retire(Completed, ComputeCompleted);
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
		if (Fence->GetCompletedValue() < serial)
		{
			Fence->SetEventOnCompletion(serial, FenceEvent);
			// 10 초 넘게 끝나지 않으면 GPU 가 멈춘 것으로 본다 (기다리다 PC 가 굳지 않게)
			if (::WaitForSingleObject(FenceEvent, 10000) == WAIT_TIMEOUT)
			{
				EditorLog::Write("DX12", "GPU did not finish submit %llu in 10 s - treated as device lost (removed reason 0x%08X)",
					(unsigned long long)serial, (unsigned)Device->GetDeviceRemovedReason());
				Lost = true;
				return;
			}
		}
		Poll();
	}

	void Dev::WaitCompute(uint64_t serial)
	{
		if (Lost || !ComputeFence || serial == 0 || serial <= ComputeCompleted) return;
		if (ComputeFence->GetCompletedValue() < serial)
		{
			ComputeFence->SetEventOnCompletion(serial, ComputeEvent);
			if (::WaitForSingleObject(ComputeEvent, 10000) == WAIT_TIMEOUT)
			{
				EditorLog::Write("DX12", "compute queue did not finish submit %llu in 10 s - treated as device lost", (unsigned long long)serial);
				Lost = true;
				return;
			}
		}
		Poll();
	}

	void Dev::OpenCompute()
	{
		Poll();
		if (!FreeComputeAllocs.empty())
		{
			ComputeAlloc = FreeComputeAllocs.back();
			FreeComputeAllocs.pop_back();
			ComputeAlloc.Alloc->Reset();
		}
		else
		{
			ComputeAlloc = CmdSlot();
			Check(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&ComputeAlloc.Alloc)), "CreateCommandAllocator(compute)");
		}
		if (!ComputeList || FAILED(ComputeList->Reset(ComputeAlloc.Alloc.Get(), nullptr)))
		{
			ComputeList = nullptr;
			Check(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, ComputeAlloc.Alloc.Get(), nullptr, IID_PPV_ARGS(&ComputeList)), "CreateCommandList(compute)");
		}
		ID3D12DescriptorHeap* heaps[] = { ViewRing.Heap(), SamplerGpu.Get() };
		ComputeList->SetDescriptorHeaps(2, heaps);
		ComputeEpoch = ++EpochCounter;
		AsyncOpen = true;
	}

	void Dev::SubmitCompute()
	{
		// 1) 그래픽 목록 (컴퓨트가 읽을 것을 만든 일 + 그래픽 전용 상태에서 나오는 장벽) 을 먼저 — 컴퓨트 큐가 이 값을 기다린다
		Submit(false);
		const uint64_t graphics = Submitted;
		// 2) 컴퓨트 목록
		const HRESULT hr = ComputeList->Close();
		const uint64_t serial = ComputeSubmitted + 1;
		ComputeQueue->Wait(Fence.Get(), graphics);
		if (SUCCEEDED(hr) && !Lost)
		{
			ID3D12CommandList* lists[] = { ComputeList.Get() };
			ComputeQueue->ExecuteCommandLists(1, lists);
			++AsyncSubmits;
		}
		else if (FAILED(hr))
		{
			Once("compute-close-fail", "%s", "compute command list Close failed - the async compute commands were dropped (see the debug layer messages)");
			ComputeList = nullptr;
		}
		CheckRemoved(ComputeQueue->Signal(ComputeFence.Get(), serial), "Signal(compute)");
		ComputeSubmitted = serial;
		ComputePending = serial;
		ComputeAlloc.Retire = serial;
		InFlightComputeAllocs.push_back(ComputeAlloc);
		ComputeAlloc = CmdSlot();
		AsyncOpen = false;
		DrainMessages();
	}

	// ============================================================ 업로드 링
	bool Dev::Upload(UINT64 size, UINT64 align, UploadLoc& loc)
	{
		size = (std::max<UINT64>)(size, 4);
		align = (std::max<UINT64>)(align, 4);
		if (Current)
		{
			const UINT64 at = AlignUp(Current->Used, align);
			if (at + size <= Current->Size)
			{
				Current->Used = at + size;
				loc = { Current->Res.Get(), at, Current->Cpu + at, Current->Gpu + at, Current, Current->Generation };
				return true;
			}
			Current->Retire = Recording();
			Current->RetireCompute = ComputeCover();
			Retired.push_back(Current);
			Current = nullptr;
		}
		Poll();
		for (auto it = Retired.begin(); it != Retired.end(); ++it)
			if ((*it)->Retire <= Completed && (*it)->RetireCompute <= ComputeCompleted && (*it)->Size >= size + align)
			{
				Current = *it;
				Retired.erase(it);
				Current->Used = 0;
				++Current->Generation;
				break;
			}
		if (!Current)
		{
			auto c = std::make_unique<UploadChunk>();
			c->Size = (std::max)(kUploadChunk, AlignUp(size + align, 1ull << 20));
			const D3D12_HEAP_PROPERTIES hp = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
			const D3D12_RESOURCE_DESC rd = BufferDesc(c->Size);
			if (!Check(Device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&c->Res)), "upload chunk"))
				return false;
			void* p = nullptr;
			const D3D12_RANGE none = { 0, 0 };
			if (FAILED(c->Res->Map(0, &none, &p))) return false;
			c->Cpu = static_cast<uint8_t*>(p);
			c->Gpu = c->Res->GetGPUVirtualAddress();
			Current = c.get();
			Chunks.push_back(std::move(c));
			if (Chunks.size() == 16 || Chunks.size() == 64)
				EditorLog::Write("DX12", "upload ring grew to %zu chunks", Chunks.size());
		}
		const UINT64 at = AlignUp(0, align);
		Current->Used = at + size;
		loc = { Current->Res.Get(), at, Current->Cpu + at, Current->Gpu + at, Current, Current->Generation };
		return true;
	}

	bool Dev::AllocViews(UINT count, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu)
	{
		if (ViewRing.Alloc(count, cpu, gpu)) return true;
		Poll();
		if (ViewRing.Alloc(count, cpu, gpu)) return true;
		// 링이 다 찼다: 지금까지를 제출하고 모두 끝날 때까지 (드물다 — 50 만 칸)
		Once("view-ring-full", "%s", "shader-visible descriptor ring is full - waiting for the GPU");
		Submit(false);
		WaitSerial(Submitted);
		WaitCompute(ComputeSubmitted);
		return ViewRing.Alloc(count, cpu, gpu);
	}

	bool Dev::SamplerTable(const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>& samplers, const std::vector<uint64_t>& key, D3D12_GPU_DESCRIPTOR_HANDLE& out)
	{
		const uint64_t h = HashBytes(key.data(), key.size() * sizeof(uint64_t));
		auto it = SamplerTables.find(h);
		if (it != SamplerTables.end() && it->second.first == key)
		{
			out = it->second.second;
			return true;
		}
		const UINT n = (UINT)samplers.size();
		if (SamplerGpuUsed + n > SamplerGpuSize)
		{
			// 샘플러 힙이 다 찼다: 모든 명령이 끝난 뒤 처음부터 (샘플러 조합은 많지 않아 드물다)
			Once("sampler-heap-full", "%s", "shader-visible sampler heap is full - waiting for the GPU and starting over");
			if (AsyncOpen && Immediate) Immediate->EndAsyncCompute();   // 열린 컴퓨트 목록이 옛 표를 가리킨다 — 먼저 보낸다
			Submit(false);
			WaitSerial(Submitted);
			WaitCompute(ComputeSubmitted);
			SamplerTables.clear();
			SamplerGpuUsed = 0;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = SamplerGpu->GetCPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE gpu = SamplerGpu->GetGPUDescriptorHandleForHeapStart();
		cpu.ptr += (SIZE_T)SamplerGpuUsed * SamplerInc;
		gpu.ptr += (UINT64)SamplerGpuUsed * SamplerInc;
		for (UINT i = 0; i < n; ++i)
		{
			D3D12_CPU_DESCRIPTOR_HANDLE dst = { cpu.ptr + (SIZE_T)i * SamplerInc };
			Device->CopyDescriptorsSimple(1, dst, samplers[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		}
		SamplerGpuUsed += n;
		SamplerTables[h] = { key, gpu };
		out = gpu;
		return true;
	}

	// ============================================================ 자원
	Image* ImageOf(GfxResource* r)
	{
		if (!r || r->Api() != GfxApi::DirectX12) return nullptr;
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
		if (!r || r->Api() != GfxApi::DirectX12) return nullptr;
		D3D11_RESOURCE_DIMENSION dim;
		r->GetType(&dim);
		return dim == D3D11_RESOURCE_DIMENSION_BUFFER ? static_cast<Buf*>(r) : nullptr;
	}

	// 자원은 기록된 명령이 아직 가리킬 수 있다 → 지금 기록 중인 제출이 끝난 뒤에 놓는다
	Buf::~Buf() { D->DeferRelease(R.Res); }
	Tex1D::~Tex1D() { D->DeferRelease(I.R.Res); D->DeferRelease(I.Staging); }
	Tex2D::~Tex2D() { D->DeferRelease(I.R.Res); D->DeferRelease(I.Staging); }
	Tex3D::~Tex3D() { D->DeferRelease(I.R.Res); D->DeferRelease(I.Staging); }
	Query::~Query() { D->DeferRelease(Heap); D->DeferRelease(Readback); D->DeferRelease(Predicate.Res); }

	template <class Iface, class Desc>
	View<Iface, Desc>::~View()
	{
		if (!V.Cpu.ptr) return;
		Dev* d = this->D;
		const D3D12_CPU_DESCRIPTOR_HANDLE h = V.Cpu;
		const int heap = Heap;
		// 디스크립터 칸은 복사해 둔 링 칸과 따로라 바로 다시 써도 되지만, RTV · DSV 는 기록된 명령이 아직 가리킨다 → 제출이 끝난 뒤
		d->Defer([d, h, heap]() { (heap == 1 ? d->RtvHeap : heap == 2 ? d->DsvHeap : d->ViewHeap).Free(h); });
	}
	template class View<GfxShaderResourceView, D3D11_SHADER_RESOURCE_VIEW_DESC>;
	template class View<GfxRenderTargetView, D3D11_RENDER_TARGET_VIEW_DESC>;
	template class View<GfxDepthStencilView, D3D11_DEPTH_STENCIL_VIEW_DESC>;
	template class View<GfxUnorderedAccessView, D3D11_UNORDERED_ACCESS_VIEW_DESC>;

	Sampler::~Sampler()
	{
		Dev* d = D;
		const D3D12_CPU_DESCRIPTOR_HANDLE h = Cpu;
		if (h.ptr) d->Defer([d, h]() { d->SamplerHeap.Free(h); });
	}

	Program::~Program()
	{
		D->ForgetProgram(Id);
		D->DeferRelease(Root);
		D->DeferRelease(Compute);
	}

	HRESULT Dev::MakeTexture(Image& img, const D3D11_SUBRESOURCE_DATA* data, const char* what)
	{
		img.Id = NextId();
		img.R.Planes = PlaneCount(img.Dxgi);
		img.DepthFormat = (img.Bind & D3D11_BIND_DEPTH_STENCIL) != 0 || DirectX::IsDepthStencil(img.Dxgi);
		D3D12_RESOURCE_DESC rd = {};
		rd.Dimension = img.Dim;
		rd.Width = img.Width;
		rd.Height = img.Height;
		rd.DepthOrArraySize = (UINT16)(img.Dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? img.Depth : img.R.Layers);
		rd.MipLevels = (UINT16)img.R.Mips;
		rd.Format = img.Dxgi;
		rd.SampleDesc.Count = img.Samples;
		rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		if (img.Usage == D3D11_USAGE_STAGING)
		{
			// CPU 읽기 · 쓰기 텍스처 = CPU 쪽 버퍼 (서브리소스마다 256 바이트 행 배치)
			const UINT subs = img.R.Mips * img.R.Layers;
			img.Footprints.resize(subs);
			img.Rows.resize(subs);
			img.RowBytes.resize(subs);
			UINT64 total = 0;
			Device->GetCopyableFootprints(&rd, 0, subs, 0, img.Footprints.data(), img.Rows.data(), img.RowBytes.data(), &total);
			const D3D12_HEAP_PROPERTIES hp = HeapProps(D3D12_HEAP_TYPE_CUSTOM);
			const D3D12_RESOURCE_DESC bd = BufferDesc(total);
			if (!Check(Device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&img.Staging)), what))
				return E_OUTOFMEMORY;
			void* p = nullptr;
			if (FAILED(img.Staging->Map(0, nullptr, &p))) return E_FAIL;
			img.StagingCpu = static_cast<uint8_t*>(p);
			img.R.Res = img.Staging;
			img.R.Buffer = true;
			img.R.States.assign(1, D3D12_RESOURCE_STATE_COMMON);
			img.R.Bytes = total;
			if (data)
				for (UINT sub = 0; sub < subs; ++sub)
				{
					const D3D11_SUBRESOURCE_DATA& d = data[sub];
					if (!d.pSysMem) continue;
					const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp = img.Footprints[sub];
					for (UINT z = 0; z < fp.Footprint.Depth; ++z)
						for (UINT r = 0; r < img.Rows[sub]; ++r)
							memcpy(img.StagingCpu + fp.Offset + (UINT64)z * fp.Footprint.RowPitch * img.Rows[sub] + (UINT64)r * fp.Footprint.RowPitch,
								static_cast<const uint8_t*>(d.pSysMem) + (size_t)z * d.SysMemSlicePitch + (size_t)r * d.SysMemPitch, (size_t)img.RowBytes[sub]);
				}
			return S_OK;
		}

		if (img.Bind & D3D11_BIND_RENDER_TARGET) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		if (img.Bind & D3D11_BIND_UNORDERED_ACCESS) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		if (img.Bind & D3D11_BIND_DEPTH_STENCIL)
		{
			rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
			if (!(img.Bind & D3D11_BIND_SHADER_RESOURCE)) rd.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
		}
		D3D12_CLEAR_VALUE clear = {};
		const D3D12_CLEAR_VALUE* clearPtr = nullptr;
		if (img.Bind & D3D11_BIND_DEPTH_STENCIL)
		{
			clear.Format = DepthViewFormat(img.Dxgi);
			clear.DepthStencil = { 1.0f, 0 };
			clearPtr = &clear;
		}
		else if ((img.Bind & D3D11_BIND_RENDER_TARGET) && !IsTypeless(img.Dxgi))
		{
			clear.Format = img.Dxgi;
			clearPtr = &clear;
		}
		const D3D12_HEAP_PROPERTIES hp = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
		const D3D12_RESOURCE_STATES initial = data ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COMMON;
		const HRESULT hr = Device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initial, clearPtr, IID_PPV_ARGS(&img.R.Res));
		if (FAILED(hr))
		{
			char buf[160];
			snprintf(buf, sizeof(buf), "%s %u x %u x %u (%u mips, DXGI %d, bind 0x%X): hr=0x%08X", what, img.Width, img.Height, (unsigned)rd.DepthOrArraySize,
				img.R.Mips, (int)img.Dxgi, img.Bind, (unsigned)hr);
			Once(std::string("tex-fail:") + buf, "texture creation failed: %s", buf);
			CheckRemoved(hr, what);
			return hr == E_OUTOFMEMORY ? E_OUTOFMEMORY : E_FAIL;
		}
		if (Debug)
		{
			wchar_t name[96];
			swprintf_s(name, L"%S %ux%ux%u mips %u DXGI %d bind 0x%X", what, img.Width, img.Height, (unsigned)rd.DepthOrArraySize, img.R.Mips, (int)img.Dxgi, img.Bind);
			img.R.Res->SetName(name);
		}
		const D3D12_RESOURCE_ALLOCATION_INFO ai = Device->GetResourceAllocationInfo(0, 1, &rd);
		img.R.Bytes = ai.SizeInBytes;
		img.R.States.assign((size_t)img.R.Mips * img.R.Layers * img.R.Planes, initial);
		if (data)
			for (UINT sub = 0; sub < img.R.Mips * img.R.Layers; ++sub)
				if (data[sub].pSysMem)
					UploadImage(img, sub, nullptr, data[sub].pSysMem, data[sub].SysMemPitch, data[sub].SysMemSlicePitch);
		return S_OK;
	}

	void Dev::UploadImage(Image& img, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch)
	{
		if (img.R.Planes > 1 || img.DepthFormat)
		{
			Once("upload-depth", "%s", "uploading data into a depth texture is not supported");
			return;
		}
		const UINT m = sub % img.R.Mips;
		const UINT x = box ? box->left : 0, y = box ? box->top : 0, z = box ? box->front : 0;
		const UINT w = box ? box->right - box->left : img.MipW(m);
		const UINT h = box ? box->bottom - box->top : img.MipH(m);
		const UINT d = box ? box->back - box->front : img.MipD(m);
		if (!w || !h || !d) return;
		const bool bc = IsBlockCompressed(img.Dxgi);
		const UINT64 bpp = DirectX::BitsPerPixel(img.Dxgi);
		const UINT64 row = bc ? (UINT64)((w + 3) / 4) * (bpp * 2) : (UINT64)w * bpp / 8;   // BC: 블록 16 바이트 (BC1 · BC4 는 8) = bpp * 16 / 8
		const UINT rows = bc ? (h + 3) / 4 : h;
		if (!rowPitch) rowPitch = (UINT)row;
		if (!depthPitch) depthPitch = rowPitch * rows;
		const UINT64 pitch = AlignUp(row, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
		UploadLoc loc;
		if (!Upload(pitch * rows * d, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, loc)) return;
		for (UINT zz = 0; zz < d; ++zz)
			for (UINT r = 0; r < rows; ++r)
				memcpy(loc.Cpu + ((UINT64)zz * rows + r) * pitch, static_cast<const uint8_t*>(data) + (size_t)zz * depthPitch + (size_t)r * rowPitch, (size_t)row);
		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = loc.Res;
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint.Offset = loc.Offset;
		src.PlacedFootprint.Footprint = { img.Dxgi, bc ? (UINT)AlignUp(w, 4) : w, bc ? (UINT)AlignUp(h, 4) : h, d, (UINT)pitch };
		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource = img.R.Res.Get();
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = sub;
		// BC: 발자국 = 블록 (4) 단위로 늘린 크기 — 상자 없이 통째로 (작은 밉 2 x 2 · 1 x 1 도 블록 하나)
		Cmd()->CopyTextureRegion(&dst, x, y, z, &src, nullptr);
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
		b->R.Buffer = true;
		if (desc->Usage == D3D11_USAGE_DYNAMIC)
		{
			// 업로드 링에서 그린다 (Map 마다 새 자리). 처음 데이터는 사본에 두고 첫 사용 때 링으로
			b->Shadow.assign(desc->ByteWidth, 0);
			if (data && data->pSysMem) memcpy(b->Shadow.data(), data->pSysMem, desc->ByteWidth);
			b->R.Fixed = true;
			*out = b;
			return S_OK;
		}
		const bool staging = desc->Usage == D3D11_USAGE_STAGING;
		const D3D12_HEAP_PROPERTIES hp = HeapProps(staging ? D3D12_HEAP_TYPE_CUSTOM : D3D12_HEAP_TYPE_DEFAULT);
		const D3D12_RESOURCE_DESC rd = BufferDesc(AlignUp(desc->ByteWidth, 4),
			(desc->BindFlags & D3D11_BIND_UNORDERED_ACCESS) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);
		const HRESULT hr = Device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&b->R.Res));
		if (FAILED(hr))
		{
			EditorLog::Write("DX12", "buffer of %.1f MB: creation failed hr=0x%08X", desc->ByteWidth / 1048576.0, (unsigned)hr);
			CheckRemoved(hr, "CreateBuffer");
			b->Release();
			return E_OUTOFMEMORY;
		}
		b->R.States.assign(1, D3D12_RESOURCE_STATE_COMMON);
		b->R.Bytes = rd.Width;
		if (Debug)
		{
			wchar_t name[64];
			swprintf_s(name, L"Buffer %u bytes bind 0x%X usage %d", desc->ByteWidth, desc->BindFlags, (int)desc->Usage);
			b->R.Res->SetName(name);
		}
		if (staging)
		{
			void* p = nullptr;
			if (FAILED(b->R.Res->Map(0, nullptr, &p))) { b->Release(); return E_FAIL; }
			b->Mapped = static_cast<uint8_t*>(p);
			if (data && data->pSysMem) memcpy(b->Mapped, data->pSysMem, desc->ByteWidth);
		}
		else if (data && data->pSysMem)
		{
			UploadLoc loc;
			if (Upload(desc->ByteWidth, 16, loc))
			{
				memcpy(loc.Cpu, data->pSysMem, desc->ByteWidth);
				// 새 버퍼 = 이 목록에서 처음 쓴다 → COMMON 에서 COPY_DEST 로 승격 (장벽 없음)
				Cmd()->CopyBufferRegion(b->R.Res.Get(), 0, loc.Res, loc.Offset, desc->ByteWidth);
				b->R.States[0] = D3D12_RESOURCE_STATE_COPY_DEST;
				b->R.StateEpoch = CurEpoch();
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
		i.Dim = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
		i.Width = desc->Width;
		i.R.Layers = (std::max)(1u, desc->ArraySize);
		i.R.Mips = desc->MipLevels ? desc->MipLevels : FullMips(desc->Width, 1, 1);
		t->Desc.MipLevels = i.R.Mips;
		i.Usage = desc->Usage;
		i.CpuAccess = desc->CPUAccessFlags;
		i.Bind = desc->BindFlags | ((desc->MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS) ? D3D11_BIND_RENDER_TARGET : 0);
		const HRESULT hr = MakeTexture(i, data, "texture1D");
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
		i.Dim = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		i.Width = desc->Width;
		i.Height = desc->Height;
		i.R.Layers = (std::max)(1u, desc->ArraySize);
		i.R.Mips = desc->MipLevels ? desc->MipLevels : FullMips(desc->Width, desc->Height, 1);
		t->Desc.MipLevels = i.R.Mips;
		i.Samples = (std::max)(1u, desc->SampleDesc.Count);
		i.Cube = (desc->MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0 && i.R.Layers >= 6;
		i.Usage = desc->Usage;
		i.CpuAccess = desc->CPUAccessFlags;
		i.Bind = desc->BindFlags | ((desc->MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS) ? D3D11_BIND_RENDER_TARGET : 0);
		const HRESULT hr = MakeTexture(i, data, "texture2D");
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
		i.Dim = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
		i.Width = desc->Width;
		i.Height = desc->Height;
		i.Depth = desc->Depth;
		i.R.Mips = desc->MipLevels ? desc->MipLevels : FullMips(desc->Width, desc->Height, desc->Depth);
		t->Desc.MipLevels = i.R.Mips;
		i.Usage = desc->Usage;
		i.CpuAccess = desc->CPUAccessFlags;
		i.Bind = desc->BindFlags | ((desc->MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS) ? D3D11_BIND_RENDER_TARGET : 0);
		const HRESULT hr = MakeTexture(i, data, "texture3D");
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
			// 버퍼 SRV: 형식 (Buffer<T>) · 구조 · raw
			D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
			s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			UINT64 elementBytes = 0;
			if (desc && desc->ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX && (desc->BufferEx.Flags & D3D11_BUFFEREX_SRV_FLAG_RAW))
			{
				s.Format = DXGI_FORMAT_R32_TYPELESS;
				s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
				s.Buffer.FirstElement = desc->BufferEx.FirstElement;
				s.Buffer.NumElements = desc->BufferEx.NumElements;
				elementBytes = 4;
			}
			else if (desc && (desc->ViewDimension == D3D11_SRV_DIMENSION_BUFFER || desc->ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX))
			{
				const UINT first = desc->ViewDimension == D3D11_SRV_DIMENSION_BUFFER ? desc->Buffer.FirstElement : desc->BufferEx.FirstElement;
				const UINT count = desc->ViewDimension == D3D11_SRV_DIMENSION_BUFFER ? desc->Buffer.NumElements : desc->BufferEx.NumElements;
				s.Buffer.FirstElement = first;
				s.Buffer.NumElements = count;
				if ((b->Desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) && b->Desc.StructureByteStride)
				{
					s.Format = DXGI_FORMAT_UNKNOWN;
					s.Buffer.StructureByteStride = b->Desc.StructureByteStride;
					elementBytes = b->Desc.StructureByteStride;
				}
				else
				{
					s.Format = desc->Format;
					elementBytes = DirectX::BitsPerPixel(desc->Format) / 8;
				}
			}
			else
			{
				Once("srv-buffer-nodesc", "%s", "buffer shader resource view without a description is not supported");
				return E_INVALIDARG;
			}
			if (!elementBytes || !s.Buffer.NumElements || (s.Buffer.FirstElement + s.Buffer.NumElements) * elementBytes > b->Desc.ByteWidth) return E_INVALIDARG;
			auto* v = new Srv(this);
			if (desc) v->Dsc = *desc;
			v->V.Res = r;
			v->V.Buffer = b;
			v->V.SrvDesc = s;
			v->V.ByteOffset = s.Buffer.FirstElement * elementBytes;
			v->V.ElementBytes = (UINT)elementBytes;
			v->V.Id = NextId();
			if (b->Shadow.empty())
			{
				v->V.Cpu = ViewHeap.Alloc();
				Device->CreateShaderResourceView(b->R.Res.Get(), &s, v->V.Cpu);
			}
			*out = v;
			return S_OK;
		}
		Image* t = ImageOf(r);
		if (!t || !t->R.Res || t->Usage == D3D11_USAGE_STAGING)
		{
			Once("srv-staging", "%s", "shader resource view of a staging texture is not supported");
			return E_INVALIDARG;
		}
		D3D11_SHADER_RESOURCE_VIEW_DESC d = {};
		if (desc) d = *desc;
		else
		{
			d.Format = t->Dxgi;
			if (t->Dim == D3D12_RESOURCE_DIMENSION_TEXTURE1D) { d.ViewDimension = t->R.Layers > 1 ? D3D11_SRV_DIMENSION_TEXTURE1DARRAY : D3D11_SRV_DIMENSION_TEXTURE1D; d.Texture1DArray.MipLevels = t->R.Mips; d.Texture1DArray.ArraySize = t->R.Layers; }
			else if (t->Dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D) { d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D; d.Texture3D.MipLevels = t->R.Mips; }
			else if (t->Samples > 1) d.ViewDimension = t->R.Layers > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D11_SRV_DIMENSION_TEXTURE2DMS, d.Texture2DMSArray.ArraySize = t->R.Layers;
			else if (t->Cube && t->R.Layers == 6) { d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE; d.TextureCube.MipLevels = t->R.Mips; }
			else if (t->Cube) { d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY; d.TextureCubeArray.MipLevels = t->R.Mips; d.TextureCubeArray.NumCubes = t->R.Layers / 6; }
			else if (t->R.Layers > 1) { d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY; d.Texture2DArray.MipLevels = t->R.Mips; d.Texture2DArray.ArraySize = t->R.Layers; }
			else { d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; d.Texture2D.MipLevels = t->R.Mips; }
		}
		auto mips = [&](UINT most, UINT count) { return count == (UINT)-1 ? t->R.Mips - (std::min)(most, t->R.Mips) : count; };
		D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Format = d.Format == DXGI_FORMAT_UNKNOWN ? t->Dxgi : d.Format;
		const UINT plane = (s.Format == DXGI_FORMAT_X24_TYPELESS_G8_UINT || s.Format == DXGI_FORMAT_X32_TYPELESS_G8X24_UINT) ? 1 : 0;
		UINT minLevel = 0, numLevels = 1, minLayer = 0, numLayers = 1;
		switch (d.ViewDimension)
		{
		case D3D11_SRV_DIMENSION_TEXTURE1D:
			minLevel = d.Texture1D.MostDetailedMip; numLevels = mips(minLevel, d.Texture1D.MipLevels);
			s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D; s.Texture1D = { minLevel, numLevels, 0.0f };
			break;
		case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
			minLevel = d.Texture1DArray.MostDetailedMip; numLevels = mips(minLevel, d.Texture1DArray.MipLevels);
			minLayer = d.Texture1DArray.FirstArraySlice; numLayers = d.Texture1DArray.ArraySize;
			s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY; s.Texture1DArray = { minLevel, numLevels, minLayer, numLayers, 0.0f };
			break;
		case D3D11_SRV_DIMENSION_TEXTURE2D:
			minLevel = d.Texture2D.MostDetailedMip; numLevels = mips(minLevel, d.Texture2D.MipLevels);
			s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; s.Texture2D = { minLevel, numLevels, plane, 0.0f };
			break;
		case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
			minLevel = d.Texture2DArray.MostDetailedMip; numLevels = mips(minLevel, d.Texture2DArray.MipLevels);
			minLayer = d.Texture2DArray.FirstArraySlice; numLayers = d.Texture2DArray.ArraySize;
			s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; s.Texture2DArray = { minLevel, numLevels, minLayer, numLayers, plane, 0.0f };
			break;
		case D3D11_SRV_DIMENSION_TEXTURECUBE:
			minLevel = d.TextureCube.MostDetailedMip; numLevels = mips(minLevel, d.TextureCube.MipLevels); numLayers = 6;
			s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; s.TextureCube = { minLevel, numLevels, 0.0f };
			break;
		case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
			minLevel = d.TextureCubeArray.MostDetailedMip; numLevels = mips(minLevel, d.TextureCubeArray.MipLevels);
			minLayer = d.TextureCubeArray.First2DArrayFace; numLayers = d.TextureCubeArray.NumCubes * 6;
			s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY; s.TextureCubeArray = { minLevel, numLevels, minLayer, d.TextureCubeArray.NumCubes, 0.0f };
			break;
		case D3D11_SRV_DIMENSION_TEXTURE3D:
			minLevel = d.Texture3D.MostDetailedMip; numLevels = mips(minLevel, d.Texture3D.MipLevels);
			s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D; s.Texture3D = { minLevel, numLevels, 0.0f };
			break;
		case D3D11_SRV_DIMENSION_TEXTURE2DMS:
			s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
			break;
		case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
			minLayer = d.Texture2DMSArray.FirstArraySlice; numLayers = d.Texture2DMSArray.ArraySize;
			s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY; s.Texture2DMSArray = { minLayer, numLayers };
			break;
		default:
			Once("srv-dim", "%s", "unsupported shader resource view dimension");
			return E_NOTIMPL;
		}
		numLevels = (std::min)(numLevels, t->R.Mips - (std::min)(minLevel, t->R.Mips));
		numLayers = (std::min)(numLayers, t->R.Layers - (std::min)(minLayer, t->R.Layers));
		if (!numLevels || !numLayers) return E_INVALIDARG;
		auto* v = new Srv(this);
		v->Dsc = d;
		v->V.Res = r;
		v->V.Img = t;
		v->V.BaseMip = minLevel;
		v->V.Mips = numLevels;
		v->V.BaseLayer = minLayer;
		v->V.Layers = numLayers;
		v->V.Format = s.Format;
		v->V.Id = NextId();
		v->V.Cpu = ViewHeap.Alloc();
		Device->CreateShaderResourceView(t->R.Res.Get(), &s, v->V.Cpu);
		*out = v;
		return S_OK;
	}

	HRESULT Dev::CreateRenderTargetView(GfxResource* r, const D3D11_RENDER_TARGET_VIEW_DESC* desc, GfxRenderTargetView** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		Image* t = ImageOf(r);
		if (!t || !t->R.Res || t->DepthFormat || t->Usage == D3D11_USAGE_STAGING) return E_INVALIDARG;
		auto* v = new Rtv(this);
		v->Heap = 1;
		D3D12_RENDER_TARGET_VIEW_DESC rv = {};
		UINT mip = 0, first = 0, count = 1;
		if (desc)
		{
			v->Dsc = *desc;
			rv.Format = desc->Format == DXGI_FORMAT_UNKNOWN ? t->Dxgi : desc->Format;
			switch (desc->ViewDimension)
			{
			case D3D11_RTV_DIMENSION_TEXTURE1D: mip = desc->Texture1D.MipSlice; rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D; rv.Texture1D.MipSlice = mip; break;
			case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
				mip = desc->Texture1DArray.MipSlice; first = desc->Texture1DArray.FirstArraySlice; count = desc->Texture1DArray.ArraySize;
				rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY; rv.Texture1DArray = { mip, first, count };
				break;
			case D3D11_RTV_DIMENSION_TEXTURE2D: mip = desc->Texture2D.MipSlice; rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; rv.Texture2D = { mip, 0 }; break;
			case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
				mip = desc->Texture2DArray.MipSlice; first = desc->Texture2DArray.FirstArraySlice; count = desc->Texture2DArray.ArraySize;
				rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY; rv.Texture2DArray = { mip, first, count, 0 };
				break;
			case D3D11_RTV_DIMENSION_TEXTURE2DMS: rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS; break;
			case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
				first = desc->Texture2DMSArray.FirstArraySlice; count = desc->Texture2DMSArray.ArraySize;
				rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY; rv.Texture2DMSArray = { first, count };
				break;
			case D3D11_RTV_DIMENSION_TEXTURE3D:
				mip = desc->Texture3D.MipSlice;
				rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D; rv.Texture3D = { mip, desc->Texture3D.FirstWSlice, desc->Texture3D.WSize };
				first = 0; count = 1;   // 3D 서브리소스 = 밉 하나 (깊이 조각 전체)
				break;
			default:
				v->Release();
				return E_NOTIMPL;
			}
		}
		else
		{
			rv.Format = t->Dxgi;
			if (t->Dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D) { rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D; rv.Texture3D = { 0, 0, (UINT)-1 }; v->Dsc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D; }
			else if (t->Samples > 1) { rv.ViewDimension = t->R.Layers > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D12_RTV_DIMENSION_TEXTURE2DMS; rv.Texture2DMSArray = { 0, t->R.Layers }; count = t->R.Layers; }
			else if (t->R.Layers > 1) { rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY; rv.Texture2DArray = { 0, 0, t->R.Layers, 0 }; count = t->R.Layers; v->Dsc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY; v->Dsc.Texture2DArray.ArraySize = t->R.Layers; }
			else if (t->Dim == D3D12_RESOURCE_DIMENSION_TEXTURE1D) { rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D; v->Dsc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1D; }
			else { rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; v->Dsc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; }
			v->Dsc.Format = t->Dxgi;
		}
		if (count == (UINT)-1) count = t->R.Layers - (std::min)(first, t->R.Layers);
		v->V.Res = r;
		v->V.Img = t;
		v->V.Format = rv.Format;
		v->V.BaseMip = mip;
		v->V.Mips = 1;
		v->V.BaseLayer = (std::min)(first, t->R.Layers - 1);
		v->V.Layers = (std::max)(1u, (std::min)(count, t->R.Layers - v->V.BaseLayer));
		v->V.Id = NextId();
		v->V.Cpu = RtvHeap.Alloc();
		Device->CreateRenderTargetView(t->R.Res.Get(), &rv, v->V.Cpu);
		*out = v;
		return S_OK;
	}

	HRESULT Dev::CreateDepthStencilView(GfxResource* r, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, GfxDepthStencilView** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		Image* t = ImageOf(r);
		if (!t || !t->R.Res || !(t->Bind & D3D11_BIND_DEPTH_STENCIL)) return E_INVALIDARG;
		auto* v = new Dsv(this);
		v->Heap = 2;
		D3D12_DEPTH_STENCIL_VIEW_DESC dv = {};
		UINT mip = 0, first = 0, count = 1;
		if (desc)
		{
			v->Dsc = *desc;
			dv.Format = desc->Format == DXGI_FORMAT_UNKNOWN ? DepthViewFormat(t->Dxgi) : desc->Format;
			dv.Flags = (D3D12_DSV_FLAGS)desc->Flags;
			switch (desc->ViewDimension)
			{
			case D3D11_DSV_DIMENSION_TEXTURE1D: mip = desc->Texture1D.MipSlice; dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D; dv.Texture1D.MipSlice = mip; break;
			case D3D11_DSV_DIMENSION_TEXTURE1DARRAY:
				mip = desc->Texture1DArray.MipSlice; first = desc->Texture1DArray.FirstArraySlice; count = desc->Texture1DArray.ArraySize;
				dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY; dv.Texture1DArray = { mip, first, count };
				break;
			case D3D11_DSV_DIMENSION_TEXTURE2D: mip = desc->Texture2D.MipSlice; dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; dv.Texture2D.MipSlice = mip; break;
			case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
				mip = desc->Texture2DArray.MipSlice; first = desc->Texture2DArray.FirstArraySlice; count = desc->Texture2DArray.ArraySize;
				dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY; dv.Texture2DArray = { mip, first, count };
				break;
			case D3D11_DSV_DIMENSION_TEXTURE2DMS: dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS; break;
			case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:
				first = desc->Texture2DMSArray.FirstArraySlice; count = desc->Texture2DMSArray.ArraySize;
				dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY; dv.Texture2DMSArray = { first, count };
				break;
			default:
				v->Release();
				return E_NOTIMPL;
			}
		}
		else
		{
			dv.Format = DepthViewFormat(t->Dxgi);
			v->Dsc.Format = dv.Format;
			if (t->Samples > 1) { dv.ViewDimension = t->R.Layers > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D12_DSV_DIMENSION_TEXTURE2DMS; dv.Texture2DMSArray = { 0, t->R.Layers }; count = t->R.Layers; }
			else if (t->R.Layers > 1) { dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY; dv.Texture2DArray = { 0, 0, t->R.Layers }; count = t->R.Layers; v->Dsc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY; v->Dsc.Texture2DArray.ArraySize = t->R.Layers; }
			else { dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; v->Dsc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D; }
		}
		if (count == (UINT)-1) count = t->R.Layers - (std::min)(first, t->R.Layers);
		v->V.Res = r;
		v->V.Img = t;
		v->V.Format = dv.Format;
		v->V.BaseMip = mip;
		v->V.Mips = 1;
		v->V.BaseLayer = (std::min)(first, t->R.Layers - 1);
		v->V.Layers = (std::max)(1u, (std::min)(count, t->R.Layers - v->V.BaseLayer));
		// 깊이만 읽기 전용이어도 깊이 쓰기를 막는다 (스텐실만 쓰는 경우는 쓰지 않는다)
		v->V.ReadOnly = (v->Dsc.Flags & D3D11_DSV_READ_ONLY_DEPTH) != 0;
		if (v->V.ReadOnly && HasStencil(dv.Format)) dv.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
		v->V.Id = NextId();
		v->V.Cpu = DsvHeap.Alloc();
		Device->CreateDepthStencilView(t->R.Res.Get(), &dv, v->V.Cpu);
		*out = v;
		return S_OK;
	}

	HRESULT Dev::CreateUnorderedAccessView(GfxResource* r, const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc, GfxUnorderedAccessView** out)
	{
		if (!out) return S_FALSE;
		*out = nullptr;
		if (Buf* b = BufOf(r))
		{
			if (!b->R.Res || !(b->Desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)) return E_INVALIDARG;
			if (!desc || desc->ViewDimension != D3D11_UAV_DIMENSION_BUFFER || (desc->Buffer.Flags & (D3D11_BUFFER_UAV_FLAG_APPEND | D3D11_BUFFER_UAV_FLAG_COUNTER)))
			{
				Once("uav-buffer", "%s", "only typed / structured / raw buffer UAVs without append or counter are supported");
				return E_NOTIMPL;
			}
			D3D12_UNORDERED_ACCESS_VIEW_DESC u = {};
			u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			u.Buffer.FirstElement = desc->Buffer.FirstElement;
			u.Buffer.NumElements = desc->Buffer.NumElements;
			UINT64 elementBytes;
			if (desc->Buffer.Flags & D3D11_BUFFER_UAV_FLAG_RAW)
			{
				u.Format = DXGI_FORMAT_R32_TYPELESS;
				u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
				elementBytes = 4;
			}
			else if ((b->Desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) && b->Desc.StructureByteStride)
			{
				u.Format = DXGI_FORMAT_UNKNOWN;
				u.Buffer.StructureByteStride = b->Desc.StructureByteStride;
				elementBytes = b->Desc.StructureByteStride;
			}
			else
			{
				u.Format = desc->Format;
				elementBytes = DirectX::BitsPerPixel(desc->Format) / 8;
			}
			if (!elementBytes || !u.Buffer.NumElements || (u.Buffer.FirstElement + u.Buffer.NumElements) * elementBytes > b->Desc.ByteWidth) return E_INVALIDARG;
			auto* v = new Uav(this);
			v->Dsc = *desc;
			v->V.Res = r;
			v->V.Buffer = b;
			v->V.ByteOffset = u.Buffer.FirstElement * elementBytes;
			v->V.ElementBytes = (UINT)elementBytes;
			v->V.Id = NextId();
			v->V.Cpu = ViewHeap.Alloc();
			Device->CreateUnorderedAccessView(b->R.Res.Get(), nullptr, &u, v->V.Cpu);
			*out = v;
			return S_OK;
		}
		Image* t = ImageOf(r);
		if (!t || !t->R.Res || !(t->Bind & D3D11_BIND_UNORDERED_ACCESS) || t->DepthFormat) return E_INVALIDARG;
		D3D12_UNORDERED_ACCESS_VIEW_DESC u = {};
		UINT mip = 0, first = 0, count = 1;
		const D3D11_UAV_DIMENSION dim = desc ? desc->ViewDimension
			: t->Dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? D3D11_UAV_DIMENSION_TEXTURE3D
			: t->R.Layers > 1 ? D3D11_UAV_DIMENSION_TEXTURE2DARRAY : D3D11_UAV_DIMENSION_TEXTURE2D;
		u.Format = desc && desc->Format != DXGI_FORMAT_UNKNOWN ? desc->Format : t->Dxgi;
		switch (dim)
		{
		case D3D11_UAV_DIMENSION_TEXTURE1D: mip = desc ? desc->Texture1D.MipSlice : 0; u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D; u.Texture1D.MipSlice = mip; break;
		case D3D11_UAV_DIMENSION_TEXTURE1DARRAY:
			mip = desc ? desc->Texture1DArray.MipSlice : 0; first = desc ? desc->Texture1DArray.FirstArraySlice : 0; count = desc ? desc->Texture1DArray.ArraySize : t->R.Layers;
			u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY; u.Texture1DArray = { mip, first, count };
			break;
		case D3D11_UAV_DIMENSION_TEXTURE2D: mip = desc ? desc->Texture2D.MipSlice : 0; u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; u.Texture2D = { mip, 0 }; break;
		case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
			mip = desc ? desc->Texture2DArray.MipSlice : 0; first = desc ? desc->Texture2DArray.FirstArraySlice : 0; count = desc ? desc->Texture2DArray.ArraySize : t->R.Layers;
			u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY; u.Texture2DArray = { mip, first, count, 0 };
			break;
		case D3D11_UAV_DIMENSION_TEXTURE3D:
			mip = desc ? desc->Texture3D.MipSlice : 0;
			u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D; u.Texture3D = { mip, desc ? desc->Texture3D.FirstWSlice : 0, desc ? desc->Texture3D.WSize : (UINT)-1 };
			first = 0; count = 1;
			break;
		default:
			Once("uav-dim", "%s", "unsupported unordered access view dimension");
			return E_NOTIMPL;
		}
		if (mip >= t->R.Mips || first >= t->R.Layers) return E_INVALIDARG;
		if (count == (UINT)-1) count = t->R.Layers - first;
		auto* v = new Uav(this);
		if (desc) v->Dsc = *desc;
		else { v->Dsc.Format = t->Dxgi; v->Dsc.ViewDimension = dim; }
		v->V.Res = r;
		v->V.Img = t;
		v->V.Format = u.Format;
		v->V.BaseMip = mip;
		v->V.Mips = 1;
		v->V.BaseLayer = first;
		v->V.Layers = (std::max)(1u, (std::min)(count, t->R.Layers - first));
		v->V.Id = NextId();
		v->V.Cpu = ViewHeap.Alloc();
		Device->CreateUnorderedAccessView(t->R.Res.Get(), nullptr, &u, v->V.Cpu);
		*out = v;
		return S_OK;
	}

	HRESULT Dev::CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* elements, UINT count, const void*, SIZE_T, GfxInputLayout** out)
	{
		// D3D12 입력 배치 = D3D11 과 같은 모양 (의미 이름 · 형식 · 칸 · 자리). 셰이더 입력과는 PSO 를 만들 때 맞춘다
		if (!out) return S_FALSE;
		*out = nullptr;
		auto* l = new InputLayout(this);
		l->Id = NextId();
		for (UINT i = 0; i < count; ++i)
			l->Names.push_back(elements[i].SemanticName ? elements[i].SemanticName : "");
		for (UINT i = 0; i < count; ++i)
		{
			const D3D11_INPUT_ELEMENT_DESC& e = elements[i];
			D3D12_INPUT_ELEMENT_DESC d = {};
			d.SemanticName = l->Names[i].c_str();
			d.SemanticIndex = e.SemanticIndex;
			d.Format = e.Format;
			d.InputSlot = e.InputSlot;
			d.AlignedByteOffset = e.AlignedByteOffset;
			d.InputSlotClass = (D3D12_INPUT_CLASSIFICATION)e.InputSlotClass;
			d.InstanceDataStepRate = e.InstanceDataStepRate;
			l->Elements.push_back(d);
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
		D3D12_SAMPLER_DESC d = {};
		d.Filter = (D3D12_FILTER)desc->Filter;   // D3D11 · D3D12 필터 값은 같다
		d.AddressU = (D3D12_TEXTURE_ADDRESS_MODE)desc->AddressU;
		d.AddressV = (D3D12_TEXTURE_ADDRESS_MODE)desc->AddressV;
		d.AddressW = (D3D12_TEXTURE_ADDRESS_MODE)desc->AddressW;
		d.MipLODBias = desc->MipLODBias;
		d.MaxAnisotropy = std::clamp(desc->MaxAnisotropy, 1u, 16u);
		d.ComparisonFunc = desc->ComparisonFunc ? (D3D12_COMPARISON_FUNC)desc->ComparisonFunc : D3D12_COMPARISON_FUNC_NEVER;
		for (int i = 0; i < 4; ++i) d.BorderColor[i] = desc->BorderColor[i];
		d.MinLOD = desc->MinLOD;
		d.MaxLOD = desc->MaxLOD;
		s->Cpu = SamplerHeap.Alloc();
		Device->CreateSampler(&d, s->Cpu);
		s->Id = NextId();
		*out = s;
		return S_OK;
	}

	HRESULT Dev::CreateQuery(const D3D11_QUERY_DESC* desc, GfxQuery** out)
	{
		auto* q = new Query(this);
		q->Dsc = *desc;
		D3D12_QUERY_HEAP_DESC hd = {};
		hd.Count = 1;
		UINT64 resultBytes = 8;
		bool heap = true;
		switch (desc->Query)
		{
		case D3D11_QUERY_TIMESTAMP: hd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; q->Type = D3D12_QUERY_TYPE_TIMESTAMP; break;
		case D3D11_QUERY_OCCLUSION: hd.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION; q->Type = D3D12_QUERY_TYPE_OCCLUSION; break;
		case D3D11_QUERY_OCCLUSION_PREDICATE: hd.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION; q->Type = D3D12_QUERY_TYPE_BINARY_OCCLUSION; break;
		case D3D11_QUERY_PIPELINE_STATISTICS:
			hd.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS; q->Type = D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
			resultBytes = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
			break;
		default: heap = false; break;   // TIMESTAMP_DISJOINT · EVENT: GPU 쿼리 없이
		}
		if (heap)
		{
			const D3D12_HEAP_PROPERTIES hp = HeapProps(D3D12_HEAP_TYPE_READBACK);
			const D3D12_RESOURCE_DESC rd = BufferDesc(resultBytes);
			if (FAILED(Device->CreateQueryHeap(&hd, IID_PPV_ARGS(&q->Heap))) ||
				FAILED(Device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&q->Readback))))
			{
				q->Heap = nullptr;
				q->Readback = nullptr;
			}
			else
			{
				void* p = nullptr;
				q->Readback->Map(0, nullptr, &p);
				q->ReadbackCpu = static_cast<uint8_t*>(p);
			}
			if (q->Heap && desc->Query == D3D11_QUERY_OCCLUSION_PREDICATE)
			{
				const D3D12_HEAP_PROPERTIES dp = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
				const D3D12_RESOURCE_DESC pd = BufferDesc(8);
				if (SUCCEEDED(Device->CreateCommittedResource(&dp, D3D12_HEAP_FLAG_NONE, &pd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&q->Predicate.Res))))
				{
					q->Predicate.Buffer = true;
					q->Predicate.States.assign(1, D3D12_RESOURCE_STATE_COMMON);
				}
			}
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
		bool IsDualSource(D3D11_BLEND b) { return b == D3D11_BLEND_SRC1_COLOR || b == D3D11_BLEND_INV_SRC1_COLOR || b == D3D11_BLEND_SRC1_ALPHA || b == D3D11_BLEND_INV_SRC1_ALPHA; }
		// 알파 쪽 블렌드에는 색 인수가 올 수 없다 (D3D12 는 PSO 를 만들지 않는다) → 같은 뜻의 알파 인수로
		D3D12_BLEND AlphaBlend(D3D11_BLEND b)
		{
			switch (b)
			{
			case D3D11_BLEND_SRC_COLOR: return D3D12_BLEND_SRC_ALPHA;
			case D3D11_BLEND_INV_SRC_COLOR: return D3D12_BLEND_INV_SRC_ALPHA;
			case D3D11_BLEND_DEST_COLOR: return D3D12_BLEND_DEST_ALPHA;
			case D3D11_BLEND_INV_DEST_COLOR: return D3D12_BLEND_INV_DEST_ALPHA;
			case D3D11_BLEND_SRC1_COLOR: return D3D12_BLEND_SRC1_ALPHA;
			case D3D11_BLEND_INV_SRC1_COLOR: return D3D12_BLEND_INV_SRC1_ALPHA;
			default: return (D3D12_BLEND)b;
			}
		}
	}

	bool Dev::GetPipeline(const PipelineKey& key, Program* program, InputLayout* layout, const D3D11_RASTERIZER_DESC& rs, const D3D11_BLEND_DESC& bs,
		const D3D11_DEPTH_STENCIL_DESC& ds, ID3D12PipelineState** out)
	{
		auto it = Pipelines.find(key);
		if (it != Pipelines.end())
		{
			*out = it->second.Get();
			return *out != nullptr;
		}
		const auto t0 = std::chrono::steady_clock::now();
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
		pd.pRootSignature = program->Root.Get();
		for (const auto& s : program->Stages)
		{
			const D3D12_SHADER_BYTECODE bc = { s.Dxil.data(), s.Dxil.size() };
			switch (s.Type)
			{
			case GfxD3D12Shared::StageType::Vertex: pd.VS = bc; break;
			case GfxD3D12Shared::StageType::Hull: pd.HS = bc; break;
			case GfxD3D12Shared::StageType::Domain: pd.DS = bc; break;
			case GfxD3D12Shared::StageType::Geometry: pd.GS = bc; break;
			case GfxD3D12Shared::StageType::Pixel: pd.PS = bc; break;
			default: break;
			}
		}
		pd.BlendState.AlphaToCoverageEnable = bs.AlphaToCoverageEnable;
		pd.BlendState.IndependentBlendEnable = TRUE;   // 칸마다 쓰기 마스크가 다를 수 있다 (쓰지 않는 출력 = 0)
		for (UINT i = 0; i < 8; ++i)
		{
			const D3D11_RENDER_TARGET_BLEND_DESC& b = bs.RenderTarget[bs.IndependentBlendEnable ? i : 0];
			D3D12_RENDER_TARGET_BLEND_DESC& a = pd.BlendState.RenderTarget[i];
			const bool used = i < key.ColorCount && key.Colors[i] != DXGI_FORMAT_UNKNOWN && (program->PixelOutputs & (1u << i));
			const bool dual = IsDualSource(b.SrcBlend) || IsDualSource(b.DestBlend) || IsDualSource(b.SrcBlendAlpha) || IsDualSource(b.DestBlendAlpha);
			a.BlendEnable = used && b.BlendEnable && !IsIntegerFormat(key.Colors[i]) && (!dual || i == 0);
			a.SrcBlend = (D3D12_BLEND)b.SrcBlend;
			a.DestBlend = (D3D12_BLEND)b.DestBlend;
			a.BlendOp = (D3D12_BLEND_OP)b.BlendOp;
			a.SrcBlendAlpha = AlphaBlend(b.SrcBlendAlpha);
			a.DestBlendAlpha = AlphaBlend(b.DestBlendAlpha);
			a.BlendOpAlpha = (D3D12_BLEND_OP)b.BlendOpAlpha;
			a.LogicOp = D3D12_LOGIC_OP_NOOP;
			a.RenderTargetWriteMask = used ? (UINT8)(b.RenderTargetWriteMask & 0xF) : 0;
		}
		pd.SampleMask = key.SampleMask;
		pd.RasterizerState.FillMode = (D3D12_FILL_MODE)rs.FillMode;
		pd.RasterizerState.CullMode = (D3D12_CULL_MODE)rs.CullMode;
		pd.RasterizerState.FrontCounterClockwise = rs.FrontCounterClockwise;
		pd.RasterizerState.DepthBias = rs.DepthBias;
		pd.RasterizerState.DepthBiasClamp = rs.DepthBiasClamp;
		pd.RasterizerState.SlopeScaledDepthBias = rs.SlopeScaledDepthBias;
		pd.RasterizerState.DepthClipEnable = rs.DepthClipEnable;
		pd.RasterizerState.MultisampleEnable = rs.MultisampleEnable;
		pd.RasterizerState.AntialiasedLineEnable = rs.AntialiasedLineEnable;
		const bool hasDepth = key.DepthFormat != DXGI_FORMAT_UNKNOWN;
		pd.DepthStencilState.DepthEnable = hasDepth && ds.DepthEnable;
		pd.DepthStencilState.DepthWriteMask = ds.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL && !key.ReadOnlyDepth ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		pd.DepthStencilState.DepthFunc = (D3D12_COMPARISON_FUNC)ds.DepthFunc;
		pd.DepthStencilState.StencilEnable = hasDepth && ds.StencilEnable && HasStencil(key.DepthFormat);
		pd.DepthStencilState.StencilReadMask = ds.StencilReadMask;
		pd.DepthStencilState.StencilWriteMask = key.ReadOnlyDepth ? 0 : ds.StencilWriteMask;
		auto face = [](const D3D11_DEPTH_STENCILOP_DESC& f) {
			return D3D12_DEPTH_STENCILOP_DESC{ (D3D12_STENCIL_OP)f.StencilFailOp, (D3D12_STENCIL_OP)f.StencilDepthFailOp, (D3D12_STENCIL_OP)f.StencilPassOp, (D3D12_COMPARISON_FUNC)f.StencilFunc };
		};
		pd.DepthStencilState.FrontFace = face(ds.FrontFace);
		pd.DepthStencilState.BackFace = face(ds.BackFace);
		if (pd.DepthStencilState.DepthFunc == 0) pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		if (layout)
			pd.InputLayout = { layout->Elements.data(), (UINT)layout->Elements.size() };
		pd.IBStripCutValue = key.Cut == 1 ? D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF : key.Cut == 2 ? D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF : D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
		pd.PrimitiveTopologyType = (D3D12_PRIMITIVE_TOPOLOGY_TYPE)key.TopologyType;
		pd.NumRenderTargets = key.ColorCount;
		for (UINT i = 0; i < key.ColorCount; ++i)
			pd.RTVFormats[i] = key.Colors[i];
		pd.DSVFormat = key.DepthFormat;
		pd.SampleDesc.Count = (std::max<UINT>)(1, key.Samples);
		ComPtr<ID3D12PipelineState> p;
		const HRESULT hr = Device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&p));
		if (FAILED(hr))
		{
			char buf[64];
			snprintf(buf, sizeof(buf), "hr=0x%08X", (unsigned)hr);
			Once("pipe:" + program->Name, "%s", (program->Name + ": CreateGraphicsPipelineState failed: " + buf).c_str());
			DrainMessages();
			CheckRemoved(hr, "CreateGraphicsPipelineState");
			p = nullptr;
		}
		Pipelines[key] = p;   // 실패도 기억 (다시 만들지 않는다)
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		if (ms > 50.0)
			EditorLog::Write("DX12", "pipeline %s took %.0f ms (%zu pipelines)", program->Name.c_str(), ms, Pipelines.size());
		*out = p.Get();
		return p != nullptr;
	}

	void Dev::ForgetProgram(uint64_t id)
	{
		for (auto it = Pipelines.begin(); it != Pipelines.end();)
		{
			if (it->first.Program == id)
			{
				DeferRelease(it->second);
				it = Pipelines.erase(it);
			}
			else ++it;
		}
	}
}

// ============================================================ 효과 쪽 (GfxD3D12Shared)
namespace GfxD3D12Shared
{
	using namespace GfxD3D12Impl;

	HRESULT CreateProgram(GfxDevice* device, const StageCode* stages, uint32_t count, uint32_t pixelOutputs, const std::string& name, GfxObject** out, std::string& error)
	{
		*out = nullptr;
		auto* d = static_cast<Dev*>(device);
		auto* p = new Program(d);
		p->PixelOutputs = pixelOutputs;
		p->Name = name;
		p->Id = NextId();
		std::vector<D3D12_ROOT_PARAMETER> params;
		std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> ranges;   // 매개변수마다 (주소가 바뀌지 않게 먼저 다 만든다)
		ranges.reserve(count * 2);
		for (uint32_t i = 0; i < count; ++i)
		{
			const StageCode& sc = stages[i];
			if (sc.Stage == StageType::Compute && count != 1)
			{
				error = "a compute pass must have only the compute shader";
				p->Release();
				return E_INVALIDARG;
			}
			Program::Stage st;
			st.Type = sc.Stage;
			st.Dxil = *sc.Dxil;
			// 레지스터 범위 (0 .. 최대): 빈 칸은 널 디스크립터
			UINT samplers = 0;
			for (const StageSlot& s : sc.Slots)
			{
				switch (s.Kind)
				{
				case SlotKind::Cbv: st.CbvCount = (std::max)(st.CbvCount, s.Register + 1); break;
				case SlotKind::Srv: st.SrvCount = (std::max)(st.SrvCount, s.Register + 1); break;
				case SlotKind::Uav: st.UavCount = (std::max)(st.UavCount, s.Register + 1); break;
				case SlotKind::Sampler: samplers = (std::max)(samplers, s.Register + 1); break;
				}
			}
			st.Resources.resize(st.CbvCount + st.SrvCount + st.UavCount);
			for (UINT k = 0; k < st.CbvCount; ++k) { st.Resources[k].Kind = SlotKind::Cbv; st.Resources[k].Register = k; }
			for (UINT k = 0; k < st.SrvCount; ++k) { auto& r = st.Resources[st.CbvCount + k]; r.Kind = SlotKind::Srv; r.Register = k; r.Dimension = D3D_SRV_DIMENSION_TEXTURE2D; }
			for (UINT k = 0; k < st.UavCount; ++k) { auto& r = st.Resources[st.CbvCount + st.SrvCount + k]; r.Kind = SlotKind::Uav; r.Register = k; r.Dimension = D3D_SRV_DIMENSION_TEXTURE2D; }
			st.Samplers.resize(samplers);
			for (UINT k = 0; k < samplers; ++k) { st.Samplers[k].Kind = SlotKind::Sampler; st.Samplers[k].Register = k; }
			for (const StageSlot& s : sc.Slots)
			{
				switch (s.Kind)
				{
				case SlotKind::Cbv: st.Resources[s.Register] = s; break;
				case SlotKind::Srv: st.Resources[st.CbvCount + s.Register] = s; break;
				case SlotKind::Uav: st.Resources[st.CbvCount + st.SrvCount + s.Register] = s; p->HasUav = true; break;
				case SlotKind::Sampler: st.Samplers[s.Register] = s; break;
				}
			}
			const D3D12_SHADER_VISIBILITY vis = Visibility(sc.Stage);
			if (!st.Resources.empty())
			{
				std::vector<D3D12_DESCRIPTOR_RANGE> rg;
				if (st.CbvCount) rg.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_CBV, st.CbvCount, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND });
				if (st.SrvCount) rg.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, st.SrvCount, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND });
				if (st.UavCount) rg.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_UAV, st.UavCount, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND });
				ranges.push_back(std::move(rg));
				D3D12_ROOT_PARAMETER rp = {};
				rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				rp.DescriptorTable = { (UINT)ranges.back().size(), ranges.back().data() };
				rp.ShaderVisibility = vis;
				st.ResourceParam = (int)params.size();
				params.push_back(rp);
			}
			if (!st.Samplers.empty())
			{
				ranges.push_back({ { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, (UINT)st.Samplers.size(), 0, 0, 0 } });
				D3D12_ROOT_PARAMETER rp = {};
				rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				rp.DescriptorTable = { 1, ranges.back().data() };
				rp.ShaderVisibility = vis;
				st.SamplerParam = (int)params.size();
				params.push_back(rp);
			}
			if (sc.Stage == StageType::Hull || sc.Stage == StageType::Domain) p->Tessellation = true;
			if (sc.Stage == StageType::Compute) p->IsCompute = true;
			p->Stages.push_back(std::move(st));
		}
		D3D12_ROOT_SIGNATURE_DESC rd = {};
		rd.NumParameters = (UINT)params.size();
		rd.pParameters = params.data();
		rd.Flags = p->IsCompute ? D3D12_ROOT_SIGNATURE_FLAG_NONE : D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		ComPtr<ID3DBlob> blob, err;
		HRESULT hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
		if (SUCCEEDED(hr))
			hr = d->Device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&p->Root));
		if (FAILED(hr))
		{
			error = err ? std::string((const char*)err->GetBufferPointer(), err->GetBufferSize()) : "root signature failed";
			p->Release();
			return E_FAIL;
		}
		if (p->IsCompute)
		{
			// compute 파이프라인 = 셰이더 + 루트뿐 (그리기 상태 · 타깃과 상관없다) → 지금 만든다
			D3D12_COMPUTE_PIPELINE_STATE_DESC cd = {};
			cd.pRootSignature = p->Root.Get();
			cd.CS = { p->Stages[0].Dxil.data(), p->Stages[0].Dxil.size() };
			hr = d->Device->CreateComputePipelineState(&cd, IID_PPV_ARGS(&p->Compute));
			if (FAILED(hr))
			{
				char buf[64];
				snprintf(buf, sizeof(buf), "CreateComputePipelineState hr=0x%08X", (unsigned)hr);
				error = buf;
				d->DrainMessages();
				p->Release();
				return E_FAIL;
			}
		}
		*out = p;
		return S_OK;
	}

	bool WriteConstants(GfxDevice* device, const void* data, uint32_t size, RingLoc& loc)
	{
		auto* d = static_cast<Dev*>(device);
		UploadLoc u;
		if (!d->Upload(AlignUp(size, 256), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, u)) return false;
		memcpy(u.Cpu, data, size);
		loc = { u.Gpu, u.Chunk, u.Generation };
		return true;
	}

	bool IsCurrent(GfxDevice* device, const RingLoc& loc)
	{
		auto* d = static_cast<Dev*>(device);
		return d->Current && loc.Chunk == d->Current && loc.Generation == d->Current->Generation;
	}
}

// ============================================================ 공개 함수
std::unique_ptr<Rhi::Device> CreateD3D12RhiDevice(GfxDevice* device, GfxContext* context, std::string& error);   // D3D12Rhi.cpp

namespace GfxD3D12
{
	using namespace GfxD3D12Impl;

	bool CreateDevice(HWND window, GfxDevice** device, GfxContext** context, std::string& error)
	{
		*device = nullptr;
		*context = nullptr;
		auto* d = new Dev();
		if (!d->Init(window, error))
		{
			EditorLog::Write("DX12", "device creation failed: %s", error.c_str());
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
		return CreateD3D12RhiDevice(device, context, error);
	}

	bool IsD3D12(const GfxObject* object) { return object && object->Api() == GfxApi::DirectX12; }

	void WaitIdle(GfxDevice* device)
	{
		auto* d = static_cast<Dev*>(device);
		if (d->Immediate) d->Immediate->FinishAsync();
		d->Submit(true);
		d->WaitCompute(d->ComputeSubmitted);
	}

	bool IsFormatSupported(DXGI_FORMAT format)
	{
		return format != DXGI_FORMAT_UNKNOWN && DirectX::IsValid(format) && !DirectX::IsVideo(format);
	}

	std::string Description(GfxDevice* device)
	{
		auto* d = static_cast<Dev*>(device);
		return "Direct3D 12 (" + d->Name + (d->Debug ? ", debug layer" : "") + ")";
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
