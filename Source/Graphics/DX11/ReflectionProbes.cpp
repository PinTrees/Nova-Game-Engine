#include "pch.h"
#include "ReflectionProbes.h"
#include "ReflectionProbe.h"
#include "Effects.h"
#include "GameObject.h"
#include "Scene.h"
#include "SceneManager.h"
#include "PathManager.h"
#include "EditorLog.h"
#include "Profiler.h"
#include "CliServer.h"
#include "Debug.h"
#include "DayNightState.h"

namespace fs = std::filesystem;

namespace
{
	using ReflectionProbes::kMaxProbes;

	// 프로브 하나의 GPU 상태
	struct ProbeGpu
	{
		// 찍기 결과 (실시간 · 굽기): 큐브 6 면, 밉 = GenerateMips
		ComPtr<GfxTexture2D> Tex;
		ComPtr<GfxShaderResourceView> SRV;
		ComPtr<GfxRenderTargetView> Face[6];
		int Res = 0;
		bool Hdr = true;
		bool Captured = false;      // 6 면을 다 찍은 적 있음
		int NextFace = 0;           // Individual Faces: 다음에 찍을 면 (0 = 새로 시작)
		uint32 SeenSettings = 0;
		bool RenderRequested = false;
		// Baked · Custom: 파일에서 읽은 큐브
		ComPtr<GfxShaderResourceView> Loaded;
		std::string LoadedPath;
		fs::file_time_type LoadedTime = {};
		int LoadedRes = 0;
		uint64_t Version = 1;       // 내용이 바뀔 때마다 (배열 칸을 다시 필터)
		// 낮 · 밤: Baked · Custom 도 실행 중에 다시 찍은 큐브를 쓴다 (파일은 그대로)
		bool Relit = false;
		float CapturedTime = -1.0f; // 마지막으로 6 면을 다 찍은 시각 (낮 · 밤의 시, 없으면 -1)
	};
	std::unordered_map<const ReflectionProbe*, ProbeGpu> s_Gpu;
	float s_DayNightLast = -1.0f;   // 마지막으로 다시 찍게 한 시각 (-1 = 낮 · 밤 다시 찍기 꺼짐)
	uint32 s_DayNightSerial = 0;
	std::vector<ReflectionProbe*> s_BakeRequests;
	std::function<void(const ReflectionProbes::CaptureView&)> s_Capture;
	bool s_Capturing = false;

	// 큐브 배열 (뷰에 고른 프로브의 필터 결과)
	ComPtr<GfxTexture2D> s_ArrayTex;
	ComPtr<GfxShaderResourceView> s_ArraySRV;
	std::vector<ComPtr<GfxRenderTargetView>> s_ArrayRTV;   // [(칸 * 6 + 면) * 밉 수 + 밉]
	int s_ArrayRes = 0, s_ArraySlots = 0, s_ArrayMips = 0;
	struct Slot
	{
		const ReflectionProbe* Probe = nullptr;
		uint64_t Version = 0;
		GfxShaderResourceView* Source = nullptr;
	};
	Slot s_Slots[kMaxProbes];

	// 이번 뷰 (Select → Bind)
	int s_Count = 0;
	XMFLOAT4 s_Data[kMaxProbes * 3];   // 상자 최소 + Blend Distance, 상자 최대 + Intensity, 찍은 점 + (칸 * 2 + Box Projection)

	std::unique_ptr<Effect> s_Filter;
	bool s_FilterTried = false;

	FxVar* Var(FxEffect* fx, const char* name)
	{
		FxVar* v = fx ? fx->GetVariableByName(name) : nullptr;
		return v && v->IsValid() ? v : nullptr;
	}

	std::string NameOf(ReflectionProbe* p)
	{
		return p && p->GetGameObject() ? p->GetGameObject()->GetName() : std::string();
	}

	fs::path FullPath(const std::string& rel)
	{
		return fs::path(PathManager::GetI()->GetContentPathW()) / string_to_wstring(rel);
	}

	int MipCount(int res)
	{
		int m = 1;
		while ((res >> m) > 0) ++m;
		return m;
	}

