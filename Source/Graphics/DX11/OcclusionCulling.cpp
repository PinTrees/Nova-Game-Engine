#include "pch.h"
#include "OcclusionCulling.h"
#include "MeshGeometry.h"
#include "RenderStats.h"
#include "EditorLog.h"
#include "CliServer.h"
#include "Application.h"
#include "SceneCulling.h"
#include "Profiler.h"
#include "RenderManager.h"

namespace
{
	OcclusionCulling::Stats s_Stats[2];   // Game · Scene 뷰
}

#ifndef __ANDROID__

namespace
{
	using Microsoft::WRL::ComPtr;

	// 57. OcclusionCulling.hlsl 의 cbOcclusion 과 같은 배치
	struct Constants
	{
		float ViewProj[16];
		float ViewSize[2];
		uint32_t ViewOrigin[2];
		uint32_t SrcSize[2];
		uint32_t DstSize[2];
		uint32_t Count, Frame, Mode, Levels;
		uint32_t Stride, Pad[3];
	};
	static_assert(sizeof(Constants) == 128, "cbOcclusion");

	enum Kernel { KPrepare, KCompact, KReduce, KCull, KList, KShadow, KCount };
	ComPtr<ID3D11ComputeShader> s_Kernels[KCount];
	int s_KernelState = 0;   // 0 아직, 1 됨, -1 실패 (다시 시도하지 않는다)

	struct Buf
	{
		ComPtr<ID3D11Buffer> B;
		ComPtr<ID3D11ShaderResourceView> Srv;
		ComPtr<ID3D11UnorderedAccessView> Uav;
		UINT Bytes = 0;
	};

	struct View
	{
		uint32_t Frame = 0;          // 이 뷰를 그린 횟수 (기록 값)
		uint32_t LastUsed = 0;       // SceneCulling::FrameIndex
		bool Editor = false;
		Buf History;                 // 렌더러 자리마다 마지막으로 보인 Frame
		ComPtr<ID3D11Texture2D> HiZ;
		std::vector<ComPtr<ID3D11ShaderResourceView>> MipSrv;
		std::vector<ComPtr<ID3D11UnorderedAccessView>> MipUav;
		ComPtr<ID3D11ShaderResourceView> AllSrv;
		UINT W = 0, H = 0, Levels = 0;
		UINT FailedW = 0, FailedH = 0;   // 만들지 못한 크기 (프레임마다 다시 시도하지 않는다)
		uint32_t Calls = 0;              // Begin 횟수
		uint32_t SkipUntil = 0;          // 가린 것이 거의 없으면 이 횟수까지 쉰다 (Hi-Z 비용이 아낀 것보다 크다)
		ComPtr<ID3D11Buffer> Staging[3];   // 검사 · 보임 수 (몇 프레임 뒤에 읽는다)
		ComPtr<ID3D11Query> Ready[3];      // 그 복사가 끝났는가 (DONOTFLUSH 로 묻는다 — Map 이 명령을 밀어 넣어 GPU 가 쉬지 않게)
		std::vector<ComPtr<ID3D11Predicate>> Predicates;   // Skinned Mesh Renderer 상자 쿼리 (뷰마다 — 다음 프레임에 결과를 읽는다)
		size_t PredicatesUsed = 0;
		Buf ListCounters;                  // 인스턴스 목록 (나무) 검사 · 보임 수 — 다음 프레임 Finish 에서 Staging 으로
		bool ListCountersUsed = false;
		Buf ShadowCounters;                // 그림자 캐스터 검사 · 보임 수 (캐스케이드를 모두 더함) — 같은 방식
		bool ShadowCountersUsed = false;
		bool Pending[3] = {};
		int Next = 0;
	};
	std::unordered_map<const void*, View> s_Views;

	// 프레임 하나 (Begin ~ 본 패스) 동안 쓰는 버퍼 — 뷰끼리 차례로 다시 쓴다 (GPU 는 명령 순서대로)
	Buf s_Casters, s_Worlds, s_Items[2], s_Flags, s_Counters, s_Args[OcclusionCulling::SetCount], s_Out[OcclusionCulling::SetCount];
	ComPtr<ID3D11Buffer> s_Constants;
	std::vector<OcclusionCulling::Batch> s_Batches[2];
	std::vector<OcclusionCulling::Batch> s_ShadowBatches;   // 그림자 (깊이 묶음, 캐스케이드마다 다시)
	ComPtr<ID3D11Texture2D> s_DepthTex;   // 깊이 SRV 를 만든 텍스처 (같으면 다시 쓴다)
	ComPtr<ID3D11ShaderResourceView> s_DepthSrv;

	// Skinned Mesh Renderer 오클루전 예측 쿼리 (이 뷰 — RenderManager::ViewSerial 이 같을 때만 쓴다)
	ComPtr<ID3D11VertexShader> s_BoxVS;
	ComPtr<ID3D11Buffer> s_BoxCB;
	ComPtr<ID3D11DepthStencilState> s_BoxDSS;
	ComPtr<ID3D11BlendState> s_BoxBS;
	ComPtr<ID3D11RasterizerState> s_BoxRS;
	int s_BoxState = 0;   // 0 아직, 1 됨, -1 실패
	std::unordered_map<const void*, ID3D11Predicate*> s_PredicateOf;
	uint32_t s_PredicateView = ~0u;

