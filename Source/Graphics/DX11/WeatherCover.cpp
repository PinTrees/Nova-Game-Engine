#include "pch.h"
#include "WeatherCover.h"
#include "WeatherState.h"
#include "ShadowRenderer.h"
#include "CustomShaders.h"
#include "Effects.h"
#include "RenderManager.h"
#include "SceneCulling.h"
#include "Profiler.h"

namespace
{
	constexpr UINT kRes = 1024;          // 12.5 cm 칸
	constexpr float kSize = 128.0f;      // 카메라 둘레 (m)
	constexpr float kSnap = 8.0f;        // 이만큼 움직이면 다시 그린다
	constexpr float kAbove = 300.0f;     // 카메라 위 이 높이에서 내려다본다
	constexpr float kRange = 700.0f;     // 깊이 범위 (m)
	constexpr int kRefreshFrames = 30;   // 움직이는 물체 (문 · 차) 를 따라가게 가끔 다시

	// 눈 발자국 맵 (59. WeatherSnow.fx): 카메라 둘레 48 m, 4.7 cm 칸
	constexpr int kSnowRes = 1024;
	constexpr float kSnowSize = 48.0f;
	constexpr float kBelow = 150.0f;     // 카메라 아래 이 깊이에서 올려다본다 (밟는 것의 바닥)
	constexpr float kBelowRange = 300.0f;