	// 실시간 · 굽기 찍기용 큐브 (HDR = R16G16B16A16 float, 아니면 R8G8B8A8)
	bool EnsureCaptureTex(ProbeGpu& g, int res, bool hdr)
	{
		if (g.Tex && g.Res == res && g.Hdr == hdr)
			return true;
		g.Tex.Reset(); g.SRV.Reset();
		for (auto& f : g.Face) f.Reset();
		g.Captured = false;
		g.NextFace = 0;
		GfxDevice* dev = Gfx::Device();
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = td.Height = (UINT)res;
		td.MipLevels = (UINT)MipCount(res);
		td.ArraySize = 6;
		td.Format = hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		td.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_GENERATE_MIPS;
		if (FAILED(dev->CreateTexture2D(&td, nullptr, g.Tex.GetAddressOf())))
		{
			EditorLog::Write("Probe", "capture cube %d failed", res);
			return false;
		}
		D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
		sd.Format = td.Format;
		sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
		sd.TextureCube.MipLevels = td.MipLevels;
		dev->CreateShaderResourceView(g.Tex.Get(), &sd, g.SRV.GetAddressOf());
		for (int f = 0; f < 6; ++f)
		{
			D3D11_RENDER_TARGET_VIEW_DESC rd = {};
			rd.Format = td.Format;
			rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			rd.Texture2DArray.MipSlice = 0;
			rd.Texture2DArray.FirstArraySlice = (UINT)f;
			rd.Texture2DArray.ArraySize = 1;
			dev->CreateRenderTargetView(g.Tex.Get(), &rd, g.Face[f].GetAddressOf());
		}
		g.Res = res;
		g.Hdr = hdr;
		return g.SRV != nullptr;
	}

	// 면 방향 (D3D 큐브 순서 +X -X +Y -Y +Z -Z) 과 위쪽 — 셰이더의 면 → 방향과 같은 약속
	void FaceBasis(int face, XMVECTOR& look, XMVECTOR& up)
	{
		static const float kLook[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
		static const float kUp[6][3] = { {0,1,0}, {0,1,0}, {0,0,-1}, {0,0,1}, {0,1,0}, {0,1,0} };
		look = XMVectorSet(kLook[face][0], kLook[face][1], kLook[face][2], 0);
		up = XMVectorSet(kUp[face][0], kUp[face][1], kUp[face][2], 0);
	}

	// 면 [first, first + count) 찍기. 마지막 면까지 찍으면 밉을 만들고 내용 버전을 올린다
	bool CaptureFaces(ReflectionProbe* p, ProbeGpu& g, int first, int count)
	{
		if (!s_Capture || !EnsureCaptureTex(g, std::clamp(p->GetResolution(), 16, 2048), p->GetHdr()))
			return false;
		PROFILE_SCOPE("Reflection Probe Capture");
		const Vec3 pos = p->CapturePosition();
		const XMVECTOR eye = XMVectorSet(pos.x, pos.y, pos.z, 1);
		ReflectionProbes::CaptureView v;
		v.Viewport = { 0, 0, (float)g.Res, (float)g.Res, 0.0f, 1.0f };
		v.Proj = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.0f, p->GetNearClip(), p->GetFarClip());
		v.Position = XMFLOAT3(pos.x, pos.y, pos.z);
		v.CullingMask = p->GetCullingMask();
		v.SolidColor = p->UsesSolidColor();
		memcpy(v.Background, p->GetBackgroundColor(), sizeof(v.Background));
		v.ShadowDistance = p->GetShadowDistance();
		s_Capturing = true;
		for (int f = first; f < first + count && f < 6; ++f)
		{
			XMVECTOR look, up;
			FaceBasis(f, look, up);
			v.View = XMMatrixLookToLH(eye, look, up);
			v.Target = g.Face[f].Get();
			s_Capture(v);
		}
		s_Capturing = false;
		g.NextFace = first + count;
		if (g.NextFace >= 6)
		{
			g.NextFace = 0;
			Gfx::Context()->GenerateMips(g.SRV.Get());
			g.Captured = true;
			g.CapturedTime = DayNightState::Get().Enabled ? DayNightState::Get().TimeOfDay : -1.0f;
			++g.Version;
		}
		return true;
	}

