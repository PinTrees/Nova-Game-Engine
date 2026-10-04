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
#include "Effects.h"

namespace
{
	OcclusionCulling::Stats s_Stats[2];   // Game · Scene 뷰
}

// Gfx 층 (DirectX 11 · OpenGL · Vulkan · OpenGL ES) 위에서: 커널은 57. OcclusionCulling.fx 의 기법 (Effects11 / ShaderCross), 버퍼 · 뷰 · 쿼리 ·
//  간접 그리기는 GfxDevice · GfxContext. GfxContext::SupportsGpuDriven 이 false 인 구현은 절두체 컬링만
namespace
{
	using Microsoft::WRL::ComPtr;

	// OpenGL ES (안드로이드) 에 없는 것 — 같은 일을 다른 길로:
	//  · 간접 그리기의 첫 인스턴스 (reservedMustBeZero): 인자는 0, 묶음의 인스턴스 정점 버퍼를 그 자리부터 묶는다
	//  · 텍스처 뷰 (밉 하나만 읽는 SRV): Hi-Z 를 만들 때 전체 밉 SRV 에서 앞 밉 (gSrcLevel) 을 읽는다
	//  · 조건부 렌더링: Skinned Mesh Renderer 상자를 compute 로 Hi-Z 검사 → 상자마다 0 / 1 → 그리기의 InstanceCount 로 (SetPredicationBuffer)
#ifdef __ANDROID__
	constexpr bool kGles = true;
#else
	constexpr bool kGles = false;
#endif

	enum Kernel { KPrepare, KCompact, KReduce, KCull, KList, KShadow, KBox, KCount };
	std::unique_ptr<Effect> s_Effect;
	int s_EffectState = 0;   // 0 아직, 1 됨, -1 실패 (다시 시도하지 않는다)
	FxPass* s_Pass[KCount] = {};
	FxPass* s_BoxPass = nullptr;
	struct Vars
	{
		FxVar *ViewProj, *ViewSize, *ViewOrigin, *SrcSize, *DstSize, *Count, *Frame, *Mode, *Levels, *Stride, *SrcLevel, *BoxMin, *BoxMax;
		FxVar *Casters, *Items, *Worlds, *Src, *Spheres, *Boxes, *FlagsIn, *Input, *HiZ, *History, *Flags, *Counters, *Args, *Out, *Dst;
	} s_V = {};

	// 57. OcclusionCulling.fx 의 cbOcclusion 값 (변수마다 넣는다)
	struct Constants
	{
		float ViewProj[16] = {};
		float ViewSize[2] = {};
		uint32_t ViewOrigin[2] = {}, SrcSize[2] = {}, DstSize[2] = {};
		uint32_t Count = 0, Frame = 0, Mode = 0, Levels = 0, Stride = 0, SrcLevel = 0;
	};

	struct Buf
	{
		ComPtr<GfxBuffer> B;
		ComPtr<GfxShaderResourceView> Srv;
		ComPtr<GfxUnorderedAccessView> Uav;
		UINT Bytes = 0;
	};

	struct View
	{
		uint32_t Frame = 0;          // 이 뷰를 그린 횟수 (기록 값)
		uint32_t LastUsed = 0;       // SceneCulling::FrameIndex
		bool Editor = false;
		Buf History;                 // 렌더러 자리마다 마지막으로 보인 Frame
		ComPtr<GfxTexture2D> HiZ;
		std::vector<ComPtr<GfxShaderResourceView>> MipSrv;
		std::vector<ComPtr<GfxUnorderedAccessView>> MipUav;
		ComPtr<GfxShaderResourceView> AllSrv;
		UINT W = 0, H = 0, Levels = 0;
		UINT FailedW = 0, FailedH = 0;   // 만들지 못한 크기 (프레임마다 다시 시도하지 않는다)
		uint32_t Calls = 0;              // Begin 횟수
		uint32_t SkipUntil = 0;          // 가린 것이 거의 없으면 이 횟수까지 쉰다 (Hi-Z 비용이 아낀 것보다 크다)
		ComPtr<GfxBuffer> Staging[3];    // 검사 · 보임 수 (몇 프레임 뒤에 읽는다)
		ComPtr<GfxQuery> Ready[3];       // 그 복사가 끝났는가 (DONOTFLUSH 로 묻는다 — Map 이 명령을 밀어 넣어 GPU 가 쉬지 않게)
		std::vector<ComPtr<GfxQuery>> Predicates;   // Skinned Mesh Renderer 상자 쿼리 (뷰마다 — 다음 프레임에 결과를 읽는다)
		size_t PredicatesUsed = 0;
		Buf BoxCounters;                   // OpenGL ES: 상자 compute 의 가려진 수 · 검사 수 — 다음 프레임 Finish 에서 Staging 으로
		bool BoxCountersUsed = false;
		Buf ListCounters;                  // 인스턴스 목록 (나무) 검사 · 보임 수 — 다음 프레임 Finish 에서 Staging 으로
		bool ListCountersUsed = false;
		Buf ShadowCounters;                // 그림자 캐스터 검사 · 보임 수 (캐스케이드를 모두 더함) — 같은 방식
		bool ShadowCountersUsed = false;
		bool Pending[3] = {};
		uint32_t StagingFrame[3] = {};   // 그 Staging 에 복사한 검사의 Frame (1 = 지난 프레임 기록이 없어 가릴 수 없던 첫 프레임)
		int Next = 0;
	};
	std::unordered_map<const void*, View> s_Views;