	bool InitBoxQueries(ID3D11Device* dev)
	{
		if (s_BoxState != 0)
			return s_BoxState > 0;
		s_BoxState = -1;
		const D3D_SHADER_MACRO macros[] = { { "KERNEL_BOX", "1" }, { nullptr, nullptr } };
		ComPtr<ID3DBlob> blob, msgs;
		if (FAILED(::D3DCompileFromFile(L"../Shaders/57. OcclusionCulling.hlsl", macros, nullptr, "BoxVS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.GetAddressOf(), msgs.GetAddressOf()))
			|| FAILED(dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, s_BoxVS.GetAddressOf())))
		{
			EditorLog::Write("Occlusion", "box query shader failed%s%s", msgs ? "\n" : "", msgs ? (const char*)msgs->GetBufferPointer() : "");
			return false;
		}
		D3D11_BUFFER_DESC cb = {};
		cb.ByteWidth = 96;
		cb.Usage = D3D11_USAGE_DYNAMIC;
		cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		D3D11_DEPTH_STENCIL_DESC ds = {};
		ds.DepthEnable = TRUE;
		ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		D3D11_BLEND_DESC bs = {};
		bs.RenderTarget[0].RenderTargetWriteMask = 0;   // 색은 쓰지 않는다
		D3D11_RASTERIZER_DESC rs = {};
		rs.FillMode = D3D11_FILL_SOLID;
		rs.CullMode = D3D11_CULL_NONE;
		rs.DepthClipEnable = TRUE;
		if (FAILED(dev->CreateBuffer(&cb, nullptr, s_BoxCB.GetAddressOf())) || FAILED(dev->CreateDepthStencilState(&ds, s_BoxDSS.GetAddressOf()))
			|| FAILED(dev->CreateBlendState(&bs, s_BoxBS.GetAddressOf())) || FAILED(dev->CreateRasterizerState(&rs, s_BoxRS.GetAddressOf())))
			return false;
		s_BoxState = 1;
		return true;
	}

	// CPU 가 만든 인스턴스 목록 (나무): 이 뷰의 Hi-Z 로 걸러 간접 그리기. 뷰마다 처음부터 다시 쓴다
	struct ListBufs
	{
		Buf Src, Spheres, Args, Out;
		uint32_t Stride = 0;
		std::vector<OcclusionCulling::ListDraw> Draws;
	};
	std::vector<ListBufs> s_Lists;
	int s_ListUsed = 0;
	uint32_t s_HiZSerial = ~0u;   // Hi-Z 가 있는 뷰 (RenderManager::ViewSerial)
	UINT s_HiZW = 0, s_HiZH = 0, s_HiZLevels = 0;

	struct Current
	{
		View* V = nullptr;
		uint32_t Casters = 0, Items[2] = {};
		float ViewProj[16] = {};
		bool Active = false;
		bool Finished = false;   // Finish 를 지났다 (상자 쿼리는 그 뒤 — 깊이가 다 찼다)
	} s_Cur;

	ID3D11DeviceContext* Native(GfxContext* dc) { return dc ? static_cast<ID3D11DeviceContext*>(dc->Native()) : nullptr; }

	bool CompileKernels(ID3D11Device* dev)
	{
		if (s_KernelState != 0)
			return s_KernelState > 0;
		static const char* entries[KCount] = { "PrepareCS", "CompactCS", "ReduceCS", "CullCS", "CullListCS", "ShadowCullCS" };
		static const char* defines[KCount] = { "KERNEL_PREPARE", "KERNEL_COMPACT", "KERNEL_REDUCE", "KERNEL_CULL", "KERNEL_LIST", "KERNEL_SHADOW" };
		const ULONGLONG start = ::GetTickCount64();
		for (int k = 0; k < KCount; ++k)
		{
			const D3D_SHADER_MACRO macros[] = { { defines[k], "1" }, { nullptr, nullptr } };
			ComPtr<ID3DBlob> blob, msgs;
			HRESULT hr = ::D3DCompileFromFile(L"../Shaders/57. OcclusionCulling.hlsl", macros, nullptr, entries[k], "cs_5_0",
				D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.GetAddressOf(), msgs.GetAddressOf());
			if (SUCCEEDED(hr))
				hr = dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, s_Kernels[k].ReleaseAndGetAddressOf());
			if (FAILED(hr))
			{
				EditorLog::Write("Occlusion", "kernel %s failed (hr=0x%08X)%s%s — occlusion culling off", entries[k], (unsigned)hr,
					msgs ? "\n" : "", msgs ? (const char*)msgs->GetBufferPointer() : "");
				s_KernelState = -1;
				return false;
			}
		}
		D3D11_BUFFER_DESC cb = {};
		cb.ByteWidth = sizeof(Constants);
		cb.Usage = D3D11_USAGE_DYNAMIC;
		cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(dev->CreateBuffer(&cb, nullptr, s_Constants.ReleaseAndGetAddressOf())))
		{
			s_KernelState = -1;
			return false;
		}
		EditorLog::Write("Occlusion", "kernels compiled in %llu ms", ::GetTickCount64() - start);
		s_KernelState = 1;
		return true;
	}

	// CPU 가 프레임마다 쓰는 구조 버퍼 (커지기만 함)
	bool EnsureStructured(ID3D11Device* dev, Buf& b, UINT stride, UINT count)
	{
		const UINT bytes = (std::max)(count, 1u) * stride;
		if (b.B && b.Bytes >= bytes)
			return true;
		const UINT cap = (std::max)(bytes, b.Bytes + b.Bytes / 2);
		b = Buf();
		D3D11_BUFFER_DESC d = {};
		d.ByteWidth = cap / stride * stride;
		d.Usage = D3D11_USAGE_DYNAMIC;
		d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		d.StructureByteStride = stride;
		if (FAILED(dev->CreateBuffer(&d, nullptr, b.B.GetAddressOf())))
			return false;
		D3D11_SHADER_RESOURCE_VIEW_DESC s = {};
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		s.Buffer.NumElements = d.ByteWidth / stride;
		if (FAILED(dev->CreateShaderResourceView(b.B.Get(), &s, b.Srv.GetAddressOf())))
			return false;
		b.Bytes = d.ByteWidth;
		return true;
	}

	bool Upload(ID3D11DeviceContext* ctx, Buf& b, const void* data, size_t bytes)
	{
		if (bytes == 0)
			return true;
		D3D11_MAPPED_SUBRESOURCE m;
		if (FAILED(ctx->Map(b.B.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
			return false;
		memcpy(m.pData, data, bytes);
		ctx->Unmap(b.B.Get(), 0);
		return true;
	}

	// GPU 가 쓰는 raw 버퍼 (커지기만 함). keep = 키울 때 예전 내용을 옮긴다 (지난 프레임 기록)
	bool EnsureRaw(ID3D11Device* dev, ID3D11DeviceContext* ctx, Buf& b, UINT bytes, UINT bind, UINT misc, bool srv, bool keep = false)
	{
		bytes = (std::max)((bytes + 15) & ~15u, 16u);
		if (b.B && b.Bytes >= bytes)
			return true;
		const UINT cap = (std::max)(bytes, (b.Bytes + b.Bytes / 2 + 15) & ~15u);
		Buf n;
		D3D11_BUFFER_DESC d = {};
		d.ByteWidth = cap;
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = bind | D3D11_BIND_UNORDERED_ACCESS | (srv ? D3D11_BIND_SHADER_RESOURCE : 0);
		d.MiscFlags = misc | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		std::vector<uint8_t> zero(cap, 0);   // 기록 0 = 본 적 없음
		D3D11_SUBRESOURCE_DATA init = { zero.data(), 0, 0 };
		if (FAILED(dev->CreateBuffer(&d, &init, n.B.GetAddressOf())))
			return false;
		D3D11_UNORDERED_ACCESS_VIEW_DESC u = {};
		u.Format = DXGI_FORMAT_R32_TYPELESS;
		u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		u.Buffer.NumElements = cap / 4;
		u.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
		if (FAILED(dev->CreateUnorderedAccessView(n.B.Get(), &u, n.Uav.GetAddressOf())))
			return false;
		if (srv)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC s = {};
			s.Format = DXGI_FORMAT_R32_TYPELESS;
			s.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
			s.BufferEx.NumElements = cap / 4;
			s.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
			if (FAILED(dev->CreateShaderResourceView(n.B.Get(), &s, n.Srv.GetAddressOf())))
				return false;
		}
		if (keep && b.B)
		{
			const D3D11_BOX box = { 0, 0, 0, b.Bytes, 1, 1 };
			ctx->CopySubresourceRegion(n.B.Get(), 0, 0, 0, 0, b.B.Get(), 0, &box);
		}
		n.Bytes = cap;
		b = n;
		return true;
	}

	// 지금 묶인 깊이 타깃의 SRV (R24G8 · R32 · R32G8X24 · R16 TYPELESS + SHADER_RESOURCE 일 때만)
	ID3D11ShaderResourceView* DepthSrv(ID3D11Device* dev, ID3D11DeviceContext* ctx)
	{
		ComPtr<ID3D11DepthStencilView> dsv;
		ctx->OMGetRenderTargets(0, nullptr, dsv.GetAddressOf());
		if (!dsv)
			return nullptr;
		ComPtr<ID3D11Resource> res;
		dsv->GetResource(res.GetAddressOf());
		ComPtr<ID3D11Texture2D> tex;
		if (!res || FAILED(res.As(&tex)))
			return nullptr;
		if (tex.Get() == s_DepthTex.Get() && s_DepthSrv)
			return s_DepthSrv.Get();
		D3D11_TEXTURE2D_DESC td;
		tex->GetDesc(&td);
		DXGI_FORMAT f = DXGI_FORMAT_UNKNOWN;
		switch (td.Format)
		{
		case DXGI_FORMAT_R24G8_TYPELESS: f = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; break;
		case DXGI_FORMAT_R32_TYPELESS: f = DXGI_FORMAT_R32_FLOAT; break;
		case DXGI_FORMAT_R32G8X24_TYPELESS: f = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;
		case DXGI_FORMAT_R16_TYPELESS: f = DXGI_FORMAT_R16_UNORM; break;
		default: break;
		}
		if (f == DXGI_FORMAT_UNKNOWN || !(td.BindFlags & D3D11_BIND_SHADER_RESOURCE) || td.SampleDesc.Count != 1)
			return nullptr;
		D3D11_SHADER_RESOURCE_VIEW_DESC s = {};
		s.Format = f;
		s.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		s.Texture2D.MipLevels = 1;
		ComPtr<ID3D11ShaderResourceView> srv;
		if (FAILED(dev->CreateShaderResourceView(tex.Get(), &s, srv.GetAddressOf())))
			return nullptr;
		s_DepthTex = tex;
		s_DepthSrv = srv;
		return s_DepthSrv.Get();
	}

	bool EnsureHiZ(ID3D11Device* dev, View& v, UINT w, UINT h)
	{
		if (v.HiZ && v.W == w && v.H == h)
			return true;
		if (v.FailedW == w && v.FailedH == h)
			return false;
		v.HiZ.Reset();
		v.MipSrv.clear();
		v.MipUav.clear();
		v.AllSrv.Reset();
		v.W = v.H = v.Levels = 0;
		UINT levels = 1;   // D3D 밉 크기 = 내림 (max(1, 크기 >> i))
		for (UINT m = (std::max)(w, h); m > 1; m >>= 1)
			++levels;
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = w;
		d.Height = h;
		d.MipLevels = levels;
		d.ArraySize = 1;
		d.Format = DXGI_FORMAT_R32_FLOAT;
		d.SampleDesc.Count = 1;
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		if (FAILED(dev->CreateTexture2D(&d, nullptr, v.HiZ.GetAddressOf())))
		{
			EditorLog::Write("Occlusion", "Hi-Z %u x %u (%u mips) failed — no occlusion in this view", w, h, levels);
			v.FailedW = w;
			v.FailedH = h;
			return false;
		}
		for (UINT i = 0; i < levels; ++i)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC s = {};
			s.Format = DXGI_FORMAT_R32_FLOAT;
			s.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			s.Texture2D.MostDetailedMip = i;
			s.Texture2D.MipLevels = 1;
			D3D11_UNORDERED_ACCESS_VIEW_DESC u = {};
			u.Format = DXGI_FORMAT_R32_FLOAT;
			u.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			u.Texture2D.MipSlice = i;
			ComPtr<ID3D11ShaderResourceView> srv;
			ComPtr<ID3D11UnorderedAccessView> uav;
			if (FAILED(dev->CreateShaderResourceView(v.HiZ.Get(), &s, srv.GetAddressOf())) || FAILED(dev->CreateUnorderedAccessView(v.HiZ.Get(), &u, uav.GetAddressOf())))
				return false;
			v.MipSrv.push_back(srv);
			v.MipUav.push_back(uav);
		}
		if (FAILED(dev->CreateShaderResourceView(v.HiZ.Get(), nullptr, v.AllSrv.GetAddressOf())))
			return false;
		v.W = w;
		v.H = h;
		v.Levels = levels;
		EditorLog::Write("Occlusion", "Hi-Z %u x %u, %u mips (%s view)", w, h, levels, v.Editor ? "Scene" : "Game");
		return true;
	}

	void SetConstants(ID3D11DeviceContext* ctx, const Constants& c)
	{
		D3D11_MAPPED_SUBRESOURCE m;
		if (SUCCEEDED(ctx->Map(s_Constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
		{
			memcpy(m.pData, &c, sizeof(c));
			ctx->Unmap(s_Constants.Get(), 0);
		}
		ID3D11Buffer* cb = s_Constants.Get();
		ctx->CSSetConstantBuffers(0, 1, &cb);
	}

	// 커널 하나: SRV · UAV 를 묶고 돌린 뒤 바로 푼다 (다음 단계가 같은 버퍼를 다른 쓰임으로 묶는다)
	void Run(ID3D11DeviceContext* ctx, Kernel k, std::initializer_list<ID3D11ShaderResourceView*> srvs, std::initializer_list<ID3D11UnorderedAccessView*> uavs, UINT x, UINT y = 1)
	{
		ID3D11ShaderResourceView* s[4] = {};
		ID3D11UnorderedAccessView* u[4] = {};
		UINT ns = 0, nu = 0;
		for (auto* v : srvs) s[ns++] = v;
		for (auto* v : uavs) u[nu++] = v;
		ctx->CSSetShader(s_Kernels[k].Get(), nullptr, 0);
		ctx->CSSetShaderResources(0, ns, s);
		ctx->CSSetUnorderedAccessViews(0, nu, u, nullptr);
		if (x > 0 && y > 0)
			ctx->Dispatch(x, y, 1);
		ID3D11ShaderResourceView* ns4[4] = {};
		ID3D11UnorderedAccessView* nu4[4] = {};
		ctx->CSSetShaderResources(0, 4, ns4);
		ctx->CSSetUnorderedAccessViews(0, 4, nu4, nullptr);
	}

	UINT Groups(UINT n, UINT size) { return (n + size - 1) / size; }

	void Compact(ID3D11DeviceContext* ctx, Constants& c, int items, OcclusionCulling::Set set, uint32_t mode)
	{
		c.Count = s_Cur.Items[items];
		c.Mode = mode;
		SetConstants(ctx, c);
		Run(ctx, KCompact, { s_Items[items].Srv.Get(), s_Worlds.Srv.Get(), s_Flags.Srv.Get() }, { s_Args[set].Uav.Get(), s_Out[set].Uav.Get() }, Groups(c.Count, 64));
	}

	// 몇 프레임 전 검사 결과를 기다리지 않고 읽는다
	void ReadStats(ID3D11DeviceContext* ctx, View& v)
	{
		for (int i = 0; i < 3; ++i)
		{
			const int k = (v.Next + i) % 3;   // 오래된 것부터
			if (!v.Pending[k] || !v.Staging[k] || !v.Ready[k])
				continue;
			// 끝났는지 먼저 (명령 버퍼를 밀어 넣지 않고) — Map(DO_NOT_WAIT) 만 쓰면 드라이버가 그때마다 명령을 GPU 에 보내
			//  CPU 가 늦은 프레임에서 GPU 가 그 뒤로 쉬게 된다
			if (ctx->GetData(v.Ready[k].Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;
			D3D11_MAPPED_SUBRESOURCE m;
			if (ctx->Map(v.Staging[k].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m) != S_OK)
				continue;
			const uint32_t* p = static_cast<const uint32_t*>(m.pData);
			OcclusionCulling::Stats& st = s_Stats[v.Editor ? 1 : 0];
			st.Tested = (int)p[0];
			st.Visible = (int)p[1];
			st.ListTested = (int)p[2];
			st.ListVisible = (int)p[3];
			st.ShadowTested = (int)p[4];
			st.ShadowVisible = (int)p[5];
			++st.Frames;
			// 거의 아무것도 가리지 않았다 → 30 번 쉬고 다시 본다 (탁 트인 장면에서 Hi-Z 비용만 들지 않게)
			const int culled = st.Tested - st.Visible;
			if (culled < (std::max)(4, st.Tested / 50))
				v.SkipUntil = v.Calls + 30;
			ctx->Unmap(v.Staging[k].Get(), 0);
			v.Pending[k] = false;
		}
	}
}

namespace OcclusionCulling
{
	bool Supported(GfxContext* dc)
	{
		ID3D11DeviceContext* ctx = Native(dc);
		if (!ctx)
			return false;
		ComPtr<ID3D11Device> dev;
		ctx->GetDevice(dev.GetAddressOf());
		return dev && dev->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0;
	}

	bool Begin(GfxContext* dc, const void* view, bool editor, const float viewProj[16], Frame& frame, uint32_t slotCount)
	{
		s_Cur.Active = false;
		s_Cur.Finished = false;
		static const bool s_Off = [] { char v[8] = {}; return ::GetEnvironmentVariableA("NOVA_DEV_NOOCCLUSION", v, sizeof(v)) > 0 && v[0] == '1'; }();
		Stats& st = s_Stats[editor ? 1 : 0];
		st.Active = false;
		// 렌더러가 적으면 Hi-Z 를 만드는 비용이 아낄 수 있는 것보다 크다 → 절두체 컬링만
		constexpr size_t kMinCasters = 64;
		if (!Enabled || s_Off || view == nullptr || frame.Casters.size() < kMinCasters || !Supported(dc))
			return false;
		ID3D11DeviceContext* ctx = Native(dc);
		ComPtr<ID3D11Device> dev;
		ctx->GetDevice(dev.GetAddressOf());
		if (!CompileKernels(dev.Get()))
			return false;
		ID3D11ShaderResourceView* depth = DepthSrv(dev.Get(), ctx);
		if (!depth)
			return false;   // 깊이를 읽을 수 없는 타깃 (예: 창 깊이 버퍼) — CPU 컬링만

		// 묶음마다 자리 (후보 수만큼 이어서)
		for (int s = 0; s < 2; ++s)
		{
			std::vector<Batch>& batches = frame.Batches[s];
			for (Batch& b : batches) b.Candidates = 0;
			for (const Item& it : frame.Items[s]) ++batches[it.Batch].Candidates;
			std::vector<uint32_t> base(batches.size());
			uint32_t next = 0;
			for (size_t b = 0; b < batches.size(); ++b) { base[b] = next; next += batches[b].Candidates; }
			for (Item& it : frame.Items[s]) it.Base = base[it.Batch];
			s_Batches[s] = batches;
		}

		// 뷰 (지난 프레임 기록 · Hi-Z) — 오래 안 쓴 카메라는 지운다
		const uint32_t now = SceneCulling::FrameIndex();
		for (auto it = s_Views.begin(); it != s_Views.end();)
			it = (it->first != view && now - it->second.LastUsed > 600) ? s_Views.erase(it) : std::next(it);
		View& v = s_Views[view];
		v.Editor = editor;
		v.LastUsed = now;
		if (++v.Calls < v.SkipUntil)
			return false;   // 쉬는 중 (지난 검사에서 가린 것이 거의 없었다)
		++v.Frame;

		const UINT casters = (UINT)frame.Casters.size();
		bool ok = EnsureStructured(dev.Get(), s_Casters, sizeof(Caster), casters) && Upload(ctx, s_Casters, frame.Casters.data(), casters * sizeof(Caster))
			&& EnsureStructured(dev.Get(), s_Worlds, 64, casters) && Upload(ctx, s_Worlds, frame.Worlds.data(), casters * 64)
			&& EnsureRaw(dev.Get(), ctx, s_Flags, casters * 4, 0, 0, true)
			&& EnsureRaw(dev.Get(), ctx, s_Counters, 32, 0, 0, false)
			&& EnsureRaw(dev.Get(), ctx, v.History, (slotCount + 1) * 4, 0, 0, false, true);
		for (int s = 0; s < 2 && ok; ++s)
			ok = EnsureStructured(dev.Get(), s_Items[s], sizeof(Item), (UINT)frame.Items[s].size()) && Upload(ctx, s_Items[s], frame.Items[s].data(), frame.Items[s].size() * sizeof(Item));
		for (int set = 0; set < SetCount && ok; ++set)
		{
			const int s = set == Main ? 1 : 0;
			std::vector<uint32_t> args;
			args.reserve(s_Batches[s].size() * 5);
			uint32_t next = 0;
			for (const Batch& b : s_Batches[s])
			{
				args.insert(args.end(), { b.IndexCount, 0u, b.StartIndex, (uint32_t)b.BaseVertex, next });
				next += b.Candidates;
			}
			ok = EnsureRaw(dev.Get(), ctx, s_Args[set], (UINT)args.size() * 4, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, false)
				&& EnsureRaw(dev.Get(), ctx, s_Out[set], (UINT)frame.Items[s].size() * 64, D3D11_BIND_VERTEX_BUFFER, 0, false);
			if (ok && !args.empty())
			{
				const D3D11_BOX box = { 0, 0, 0, (UINT)args.size() * 4, 1, 1 };
				ctx->UpdateSubresource(s_Args[set].B.Get(), 0, &box, args.data(), 0, 0);
			}
		}
		if (!ok)
		{
			EditorLog::Write("Occlusion", "buffer creation failed — CPU culling for this view");
			return false;
		}

		s_Cur.V = &v;
		s_Cur.Casters = casters;
		s_Cur.Items[0] = (uint32_t)frame.Items[0].size();
		s_Cur.Items[1] = (uint32_t)frame.Items[1].size();
		memcpy(s_Cur.ViewProj, viewProj, sizeof(s_Cur.ViewProj));

		// 지난 프레임에 보였는가 → 깊이 프리패스 1 단계 목록
		Constants c = {};
		memcpy(c.ViewProj, viewProj, sizeof(c.ViewProj));
		c.Count = casters;
		c.Frame = v.Frame;
		SetConstants(ctx, c);
		Run(ctx, KPrepare, { s_Casters.Srv.Get() }, { v.History.Uav.Get(), s_Flags.Uav.Get() }, Groups(casters, 64));
		Compact(ctx, c, 0, DepthPhase1, 0);
		ctx->CSSetShader(nullptr, nullptr, 0);
		s_Cur.Active = true;
		st.Active = true;
		return true;
	}

	void Finish(GfxContext* dc)
	{
		if (!s_Cur.Active)
			return;
		s_Cur.Active = false;
		s_Cur.Finished = true;
		ID3D11DeviceContext* ctx = Native(dc);
		ComPtr<ID3D11Device> dev;
		ctx->GetDevice(dev.GetAddressOf());
		View& v = *s_Cur.V;

		// 깊이를 읽으려면 깊이 타깃을 잠시 푼다 (끝나면 그대로 다시)
		ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView* dsv = nullptr;
		ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
		UINT rtCount = 0;
		for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) if (rtvs[i]) rtCount = i + 1;
		D3D11_VIEWPORT vp = {};
		UINT vpCount = 1;
		ctx->RSGetViewports(&vpCount, &vp);
		ID3D11ShaderResourceView* depth = DepthSrv(dev.Get(), ctx);
		ctx->OMSetRenderTargets(0, nullptr, nullptr);

		Constants c = {};
		memcpy(c.ViewProj, s_Cur.ViewProj, sizeof(c.ViewProj));
		c.Frame = v.Frame;
		const UINT w = vpCount ? (UINT)vp.Width : 0, h = vpCount ? (UINT)vp.Height : 0;
		// Hi-Z 0 번 = 반 해상도 (칸 = 깊이 2x2) — 전체 해상도 복사 단계를 건너뛴다
		if (depth && w > 0 && h > 0 && EnsureHiZ(dev.Get(), v, (std::max)(w >> 1, 1u), (std::max)(h >> 1, 1u)))
		{
			PROFILE_GPU("Hi-Z");
			UINT sw = w, sh = h;
			for (UINT i = 0; i < v.Levels; ++i)
			{
				const UINT dw = (std::max)(sw >> 1, 1u), dh = (std::max)(sh >> 1, 1u);
				c.ViewOrigin[0] = i == 0 ? (uint32_t)vp.TopLeftX : 0;
				c.ViewOrigin[1] = i == 0 ? (uint32_t)vp.TopLeftY : 0;
				c.SrcSize[0] = sw; c.SrcSize[1] = sh;
				c.DstSize[0] = dw; c.DstSize[1] = dh;
				SetConstants(ctx, c);
				Run(ctx, KReduce, { i == 0 ? depth : v.MipSrv[i - 1].Get() }, { v.MipUav[i].Get() }, Groups(dw, 8), Groups(dh, 8));
				sw = dw;
				sh = dh;
			}
			c.Levels = v.Levels;
			c.ViewSize[0] = (float)w;
			c.ViewSize[1] = (float)h;
			s_HiZSerial = RenderManager::GetI()->ViewSerial;
			s_HiZW = w;
			s_HiZH = h;
			s_HiZLevels = v.Levels;
			s_ListUsed = 0;
		}
		// Hi-Z 가 없으면 (Levels 0) 모두 보인다

		// 가려짐 검사 → 새로 보인 것 (깊이 2 단계) · 지금 보이는 것 (본 패스)
		PROFILE_GPU("Occlusion Test");
		const UINT zero[4] = {};
		ctx->ClearUnorderedAccessViewUint(s_Counters.Uav.Get(), zero);
		// 지난 프레임 이 뷰의 인스턴스 목록 수 (나무) → 카운터 8 · 12 바이트 (같은 staging 으로 읽는다), 그리고 지운다
		if (v.ListCounters.B)
		{
			if (v.ListCountersUsed)
			{
				const D3D11_BOX box = { 0, 0, 0, 8, 1, 1 };
				ctx->CopySubresourceRegion(s_Counters.B.Get(), 0, 8, 0, 0, v.ListCounters.B.Get(), 0, &box);
			}
			ctx->ClearUnorderedAccessViewUint(v.ListCounters.Uav.Get(), zero);
			v.ListCountersUsed = false;
		}
		// 지난 프레임의 그림자 캐스터 수 → 16 · 20 바이트
		if (v.ShadowCounters.B)
		{
			if (v.ShadowCountersUsed)
			{
				const D3D11_BOX box = { 0, 0, 0, 8, 1, 1 };
				ctx->CopySubresourceRegion(s_Counters.B.Get(), 0, 16, 0, 0, v.ShadowCounters.B.Get(), 0, &box);
			}
			ctx->ClearUnorderedAccessViewUint(v.ShadowCounters.Uav.Get(), zero);
			v.ShadowCountersUsed = false;
		}
		c.Count = s_Cur.Casters;
		SetConstants(ctx, c);
		Run(ctx, KCull, { s_Casters.Srv.Get(), v.AllSrv.Get() }, { v.History.Uav.Get(), s_Flags.Uav.Get(), s_Counters.Uav.Get() }, Groups(c.Count, 64));
		Compact(ctx, c, 0, DepthPhase2, 1);
		Compact(ctx, c, 1, Main, 2);
		ctx->CSSetShader(nullptr, nullptr, 0);
		ID3D11Buffer* nullCb = nullptr;
		ctx->CSSetConstantBuffers(0, 1, &nullCb);

		// 검사 수 · 보인 수 (몇 프레임 뒤에 읽는다)
		if (!v.Staging[v.Next])
		{
			D3D11_BUFFER_DESC d = {};
			d.ByteWidth = 32;
			d.Usage = D3D11_USAGE_STAGING;
			d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			dev->CreateBuffer(&d, nullptr, v.Staging[v.Next].GetAddressOf());
			D3D11_QUERY_DESC q = { D3D11_QUERY_EVENT, 0 };
			dev->CreateQuery(&q, v.Ready[v.Next].GetAddressOf());
		}
		if (v.Staging[v.Next] && v.Ready[v.Next] && !v.Pending[v.Next])
		{
			ctx->CopyResource(v.Staging[v.Next].Get(), s_Counters.B.Get());
			ctx->End(v.Ready[v.Next].Get());
			v.Pending[v.Next] = true;
		}
		v.Next = (v.Next + 1) % 3;
		ReadStats(ctx, v);

		ctx->OMSetRenderTargets(rtCount, rtvs, dsv);
		for (ID3D11RenderTargetView* r : rtvs) if (r) r->Release();
		if (dsv) dsv->Release();
	}

	bool BeginShadow(GfxContext* dc, Frame& frame)
	{
		ID3D11DeviceContext* ctx = Native(dc);
		if (!ctx || !HasHiZ() || frame.Casters.empty() || frame.Items[0].empty())
			return false;
		ComPtr<ID3D11Device> dev;
		ctx->GetDevice(dev.GetAddressOf());
		View& v = *s_Cur.V;
		// 깊이 묶음마다 자리
		std::vector<Batch>& batches = frame.Batches[0];
		for (Batch& b : batches) b.Candidates = 0;
		for (const Item& it : frame.Items[0]) ++batches[it.Batch].Candidates;
		std::vector<uint32_t> args;
		args.reserve(batches.size() * 5);
		{
			std::vector<uint32_t> base(batches.size());
			uint32_t next = 0;
			for (size_t b = 0; b < batches.size(); ++b)
			{
				base[b] = next;
				args.insert(args.end(), { batches[b].IndexCount, 0u, batches[b].StartIndex, (uint32_t)batches[b].BaseVertex, next });
				next += batches[b].Candidates;
			}
			for (Item& it : frame.Items[0]) it.Base = base[it.Batch];
		}
		s_ShadowBatches = batches;
		const UINT casters = (UINT)frame.Casters.size(), items = (UINT)frame.Items[0].size();
		// 카메라 컬링이 끝난 뒤라 캐스터 · 월드 · 항목 · 표시 버퍼를 다시 쓴다 (본 패스는 Main 결과만 쓴다)
		const bool ok = EnsureStructured(dev.Get(), s_Casters, sizeof(Caster), casters) && Upload(ctx, s_Casters, frame.Casters.data(), casters * sizeof(Caster))
			&& EnsureStructured(dev.Get(), s_Worlds, 64, casters) && Upload(ctx, s_Worlds, frame.Worlds.data(), casters * 64)
			&& EnsureStructured(dev.Get(), s_Items[0], sizeof(Item), items) && Upload(ctx, s_Items[0], frame.Items[0].data(), items * sizeof(Item))
			&& EnsureRaw(dev.Get(), ctx, s_Flags, casters * 4, 0, 0, true)
			&& EnsureRaw(dev.Get(), ctx, v.ShadowCounters, 16, 0, 0, false)
			&& EnsureRaw(dev.Get(), ctx, s_Args[ShadowSet], (UINT)args.size() * 4, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, false)
			&& EnsureRaw(dev.Get(), ctx, s_Out[ShadowSet], items * 64, D3D11_BIND_VERTEX_BUFFER, 0, false);
		if (!ok)
			return false;
		const D3D11_BOX box = { 0, 0, 0, (UINT)args.size() * 4, 1, 1 };
		ctx->UpdateSubresource(s_Args[ShadowSet].B.Get(), 0, &box, args.data(), 0, 0);

		PROFILE_GPU("Shadow Occlusion");
		Constants c = {};
		memcpy(c.ViewProj, s_Cur.ViewProj, sizeof(c.ViewProj));
		c.ViewSize[0] = (float)s_HiZW;
		c.ViewSize[1] = (float)s_HiZH;
		c.Levels = s_HiZLevels;
		c.Count = casters;
		SetConstants(ctx, c);
		Run(ctx, KShadow, { s_Casters.Srv.Get(), v.AllSrv.Get() }, { nullptr, s_Flags.Uav.Get(), v.ShadowCounters.Uav.Get() }, Groups(casters, 64));
		c.Count = items;
		c.Mode = 2;   // 보이는 것만
		SetConstants(ctx, c);
		Run(ctx, KCompact, { s_Items[0].Srv.Get(), s_Worlds.Srv.Get(), s_Flags.Srv.Get() }, { s_Args[ShadowSet].Uav.Get(), s_Out[ShadowSet].Uav.Get() }, Groups(items, 64));
		ctx->CSSetShader(nullptr, nullptr, 0);
		ID3D11Buffer* nullCb = nullptr;
		ctx->CSSetConstantBuffers(0, 1, &nullCb);
		v.ShadowCountersUsed = true;
		return true;
	}

	bool HasHiZ()
	{
		return s_Cur.Finished && s_Cur.V && s_Cur.V->AllSrv && s_HiZLevels > 0 && s_HiZSerial == RenderManager::GetI()->ViewSerial;
	}

	int CullList(GfxContext* dc, const void* instances, uint32_t stride, uint32_t count, const float* spheres, const ListDraw* draws, int drawCount)
	{
		ID3D11DeviceContext* ctx = Native(dc);
		if (!ctx || !HasHiZ() || count == 0 || stride == 0 || stride % 4 != 0 || drawCount <= 0 || drawCount > 4)
			return -1;
		ComPtr<ID3D11Device> dev;
		ctx->GetDevice(dev.GetAddressOf());
		View& v = *s_Cur.V;
		if (!EnsureRaw(dev.Get(), ctx, v.ListCounters, 16, 0, 0, false))
			return -1;
		if (s_ListUsed >= (int)s_Lists.size())
			s_Lists.emplace_back();
		const int id = s_ListUsed;
		ListBufs& l = s_Lists[id];
		const uint32_t strideU = stride / 4;
		bool ok = EnsureStructured(dev.Get(), l.Src, 4, count * strideU) && Upload(ctx, l.Src, instances, (size_t)count * stride)
			&& EnsureStructured(dev.Get(), l.Spheres, 16, count) && Upload(ctx, l.Spheres, spheres, (size_t)count * 16)
			&& EnsureRaw(dev.Get(), ctx, l.Args, drawCount * 20, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, false)
			&& EnsureRaw(dev.Get(), ctx, l.Out, count * stride, D3D11_BIND_VERTEX_BUFFER, 0, false);
		if (!ok)
			return -1;
		uint32_t args[20] = {};
		for (int d = 0; d < drawCount; ++d)
		{
			memcpy(&args[d * 5], draws[d].Args, sizeof(draws[d].Args));
			args[d * 5 + 1] = 0;   // InstanceCount = GPU 가 센다
		}
		const D3D11_BOX box = { 0, 0, 0, (UINT)drawCount * 20, 1, 1 };
		ctx->UpdateSubresource(l.Args.B.Get(), 0, &box, args, 0, 0);
		l.Stride = stride;
		l.Draws.assign(draws, draws + drawCount);
		Constants c = {};
		memcpy(c.ViewProj, s_Cur.ViewProj, sizeof(c.ViewProj));
		c.ViewSize[0] = (float)s_HiZW;
		c.ViewSize[1] = (float)s_HiZH;
		c.Count = count;
		c.Mode = (uint32_t)drawCount;
		c.Levels = s_HiZLevels;
		c.Stride = strideU;
		SetConstants(ctx, c);
		Run(ctx, KList, { l.Src.Srv.Get(), v.AllSrv.Get(), l.Spheres.Srv.Get() }, { l.Args.Uav.Get(), l.Out.Uav.Get(), v.ListCounters.Uav.Get() }, Groups(count, 64));
		ctx->CSSetShader(nullptr, nullptr, 0);
		ID3D11Buffer* nullCb = nullptr;
		ctx->CSSetConstantBuffers(0, 1, &nullCb);
		v.ListCountersUsed = true;
		++s_ListUsed;
		return id;
	}

	void DrawList(GfxContext* dc, int list, int draw, uint32_t instanceSlot)
	{
		ID3D11DeviceContext* ctx = Native(dc);
		if (!ctx || list < 0 || list >= s_ListUsed || draw < 0 || draw >= (int)s_Lists[(size_t)list].Draws.size())
			return;
		const ListBufs& l = s_Lists[(size_t)list];
		ID3D11Buffer* vb = l.Out.B.Get();
		const UINT stride = l.Stride, offset = 0;
		ctx->IASetVertexBuffers(instanceSlot, 1, &vb, &stride, &offset);
		const OcclusionCulling::ListDraw& d = l.Draws[(size_t)draw];
		RenderStats::AddDraw(d.Indexed ? d.Args[0] : d.Args[0], 0, 1);
		if (d.Indexed)
			ctx->DrawIndexedInstancedIndirect(l.Args.B.Get(), (UINT)draw * 20);
		else
			ctx->DrawInstancedIndirect(l.Args.B.Get(), (UINT)draw * 20);
	}

	void QueryBoxes(GfxContext* dc, const std::vector<BoxQuery>& boxes)
	{
		s_PredicateOf.clear();
		s_PredicateView = ~0u;
		ID3D11DeviceContext* ctx = Native(dc);
		if (!ctx || !s_Cur.Finished || boxes.empty())
			return;
		ComPtr<ID3D11Device> dev;
		ctx->GetDevice(dev.GetAddressOf());
		if (!InitBoxQueries(dev.Get()) || !s_Cur.V)
			return;
		PROFILE_GPU("Occlusion Queries");
		View& view = *s_Cur.V;
		// 지난 프레임 이 뷰의 쿼리 결과 (기다리지 않고 — 아직이면 세지 않는다)
		{
			int hidden = 0, read = 0;
			for (size_t i = 0; i < view.PredicatesUsed; ++i)
			{
				BOOL visible = TRUE;
				if (ctx->GetData(view.Predicates[i].Get(), &visible, sizeof(visible), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
				{
					++read;
					hidden += visible ? 0 : 1;
				}
			}
			if (read > 0)
				s_Stats[view.Editor ? 1 : 0].QueriesHidden = hidden;
		}
		// 지금 상태를 저장해 두고 끝에 되돌린다
		ComPtr<ID3D11InputLayout> il; ctx->IAGetInputLayout(il.GetAddressOf());
		D3D11_PRIMITIVE_TOPOLOGY topo; ctx->IAGetPrimitiveTopology(&topo);
		ComPtr<ID3D11VertexShader> vs; ctx->VSGetShader(vs.GetAddressOf(), nullptr, nullptr);
		ComPtr<ID3D11PixelShader> ps; ctx->PSGetShader(ps.GetAddressOf(), nullptr, nullptr);
		ComPtr<ID3D11GeometryShader> gs; ctx->GSGetShader(gs.GetAddressOf(), nullptr, nullptr);
		ComPtr<ID3D11HullShader> hs; ctx->HSGetShader(hs.GetAddressOf(), nullptr, nullptr);
		ComPtr<ID3D11DomainShader> ds; ctx->DSGetShader(ds.GetAddressOf(), nullptr, nullptr);
		ComPtr<ID3D11Buffer> vcb; ctx->VSGetConstantBuffers(0, 1, vcb.GetAddressOf());
		ComPtr<ID3D11RasterizerState> rs; ctx->RSGetState(rs.GetAddressOf());
		ComPtr<ID3D11DepthStencilState> dss; UINT ref = 0; ctx->OMGetDepthStencilState(dss.GetAddressOf(), &ref);
		ComPtr<ID3D11BlendState> bs; float factor[4]; UINT mask = 0; ctx->OMGetBlendState(bs.GetAddressOf(), factor, &mask);

		ctx->IASetInputLayout(nullptr);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->VSSetShader(s_BoxVS.Get(), nullptr, 0);
		ctx->PSSetShader(nullptr, nullptr, 0);
		ctx->GSSetShader(nullptr, nullptr, 0);
		ctx->HSSetShader(nullptr, nullptr, 0);
		ctx->DSSetShader(nullptr, nullptr, 0);
		ID3D11Buffer* cb = s_BoxCB.Get();
		ctx->VSSetConstantBuffers(0, 1, &cb);
		ctx->RSSetState(s_BoxRS.Get());
		ctx->OMSetDepthStencilState(s_BoxDSS.Get(), 0);
		const float zero4[4] = {};
		ctx->OMSetBlendState(s_BoxBS.Get(), zero4, 0xFFFFFFFF);

		const XMMATRIX vp = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(s_Cur.ViewProj));
		size_t used = 0;
		for (const BoxQuery& b : boxes)
		{
			// 카메라가 상자 안이거나 가까운 면이 상자를 자르면 쿼리하지 않는다 (잘린 상자는 0 이 나올 수 있다 → 늘 그린다)
			bool nearPlane = false;
			for (int i = 0; i < 8 && !nearPlane; ++i)
			{
				const XMVECTOR c = XMVector4Transform(XMVectorSet(i & 1 ? b.Max[0] : b.Min[0], i & 2 ? b.Max[1] : b.Min[1], i & 4 ? b.Max[2] : b.Min[2], 1.0f), vp);
				nearPlane = XMVectorGetW(c) <= 1e-3f || XMVectorGetZ(c) <= 0.0f;
			}
			if (nearPlane)
				continue;
			if (used >= view.Predicates.size())
			{
				D3D11_QUERY_DESC q = { D3D11_QUERY_OCCLUSION_PREDICATE, 0 };
				ComPtr<ID3D11Predicate> p;
				if (FAILED(dev->CreatePredicate(&q, p.GetAddressOf())))
					break;
				view.Predicates.push_back(p);
			}
			ID3D11Predicate* pred = view.Predicates[used++].Get();
			D3D11_MAPPED_SUBRESOURCE m;
			if (FAILED(ctx->Map(s_BoxCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
				break;
			float* f = static_cast<float*>(m.pData);
			memcpy(f, s_Cur.ViewProj, 64);
			f[16] = b.Min[0]; f[17] = b.Min[1]; f[18] = b.Min[2]; f[19] = 0.0f;
			f[20] = b.Max[0]; f[21] = b.Max[1]; f[22] = b.Max[2]; f[23] = 0.0f;
			ctx->Unmap(s_BoxCB.Get(), 0);
			ctx->Begin(pred);
			ctx->Draw(36, 0);
			ctx->End(pred);
			s_PredicateOf[b.Renderer] = pred;
		}
		s_PredicateView = RenderManager::GetI()->ViewSerial;
		view.PredicatesUsed = used;
		s_Stats[view.Editor ? 1 : 0].Queries = (int)used;

		ctx->IASetInputLayout(il.Get());
		ctx->IASetPrimitiveTopology(topo);
		ctx->VSSetShader(vs.Get(), nullptr, 0);
		ctx->PSSetShader(ps.Get(), nullptr, 0);
		ctx->GSSetShader(gs.Get(), nullptr, 0);
		ctx->HSSetShader(hs.Get(), nullptr, 0);
		ctx->DSSetShader(ds.Get(), nullptr, 0);
		ID3D11Buffer* prevCb = vcb.Get();
		ctx->VSSetConstantBuffers(0, 1, &prevCb);
		ctx->RSSetState(rs.Get());
		ctx->OMSetDepthStencilState(dss.Get(), ref);
		ctx->OMSetBlendState(bs.Get(), factor, mask);
	}

	bool BeginPredicated(GfxContext* dc, const void* renderer)
	{
		if (s_PredicateView != RenderManager::GetI()->ViewSerial)
			return false;   // 다른 뷰 (찍기 등) 의 쿼리는 쓰지 않는다
		auto it = s_PredicateOf.find(renderer);
		ID3D11DeviceContext* ctx = Native(dc);
		if (it == s_PredicateOf.end() || !ctx)
			return false;
		ctx->SetPredication(it->second, FALSE);   // 상자가 한 픽셀도 통과하지 못했으면 그리기를 건너뛴다
		return true;
	}

	void EndPredicated(GfxContext* dc)
	{
		if (ID3D11DeviceContext* ctx = Native(dc))
			ctx->SetPredication(nullptr, FALSE);
	}

	void DrawIndirect(GfxContext* dc, Set set, uint32_t batch, MeshGeometry& geometry, uint32_t subset)
	{
		ID3D11DeviceContext* ctx = Native(dc);
		const std::vector<Batch>& batches = set == ShadowSet ? s_ShadowBatches : s_Batches[set == Main ? 1 : 0];
		if (!ctx || batch >= batches.size() || !s_Args[set].B)
			return;
		geometry.BindForInstancing(dc);
		ID3D11Buffer* inst = s_Out[set].B.Get();
		const UINT stride = 64, offset = 0;
		ctx->IASetVertexBuffers(1, 1, &inst, &stride, &offset);
		const Batch& b = batches[batch];
		RenderStats::AddDraw(b.IndexCount, geometry.GetSubset(subset).VertexCount, set == Main ? b.Candidates : 1);   // 본 패스 = 가려짐 전 후보 수
		ctx->DrawIndexedInstancedIndirect(s_Args[set].B.Get(), batch * 20);
	}
}

#else   // 안드로이드: GLES 는 절두체 컬링만

namespace OcclusionCulling
{
	bool Supported(GfxContext*) { return false; }
	bool Begin(GfxContext*, const void*, bool editor, const float*, Frame&, uint32_t) { s_Stats[editor ? 1 : 0].Active = false; return false; }
	void Finish(GfxContext*) {}
	void DrawIndirect(GfxContext*, Set, uint32_t, MeshGeometry&, uint32_t) {}
	void QueryBoxes(GfxContext*, const std::vector<BoxQuery>&) {}
	bool HasHiZ() { return false; }
	bool BeginShadow(GfxContext*, Frame&) { return false; }
	int CullList(GfxContext*, const void*, uint32_t, uint32_t, const float*, const ListDraw*, int) { return -1; }
	void DrawList(GfxContext*, int, int, uint32_t) {}
	bool BeginPredicated(GfxContext*, const void*) { return false; }
	void EndPredicated(GfxContext*) {}
}

#endif

namespace OcclusionCulling
{
	const Stats& LastStats(bool editor) { return s_Stats[editor ? 1 : 0]; }

	void RegisterEditor()
	{
		// nova occlusion info | set --enabled false
		CliServer::Register("occlusion", "occlusion culling op: {op: info | set, enabled?} (nova occlusion info)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
			const std::string op = args.value("op", std::string("info"));
			if (op == "set")
			{
				if (args.contains("enabled"))
				{
					const auto& e = args["enabled"];
					Enabled = e.is_boolean() ? e.get<bool>() : (e.is_string() ? e.get<std::string>() != "false" : e.get<int>() != 0);
				}
			}
			else if (op != "info")
			{
				error = "unknown op (info | set --enabled true|false)";
				return false;
			}
			auto view = [](bool editor) {
				const Stats& s = LastStats(editor);
				return nlohmann::json{ { "active", s.Active }, { "tested", s.Tested }, { "visible", s.Visible }, { "culled", s.Tested - s.Visible }, { "frames", s.Frames }, { "queries", s.Queries }, { "queriesHidden", s.QueriesHidden },
					{ "instancesTested", s.ListTested }, { "instancesCulled", s.ListTested - s.ListVisible },
					{ "shadowTested", s.ShadowTested }, { "shadowCulled", s.ShadowTested - s.ShadowVisible } };
			};
			result = { { "supported", Supported(Application::GetI()->GetDeviceContext()) }, { "enabled", Enabled }, { "game", view(false) }, { "scene", view(true) } };
			return true;
		});
	}
}