	// Baked · Custom: DDS 큐브를 직접 읽는다 (가져오기 설정을 거치지 않음 — 크기 · 형식 그대로)
	GfxShaderResourceView* LoadCube(ProbeGpu& g, const std::string& rel)
	{
		if (rel.empty())
		{
			g.Loaded.Reset();
			g.LoadedPath.clear();
			return nullptr;
		}
		const fs::path full = FullPath(rel);
		std::error_code ec;
		const auto time = fs::last_write_time(full, ec);
		if (ec)
		{
			g.Loaded.Reset();
			g.LoadedPath = rel;
			return nullptr;
		}
		if (g.Loaded && g.LoadedPath == rel && g.LoadedTime == time)
			return g.Loaded.Get();
		g.Loaded.Reset();
		g.LoadedPath = rel;
		g.LoadedTime = time;
		DirectX::TexMetadata md = {};
		DirectX::ScratchImage img;
		if (FAILED(DirectX::LoadFromDDSFile(full.c_str(), DirectX::DDS_FLAGS_NONE, &md, img)) || !md.IsCubemap())
		{
			EditorLog::Write("Probe", "not a cubemap DDS: %s", rel.c_str());
			return nullptr;
		}
		if (FAILED(Gfx::CreateShaderResourceView(Gfx::Device(), img.GetImages(), img.GetImageCount(), md, g.Loaded.GetAddressOf())))
			return nullptr;
		g.LoadedRes = (int)md.width;
		++g.Version;
		EditorLog::Write("Probe", "loaded %s (%d, %d mips)", rel.c_str(), g.LoadedRes, (int)md.mipLevels);
		return g.Loaded.Get();
	}

	GfxShaderResourceView* SourceOf(ReflectionProbe* p, ProbeGpu& g, int& res)
	{
		switch (p->GetMode())
		{
		case ReflectionProbe::Mode::Realtime:
			res = g.Res;
			return g.Captured ? g.SRV.Get() : nullptr;
		case ReflectionProbe::Mode::Custom:
		{
			if (g.Relit && g.Captured)
			{
				res = g.Res;
				return g.SRV.Get();
			}
			GfxShaderResourceView* s = LoadCube(g, p->GetCustomCubemap());
			res = g.LoadedRes;
			return s;
		}
		default:
		{
			if (g.Relit && g.Captured)   // 낮 · 밤이 다시 찍은 큐브
			{
				res = g.Res;
				return g.SRV.Get();
			}
			GfxShaderResourceView* s = LoadCube(g, p->GetBakedTexture());
			res = g.LoadedRes;
			return s;
		}
		}
	}

	bool EnsureArray(int res, int slots)
	{
		if (s_ArrayTex && s_ArrayRes == res && s_ArraySlots == slots)
			return true;
		s_ArrayTex.Reset(); s_ArraySRV.Reset(); s_ArrayRTV.clear();
		for (Slot& s : s_Slots) s = Slot();
		GfxDevice* dev = Gfx::Device();
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = td.Height = (UINT)res;
		td.MipLevels = (UINT)MipCount(res);
		td.ArraySize = (UINT)(6 * slots);
		td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		td.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
		if (FAILED(dev->CreateTexture2D(&td, nullptr, s_ArrayTex.GetAddressOf())))
		{
			EditorLog::Write("Probe", "probe array %d x %d failed", res, slots);
			s_ArrayRes = s_ArraySlots = 0;
			return false;
		}
		D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
		sd.Format = td.Format;
		sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
		sd.TextureCubeArray.MipLevels = td.MipLevels;
		sd.TextureCubeArray.First2DArrayFace = 0;
		sd.TextureCubeArray.NumCubes = (UINT)slots;
		dev->CreateShaderResourceView(s_ArrayTex.Get(), &sd, s_ArraySRV.GetAddressOf());
		s_ArrayMips = (int)td.MipLevels;
		s_ArrayRTV.resize((size_t)slots * 6 * s_ArrayMips);
		for (int s = 0; s < slots; ++s)
			for (int f = 0; f < 6; ++f)
				for (int m = 0; m < s_ArrayMips; ++m)
				{
					D3D11_RENDER_TARGET_VIEW_DESC rd = {};
					rd.Format = td.Format;
					rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
					rd.Texture2DArray.MipSlice = (UINT)m;
					rd.Texture2DArray.FirstArraySlice = (UINT)(s * 6 + f);
					rd.Texture2DArray.ArraySize = 1;
					dev->CreateRenderTargetView(s_ArrayTex.Get(), &rd, s_ArrayRTV[((size_t)s * 6 + f) * s_ArrayMips + m].GetAddressOf());
				}
		s_ArrayRes = res;
		s_ArraySlots = slots;
		EditorLog::Write("Probe", "probe array %d x %d slots (%d mips)", res, slots, s_ArrayMips);
		return s_ArraySRV != nullptr;
	}