	// 프레임 하나 (Begin ~ 본 패스) 동안 쓰는 버퍼 — 뷰끼리 차례로 다시 쓴다 (GPU 는 명령 순서대로)
	Buf s_Casters, s_Worlds, s_Items[2], s_Flags, s_Counters, s_Args[OcclusionCulling::SetCount], s_Out[OcclusionCulling::SetCount];
	std::vector<OcclusionCulling::Batch> s_Batches[2];
	std::vector<OcclusionCulling::Batch> s_ShadowBatches;   // 그림자 (깊이 묶음, 캐스케이드마다 다시)
	ComPtr<GfxResource> s_DepthTex;   // 깊이 SRV 를 만든 텍스처 (같으면 다시 쓴다)
	ComPtr<GfxShaderResourceView> s_DepthSrv;

	// Skinned Mesh Renderer 오클루전 예측 쿼리 (이 뷰 — RenderManager::ViewSerial 이 같을 때만 쓴다)
	std::unordered_map<const void*, GfxQuery*> s_PredicateOf;
	uint32_t s_PredicateView = ~0u;
	// OpenGL ES: 상자마다 보임 0 / 1 (BoxCullTech) — 렌더러 → 상자 번호
	Buf s_Boxes, s_BoxVisible;
	std::unordered_map<const void*, uint32_t> s_BoxOf;

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

	GfxDevice* Dev() { return Application::GetI()->GetDevice(); }

	bool LoadEffect()
	{
		if (s_EffectState != 0)
			return s_EffectState > 0;
		s_EffectState = -1;
		const auto start = std::chrono::steady_clock::now();
		s_Effect = std::make_unique<Effect>(Dev(), L"../Shaders/57. OcclusionCulling.fx");
		FxEffect* fx = s_Effect->GetFX();
		if (fx == nullptr)
		{
			EditorLog::Write("Occlusion", "57. OcclusionCulling.fx failed to load — occlusion culling off");
			s_Effect.reset();
			return false;
		}
		static const char* techniques[KCount] = { "PrepareTech", "CompactTech", "ReduceTech", "CullTech", "CullListTech", "ShadowCullTech", "BoxCullTech" };
		for (int k = 0; k < KCount; ++k)
		{
			FxTechnique* t = fx->GetTechniqueByName(techniques[k]);
			s_Pass[k] = t && t->IsValid() ? t->GetPassByIndex(0) : nullptr;
			if (!s_Pass[k] || !s_Pass[k]->IsValid())
			{
				EditorLog::Write("Occlusion", "technique %s is missing or failed — occlusion culling off", techniques[k]);
				return false;
			}
		}
		if (FxTechnique* t = fx->GetTechniqueByName("BoxTech"); t && t->IsValid())
			s_BoxPass = t->GetPassByIndex(0);
		auto var = [&](const char* n) { return fx->GetVariableByName(n); };
		s_V = { var("gViewProj"), var("gViewSize"), var("gViewOrigin"), var("gSrcSize"), var("gDstSize"), var("gCount"), var("gFrame"), var("gMode"),
			var("gLevels"), var("gStride"), var("gSrcLevel"), var("gBoxMin"), var("gBoxMax"),
			var("gCasters"), var("gItems"), var("gWorlds"), var("gSrc"), var("gSpheres"), var("gBoxes"), var("gFlagsIn"), var("gInput"), var("gHiZ"),
			var("gHistory"), var("gFlags"), var("gCounters"), var("gArgs"), var("gOut"), var("gDst") };
		EditorLog::Write("Occlusion", "kernels loaded in %.0f ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
		s_EffectState = 1;
		return true;
	}

	void SetConstants(const Constants& c)
	{
		auto u1 = [](FxVar* v, uint32_t x) { if (v && v->IsValid()) v->SetRawValue(&x, 0, 4); };
		auto u2 = [](FxVar* v, const uint32_t* x) { if (v && v->IsValid()) v->SetRawValue(x, 0, 8); };
		if (s_V.ViewProj && s_V.ViewProj->IsValid()) s_V.ViewProj->SetMatrix(c.ViewProj);
		if (s_V.ViewSize && s_V.ViewSize->IsValid()) s_V.ViewSize->SetRawValue(c.ViewSize, 0, 8);
		u2(s_V.ViewOrigin, c.ViewOrigin);
		u2(s_V.SrcSize, c.SrcSize);
		u2(s_V.DstSize, c.DstSize);
		u1(s_V.Count, c.Count);
		u1(s_V.Frame, c.Frame);
		u1(s_V.Mode, c.Mode);
		u1(s_V.Levels, c.Levels);
		u1(s_V.Stride, c.Stride);
		u1(s_V.SrcLevel, c.SrcLevel);
	}

	// CPU 가 프레임마다 쓰는 구조 버퍼 (커지기만 함)
	bool EnsureStructured(Buf& b, UINT stride, UINT count)
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
		if (FAILED(Dev()->CreateBuffer(&d, nullptr, b.B.GetAddressOf())))
			return false;
		D3D11_SHADER_RESOURCE_VIEW_DESC s = {};
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		s.Buffer.NumElements = d.ByteWidth / stride;
		if (FAILED(Dev()->CreateShaderResourceView(b.B.Get(), &s, b.Srv.GetAddressOf())))
			return false;
		b.Bytes = d.ByteWidth;
		return true;
	}