	const XMMATRIX kToTex(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	// 깊이 맵 하나 (그림자 맵과 같은 형식 — DSV = D24S8, SRV = R24 로 같은 비트)
	struct Depth
	{
		ComPtr<GfxTexture2D> Tex;
		ComPtr<GfxShaderResourceView> Srv;
		ComPtr<GfxDepthStencilView> Dsv;
		bool Failed = false;
	};

	struct View
	{
		Depth Map;
		float CenterX = 1e30f, CenterZ = 1e30f, Top = 0.0f;
		int Age = 0;
		WeatherCover::Info Info;
	};
	View s_Views[2];

	struct Snow
	{
		ComPtr<GfxTexture2D> Tex[2];              // 눌림 (RGBA16F 의 r) — 번갈아 쓴다
		ComPtr<GfxShaderResourceView> Srv[2];
		ComPtr<GfxUnorderedAccessView> Uav[2];
		int Cur = 0;                              // 지금 그릴 때 읽는 쪽
		ComPtr<GfxTexture2D> StampTex;            // 이번 프레임 도장 (비탈로 넓히기 전)
		ComPtr<GfxUnorderedAccessView> StampUav;
		Depth Bottom;
		std::unique_ptr<Effect> Fx;
		FxPass* Pass = nullptr;
		FxPass* StampPass = nullptr;
		bool Failed = false;
		bool Valid = false;
		int OriginX = 0, OriginZ = 0;   // 창의 첫 칸 (월드 칸 번호)
		bool HaveOrigin = false;
	};
	Snow s_Snow;

	bool EnsureDepth(Depth& d, UINT res, const char* what)
	{
		if (d.Tex)
			return true;
		if (d.Failed)
			return false;
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = td.Height = res;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R24G8_TYPELESS;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		GfxDevice* dev = Gfx::Device();
		D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
		sd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		sd.Texture2D.MipLevels = 1;
		D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
		dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		if (FAILED(dev->CreateTexture2D(&td, nullptr, d.Tex.GetAddressOf())) ||
			FAILED(dev->CreateShaderResourceView(d.Tex.Get(), &sd, d.Srv.GetAddressOf())) ||
			FAILED(dev->CreateDepthStencilView(d.Tex.Get(), &dd, d.Dsv.GetAddressOf())))
		{
			EditorLog::Write("Weather", "%s create failed", what);
			d = Depth();
			d.Failed = true;
			return false;
		}
		return true;
	}

	// 그림자 캐스터 패스로 깊이 하나 (eye 에서 dir 쪽을 정사영). draw = 그릴 것
	void DrawDepth(GfxContext* dc, Depth& d, UINT res, CXMMATRIX vp, const XMFLOAT3& eye, const XMFLOAT3& dir, float size, float range, const std::function<void()>& draw)
	{
		auto fx = Effects::BuildShadowMapFX;
		const XMFLOAT4 toLight(-dir.x, -dir.y, -dir.z, 0.0f);
		fx->SetEyePosW(eye);
		fx->SetShadowLight(toLight);
		fx->SetShadowBias(0.0f, 0.0f);
		fx->SetViewProj(vp);
		auto& cs = CustomShaders::CurrentShadow();
		const XMFLOAT4 savedLight = cs.Light;
		const float savedBias[2] = { cs.Bias[0], cs.Bias[1] };
		cs.Light = toLight;
		cs.Bias[0] = cs.Bias[1] = 0.0f;
		RenderManager* rm = RenderManager::GetI();
		const XMMATRIX savedVP = rm->LightViewProjection;
		const float savedTexel = rm->ShadowTexelWorld;
		rm->LightViewProjection = vp;
		rm->ShadowTexelWorld = size / (float)res;

		D3D11_VIEWPORT port = { 0.0f, 0.0f, (float)res, (float)res, 0.0f, 1.0f };
		dc->RSSetViewports(1, &port);
		GfxRenderTargetView* none[1] = { nullptr };
		dc->OMSetRenderTargets(1, none, d.Dsv.Get());
		dc->ClearDepthStencilView(d.Dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
		ShadowRenderer::Current = ShadowRenderer::CasterPass{ true, false, dir, range };
		SceneCulling::Cull(vp, true);
		draw();
		ShadowRenderer::Current = ShadowRenderer::CasterPass();
		dc->OMSetRenderTargets(1, none, nullptr);

		rm->LightViewProjection = savedVP;
		rm->ShadowTexelWorld = savedTexel;
		cs.Light = savedLight;
		cs.Bias[0] = savedBias[0];
		cs.Bias[1] = savedBias[1];
	}

	FxVar* Var(FxEffect* fx, const char* name)
	{
		FxVar* v = fx ? fx->GetVariableByName(name) : nullptr;
		return v && v->IsValid() ? v : nullptr;
	}

	bool EnsureSnow()
	{
		Snow& s = s_Snow;
		if (s.Pass && s.Tex[0])
			return true;
		if (s.Failed)
			return false;
		s.Failed = true;   // 아래에서 다 되면 false
		s.Fx = std::make_unique<Effect>(Gfx::Device(), L"../Shaders/59. WeatherSnow.fx");
		FxEffect* fx = s.Fx->GetFX();
		auto pass = [fx](const char* name) -> FxPass* {
			FxTechnique* t = fx ? fx->GetTechniqueByName(name) : nullptr;
			FxPass* p = t && t->IsValid() ? t->GetPassByIndex(0) : nullptr;
			return p && p->IsValid() ? p : nullptr;
		};
		s.Pass = pass("DeformTech");
		s.StampPass = pass("StampTech");
		if (s.Pass == nullptr || s.StampPass == nullptr)
		{
			EditorLog::Write("Weather", "59. WeatherSnow.fx failed - no footprints");
			s.Pass = s.StampPass = nullptr;
			return false;
		}
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = td.Height = kSnowRes;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		GfxDevice* dev = Gfx::Device();
		D3D11_UNORDERED_ACCESS_VIEW_DESC u = {};
		u.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		u.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		bool ok = true;
		for (int i = 0; i < 2 && ok; ++i)
			ok = SUCCEEDED(dev->CreateTexture2D(&td, nullptr, s.Tex[i].GetAddressOf())) &&
				SUCCEEDED(dev->CreateShaderResourceView(s.Tex[i].Get(), nullptr, s.Srv[i].GetAddressOf())) &&
				SUCCEEDED(dev->CreateUnorderedAccessView(s.Tex[i].Get(), &u, s.Uav[i].GetAddressOf()));
		// 도장 (이번 프레임만): R32F — compute 안에서만 읽고 쓴다
		D3D11_TEXTURE2D_DESC sd = td;
		sd.Format = DXGI_FORMAT_R32_FLOAT;
		sd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		D3D11_UNORDERED_ACCESS_VIEW_DESC su = u;
		su.Format = DXGI_FORMAT_R32_FLOAT;
		ok = ok && SUCCEEDED(dev->CreateTexture2D(&sd, nullptr, s.StampTex.GetAddressOf())) &&
			SUCCEEDED(dev->CreateUnorderedAccessView(s.StampTex.Get(), &su, s.StampUav.GetAddressOf())) &&
			EnsureDepth(s.Bottom, kSnowRes, "snow bottom map");
		if (!ok)
		{
			EditorLog::Write("Weather", "snow footprint map create failed - no footprints");
			for (int i = 0; i < 2; ++i) { s.Tex[i].Reset(); s.Srv[i].Reset(); s.Uav[i].Reset(); }
			s.StampTex.Reset(); s.StampUav.Reset();
			return false;
		}
		s.Failed = false;
		s.HaveOrigin = false;
		EditorLog::Write("Weather", "snow footprint map %d x %d (%.0f m)", kSnowRes, kSnowRes, kSnowSize);
		return true;
	}

	// 발자국: 아래에서 본 깊이 (밟는 것) + 위에서 본 덮개 (땅) → 눌린 정도 (고리 배치, 프레임마다)
	void UpdateSnow(GfxContext* dc, const View& cover, const XMFLOAT3& viewer)
	{
		const WeatherState& w = WeatherState::Get();
		Snow& s = s_Snow;
		if (w.SnowCover <= 0.001f)
		{
			s.Valid = false;
			s.HaveOrigin = false;   // 눈이 다 녹으면 발자국도 지운다 (다시 쌓이면 새 눈)
			return;
		}
		if (!EnsureSnow())
			return;
		PROFILE_GPU("Weather Snow");
		const float texel = kSnowSize / (float)kSnowRes;
		// 창: 카메라가 가운데 (16 칸 단위)
		const int ox = (int)floorf((viewer.x - kSnowSize * 0.5f) / texel / 16.0f) * 16;
		const int oz = (int)floorf((viewer.z - kSnowSize * 0.5f) / texel / 16.0f) * 16;
		const int oldX = s.HaveOrigin ? s.OriginX : INT_MIN / 2, oldZ = s.HaveOrigin ? s.OriginZ : INT_MIN / 2;

		// 아래에서 올려다본 깊이 (화면 위 = +Z)
		const float cx = (ox + kSnowRes * 0.5f) * texel, cz = (oz + kSnowRes * 0.5f) * texel;
		const float eyeY = viewer.y - kBelow;
		const XMMATRIX viewM = XMMatrixLookToLH(XMVectorSet(cx, eyeY, cz, 1.0f), XMVectorSet(0, 1, 0, 0), XMVectorSet(0, 0, 1, 0));
		const XMMATRIX vp = viewM * XMMatrixOrthographicLH(kSnowSize, kSnowSize, 0.0f, kBelowRange);
		DrawDepth(dc, s.Bottom, kSnowRes, vp, XMFLOAT3(cx, eyeY, cz), XMFLOAT3(0.0f, 1.0f, 0.0f), kSnowSize, kBelowRange, []() {
			if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
				scene->RenderSceneDeformers();
		});

		FxEffect* fx = s.Fx->GetFX();
		const int origin[2] = { ox, oz }, old[2] = { oldX, oldZ };
		const int size = kSnowRes;
		const float depth = (std::max)(0.02f, w.SnowDepth);
		const float refill = std::clamp((float)DT, 0.0f, 0.1f) * 0.05f * std::clamp(w.SnowFall, 0.0f, 1.0f);   // 센 눈이면 20 초에 다시 덮인다
		XMFLOAT4X4 bottom;
		XMStoreFloat4x4(&bottom, vp * kToTex);
		const float coverInfo[4] = { cover.Info.Valid ? 1.0f : 0.0f, cover.Info.TopY, cover.Info.Range, (float)kRes };
		const float bottomInfo[4] = { 1.0f, eyeY, kBelowRange, (float)kSnowRes };
		if (FxVar* v = Var(fx, "gOrigin")) v->SetRawValue(origin, 0, sizeof(origin));
		if (FxVar* v = Var(fx, "gOldOrigin")) v->SetRawValue(old, 0, sizeof(old));
		if (FxVar* v = Var(fx, "gSize")) v->SetRawValue(&size, 0, sizeof(size));
		if (FxVar* v = Var(fx, "gTexel")) v->SetRawValue(&texel, 0, sizeof(texel));
		if (FxVar* v = Var(fx, "gSnowDepth")) v->SetRawValue(&depth, 0, sizeof(depth));
		if (FxVar* v = Var(fx, "gRefill")) v->SetRawValue(&refill, 0, sizeof(refill));
		if (FxVar* v = Var(fx, "gCoverToTex")) v->AsMatrix()->SetMatrix(&cover.Info.ToTex._11);
		if (FxVar* v = Var(fx, "gBottomToTex")) v->AsMatrix()->SetMatrix(&bottom._11);
		if (FxVar* v = Var(fx, "gCoverInfo")) v->SetRawValue(coverInfo, 0, sizeof(coverInfo));
		if (FxVar* v = Var(fx, "gBottomInfo")) v->SetRawValue(bottomInfo, 0, sizeof(bottomInfo));
		const float slope = texel / 0.12f;   // 발자국 벽 너비 12 cm
		if (FxVar* v = Var(fx, "gSlope")) v->SetRawValue(&slope, 0, sizeof(slope));
		FxVar* vDeform = Var(fx, "gDeform");
		FxVar* vPrev = Var(fx, "gPrev");
		FxVar* vStamp = Var(fx, "gStamp");
		FxVar* vCover = Var(fx, "gCover");
		FxVar* vBottom = Var(fx, "gBottom");
		auto unbind = [&]() {
			if (vDeform) vDeform->SetUnorderedAccessView(nullptr);
			if (vPrev) vPrev->SetResource(nullptr);
			if (vStamp) vStamp->SetUnorderedAccessView(nullptr);
			if (vCover) vCover->SetResource(nullptr);
			if (vBottom) vBottom->SetResource(nullptr);
			GfxShaderResourceView* ns[4] = {};
			GfxUnorderedAccessView* nu[4] = {};
			dc->CSSetShaderResources(0, 4, ns);
			dc->CSSetUnorderedAccessViews(0, 4, nu, nullptr);
		};
		// 1) 도장
		if (vStamp) vStamp->SetUnorderedAccessView(s.StampUav.Get());
		if (vCover) vCover->SetResource(cover.Info.Valid ? cover.Info.Srv : nullptr);
		if (vBottom) vBottom->SetResource(s.Bottom.Srv.Get());
		s.StampPass->Apply(0, dc);
		dc->Dispatch(kSnowRes / 8, kSnowRes / 8, 1);
		unbind();
		// 2) 비탈로 넓혀 남은 발자국과 합치기 (지난 쪽 → 다른 쪽)
		const int next = 1 - s.Cur;
		if (vPrev) vPrev->SetResource(s.Srv[s.Cur].Get());
		if (vDeform) vDeform->SetUnorderedAccessView(s.Uav[next].Get());
		if (vStamp) vStamp->SetUnorderedAccessView(s.StampUav.Get());
		s.Pass->Apply(0, dc);
		dc->Dispatch(kSnowRes / 8, kSnowRes / 8, 1);
		unbind();
		s.Cur = next;

		s.OriginX = ox;
		s.OriginZ = oz;
		s.HaveOrigin = true;
		s.Valid = true;
	}
}

namespace WeatherCover
{
	const Info& Get(int view)
	{
		return s_Views[std::clamp(view, 0, 1)].Info;
	}

	size_t MemoryBytes()
	{
		size_t b = 0;
		for (const View& v : s_Views)
			if (v.Map.Tex) b += (size_t)kRes * kRes * 4;
		if (s_Snow.Tex[0]) b += (size_t)kSnowRes * kSnowRes * (8 * 2 + 4 + 4);   // 눌림 RGBA16F 두 장 + 도장 R32F + 아래 깊이 D24S8
		return b;
	}

	void Render(GfxContext* dc, int view, const XMFLOAT3& viewer)
	{
		View& v = s_Views[std::clamp(view, 0, 1)];
		if (!WeatherState::Get().NeedsCover())
		{
			v.Info.Valid = false;
			v.CenterX = 1e30f;   // 다시 켜지면 바로 그린다
			s_Snow.Valid = false;
			s_Snow.HaveOrigin = false;
			return;
		}
		if (!EnsureDepth(v.Map, kRes, "cover map"))
			return;
		const float cx = floorf(viewer.x / kSnap + 0.5f) * kSnap;
		const float cz = floorf(viewer.z / kSnap + 0.5f) * kSnap;
		const float top = viewer.y + kAbove;
		++v.Age;
		if (!v.Info.Valid || cx != v.CenterX || cz != v.CenterZ || fabsf(top - v.Top) >= kAbove * 0.5f || v.Age >= kRefreshFrames)
		{
			v.CenterX = cx;
			v.CenterZ = cz;
			v.Top = top;
			v.Age = 0;
			PROFILE_GPU("Weather Cover");
			// 바로 아래를 본다 (화면 위 = +Z)
			const XMMATRIX viewM = XMMatrixLookToLH(XMVectorSet(cx, top, cz, 1.0f), XMVectorSet(0, -1, 0, 0), XMVectorSet(0, 0, 1, 0));
			const XMMATRIX vp = viewM * XMMatrixOrthographicLH(kSize, kSize, 0.0f, kRange);
			DrawDepth(dc, v.Map, kRes, vp, XMFLOAT3(cx, top, cz), XMFLOAT3(0.0f, -1.0f, 0.0f), kSize, kRange, []() {
				if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
					scene->RenderSceneCover();
			});
			XMStoreFloat4x4(&v.Info.ToTex, vp * kToTex);
			v.Info.Srv = v.Map.Srv.Get();
			v.Info.TopY = top;
			v.Info.Range = kRange;
			v.Info.InvSize = 1.0f / (float)kRes;
			v.Info.Valid = true;
		}
		// 발자국은 날씨가 따라가는 화면 하나만 (Play = Game 뷰, 편집 = Scene 뷰) — 두 화면이 같은 맵을 그린다
		if (view == (Application::IsPlaying() ? 0 : 1))
			UpdateSnow(dc, v, viewer);
	}

	void Bind(InstancedBasicEffect* effect, int view)
	{
		FxEffect* fx = effect ? effect->GetFX() : nullptr;
		if (fx == nullptr)
			return;
		const WeatherState& w = WeatherState::Get();
		const Info& info = Get(view);
		const bool on = w.NeedsCover();
		const float surface[4] = { on ? w.Wetness : 0.0f, on ? w.PuddleLevel : 0.0f, on ? w.RainIntensity : 0.0f, w.Time };
		// z = 깊이 바이어스 (0.25 m — 표면 자신이 덮개가 되지 않게)
		const float params[4] = { on && info.Valid ? 1.0f : 0.0f, info.InvSize, 0.25f / kRange, 0.0f };
		// 먹구름 하늘 (덮개 맵과 상관없이 늘): 0 = 그대로
		const float sky[4] = { 1.0f - w.SkyTint.x * w.SkyBrightness, 1.0f - w.SkyTint.y * w.SkyBrightness, 1.0f - w.SkyTint.z * w.SkyBrightness, w.SkyDesaturate };
		const bool snowMap = on && s_Snow.Valid;
		const float texel = kSnowSize / (float)kSnowRes;
		const float snow[4] = { on ? w.SnowCover : 0.0f, w.SnowDepth, snowMap ? 1.0f : 0.0f, 0.0f };
		const float snowWin[4] = { s_Snow.OriginX * texel, s_Snow.OriginZ * texel, kSnowSize, (float)kSnowRes };
		if (FxVar* v = Var(fx, "gWeatherSky")) v->SetRawValue(sky, 0, sizeof(sky));
		if (FxVar* v = Var(fx, "gWeatherSurface")) v->SetRawValue(surface, 0, sizeof(surface));
		if (FxVar* v = Var(fx, "gWeatherCoverParams")) v->SetRawValue(params, 0, sizeof(params));
		if (FxVar* v = Var(fx, "gWeatherCoverVP")) v->AsMatrix()->SetMatrix(&info.ToTex._11);
		if (FxVar* v = Var(fx, "gWeatherCover")) v->AsShaderResource()->SetResource(on && info.Valid ? info.Srv : nullptr);
		if (FxVar* v = Var(fx, "gWeatherSnow")) v->SetRawValue(snow, 0, sizeof(snow));
		if (FxVar* v = Var(fx, "gWeatherSnowWin")) v->SetRawValue(snowWin, 0, sizeof(snowWin));
		if (FxVar* v = Var(fx, "gSnowDeform")) v->AsShaderResource()->SetResource(snowMap ? s_Snow.Srv[s_Snow.Cur].Get() : nullptr);
	}
}