	// 밉 → 지각 거칠기 (셰이더의 mip = r (1.7 - 0.7 r) × max(밉 수 - 4, 6) 의 역)
	float RoughnessOfMip(int mip, int mips)
	{
		const float steps = (float)(std::max)(mips - 4, 6);
		const float x = (float)mip / steps;
		if (x >= 1.0f) return 1.0f;
		return (1.7f - sqrtf(1.7f * 1.7f - 2.8f * x)) / 1.4f;
	}

	// 칸 하나: 원본 큐브 → 배열의 6 면 × 모든 밉 (밉 0 = 그대로, 그 위 = GGX)
	void FilterSlot(int slot, GfxShaderResourceView* src, int srcRes)
	{
		if (!s_FilterTried)
		{
			s_FilterTried = true;
			s_Filter = std::make_unique<Effect>(ComPtr<GfxDevice>(Gfx::Device()), L"../Shaders/54. ReflectionProbe.fx");
			if (!s_Filter->GetFX() || !s_Filter->GetFX()->IsValid())
			{
				EditorLog::Write("Probe", "54. ReflectionProbe.fx failed to load");
				s_Filter.reset();
			}
		}
		if (!s_Filter)
			return;
		FxEffect* fx = s_Filter->GetFX();
		FxTechnique* tech = fx->GetTechniqueByName("FilterTech");
		FxVar* params = Var(fx, "gFilter");
		FxVar* source = Var(fx, "gProbeSource");
		if (!tech || !tech->IsValid() || !params || !source)
			return;
		PROFILE_SCOPE("Reflection Probe Filter");
		GfxContext* ctx = Gfx::Context();
		ComPtr<GfxRenderTargetView> oldRtv;
		ComPtr<GfxDepthStencilView> oldDsv;
		ctx->OMGetRenderTargets(1, oldRtv.GetAddressOf(), oldDsv.GetAddressOf());
		UINT vpCount = 1;
		D3D11_VIEWPORT oldVp = {};
		ctx->RSGetViewports(&vpCount, &oldVp);
		ctx->OMSetDepthStencilState(nullptr, 0);
		ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		ctx->RSSetState(nullptr);
		ctx->IASetInputLayout(nullptr);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		source->SetResource(src);
		for (int f = 0; f < 6; ++f)
			for (int m = 0; m < s_ArrayMips; ++m)
			{
				GfxRenderTargetView* rtv = s_ArrayRTV[((size_t)slot * 6 + f) * s_ArrayMips + m].Get();
				if (!rtv) continue;
				ctx->OMSetRenderTargets(1, &rtv, nullptr);
				const int size = (std::max)(1, s_ArrayRes >> m);
				const D3D11_VIEWPORT vp = { 0, 0, (float)size, (float)size, 0.0f, 1.0f };
				ctx->RSSetViewports(1, &vp);
				// x 면, y 지각 거칠기, z 원본 한 변, w 이 밉의 한 변
				const float p[4] = { (float)f, m == 0 ? 0.0f : RoughnessOfMip(m, s_ArrayMips), (float)(std::max)(srcRes, 1), (float)size };
				params->SetFloatVector(p);
				tech->GetPassByIndex(0)->Apply(0, ctx);
				ctx->Draw(3, 0);
			}
		source->SetResource(nullptr);
		tech->GetPassByIndex(0)->Apply(0, ctx);
		GfxShaderResourceView* nullSRV[4] = {};
		ctx->PSSetShaderResources(0, 4, nullSRV);
		GfxRenderTargetView* restore[1] = { oldRtv.Get() };
		ctx->OMSetRenderTargets(1, restore, oldDsv.Get());
		if (vpCount > 0)
			ctx->RSSetViewports(1, &oldVp);
	}