	bool Upload(GfxContext* dc, Buf& b, const void* data, size_t bytes)
	{
		if (bytes == 0)
			return true;
		D3D11_MAPPED_SUBRESOURCE m;
		if (FAILED(dc->Map(b.B.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
			return false;
		memcpy(m.pData, data, bytes);
		dc->Unmap(b.B.Get(), 0);
		return true;
	}

	// GPU 가 쓰는 raw 버퍼 (커지기만 함). keep = 키울 때 예전 내용을 옮긴다 (지난 프레임 기록)
	bool EnsureRaw(GfxContext* dc, Buf& b, UINT bytes, UINT bind, UINT misc, bool srv, bool keep = false)
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
		if (FAILED(Dev()->CreateBuffer(&d, &init, n.B.GetAddressOf())))
			return false;
		D3D11_UNORDERED_ACCESS_VIEW_DESC u = {};
		u.Format = DXGI_FORMAT_R32_TYPELESS;
		u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		u.Buffer.NumElements = cap / 4;
		u.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
		if (FAILED(Dev()->CreateUnorderedAccessView(n.B.Get(), &u, n.Uav.GetAddressOf())))
			return false;
		if (srv)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC s = {};
			s.Format = DXGI_FORMAT_R32_TYPELESS;
			s.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
			s.BufferEx.NumElements = cap / 4;
			s.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
			if (FAILED(Dev()->CreateShaderResourceView(n.B.Get(), &s, n.Srv.GetAddressOf())))
				return false;
		}
		if (keep && b.B)
		{
			const D3D11_BOX box = { 0, 0, 0, b.Bytes, 1, 1 };
			dc->CopySubresourceRegion(n.B.Get(), 0, 0, 0, 0, b.B.Get(), 0, &box);
		}
		n.Bytes = cap;
		b = n;
		return true;
	}

	void ClearUint(GfxContext* dc, Buf& b)
	{
		const UINT zero[4] = {};
		if (!dc->ClearUnorderedAccessViewUint(b.Uav.Get(), zero))
		{
			std::vector<uint8_t> z(b.Bytes, 0);
			const D3D11_BOX box = { 0, 0, 0, b.Bytes, 1, 1 };
			dc->UpdateSubresource(b.B.Get(), 0, &box, z.data(), 0, 0);
		}
	}

	// 지금 묶인 깊이 타깃의 SRV (R24G8 · R32 · R32G8X24 · R16 TYPELESS + SHADER_RESOURCE 일 때만)
	GfxShaderResourceView* DepthSrv(GfxContext* dc)
	{
		ComPtr<GfxDepthStencilView> dsv;
		dc->OMGetRenderTargets(0, nullptr, dsv.GetAddressOf());
		if (!dsv)
			return nullptr;
		ComPtr<GfxResource> res;
		dsv->GetResource(res.GetAddressOf());
		if (!res)
			return nullptr;
		if (res.Get() == s_DepthTex.Get() && s_DepthSrv)
			return s_DepthSrv.Get();
		D3D11_RESOURCE_DIMENSION dim;
		res->GetType(&dim);
		if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
			return nullptr;
		D3D11_TEXTURE2D_DESC td;
		static_cast<GfxTexture2D*>(res.Get())->GetDesc(&td);
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
		ComPtr<GfxShaderResourceView> srv;
		if (FAILED(Dev()->CreateShaderResourceView(res.Get(), &s, srv.GetAddressOf())))
			return nullptr;
		s_DepthTex = res;
		s_DepthSrv = srv;
		return s_DepthSrv.Get();
	}

	bool EnsureHiZ(View& v, UINT w, UINT h)
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
		bool ok = SUCCEEDED(Dev()->CreateTexture2D(&d, nullptr, v.HiZ.GetAddressOf()));
		for (UINT i = 0; i < levels && ok; ++i)
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
			ComPtr<GfxShaderResourceView> srv;
			ComPtr<GfxUnorderedAccessView> uav;
			// OpenGL ES: 밉 하나의 SRV = 사본 텍스처 (compute 가 쓴 것이 사본에 오지 않는다) → 만들지 않고 전체 SRV 에서 gSrcLevel 로 읽는다
			ok = (kGles || SUCCEEDED(Dev()->CreateShaderResourceView(v.HiZ.Get(), &s, srv.GetAddressOf()))) && SUCCEEDED(Dev()->CreateUnorderedAccessView(v.HiZ.Get(), &u, uav.GetAddressOf()));
			v.MipSrv.push_back(srv);
			v.MipUav.push_back(uav);
		}
		ok = ok && SUCCEEDED(Dev()->CreateShaderResourceView(v.HiZ.Get(), nullptr, v.AllSrv.GetAddressOf()));
		if (!ok)
		{
			EditorLog::Write("Occlusion", "Hi-Z %u x %u (%u mips) failed — no occlusion in this view", w, h, levels);
			v.HiZ.Reset();
			v.MipSrv.clear();
			v.MipUav.clear();
			v.AllSrv.Reset();
			v.FailedW = w;
			v.FailedH = h;
			return false;
		}
		v.W = w;
		v.H = h;
		v.Levels = levels;
		EditorLog::Write("Occlusion", "Hi-Z %u x %u, %u mips (%s view)", w, h, levels, v.Editor ? "Scene" : "Game");
		return true;
	}

	using Srvs = std::initializer_list<std::pair<FxVar*, GfxShaderResourceView*>>;
	using Uavs = std::initializer_list<std::pair<FxVar*, GfxUnorderedAccessView*>>;

	// 커널 하나: 값 · SRV · UAV 를 넣고 돌린 뒤 바로 푼다 (다음 단계가 같은 버퍼를 다른 쓰임으로 묶는다)
	void Run(GfxContext* dc, Kernel k, const Constants& c, Srvs srvs, Uavs uavs, UINT x, UINT y = 1)
	{
		SetConstants(c);
		for (const auto& [var, view] : srvs) if (var && var->IsValid()) var->SetResource(view);
		for (const auto& [var, view] : uavs) if (var && var->IsValid()) var->SetUnorderedAccessView(view);
		s_Pass[k]->Apply(0, dc);
		if (x > 0 && y > 0)
			dc->Dispatch(x, y, 1);
		for (const auto& [var, view] : srvs) if (var && var->IsValid()) var->SetResource(nullptr);
		for (const auto& [var, view] : uavs) if (var && var->IsValid()) var->SetUnorderedAccessView(nullptr);
		GfxShaderResourceView* ns[8] = {};
		GfxUnorderedAccessView* nu[8] = {};
		dc->CSSetShaderResources(0, 8, ns);
		dc->CSSetUnorderedAccessViews(0, 8, nu, nullptr);
	}

	UINT Groups(UINT n, UINT size) { return (n + size - 1) / size; }

	void Compact(GfxContext* dc, Constants c, int items, uint32_t count, OcclusionCulling::Set set, uint32_t mode)
	{
		c.Count = count;
		c.Mode = mode;
		Run(dc, KCompact, c, { { s_V.Items, s_Items[items].Srv.Get() }, { s_V.Worlds, s_Worlds.Srv.Get() }, { s_V.FlagsIn, s_Flags.Srv.Get() } },
			{ { s_V.Args, s_Args[set].Uav.Get() }, { s_V.Out, s_Out[set].Uav.Get() } }, Groups(count, 64));
	}

	// 몇 프레임 전 검사 결과를 기다리지 않고 읽는다
	void ReadStats(GfxContext* dc, View& v)
	{
		for (int i = 0; i < 3; ++i)
		{
			const int k = (v.Next + i) % 3;   // 오래된 것부터
			if (!v.Pending[k] || !v.Staging[k] || !v.Ready[k])
				continue;
			// 끝났는지 먼저 (명령 버퍼를 밀어 넣지 않고) — Map(DO_NOT_WAIT) 만 쓰면 드라이버가 그때마다 명령을 GPU 에 보내
			//  CPU 가 늦은 프레임에서 GPU 가 그 뒤로 쉬게 된다
			if (dc->GetData(v.Ready[k].Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;
			D3D11_MAPPED_SUBRESOURCE m;
			if (dc->Map(v.Staging[k].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m) != S_OK)
				continue;
			const uint32_t* p = static_cast<const uint32_t*>(m.pData);
			OcclusionCulling::Stats& st = s_Stats[v.Editor ? 1 : 0];
			st.Tested = (int)p[0];
			st.Visible = (int)p[1];
			st.ListTested = (int)p[2];
			st.ListVisible = (int)p[3];
			st.ShadowTested = (int)p[4];
			st.ShadowVisible = (int)p[5];
			if (kGles)
				st.QueriesHidden = (int)p[6];   // 상자 compute 의 가려진 수 (쿼리 대신)
			++st.Frames;
			// 거의 아무것도 가리지 않았다 → 30 번 쉬고 다시 본다 (탁 트인 장면에서 Hi-Z 비용만 들지 않게).
			//  첫 프레임 (지난 프레임 기록이 없어 1 단계 깊이가 비었다 — 가릴 수 없다) 의 결과로는 쉬지 않는다
			const int culled = st.Tested - st.Visible;
			if (v.StagingFrame[k] > 1 && culled < (std::max)(4, st.Tested / 50))
				v.SkipUntil = v.Calls + 30;
			dc->Unmap(v.Staging[k].Get(), 0);
			v.Pending[k] = false;
		}
	}

	// 묶음마다 자리 (후보 수만큼 이어서) + 간접 인자 (InstanceCount 0)
	void LayoutBatches(std::vector<OcclusionCulling::Batch>& batches, std::vector<OcclusionCulling::Item>& items, std::vector<uint32_t>* args)
	{
		for (auto& b : batches) b.Candidates = 0;
		for (const auto& it : items) ++batches[it.Batch].Candidates;
		std::vector<uint32_t> base(batches.size());
		uint32_t next = 0;
		if (args) { args->clear(); args->reserve(batches.size() * 5); }
		for (size_t b = 0; b < batches.size(); ++b)
		{
			base[b] = next;
			batches[b].Base = next;
			// OpenGL ES 의 간접 인자에는 첫 인스턴스가 없다 (0 이어야 한다) → DrawIndirect 가 인스턴스 정점 버퍼를 그 자리부터 묶는다
			if (args) args->insert(args->end(), { batches[b].IndexCount, 0u, batches[b].StartIndex, (uint32_t)batches[b].BaseVertex, kGles ? 0u : next });
			next += batches[b].Candidates;
		}
		for (auto& it : items) it.Base = base[it.Batch];
	}

	void UploadArgs(GfxContext* dc, Buf& b, const std::vector<uint32_t>& args)
	{
		if (args.empty())
			return;
		const D3D11_BOX box = { 0, 0, 0, (UINT)args.size() * 4, 1, 1 };
		dc->UpdateSubresource(b.B.Get(), 0, &box, args.data(), 0, 0);
	}
}

namespace OcclusionCulling
{
	bool Supported(GfxContext* dc)
	{
		return dc && dc->SupportsGpuDriven() && LoadEffect();
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
		if (!DepthSrv(dc))
			return false;   // 깊이를 읽을 수 없는 타깃 (예: 창 깊이 버퍼) — CPU 컬링만

		std::vector<uint32_t> args[2];
		for (int s = 0; s < 2; ++s)
		{
			LayoutBatches(frame.Batches[s], frame.Items[s], &args[s]);
			s_Batches[s] = frame.Batches[s];
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
		bool ok = EnsureStructured(s_Casters, sizeof(Caster), casters) && Upload(dc, s_Casters, frame.Casters.data(), casters * sizeof(Caster))
			&& EnsureStructured(s_Worlds, InstanceBytes, casters) && Upload(dc, s_Worlds, frame.Worlds.data(), casters * InstanceBytes)
			&& EnsureRaw(dc, s_Flags, casters * 4, 0, 0, true)
			&& EnsureRaw(dc, s_Counters, 32, 0, 0, false)
			&& EnsureRaw(dc, v.History, (slotCount + 1) * 4, 0, 0, false, true);
		for (int s = 0; s < 2 && ok; ++s)
			ok = EnsureStructured(s_Items[s], sizeof(Item), (UINT)frame.Items[s].size()) && Upload(dc, s_Items[s], frame.Items[s].data(), frame.Items[s].size() * sizeof(Item));
		for (int set = 0; set < ShadowSet && ok; ++set)
		{
			const int s = set == Main ? 1 : 0;
			ok = EnsureRaw(dc, s_Args[set], (UINT)args[s].size() * 4, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, false)
				&& EnsureRaw(dc, s_Out[set], (UINT)frame.Items[s].size() * InstanceBytes, D3D11_BIND_VERTEX_BUFFER, 0, false);
			if (ok)
				UploadArgs(dc, s_Args[set], args[s]);
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
		Constants c;
		memcpy(c.ViewProj, viewProj, sizeof(c.ViewProj));
		c.Count = casters;
		c.Frame = v.Frame;
		Run(dc, KPrepare, c, { { s_V.Casters, s_Casters.Srv.Get() } }, { { s_V.History, v.History.Uav.Get() }, { s_V.Flags, s_Flags.Uav.Get() } }, Groups(casters, 64));
		Compact(dc, c, 0, s_Cur.Items[0], DepthPhase1, 0);
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
		View& v = *s_Cur.V;

		// 깊이를 읽으려면 깊이 타깃을 잠시 푼다 (끝나면 그대로 다시)
		GfxRenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		GfxDepthStencilView* dsv = nullptr;
		dc->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
		UINT rtCount = 0;
		for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) if (rtvs[i]) rtCount = i + 1;
		D3D11_VIEWPORT vp = {};
		UINT vpCount = 1;
		dc->RSGetViewports(&vpCount, &vp);
		GfxShaderResourceView* depth = DepthSrv(dc);
		dc->OMSetRenderTargets(0, nullptr, nullptr);

		Constants c;
		memcpy(c.ViewProj, s_Cur.ViewProj, sizeof(c.ViewProj));
		c.Frame = v.Frame;
		const UINT w = vpCount ? (UINT)vp.Width : 0, h = vpCount ? (UINT)vp.Height : 0;
		// Hi-Z 0 번 = 반 해상도 (칸 = 깊이 2x2) — 전체 해상도 복사 단계를 건너뛴다
		if (depth && w > 0 && h > 0 && EnsureHiZ(v, (std::max)(w >> 1, 1u), (std::max)(h >> 1, 1u)))
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
				GfxShaderResourceView* input = i == 0 ? depth : kGles ? v.AllSrv.Get() : v.MipSrv[i - 1].Get();
				c.SrcLevel = i == 0 || !kGles ? 0 : i - 1;
				Run(dc, KReduce, c, { { s_V.Input, input } }, { { s_V.Dst, v.MipUav[i].Get() } }, Groups(dw, 8), Groups(dh, 8));
				sw = dw;
				sh = dh;
			}
			c.SrcLevel = 0;
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
		ClearUint(dc, s_Counters);
		// 지난 프레임 이 뷰의 인스턴스 목록 수 (나무) → 카운터 8 · 12 바이트, 그림자 캐스터 → 16 · 20 (같은 staging 으로 읽는다), 그리고 지운다
		for (auto [counters, used, at] : { std::make_tuple(&v.ListCounters, &v.ListCountersUsed, 8u), std::make_tuple(&v.ShadowCounters, &v.ShadowCountersUsed, 16u),
			std::make_tuple(&v.BoxCounters, &v.BoxCountersUsed, 24u) })
		{
			if (!counters->B)
				continue;
			if (*used)
			{
				const D3D11_BOX box = { 0, 0, 0, 8, 1, 1 };
				dc->CopySubresourceRegion(s_Counters.B.Get(), 0, at, 0, 0, counters->B.Get(), 0, &box);
			}
			ClearUint(dc, *counters);
			*used = false;
		}
		c.Count = s_Cur.Casters;
		Run(dc, KCull, c, { { s_V.Casters, s_Casters.Srv.Get() }, { s_V.HiZ, v.AllSrv.Get() } },
			{ { s_V.History, v.History.Uav.Get() }, { s_V.Flags, s_Flags.Uav.Get() }, { s_V.Counters, s_Counters.Uav.Get() } }, Groups(c.Count, 64));
		Compact(dc, c, 0, s_Cur.Items[0], DepthPhase2, 1);
		Compact(dc, c, 1, s_Cur.Items[1], Main, 2);

		// 검사 수 · 보인 수 (몇 프레임 뒤에 읽는다)
		if (!v.Staging[v.Next])
		{
			D3D11_BUFFER_DESC d = {};
			d.ByteWidth = 32;
			d.Usage = D3D11_USAGE_STAGING;
			d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Dev()->CreateBuffer(&d, nullptr, v.Staging[v.Next].GetAddressOf());
			D3D11_QUERY_DESC q = { D3D11_QUERY_EVENT, 0 };
			Dev()->CreateQuery(&q, v.Ready[v.Next].GetAddressOf());
		}
		if (v.Staging[v.Next] && v.Ready[v.Next] && !v.Pending[v.Next])
		{
			dc->CopyResource(v.Staging[v.Next].Get(), s_Counters.B.Get());
			dc->End(v.Ready[v.Next].Get());
			v.Pending[v.Next] = true;
			v.StagingFrame[v.Next] = v.Frame;
		}
		v.Next = (v.Next + 1) % 3;
		ReadStats(dc, v);

		dc->OMSetRenderTargets(rtCount, rtvs, dsv);
		for (GfxRenderTargetView* r : rtvs) if (r) r->Release();
		if (dsv) dsv->Release();
	}

	bool BeginShadow(GfxContext* dc, Frame& frame)
	{
		if (!HasHiZ() || frame.Casters.empty() || frame.Items[0].empty())
			return false;
		View& v = *s_Cur.V;
		std::vector<uint32_t> args;
		LayoutBatches(frame.Batches[0], frame.Items[0], &args);
		s_ShadowBatches = frame.Batches[0];
		const UINT casters = (UINT)frame.Casters.size(), items = (UINT)frame.Items[0].size();
		// 카메라 컬링이 끝난 뒤라 캐스터 · 월드 · 항목 · 표시 버퍼를 다시 쓴다 (본 패스는 Main 결과만 쓴다)
		const bool ok = EnsureStructured(s_Casters, sizeof(Caster), casters) && Upload(dc, s_Casters, frame.Casters.data(), casters * sizeof(Caster))
			&& EnsureStructured(s_Worlds, InstanceBytes, casters) && Upload(dc, s_Worlds, frame.Worlds.data(), casters * InstanceBytes)
			&& EnsureStructured(s_Items[0], sizeof(Item), items) && Upload(dc, s_Items[0], frame.Items[0].data(), items * sizeof(Item))
			&& EnsureRaw(dc, s_Flags, casters * 4, 0, 0, true)
			&& EnsureRaw(dc, v.ShadowCounters, 16, 0, 0, false)
			&& EnsureRaw(dc, s_Args[ShadowSet], (UINT)args.size() * 4, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, false)
			&& EnsureRaw(dc, s_Out[ShadowSet], items * InstanceBytes, D3D11_BIND_VERTEX_BUFFER, 0, false);
		if (!ok)
			return false;
		UploadArgs(dc, s_Args[ShadowSet], args);

		PROFILE_GPU("Shadow Occlusion");
		Constants c;
		memcpy(c.ViewProj, s_Cur.ViewProj, sizeof(c.ViewProj));
		c.ViewSize[0] = (float)s_HiZW;
		c.ViewSize[1] = (float)s_HiZH;
		c.Levels = s_HiZLevels;
		c.Count = casters;
		Run(dc, KShadow, c, { { s_V.Casters, s_Casters.Srv.Get() }, { s_V.HiZ, v.AllSrv.Get() } },
			{ { s_V.Flags, s_Flags.Uav.Get() }, { s_V.Counters, v.ShadowCounters.Uav.Get() } }, Groups(casters, 64));
		Compact(dc, c, 0, items, ShadowSet, 2);   // 보이는 것만
		v.ShadowCountersUsed = true;
		return true;
	}

	bool HasHiZ()
	{
		return s_Cur.Finished && s_Cur.V && s_Cur.V->AllSrv && s_HiZLevels > 0 && s_HiZSerial == RenderManager::GetI()->ViewSerial;
	}

	int CullList(GfxContext* dc, const void* instances, uint32_t stride, uint32_t count, const float* spheres, const ListDraw* draws, int drawCount)
	{
		if (!dc || !HasHiZ() || count == 0 || stride == 0 || stride % 4 != 0 || drawCount <= 0 || drawCount > 4)
			return -1;
		View& v = *s_Cur.V;
		if (!EnsureRaw(dc, v.ListCounters, 16, 0, 0, false))
			return -1;
		if (s_ListUsed >= (int)s_Lists.size())
			s_Lists.emplace_back();
		const int id = s_ListUsed;
		ListBufs& l = s_Lists[id];
		const uint32_t strideU = stride / 4;
		const bool ok = EnsureStructured(l.Src, 4, count * strideU) && Upload(dc, l.Src, instances, (size_t)count * stride)
			&& EnsureStructured(l.Spheres, 16, count) && Upload(dc, l.Spheres, spheres, (size_t)count * 16)
			&& EnsureRaw(dc, l.Args, drawCount * 20, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, false)
			&& EnsureRaw(dc, l.Out, count * stride, D3D11_BIND_VERTEX_BUFFER, 0, false);
		if (!ok)
			return -1;
		std::vector<uint32_t> args((size_t)drawCount * 5, 0);
		for (int d = 0; d < drawCount; ++d)
		{
			memcpy(&args[(size_t)d * 5], draws[d].Args, sizeof(draws[d].Args));
			args[(size_t)d * 5 + 1] = 0;   // InstanceCount = GPU 가 센다
		}
		UploadArgs(dc, l.Args, args);
		l.Stride = stride;
		l.Draws.assign(draws, draws + drawCount);
		Constants c;
		memcpy(c.ViewProj, s_Cur.ViewProj, sizeof(c.ViewProj));
		c.ViewSize[0] = (float)s_HiZW;
		c.ViewSize[1] = (float)s_HiZH;
		c.Count = count;
		c.Mode = (uint32_t)drawCount;
		c.Levels = s_HiZLevels;
		c.Stride = strideU;
		Run(dc, KList, c, { { s_V.Src, l.Src.Srv.Get() }, { s_V.HiZ, v.AllSrv.Get() }, { s_V.Spheres, l.Spheres.Srv.Get() } },
			{ { s_V.Args, l.Args.Uav.Get() }, { s_V.Out, l.Out.Uav.Get() }, { s_V.Counters, v.ListCounters.Uav.Get() } }, Groups(count, 64));
		v.ListCountersUsed = true;
		++s_ListUsed;
		return id;
	}

	void DrawList(GfxContext* dc, int list, int draw, uint32_t instanceSlot)
	{
		if (!dc || list < 0 || list >= s_ListUsed || draw < 0 || draw >= (int)s_Lists[(size_t)list].Draws.size())
			return;
		const ListBufs& l = s_Lists[(size_t)list];
		GfxBuffer* vb = l.Out.B.Get();
		const UINT stride = l.Stride, offset = 0;
		dc->IASetVertexBuffers(instanceSlot, 1, &vb, &stride, &offset);
		const OcclusionCulling::ListDraw& d = l.Draws[(size_t)draw];
		RenderStats::AddDraw(d.Args[0], 0, 1);
		if (d.Indexed)
			dc->DrawIndexedInstancedIndirect(l.Args.B.Get(), (UINT)draw * 20);
		else
			dc->DrawInstancedIndirect(l.Args.B.Get(), (UINT)draw * 20);
	}

	// OpenGL ES: 상자를 Hi-Z 로 검사 (compute) → 상자마다 보임 0 / 1. 본 패스의 그리기가 그 값을 InstanceCount 로 쓴다 (같은 프레임 깊이)
	void BoxCull(GfxContext* dc, const std::vector<BoxQuery>& boxes)
	{
		if (!HasHiZ())
			return;
		PROFILE_GPU("Occlusion Boxes");
		View& v = *s_Cur.V;
		std::vector<float> data;
		data.reserve(boxes.size() * 8);
		for (const BoxQuery& b : boxes)
			data.insert(data.end(), { b.Min[0], b.Min[1], b.Min[2], 0.0f, b.Max[0], b.Max[1], b.Max[2], 0.0f });
		const UINT n = (UINT)boxes.size();
		const bool ok = EnsureStructured(s_Boxes, 16, n * 2) && Upload(dc, s_Boxes, data.data(), data.size() * sizeof(float))
			&& EnsureRaw(dc, s_BoxVisible, n * 4, 0, 0, false)
			&& EnsureRaw(dc, v.BoxCounters, 16, 0, 0, false);
		if (!ok)
			return;
		Constants c;
		memcpy(c.ViewProj, s_Cur.ViewProj, sizeof(c.ViewProj));
		c.ViewSize[0] = (float)s_HiZW;
		c.ViewSize[1] = (float)s_HiZH;
		c.Levels = s_HiZLevels;
		c.Count = n;
		// 카메라가 상자 안 · 가까운 면을 넘는 상자는 HiZVisible 이 보임으로 (쿼리처럼 CPU 가 거르지 않아도 된다)
		Run(dc, KBox, c, { { s_V.Boxes, s_Boxes.Srv.Get() }, { s_V.HiZ, v.AllSrv.Get() } },
			{ { s_V.Flags, s_BoxVisible.Uav.Get() }, { s_V.Counters, v.BoxCounters.Uav.Get() } }, Groups(n, 64));
		v.BoxCountersUsed = true;
		for (UINT i = 0; i < n; ++i)
			s_BoxOf[boxes[i].Renderer] = i;
		s_PredicateView = RenderManager::GetI()->ViewSerial;
		s_Stats[v.Editor ? 1 : 0].Queries = (int)n;
	}

	void QueryBoxes(GfxContext* dc, const std::vector<BoxQuery>& boxes)
	{
		s_PredicateOf.clear();
		s_BoxOf.clear();
		s_PredicateView = ~0u;
		if (kGles && dc && s_Cur.Finished && !boxes.empty() && s_Cur.V)
		{
			BoxCull(dc, boxes);
			return;
		}
		if (!dc || !s_Cur.Finished || boxes.empty() || !s_Cur.V || !s_BoxPass || !s_BoxPass->IsValid())
			return;
		PROFILE_GPU("Occlusion Queries");
		View& view = *s_Cur.V;
		// 지난 프레임 이 뷰의 쿼리 결과 (기다리지 않고 — 아직이면 세지 않는다)
		{
			int hidden = 0, read = 0;
			for (size_t i = 0; i < view.PredicatesUsed; ++i)
			{
				BOOL visible = TRUE;
				if (dc->GetData(view.Predicates[i].Get(), &visible, sizeof(visible), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
				{
					++read;
					hidden += visible ? 0 : 1;
				}
			}
			if (read > 0)
				s_Stats[view.Editor ? 1 : 0].QueriesHidden = hidden;
		}
		// 상태를 저장해 두고 끝에 되돌린다 (상자 기법이 깊이 · 색 쓰기를 끈다 — 다음 효과가 기본 상태를 기대할 수 있다)
		ComPtr<GfxInputLayout> il; dc->IAGetInputLayout(il.GetAddressOf());
		D3D11_PRIMITIVE_TOPOLOGY topo; dc->IAGetPrimitiveTopology(&topo);
		ComPtr<GfxRasterizerState> rs; dc->RSGetState(rs.GetAddressOf());
		ComPtr<GfxDepthStencilState> dss; UINT ref = 0; dc->OMGetDepthStencilState(dss.GetAddressOf(), &ref);
		ComPtr<GfxBlendState> bs; float factor[4] = {}; UINT mask = 0; dc->OMGetBlendState(bs.GetAddressOf(), factor, &mask);
		dc->IASetInputLayout(nullptr);
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		const XMMATRIX vp = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(s_Cur.ViewProj));
		if (s_V.ViewProj && s_V.ViewProj->IsValid())
			s_V.ViewProj->SetMatrix(s_Cur.ViewProj);
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
				ComPtr<GfxQuery> p;
				if (FAILED(Dev()->CreateQuery(&q, p.GetAddressOf())))
					break;
				view.Predicates.push_back(p);
			}
			GfxQuery* pred = view.Predicates[used++].Get();
			const float mn[4] = { b.Min[0], b.Min[1], b.Min[2], 0.0f }, mx[4] = { b.Max[0], b.Max[1], b.Max[2], 0.0f };
			if (s_V.BoxMin && s_V.BoxMin->IsValid()) s_V.BoxMin->SetFloatVector(mn);
			if (s_V.BoxMax && s_V.BoxMax->IsValid()) s_V.BoxMax->SetFloatVector(mx);
			s_BoxPass->Apply(0, dc);
			dc->Begin(pred);
			dc->Draw(36, 0);
			dc->End(pred);
			s_PredicateOf[b.Renderer] = pred;
		}
		s_PredicateView = RenderManager::GetI()->ViewSerial;
		view.PredicatesUsed = used;
		s_Stats[view.Editor ? 1 : 0].Queries = (int)used;

		dc->IASetInputLayout(il.Get());
		dc->IASetPrimitiveTopology(topo);
		dc->RSSetState(rs.Get());
		dc->OMSetDepthStencilState(dss.Get(), ref);
		dc->OMSetBlendState(bs.Get(), factor, mask);
	}

	bool BeginPredicated(GfxContext* dc, const void* renderer)
	{
		if (!dc || s_PredicateView != RenderManager::GetI()->ViewSerial)
			return false;   // 다른 뷰 (찍기 등) 의 쿼리는 쓰지 않는다
		if (kGles)
		{
			auto box = s_BoxOf.find(renderer);
			return box != s_BoxOf.end() && dc->SetPredicationBuffer(s_BoxVisible.B.Get(), box->second * 4);   // 0 = 가려짐 → InstanceCount 0
		}
		auto it = s_PredicateOf.find(renderer);
		if (it == s_PredicateOf.end())
			return false;
		return dc->SetPredication(it->second, FALSE);   // 상자가 한 픽셀도 통과하지 못했으면 그리기를 건너뛴다
	}

	void EndPredicated(GfxContext* dc)
	{
		if (!dc)
			return;
		if (kGles)
			dc->SetPredicationBuffer(nullptr, 0);
		else
			dc->SetPredication(nullptr, FALSE);
	}

	void DrawIndirect(GfxContext* dc, Set set, uint32_t batch, MeshGeometry& geometry, uint32_t subset)
	{
		const std::vector<Batch>& batches = set == ShadowSet ? s_ShadowBatches : s_Batches[set == Main ? 1 : 0];
		if (!dc || batch >= batches.size() || !s_Args[set].B)
			return;
		geometry.BindForInstancing(dc);
		GfxBuffer* inst = s_Out[set].B.Get();
		const Batch& b = batches[batch];
		const UINT stride = InstanceBytes, offset = kGles ? b.Base * InstanceBytes : 0;   // OpenGL ES: 간접 인자의 첫 인스턴스 대신
		dc->IASetVertexBuffers(1, 1, &inst, &stride, &offset);
		RenderStats::AddDraw(b.IndexCount, geometry.GetSubset(subset).VertexCount, set == Main ? b.Candidates : 1);   // 본 패스 = 가려짐 전 후보 수
		dc->DrawIndexedInstancedIndirect(s_Args[set].B.Get(), batch * 20);
	}
}


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
			result = nlohmann::json::parse(InfoJson());
			return true;
		});
	}

	std::string InfoJson()
	{
		auto view = [](bool editor) {
			const Stats& s = LastStats(editor);
			return nlohmann::json{ { "active", s.Active }, { "tested", s.Tested }, { "visible", s.Visible }, { "culled", s.Tested - s.Visible }, { "frames", s.Frames }, { "queries", s.Queries }, { "queriesHidden", s.QueriesHidden },
				{ "instancesTested", s.ListTested }, { "instancesCulled", s.ListTested - s.ListVisible },
				{ "shadowTested", s.ShadowTested }, { "shadowCulled", s.ShadowTested - s.ShadowVisible } };
		};
		const nlohmann::json j = { { "supported", Supported(Application::GetI()->GetDeviceContext()) }, { "enabled", Enabled }, { "game", view(false) }, { "scene", view(true) } };
		return j.dump();
	}
}