	// 상자가 절두체 (행 벡터 viewProj, D3D 깊이 0..1) 와 겹치는지
	bool BoxInFrustum(CXMMATRIX vp, const Vec3& mn, const Vec3& mx)
	{
		XMFLOAT4X4 m;
		XMStoreFloat4x4(&m, vp);
		const float col[4][4] = {
			{ m._11, m._21, m._31, m._41 }, { m._12, m._22, m._32, m._42 }, { m._13, m._23, m._33, m._43 }, { m._14, m._24, m._34, m._44 } };
		float planes[6][4];
		for (int i = 0; i < 4; ++i)
		{
			planes[0][i] = col[3][i] + col[0][i];
			planes[1][i] = col[3][i] - col[0][i];
			planes[2][i] = col[3][i] + col[1][i];
			planes[3][i] = col[3][i] - col[1][i];
			planes[4][i] = col[2][i];
			planes[5][i] = col[3][i] - col[2][i];
		}
		for (const auto& pl : planes)
		{
			const float x = pl[0] > 0 ? mx.x : mn.x, y = pl[1] > 0 ? mx.y : mn.y, z = pl[2] > 0 ? mx.z : mn.z;
			if (pl[0] * x + pl[1] * y + pl[2] * z + pl[3] < 0)
				return false;
		}
		return true;
	}

	ReflectionProbe* FindByName(const std::string& name)
	{
		for (ReflectionProbe* p : ReflectionProbe::All())
			if (NameOf(p) == name)
				return p;
		return nullptr;
	}

	const char* ModeName(ReflectionProbe::Mode m)
	{
		return m == ReflectionProbe::Mode::Realtime ? "Realtime" : (m == ReflectionProbe::Mode::Custom ? "Custom" : "Baked");
	}
}

namespace ReflectionProbes
{
	void SetCapture(std::function<void(const CaptureView&)> fn) { s_Capture = std::move(fn); }
	bool Capturing() { return s_Capturing; }
	int SelectedCount() { return s_Count; }

	void RequestBake(ReflectionProbe* p)
	{
		if (p && std::find(s_BakeRequests.begin(), s_BakeRequests.end(), p) == s_BakeRequests.end())
			s_BakeRequests.push_back(p);
	}

	void RequestRender(ReflectionProbe* p)
	{
		if (p)
			s_Gpu[p].RenderRequested = true;
	}

	void Forget(ReflectionProbe* p)
	{
		s_Gpu.erase(p);
		s_BakeRequests.erase(std::remove(s_BakeRequests.begin(), s_BakeRequests.end(), p), s_BakeRequests.end());
		for (Slot& s : s_Slots)
			if (s.Probe == p)
				s = Slot();
	}

	void Update()
	{
		if (!s_Capture || s_Capturing)
			return;
		// 굽기 요청 (Inspector 의 Bake)
		if (!s_BakeRequests.empty())
		{
			std::vector<ReflectionProbe*> list;
			list.swap(s_BakeRequests);
			for (ReflectionProbe* p : list)
			{
				std::string error;
				if (!Bake(p, error))
					Debug::LogWarning("Reflection Probe: " + error);
			}
		}
		// 낮 · 밤 (Day Night Cycle 의 Reflection Probe Refresh): 시각이 그만큼 흐르면 모든 프로브를 다시 찍는다 —
		//  Baked · Custom 도 실행 중의 큐브로 (파일은 그대로), 처음 뒤로는 프레임마다 한 면씩. 꺼지면 구운 큐브로 되돌린다
		const DayNightState& dn = DayNightState::Get();
		if (dn.Enabled && dn.ProbeRefreshMinutes > 0.0f)
		{
			float minutes = fabsf(dn.TimeOfDay - s_DayNightLast);
			minutes = (std::min)(minutes, 24.0f - minutes) * 60.0f;   // 자정을 넘어도
			if (s_DayNightLast < 0.0f || minutes >= dn.ProbeRefreshMinutes || dn.ProbeRefreshSerial != s_DayNightSerial)
			{
				s_DayNightLast = dn.TimeOfDay;
				s_DayNightSerial = dn.ProbeRefreshSerial;
				for (ReflectionProbe* p : ReflectionProbe::All())
					if (p && p->IsEnabled() && p->IsActiveInHierarchy())
					{
						ProbeGpu& g = s_Gpu[p];
						g.Relit = p->GetMode() != ReflectionProbe::Mode::Realtime;
						g.RenderRequested = true;
					}
			}
		}
		else if (s_DayNightLast >= 0.0f)
		{
			s_DayNightLast = -1.0f;
			for (auto& [probe, g] : s_Gpu)
				if (g.Relit)
				{
					g.Relit = false;
					++g.Version;
				}
		}
		// 실시간 프로브: On Awake = 처음 한 번 (설정이 바뀌면 다시), Every Frame = 매 프레임, Via Scripting = 요청 때. 낮 · 밤이 다시 찍는 Baked · Custom 은 요청 때
		for (ReflectionProbe* p : ReflectionProbe::All())
		{
			if (!p || !p->IsEnabled() || !p->IsActiveInHierarchy())
				continue;
			const bool realtime = p->GetMode() == ReflectionProbe::Mode::Realtime;
			ProbeGpu& g = s_Gpu[p];
			if (!realtime && !g.Relit)
				continue;
			bool due = g.NextFace > 0;   // Individual Faces 진행 중
			if (realtime)
			{
				if (g.SeenSettings != p->SettingsVersion())
				{
					g.SeenSettings = p->SettingsVersion();
					due = true;
				}
				if (!g.Captured || p->GetRefreshMode() == ReflectionProbe::Refresh::EveryFrame)
					due = true;
			}
			if (g.RenderRequested)
				due = true;
			if (!due)
				continue;
			g.RenderRequested = false;
			const bool sliced = g.Captured && (!realtime || p->GetTimeSlicing() == ReflectionProbe::TimeSlicing::IndividualFaces);
			CaptureFaces(p, g, g.NextFace, sliced ? 1 : 6 - g.NextFace);
		}
	}

	void Select(const XMFLOAT3& eye, CXMMATRIX viewProj)
	{
		s_Count = 0;
		if (s_Capturing)
			return;   // 찍는 동안은 하늘만 (1 바운스)
		struct Candidate { ReflectionProbe* P; GfxShaderResourceView* Src; int Res; float Volume; Vec3 Min, Max; };
		std::vector<Candidate> list;
		for (ReflectionProbe* p : ReflectionProbe::All())
		{
			if (!p || !p->IsEnabled() || !p->IsActiveInHierarchy())
				continue;
			ProbeGpu& g = s_Gpu[p];
			int res = 0;
			GfxShaderResourceView* src = SourceOf(p, g, res);
			if (!src)
				continue;
			Candidate c{ p, src, res, 0.0f };
			p->Bounds(c.Min, c.Max);
			const Vec3 d = c.Max - c.Min;
			c.Volume = d.x * d.y * d.z;
			if (c.Volume <= 0.0f || !BoxInFrustum(viewProj, c.Min, c.Max))
				continue;
			list.push_back(c);
		}
		if (list.empty())
			return;
		// Importance 가 높은 것부터, 같으면 작은 상자부터 (Unity 와 같은 순서)
		std::stable_sort(list.begin(), list.end(), [](const Candidate& a, const Candidate& b) {
			if (a.P->GetImportance() != b.P->GetImportance()) return a.P->GetImportance() > b.P->GetImportance();
			return a.Volume < b.Volume;
		});
		if ((int)list.size() > kMaxProbes)
			list.resize(kMaxProbes);

		int res = 16;
		for (const Candidate& c : list)
			res = (std::max)(res, c.Res);
		res = (std::min)(res, 512);
		int slots = 1;
		while (slots < (int)list.size()) slots *= 2;
		if (!EnsureArray(res, slots))
			return;

		// 칸 고르기: 이미 들어 있는 프로브는 제자리, 새 프로브는 빈 칸 (이번에 쓰지 않는 칸)
		int slotOf[kMaxProbes];
		bool used[kMaxProbes] = {};
		for (size_t i = 0; i < list.size(); ++i)
		{
			slotOf[i] = -1;
			for (int s = 0; s < s_ArraySlots; ++s)
				if (s_Slots[s].Probe == list[i].P && !used[s]) { slotOf[i] = s; used[s] = true; break; }
		}
		for (size_t i = 0; i < list.size(); ++i)
		{
			if (slotOf[i] >= 0) continue;
			for (int s = 0; s < s_ArraySlots; ++s)
				if (!used[s]) { slotOf[i] = s; used[s] = true; break; }
		}
		for (size_t i = 0; i < list.size(); ++i)
		{
			const Candidate& c = list[i];
			const int s = slotOf[i];
			if (s < 0) continue;
			ProbeGpu& g = s_Gpu[c.P];
			Slot& slot = s_Slots[s];
			if (slot.Probe != c.P || slot.Version != g.Version || slot.Source != c.Src)
			{
				FilterSlot(s, c.Src, c.Res);
				slot.Probe = c.P;
				slot.Version = g.Version;
				slot.Source = c.Src;
			}
			const Vec3 pos = c.P->CapturePosition();
			XMFLOAT4* d = &s_Data[s_Count * 3];
			d[0] = XMFLOAT4(c.Min.x, c.Min.y, c.Min.z, c.P->GetBlendDistance());
			// 실행 중에 찍은 큐브 (실시간 · 낮 · 밤이 다시 찍은 것) 는 지금의 하늘 · 빛이 이미 들어 있다 → 음수 = 셰이더가 날씨 · 낮밤 하늘 보정을 하지 않는다
			const bool live = c.Src == g.SRV.Get();
			d[1] = XMFLOAT4(c.Max.x, c.Max.y, c.Max.z, live ? -(std::max)(c.P->GetIntensity(), 1e-4f) : c.P->GetIntensity());
			d[2] = XMFLOAT4(pos.x, pos.y, pos.z, (float)(s * 2 + (c.P->GetBoxProjection() ? 1 : 0)));
			++s_Count;
		}
		(void)eye;
	}

	void Bind(InstancedBasicEffect* effect)
	{
		FxEffect* fx = effect ? effect->GetFX() : nullptr;
		if (!fx)
			return;
		const float params[4] = { (float)s_Count, (float)s_ArrayMips, 0, 0 };
		if (FxVar* v = Var(fx, "gProbeParams")) v->SetFloatVector(params);
		if (s_Count > 0)
		{
			if (FxVar* v = Var(fx, "gProbeData")) v->SetFloatVectorArray(&s_Data[0].x, 0, (UINT)(s_Count * 3));
			if (FxVar* v = Var(fx, "gProbeCubes")) v->SetResource(s_ArraySRV.Get());
		}
	}

	bool Bake(ReflectionProbe* p, std::string& error)
	{
		if (!p || !p->GetGameObject())
		{
			error = "no probe";
			return false;
		}
		if (!s_Capture)
		{
			error = "the renderer is not ready";
			return false;
		}
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		const std::wstring scenePath = scene ? scene->GetScenePath() : std::wstring();
		if (scenePath.empty())
		{
			error = "save the scene first (baked probes are stored next to the scene)";
			return false;
		}
		// Unity 와 같은 자리: <씬 폴더>/<씬 이름>/ReflectionProbe-<n>.dds — 이미 이 폴더의 파일이면 그 이름 그대로
		fs::path sceneRel = fs::path(scenePath);
		if (sceneRel.is_absolute())
		{
			std::error_code rec;
			sceneRel = fs::relative(sceneRel, fs::path(PathManager::GetI()->GetContentPathW()), rec);
			if (rec || sceneRel.empty() || sceneRel.begin()->wstring() == L"..")
			{
				error = "the scene is outside the project — save it under Assets first";
				return false;
			}
		}
		const fs::path dirRel = sceneRel.parent_path() / sceneRel.stem();
		std::string rel = p->GetBakedTexture();
		const std::string dirStr = wstring_to_string(dirRel.generic_wstring());
		if (rel.empty() || _strnicmp(rel.c_str(), dirStr.c_str(), dirStr.size()) != 0)
		{
			for (int n = 0;; ++n)
			{
				const std::string cand = dirStr + "/ReflectionProbe-" + std::to_string(n) + ".dds";
				bool taken = false;
				for (ReflectionProbe* o : ReflectionProbe::All())
					if (o != p && _stricmp(o->GetBakedTexture().c_str(), cand.c_str()) == 0)
						taken = true;
				if (!taken) { rel = cand; break; }
			}
		}
		ProbeGpu& g = s_Gpu[p];
		g.NextFace = 0;
		if (!CaptureFaces(p, g, 0, 6))
		{
			error = "capture failed";
			return false;
		}
		DirectX::ScratchImage img;
		HRESULT hr = Gfx::CaptureTexture(Gfx::Context(), g.Tex.Get(), img);
		if (FAILED(hr))
		{
			error = "cannot read the cubemap back (hr=" + std::to_string((long)hr) + ")";
			return false;
		}
		const fs::path full = FullPath(rel);
		std::error_code ec;
		fs::create_directories(full.parent_path(), ec);
		hr = DirectX::SaveToDDSFile(img.GetImages(), img.GetImageCount(), img.GetMetadata(), DirectX::DDS_FLAGS_NONE, full.c_str());
		if (FAILED(hr))
		{
			error = "cannot write " + rel;
			return false;
		}
		p->SetBakedTexture(rel);
		g.LoadedPath.clear();   // 다음 Select 가 새 파일을 읽는다
		EditorLog::Write("Probe", "baked %s -> %s (%d, %s)", NameOf(p).c_str(), rel.c_str(), g.Res, g.Hdr ? "HDR" : "LDR");
		return true;
	}

	void RegisterEditor()
	{
		// nova probe info | bake [--name X] | render [--name X]
		CliServer::Register("probe", "reflection probe op: {op: info | bake | render, name?} (nova probe help)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
			const std::string op = args.value("op", std::string("help"));
			const std::string name = args.value("name", std::string());
			std::vector<ReflectionProbe*> targets;
			if (!name.empty())
			{
				ReflectionProbe* p = FindByName(name);
				if (!p) { error = "no Reflection Probe on '" + name + "'"; return false; }
				targets.push_back(p);
			}
			if (op == "help")
			{
				result = { { "ops", { "info: probes (mode, baked texture, captured, importance) and how many the last view used",
					"bake [--name X]: capture now and save Assets/.../<scene>/ReflectionProbe-<n>.dds (all Baked probes without --name; save the scene first)",
					"render [--name X]: re-capture realtime probes on the next frame" } } };
				return true;
			}
			if (op == "info")
			{
				nlohmann::json list = nlohmann::json::array();
				for (ReflectionProbe* p : ReflectionProbe::All())
				{
					const auto it = s_Gpu.find(p);
					const bool captured = it != s_Gpu.end() && (it->second.Captured || it->second.Loaded);
					int slot = -1;
					for (int s = 0; s < s_ArraySlots; ++s)
						if (s_Slots[s].Probe == p) slot = s;
					const bool relit = it != s_Gpu.end() && it->second.Relit && it->second.Captured;
					list.push_back({ { "name", NameOf(p) }, { "mode", ModeName(p->GetMode()) }, { "bakedTexture", p->GetBakedTexture() },
						{ "captured", captured }, { "resolution", p->GetResolution() }, { "importance", p->GetImportance() }, { "slot", slot },
						{ "relit", relit }, { "capturedTime", it != s_Gpu.end() ? it->second.CapturedTime : -1.0f } });
				}
				result = { { "probes", list }, { "lastViewProbes", s_Count }, { "arrayResolution", s_ArrayRes }, { "arraySlots", s_ArraySlots } };
				return true;
			}
			if (op == "bake")
			{
				if (targets.empty())
					for (ReflectionProbe* p : ReflectionProbe::All())
						if (p->GetMode() == ReflectionProbe::Mode::Baked && p->IsActiveInHierarchy())
							targets.push_back(p);
				nlohmann::json baked = nlohmann::json::array();
				for (ReflectionProbe* p : targets)
				{
					if (!Bake(p, error))
						return false;
					baked.push_back({ { "name", NameOf(p) }, { "bakedTexture", p->GetBakedTexture() } });
				}
				result = { { "baked", baked } };
				return true;
			}
			if (op == "render")
			{
				if (targets.empty())
					for (ReflectionProbe* p : ReflectionProbe::All())
						if (p->GetMode() == ReflectionProbe::Mode::Realtime)
							targets.push_back(p);
				for (ReflectionProbe* p : targets)
					RequestRender(p);
				result = { { "requested", (int)targets.size() } };
				return true;
			}
			error = "unknown op '" + op + "' (info | bake | render)";
			return false;
		});
	}
}
