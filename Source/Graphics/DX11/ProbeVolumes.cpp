#include "pch.h"
#include "ProbeVolumes.h"
#include "AdaptiveProbeVolume.h"
#include "CustomShaders.h"
#include "Effects.h"
#include "EditorLog.h"
#include "Profiler.h"
#include "CliServer.h"
#include "SpriteBatch.h"
#include "Application.h"
#include "GameObject.h"
#include "SceneCulling.h"
#include "Scene.h"
#include "SceneManager.h"
#include "MeshRenderer.h"
#include "UMaterial.h"

namespace
{
	using ProbeVolumes::kCascades;
	constexpr int kPX = 32, kPY = 16, kPZ = 32;   // 프로브 (단계마다)
	constexpr int kVX = 64, kVY = 32, kVZ = 64;   // 복셀 (프로브 한 칸 = 2 복셀)
	constexpr int kSlab = 4;                      // 판 두께 (복셀) — 얇을수록 판 안에서 가려지는 면이 적다
	constexpr int kCapturesPerFrame = 8;          // 단계를 다시 지을 때 (장면이 바뀜 · 카메라를 따라 옮김)
	constexpr int kBackgroundCaptures = 1;        // 평소: 천천히 다시 찍기 (조명 변화는 매 프레임 다시 비추기가 맡는다)

	struct Tex3D
	{
		ComPtr<GfxTexture3D> Tex;
		ComPtr<GfxShaderResourceView> SRV;
		std::vector<ComPtr<GfxRenderTargetView>> Slice;   // Z 조각마다
		ComPtr<GfxRenderTargetView> All;                  // 모든 조각 (지우기)
	};

	bool Make3D(Tex3D& t, UINT w, UINT h, UINT d, DXGI_FORMAT fmt, bool mips, bool rtv = true)
	{
		GfxDevice* dev = Gfx::Device();
		D3D11_TEXTURE3D_DESC td = {};
		td.Width = w; td.Height = h; td.Depth = d;
		td.MipLevels = mips ? 0 : 1;
		td.Format = fmt;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE | (rtv ? D3D11_BIND_RENDER_TARGET : 0);
		td.MiscFlags = mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
		if (mips) td.BindFlags |= D3D11_BIND_RENDER_TARGET;
		if (FAILED(dev->CreateTexture3D(&td, nullptr, t.Tex.GetAddressOf())))
			return false;
		if (FAILED(dev->CreateShaderResourceView(t.Tex.Get(), nullptr, t.SRV.GetAddressOf())))
			return false;
		if (!rtv)
			return true;
		t.Slice.resize(d);
		for (UINT z = 0; z < d; ++z)
		{
			D3D11_RENDER_TARGET_VIEW_DESC rd = {};
			rd.Format = fmt;
			rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
			rd.Texture3D.MipSlice = 0;
			rd.Texture3D.FirstWSlice = z;
			rd.Texture3D.WSize = 1;
			dev->CreateRenderTargetView(t.Tex.Get(), &rd, t.Slice[z].GetAddressOf());
		}
		D3D11_RENDER_TARGET_VIEW_DESC all = {};
		all.Format = fmt;
		all.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
		all.Texture3D.MipSlice = 0;
		all.Texture3D.FirstWSlice = 0;
		all.Texture3D.WSize = d;
		dev->CreateRenderTargetView(t.Tex.Get(), &all, t.All.GetAddressOf());
		return t.All != nullptr;
	}

	struct Cascade
	{
		float Spacing = 0.0f;
		Vec3 ProbeOrigin;
		bool ProbeValid = false;
		int Front = 0;
		Vec3 VoxOrigin;
		bool VoxValid = false;
		bool Dirty = true;    // 다시 지어야 함 (장면이 바뀜 · 원점이 옮겨짐)
		bool BuildFast = false;   // 지금 짓는 것이 바뀐 단계 (프레임마다 여러 판)
		Vec3 BuildOrigin;
		int BuildStep = -1;   // -1 = 짓지 않음
	};

	struct Step { int Axis, Dir, K0, K1; };

	// 상태
	bool s_Ready = false, s_Failed = false;
	Tex3D s_VoxA[kCascades][2], s_VoxN[kCascades][2];   // 단계 복셀 (앞 · 뒤)
	// 복셀 속 면 평면 6 칸 (노멀의 가장 큰 축 x 부호: +X -X +Y -Y +Z -Z) — 방 모서리 · 얇은 벽의 두 면 · 바닥과 벽이 한 복셀에 함께 있어도 다투지 않는다
	static constexpr int kPlaneSlots = 6;
	Tex3D s_VoxPl[kCascades][2][kPlaneSlots];
	Tex3D s_VoxE[kCascades][2];   // 발광 복셀 (선형 HDR) — 발광 재질이 주변을 비춘다
	ComPtr<GfxTexture2D> s_CapETex;   // 판의 발광 찍기
	ComPtr<GfxRenderTargetView> s_CapERTV;
	ComPtr<GfxShaderResourceView> s_CapESRV;
	std::vector<std::pair<Vec3, Vec3>> s_Emissive;   // 이번 프레임의 발광 렌더러 월드 상자
	int s_CaptureMode = 1;   // 1 = 알베도, 2 = 발광 (gGIParams.z)
	int s_EmissionCaptures = 0;   // 통계: 발광 찍기 수
	float s_EmissiveSignature = 0.0f, s_PrevEmissiveSignature = 0.0f;   // 발광 렌더러 (상자 · 세기) 가 바뀌면 다시 짓는다
	Tex3D s_Rad, s_Nrm;                                 // 빛 · 노멀 아틀라스 (단계를 Z 로)
	Tex3D s_Planes;   // 면 평면 아틀라스 (그리는 셰이더의 벽 검사): 칸 s · 단계 c 의 복셀 z = (s × 단계 수 + c) × kVZ + z — 텍스처 하나 (셰이더가 작다)
	Tex3D s_SH[4], s_Old[4];                            // 프로브 SH (R · G · B · 유효도) + 옮길 때 복사본
	ComPtr<GfxTexture2D> s_CapTex;
	ComPtr<GfxRenderTargetView> s_CapRTV;
	ComPtr<GfxShaderResourceView> s_CapSRV;
	ComPtr<GfxBlendState> s_BlendFactorBS;
	ComPtr<GfxBlendState> s_KeepBS;   // 넣기: 알파 0 이면 예전 값 그대로
	InstancedBasicEffect* s_Fx = nullptr;
	bool s_FxTried = false;
	std::vector<Step> s_Steps;

	Cascade s_C[kCascades];
	int s_BuildCascade = 0;
	uint64_t s_Frame = 0;
	std::function<GfxShaderResourceView*(const ProbeVolumes::CaptureView&)> s_Capture;
	bool s_Capturing = false;
	XMFLOAT3 s_FocusEditor = {}, s_FocusGame = {};
	bool s_HasEditor = false, s_HasGame = false;

	// 이번 프레임의 볼륨 설정
	AdaptiveProbeVolume* s_Active = nullptr;
	int s_Count = 0;
	float s_BaseSpacing = 1.0f;
	int s_Rays = 32;
	float s_UpdateSpeed = 0.1f, s_Intensity = 1.0f, s_NormalBias = 0.33f, s_ViewBias = 0.0f, s_Validity = 0.25f;
	bool s_Local = false;
	Vec3 s_LocalMin, s_LocalMax;
	int s_BuiltCascades = 0;   // 통계: 지금까지 다 지은 단계 수
	bool s_DebugNoDirect = false;
	int s_DebugView = 0;   // 진단: 1 = 프로브 빛만, 2 = 섞은 방법 색 (nova probevolume debug --view N)   // 진단: 다시 비출 때 직접광 끄기 (nova probevolume debug --nodirect true)

	FxVar* Var(FxEffect* fx, const char* name)
	{
		FxVar* v = fx ? fx->GetVariableByName(name) : nullptr;
		return v && v->IsValid() ? v : nullptr;
	}
	void SetV(FxEffect* fx, const char* name, float x, float y, float z, float w) { const float f[4] = { x, y, z, w }; if (FxVar* v = Var(fx, name)) v->SetFloatVector(f); }
	void SetM(FxEffect* fx, const char* name, CXMMATRIX m) { if (FxVar* v = Var(fx, name)) v->SetMatrix(reinterpret_cast<const float*>(&m)); }
	void SetR(FxEffect* fx, const char* name, GfxShaderResourceView* srv) { if (FxVar* v = Var(fx, name)) v->SetResource(srv); }

	int VoxDim(int axis) { return axis == 0 ? kVX : (axis == 1 ? kVY : kVZ); }

	bool EnsureResources()
	{
		if (s_Ready) return true;
		if (s_Failed) return false;
		bool ok = true;
		for (int c = 0; c < kCascades; ++c)
			for (int b = 0; b < 2; ++b)
			{
				ok &= Make3D(s_VoxA[c][b], kVX, kVY, kVZ, DXGI_FORMAT_R8G8B8A8_UNORM, false);
				ok &= Make3D(s_VoxN[c][b], kVX, kVY, kVZ, DXGI_FORMAT_R8G8B8A8_UNORM, false);
				for (int s = 0; s < kPlaneSlots; ++s)
					ok &= Make3D(s_VoxPl[c][b][s], kVX, kVY, kVZ, DXGI_FORMAT_R8G8B8A8_UNORM, false);
				ok &= Make3D(s_VoxE[c][b], kVX, kVY, kVZ, DXGI_FORMAT_R16G16B16A16_FLOAT, false);
			}
		ok &= Make3D(s_Rad, kVX, kVY, kVZ * kCascades, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
		ok &= Make3D(s_Nrm, kVX, kVY, kVZ * kCascades, DXGI_FORMAT_R8G8B8A8_UNORM, false);
		ok &= Make3D(s_Planes, kVX, kVY, kVZ * kCascades * kPlaneSlots, DXGI_FORMAT_R8G8B8A8_UNORM, false, false);
		for (int i = 0; i < 4; ++i)
		{
			ok &= Make3D(s_SH[i], kPX, kPY, kPZ * kCascades, DXGI_FORMAT_R16G16B16A16_FLOAT, false);
			ok &= Make3D(s_Old[i], kPX, kPY, kPZ * kCascades, DXGI_FORMAT_R16G16B16A16_FLOAT, false, false);
		}
		GfxDevice* dev = Gfx::Device();
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = td.Height = 64;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		ok &= SUCCEEDED(dev->CreateTexture2D(&td, nullptr, s_CapTex.GetAddressOf()));
		ok &= SUCCEEDED(dev->CreateTexture2D(&td, nullptr, s_CapETex.GetAddressOf()));
		if (s_CapETex)
		{
			dev->CreateRenderTargetView(s_CapETex.Get(), nullptr, s_CapERTV.GetAddressOf());
			dev->CreateShaderResourceView(s_CapETex.Get(), nullptr, s_CapESRV.GetAddressOf());
		}
		if (s_CapTex)
		{
			dev->CreateRenderTargetView(s_CapTex.Get(), nullptr, s_CapRTV.GetAddressOf());
			dev->CreateShaderResourceView(s_CapTex.Get(), nullptr, s_CapSRV.GetAddressOf());
		}
		// 프로브 섞기: 새 값 x 계수 + 예전 값 x (1 - 계수) — 계수 = Update Speed (블렌드 계수)
		D3D11_BLEND_DESC bd = {};
		bd.RenderTarget[0].BlendEnable = TRUE;
		bd.RenderTarget[0].SrcBlend = D3D11_BLEND_BLEND_FACTOR;
		bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_BLEND_FACTOR;
		bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_BLEND_FACTOR;
		bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_BLEND_FACTOR;
		bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		ok &= SUCCEEDED(dev->CreateBlendState(&bd, s_BlendFactorBS.GetAddressOf()));
		D3D11_BLEND_DESC kd = {};
		kd.RenderTarget[0].BlendEnable = TRUE;
		kd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		kd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		kd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		kd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
		kd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		kd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		kd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		ok &= SUCCEEDED(dev->CreateBlendState(&kd, s_KeepBS.GetAddressOf()));
		if (!ok)
		{
			s_Failed = true;
			EditorLog::Write("ProbeVolume", "resources failed — Adaptive Probe Volume off");
			return false;
		}
		// 처음은 모두 0 (유효도 0 = 하늘)
		GfxContext* ctx = Gfx::Context();
		const float zero[4] = { 0, 0, 0, 0 };
		for (int i = 0; i < 4; ++i) ctx->ClearRenderTargetView(s_SH[i].All.Get(), zero);
		ctx->ClearRenderTargetView(s_Rad.All.Get(), zero);
		ctx->ClearRenderTargetView(s_Nrm.All.Get(), zero);
		// 면 평면 아틀라스 (렌더 타깃 아님): 지운 복셀 볼륨을 칸 · 단계마다 복사해 0 으로
		ctx->ClearRenderTargetView(s_VoxPl[0][0][0].All.Get(), zero);
		for (int s = 0; s < kPlaneSlots; ++s)
			for (int c = 0; c < kCascades; ++c)
				ctx->CopySubresourceRegion(s_Planes.Tex.Get(), 0, 0, 0, (UINT)((s * kCascades + c) * kVZ), s_VoxPl[0][0][0].Tex.Get(), 0, nullptr);
		// 판 찍기 순서: 축마다 두께 kSlab 의 판, 양쪽 방향
		s_Steps.clear();
		for (int axis = 0; axis < 3; ++axis)
			for (int k0 = 0; k0 < VoxDim(axis); k0 += kSlab)
				for (int dir : { 1, -1 })
					s_Steps.push_back({ axis, dir, k0, (std::min)(k0 + kSlab, VoxDim(axis)) });
		s_Ready = true;
		EditorLog::Write("ProbeVolume", "resources ready (%d cascades, %d slab captures per cascade)", kCascades, (int)s_Steps.size());
		return true;
	}

	bool EnsureEffect()
	{
		if (!s_FxTried)
		{
			s_FxTried = true;
			std::string error;
			// CustomShaders 로 읽어 프레임마다 빛 · 그림자 · 눈 위치를 받는다 (다시 비추기의 ShadeLit)
			s_Fx = CustomShaders::LoadEffect("engine:probevolume", L"../Shaders/55. ProbeVolume.fx", error);
			if (!s_Fx)
				EditorLog::Write("ProbeVolume", "55. ProbeVolume.fx failed to load: %s", error.c_str());
		}
		return s_Fx != nullptr;
	}

	void SetVoxAll(FxEffect* fx);

	FxTechnique* Tech(const char* name)
	{
		FxTechnique* t = s_Fx->GetFX()->GetTechniqueByName(name);
		return t && t->IsValid() ? t : nullptr;
	}

	void UnbindSRVs(GfxContext* ctx)
	{
		GfxShaderResourceView* nullSRV[32] = {};
		ctx->PSSetShaderResources(0, 32, nullSRV);
	}

	Vec3 Extent(float spacing) { return Vec3(kPX * spacing, kPY * spacing, kPZ * spacing); }

	// 카메라를 따라가는 프로브 원점 (프로브 4 칸마다 옮김 — 간격의 배수)
	Vec3 DesiredOrigin(int c, const XMFLOAT3& focus)
	{
		const float s = s_C[c].Spacing;
		const float snap = 4.0f * s;
		const Vec3 center(floorf(focus.x / snap + 0.5f) * snap, floorf(focus.y / snap + 0.5f) * snap, floorf(focus.z / snap + 0.5f) * snap);
		return center - Extent(s) * 0.5f;
	}

	// 판 하나: 직교로 찍고 그 판의 복셀 (뒤 버퍼) 에 넣기
	void CaptureStep(int c, const Step& st)
	{
		Cascade& cs = s_C[c];
		const float vs = cs.Spacing * 0.5f;
		const Vec3 O = cs.BuildOrigin;
		const Vec3 size((float)kVX * vs, (float)kVY * vs, (float)kVZ * vs);
		const Vec3 center = O + size * 0.5f;
		float pos[3] = { center.x, center.y, center.z };
		const float o[3] = { O.x, O.y, O.z };
		pos[st.Axis] = o[st.Axis] + (st.Dir > 0 ? st.K0 : st.K1) * vs;
		float look[3] = { 0, 0, 0 };
		look[st.Axis] = (float)st.Dir;
		const XMVECTOR up = st.Axis == 1 ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, 1, 0, 0);
		ProbeVolumes::CaptureView v;
		v.View = XMMatrixLookToLH(XMVectorSet(pos[0], pos[1], pos[2], 1), XMVectorSet(look[0], look[1], look[2], 0), up);
		// 오른쪽 · 위 축의 크기 (복셀 수)
		int resX, resY;
		float w, h;
		if (st.Axis == 0) { resX = kVZ; resY = kVY; w = size.z; h = size.y; }
		else if (st.Axis == 1) { resX = kVX; resY = kVZ; w = size.x; h = size.z; }
		else { resX = kVX; resY = kVY; w = size.x; h = size.y; }
		// 깊이 범위는 판보다 반 복셀씩 넓게 — 판 경계에 딱 걸친 면 (y = 0 바닥 등) 이 양쪽 판에서 모두 잘리지 않게
		v.Proj = XMMatrixOrthographicOffCenterLH(-w * 0.5f, w * 0.5f, -h * 0.5f, h * 0.5f, -vs * 0.5f, (st.K1 - st.K0) * vs + vs * 0.5f);
		v.Viewport = { 0, 0, (float)resX, (float)resY, 0.0f, 1.0f };
		v.Target = s_CapRTV.Get();
		v.Position = XMFLOAT3(pos[0], pos[1], pos[2]);
		s_Capturing = true;
		GfxShaderResourceView* nd = s_Capture(v);
		s_Capturing = false;
		if (!nd)
			return;

		// 넣기: 판에 걸친 복셀만 (Z 축이면 그 조각들, X · Y 축이면 모든 조각의 그 열 · 행)
		GfxContext* ctx = Gfx::Context();
		FxEffect* fx = s_Fx->GetFX();
		FxTechnique* tech = Tech("InjectTech");
		if (!tech) return;
		UnbindSRVs(ctx);
		ctx->OMSetDepthStencilState(nullptr, 0);
		ctx->OMSetBlendState(s_KeepBS.Get(), nullptr, 0xffffffff);
		ctx->RSSetState(nullptr);
		ctx->IASetInputLayout(nullptr);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		SetM(fx, "gGICapView", v.View);
		SetM(fx, "gGICapViewProj", v.View * v.Proj);
		SetM(fx, "gGICapInvView", XMMatrixInverse(nullptr, v.View));
		SetV(fx, "gGIVox", O.x, O.y, O.z, vs);
		SetR(fx, "gGICapColor", s_CapSRV.Get());
		SetR(fx, "gGICapNormalDepth", nd);
		const int back = 1 - cs.Front;
		D3D11_VIEWPORT vp = { 0, 0, (float)kVX, (float)kVY, 0.0f, 1.0f };
		int z0 = 0, z1 = kVZ;
		if (st.Axis == 0) { vp.TopLeftX = (float)st.K0; vp.Width = (float)(st.K1 - st.K0); }
		else if (st.Axis == 1) { vp.TopLeftY = (float)st.K0; vp.Height = (float)(st.K1 - st.K0); }
		else { z0 = st.K0; z1 = st.K1; }
		ctx->RSSetViewports(1, &vp);
		for (int z = z0; z < z1; ++z)   // 조각 하나 = 알베도 · 노멀 · 면 평면 6 칸 = 8 타깃
		{
			GfxRenderTargetView* rtv[8] = {};
			rtv[0] = s_VoxA[c][back].Slice[z].Get();
			rtv[1] = s_VoxN[c][back].Slice[z].Get();
			for (int s = 0; s < kPlaneSlots; ++s)
				rtv[2 + s] = s_VoxPl[c][back][s].Slice[z].Get();
			ctx->OMSetRenderTargets(8, rtv, nullptr);
			SetV(fx, "gGICap", (float)resX, (float)resY, (float)z, 0.0f);
			tech->GetPassByIndex(0)->Apply(0, ctx);
			ctx->Draw(3, 0);
		}
		GfxRenderTargetView* noneRtv[8] = {};
		ctx->OMSetRenderTargets(8, noneRtv, nullptr);
		ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		SetR(fx, "gGICapColor", nullptr);
		SetR(fx, "gGICapNormalDepth", nullptr);
		UnbindSRVs(ctx);

		// 발광: 판의 월드 상자와 겹치는 발광 렌더러가 있을 때만 (없으면 비용 0)
		Vec3 slabMin = O, slabMax = O + size;
		const float lo = o[st.Axis] + st.K0 * vs - vs, hi = o[st.Axis] + st.K1 * vs + vs;
		(&slabMin.x)[st.Axis] = lo;
		(&slabMax.x)[st.Axis] = hi;
		bool emissive = false;
		for (const auto& b : s_Emissive)
			if (b.first.x <= slabMax.x && b.second.x >= slabMin.x && b.first.y <= slabMax.y && b.second.y >= slabMin.y && b.first.z <= slabMax.z && b.second.z >= slabMin.z)
			{
				emissive = true;
				break;
			}
		FxTechnique* emitTech = Tech("InjectEmissionTech");
		if (!emissive || !emitTech || !s_CapERTV)
			return;
		v.Target = s_CapERTV.Get();
		s_Capturing = true;
		s_CaptureMode = 2;
		GfxShaderResourceView* nd2 = s_Capture(v);
		s_CaptureMode = 1;
		s_Capturing = false;
		if (!nd2)
			return;
		++s_EmissionCaptures;
		UnbindSRVs(ctx);
		ctx->OMSetDepthStencilState(nullptr, 0);
		ctx->OMSetBlendState(s_KeepBS.Get(), nullptr, 0xffffffff);
		ctx->RSSetState(nullptr);
		ctx->IASetInputLayout(nullptr);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		SetR(fx, "gGICapEmission", s_CapESRV.Get());
		SetR(fx, "gGICapNormalDepth", nd2);
		ctx->RSSetViewports(1, &vp);
		for (int z = z0; z < z1; z += 8)
		{
			GfxRenderTargetView* rtv[8] = {};
			for (int k = 0; k < 8 && z + k < kVZ; ++k)
				rtv[k] = s_VoxE[c][back].Slice[z + k].Get();
			ctx->OMSetRenderTargets(8, rtv, nullptr);
			SetV(fx, "gGICap", (float)resX, (float)resY, (float)z, 0.0f);
			emitTech->GetPassByIndex(0)->Apply(0, ctx);
			ctx->Draw(3, 0);
		}
		ctx->OMSetRenderTargets(8, noneRtv, nullptr);
		ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		SetR(fx, "gGICapEmission", nullptr);
		SetR(fx, "gGICapNormalDepth", nullptr);
		UnbindSRVs(ctx);
	}

	// 단계 c 의 복셀을 다시 비춰 빛 · 노멀 아틀라스에
	void Relight(int c)
	{
		Cascade& cs = s_C[c];
		GfxContext* ctx = Gfx::Context();
		FxEffect* fx = s_Fx->GetFX();
		FxTechnique* tech = Tech("RelightTech");
		if (!tech || !cs.VoxValid) return;
		PROFILE_SCOPE("Probe Volume Relight");
		UnbindSRVs(ctx);
		ctx->OMSetDepthStencilState(nullptr, 0);
		ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		ctx->RSSetState(nullptr);
		ctx->IASetInputLayout(nullptr);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ProbeVolumes::Bind(s_Fx);
		SetV(fx, "gGIBias", s_NormalBias, s_ViewBias, 0.0f, (float)kCascades);   // 진단 보기는 화면에만
		SetR(fx, "gSsaoMap", SpriteBatch::WhiteTexture());   // 복셀에는 SSAO 없음
		// 셰이더 설정 (보통은 재질이 그릴 때 넣는다): 그림자 켬 — 안 넣으면 그림자 없이 모든 복셀이 햇빛을 받는다 (닫힌 방이 밝아짐)
		//  순서: UseTexture, AlphaClip, UseNormalMap, UseShadowMap, UseSsaoMap, ReflectionEnabled, FogEnabled, pad
		const int setting[8] = { 0, 0, 0, 1, 0, 0, 0, 0 };
		if (FxVar* v = Var(fx, "gShaderSetting")) v->SetRawValue(setting, 0, sizeof(setting));
		if (s_DebugNoDirect)
			for (const char* n : { "gDirLightCount", "gPointLightCount", "gSpotLightCount" })
				if (FxVar* v = Var(fx, n)) v->SetInt(0);
		const float vs = cs.Spacing * 0.5f;
		SetV(fx, "gGIVox", cs.VoxOrigin.x, cs.VoxOrigin.y, cs.VoxOrigin.z, vs);
		SetR(fx, "gGIVoxAlbedo", s_VoxA[c][cs.Front].SRV.Get());
		SetR(fx, "gGIVoxNormal", s_VoxN[c][cs.Front].SRV.Get());
		SetR(fx, "gGIVoxEmission", s_VoxE[c][cs.Front].SRV.Get());
		SetR(fx, "gGIRadiance", nullptr);
		SetR(fx, "gGIPlanes", nullptr);
		const D3D11_VIEWPORT vp = { 0, 0, (float)kVX, (float)kVY, 0.0f, 1.0f };
		ctx->RSSetViewports(1, &vp);
		for (int z = 0; z < kVZ; z += 8)
		{
			GfxRenderTargetView* rtv[8] = {};
			for (int k = 0; k < 8; ++k)
				rtv[k] = s_Rad.Slice[c * kVZ + z + k].Get();
			ctx->OMSetRenderTargets(8, rtv, nullptr);
			SetV(fx, "gGIPass", (float)c, 0.0f, (float)z, (float)s_Frame);
			tech->GetPassByIndex(0)->Apply(0, ctx);
			ctx->Draw(3, 0);
		}
		SetR(fx, "gGIVoxAlbedo", nullptr);
		SetR(fx, "gGIVoxNormal", nullptr);
		SetR(fx, "gGIVoxEmission", nullptr);
		UnbindSRVs(ctx);
		GfxRenderTargetView* none[8] = {};
		ctx->OMSetRenderTargets(8, none, nullptr);
		// 노멀 아틀라스 = 이 단계 복셀 노멀 그대로
		ctx->CopySubresourceRegion(s_Nrm.Tex.Get(), 0, 0, 0, (UINT)(c * kVZ), s_VoxN[c][cs.Front].Tex.Get(), 0, nullptr);
		for (int s = 0; s < kPlaneSlots; ++s)
			ctx->CopySubresourceRegion(s_Planes.Tex.Get(), 0, 0, 0, (UINT)((s * kCascades + c) * kVZ), s_VoxPl[c][cs.Front][s].Tex.Get(), 0, nullptr);
		ctx->GenerateMips(s_Rad.SRV.Get());
	}

	void SetVoxAll(FxEffect* fx)
	{
		XMFLOAT4 all[4] = {};
		for (int c = 0; c < kCascades && c < 4; ++c)
			if (c < s_Count && s_C[c].VoxValid)
				all[c] = XMFLOAT4(s_C[c].VoxOrigin.x, s_C[c].VoxOrigin.y, s_C[c].VoxOrigin.z, s_C[c].Spacing * 0.5f);
		if (FxVar* v = Var(fx, "gGIVoxAll")) v->SetFloatVectorArray(&all[0].x, 0, 4);
	}

	// 단계 c 의 프로브: 광선 → SH, 블렌드 계수로 섞기
	void UpdateProbes(int c)
	{
		GfxContext* ctx = Gfx::Context();
		FxEffect* fx = s_Fx->GetFX();
		FxTechnique* tech = Tech("UpdateTech");
		if (!tech) return;
		PROFILE_SCOPE("Probe Volume Update");
		UnbindSRVs(ctx);
		ctx->OMSetDepthStencilState(nullptr, 0);
		const float f = s_UpdateSpeed;
		const float factor[4] = { f, f, f, f };
		ctx->OMSetBlendState(s_BlendFactorBS.Get(), factor, 0xffffffff);
		ctx->RSSetState(nullptr);
		ctx->IASetInputLayout(nullptr);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ProbeVolumes::Bind(s_Fx);
		SetVoxAll(fx);
		SetR(fx, "gGIRadiance", s_Rad.SRV.Get());
		SetR(fx, "gGINormals", s_Nrm.SRV.Get());
		SetV(fx, "gGISky", 0.0f, s_Validity, 0.0f, 0.0f);
		const D3D11_VIEWPORT vp = { 0, 0, (float)kPX, (float)kPY, 0.0f, 1.0f };
		ctx->RSSetViewports(1, &vp);
		for (int z = 0; z < kPZ; ++z)
		{
			GfxRenderTargetView* rtv[4] = { s_SH[0].Slice[c * kPZ + z].Get(), s_SH[1].Slice[c * kPZ + z].Get(), s_SH[2].Slice[c * kPZ + z].Get(), s_SH[3].Slice[c * kPZ + z].Get() };
			ctx->OMSetRenderTargets(4, rtv, nullptr);
			SetV(fx, "gGIPass", (float)c, (float)s_Rays, (float)z, (float)(s_Frame % 4096));
			tech->GetPassByIndex(0)->Apply(0, ctx);
			ctx->Draw(3, 0);
		}
		SetR(fx, "gGIRadiance", nullptr);
		SetR(fx, "gGINormals", nullptr);
		UnbindSRVs(ctx);
		GfxRenderTargetView* none[4] = {};
		ctx->OMSetRenderTargets(4, none, nullptr);
		ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	}

	// 갱신한 단계의 물체 속 프로브를 유효한 이웃 값으로 채운다 (Unity APV 의 Dilation — 여기서는 매 프레임)
	void Dilate(int c)
	{
		GfxContext* ctx = Gfx::Context();
		FxEffect* fx = s_Fx->GetFX();
		FxTechnique* tech = Tech("DilateTech");
		if (!tech) return;
		UnbindSRVs(ctx);
		for (int i = 0; i < 4; ++i)
			ctx->CopyResource(s_Old[i].Tex.Get(), s_SH[i].Tex.Get());
		SetR(fx, "gGIOldSH0", s_Old[0].SRV.Get());
		SetR(fx, "gGIOldSH1", s_Old[1].SRV.Get());
		SetR(fx, "gGIOldSH2", s_Old[2].SRV.Get());
		SetR(fx, "gGIOldValid", s_Old[3].SRV.Get());
		ctx->OMSetDepthStencilState(nullptr, 0);
		ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		ctx->RSSetState(nullptr);
		ctx->IASetInputLayout(nullptr);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		const D3D11_VIEWPORT vp = { 0, 0, (float)kPX, (float)kPY, 0.0f, 1.0f };
		ctx->RSSetViewports(1, &vp);
		for (int z = 0; z < kPZ; ++z)
		{
			GfxRenderTargetView* rtv[3] = { s_SH[0].Slice[c * kPZ + z].Get(), s_SH[1].Slice[c * kPZ + z].Get(), s_SH[2].Slice[c * kPZ + z].Get() };
			ctx->OMSetRenderTargets(3, rtv, nullptr);
			SetV(fx, "gGIPass", (float)c, 0.0f, (float)z, 0.0f);
			tech->GetPassByIndex(0)->Apply(0, ctx);
			ctx->Draw(3, 0);
		}
		SetR(fx, "gGIOldSH0", nullptr);
		SetR(fx, "gGIOldSH1", nullptr);
		SetR(fx, "gGIOldSH2", nullptr);
		SetR(fx, "gGIOldValid", nullptr);
		UnbindSRVs(ctx);
		GfxRenderTargetView* none[3] = {};
		ctx->OMSetRenderTargets(3, none, nullptr);
	}

	// 단계가 옮겨지면: 예전 값 (복사본) 을 새 자리로
	void Resample(const bool moved[kCascades], const Vec3 oldOrigin[kCascades], const bool oldValid[kCascades])
	{
		GfxContext* ctx = Gfx::Context();
		FxEffect* fx = s_Fx->GetFX();
		FxTechnique* tech = Tech("ResampleTech");
		if (!tech) return;
		UnbindSRVs(ctx);
		for (int i = 0; i < 4; ++i)
			ctx->CopyResource(s_Old[i].Tex.Get(), s_SH[i].Tex.Get());
		XMFLOAT4 old[4] = {};
		for (int c = 0; c < kCascades; ++c)
			if (oldValid[c])
				old[c] = XMFLOAT4(oldOrigin[c].x, oldOrigin[c].y, oldOrigin[c].z, s_C[c].Spacing);
		if (FxVar* v = Var(fx, "gGIOld")) v->SetFloatVectorArray(&old[0].x, 0, 4);
		ProbeVolumes::Bind(s_Fx);
		SetR(fx, "gGIOldSH0", s_Old[0].SRV.Get());
		SetR(fx, "gGIOldSH1", s_Old[1].SRV.Get());
		SetR(fx, "gGIOldSH2", s_Old[2].SRV.Get());
		SetR(fx, "gGIOldValid", s_Old[3].SRV.Get());
		ctx->OMSetDepthStencilState(nullptr, 0);
		ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		ctx->RSSetState(nullptr);
		ctx->IASetInputLayout(nullptr);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		const D3D11_VIEWPORT vp = { 0, 0, (float)kPX, (float)kPY, 0.0f, 1.0f };
		ctx->RSSetViewports(1, &vp);
		for (int c = 0; c < kCascades; ++c)
		{
			if (!moved[c]) continue;
			for (int z = 0; z < kPZ; ++z)
			{
				GfxRenderTargetView* rtv[4] = { s_SH[0].Slice[c * kPZ + z].Get(), s_SH[1].Slice[c * kPZ + z].Get(), s_SH[2].Slice[c * kPZ + z].Get(), s_SH[3].Slice[c * kPZ + z].Get() };
				ctx->OMSetRenderTargets(4, rtv, nullptr);
				SetV(fx, "gGIPass", (float)c, 0.0f, (float)z, 0.0f);
				tech->GetPassByIndex(0)->Apply(0, ctx);
				ctx->Draw(3, 0);
			}
		}
		SetR(fx, "gGIOldSH0", nullptr);
		SetR(fx, "gGIOldSH1", nullptr);
		SetR(fx, "gGIOldSH2", nullptr);
		SetR(fx, "gGIOldValid", nullptr);
		UnbindSRVs(ctx);
		GfxRenderTargetView* none[4] = {};
		ctx->OMSetRenderTargets(4, none, nullptr);
	}

	AdaptiveProbeVolume* FindActive()
	{
		for (AdaptiveProbeVolume* v : AdaptiveProbeVolume::All())
			if (v && v->IsEnabled() && v->IsActiveInHierarchy())
				return v;
		return nullptr;
	}
}

namespace ProbeVolumes
{
	void SetCapture(std::function<GfxShaderResourceView*(const CaptureView&)> fn) { s_Capture = std::move(fn); }
	bool Capturing() { return s_Capturing; }

	void SetFocus(const XMFLOAT3& eye, bool editorView)
	{
		if (editorView) { s_FocusEditor = eye; s_HasEditor = true; }
		else { s_FocusGame = eye; s_HasGame = true; }
	}

	void Update()
	{
		s_Active = FindActive();
		if (!s_Active || !s_Capture || s_Capturing)
		{
			s_Count = 0;
			return;
		}
		if (!EnsureResources() || !EnsureEffect())
		{
			s_Count = 0;
			return;
		}
		PROFILE_SCOPE("Adaptive Probe Volume");
		++s_Frame;
		// 발광 렌더러 (재질 Emission 이 0 이 아닌 Mesh Renderer) 의 월드 상자 — 판 찍기가 겹칠 때만 발광을 한 번 더 찍는다
		s_Emissive.clear();
		s_PrevEmissiveSignature = s_EmissiveSignature;
		s_EmissiveSignature = 0.0f;
		if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
			for (GameObject* go : scene->GetAllGameObjects())
			{
				if (!go || !go->IsActive())
					continue;
				MeshRenderer* mr = go->GetComponent<MeshRenderer>();
				if (!mr || !mr->IsEnabled())
					continue;
				float emits = 0.0f;
				for (const auto& m : mr->GetMaterials())
					if (m && !m->IsCustom())
					{
						const XMFLOAT3 e = m->EmissionLinear();
						emits += e.x + e.y * 1.7f + e.z * 2.3f;
					}
				Vec3 mn, mx;
				if (emits > 1e-3f && SceneCulling::TrackedBounds(mr, mn, mx))
				{
					s_Emissive.emplace_back(mn, mx);
					s_EmissiveSignature += emits * (1.0f + (float)s_Emissive.size() * 0.37f) + (mn.x + mn.y * 3.1f + mn.z * 7.3f + mx.x * 1.3f + mx.y * 2.9f + mx.z * 5.1f) * 0.01f;
				}
			}
		// 설정
		const float spacing = s_Active->GetProbeSpacing();
		s_Count = s_Active->GetCascades();
		s_Rays = s_Active->GetRaysPerProbe();
		s_UpdateSpeed = s_Active->GetUpdateSpeed();
		s_Intensity = s_Active->GetIntensity();
		s_NormalBias = s_Active->GetNormalBias();
		s_ViewBias = s_Active->GetViewBias();
		s_Validity = s_Active->GetValidityThreshold();
		s_Local = s_Active->GetMode() == AdaptiveProbeVolume::Mode::Local;
		if (s_Local)
			s_Active->Bounds(s_LocalMin, s_LocalMax);
		// 간격이 바뀌면 처음부터
		if (spacing != s_BaseSpacing || s_C[0].Spacing == 0.0f)
		{
			s_BaseSpacing = spacing;
			GfxContext* ctx = Gfx::Context();
			const float zero[4] = { 0, 0, 0, 0 };
			for (int i = 0; i < 4; ++i) ctx->ClearRenderTargetView(s_SH[i].All.Get(), zero);
			for (int c = 0; c < kCascades; ++c)
			{
				s_C[c] = Cascade();
				s_C[c].Spacing = spacing * (float)(1 << (2 * c));
			}
			s_BuildCascade = 0;
		}
		// 초점: 편집 중엔 Scene 뷰 카메라, Play · 게임은 Game 뷰 카메라. Local 은 상자 가운데
		XMFLOAT3 focus = (Application::IsPlaying() || Application::IsPlayer() || !s_HasEditor) ? s_FocusGame : s_FocusEditor;
		if (s_Local)
		{
			const Vec3 m = (s_LocalMin + s_LocalMax) * 0.5f;
			focus = XMFLOAT3(m.x, m.y, m.z);
		}

		// 1) 프로브 원점 따라가기 (옮겨진 단계는 예전 값을 새 자리로)
		bool moved[kCascades] = {}, oldValid[kCascades] = {}, any = false;
		Vec3 oldOrigin[kCascades];
		for (int c = 0; c < kCascades; ++c)
		{
			const Vec3 o = DesiredOrigin(c, focus);
			oldOrigin[c] = s_C[c].ProbeOrigin;
			oldValid[c] = s_C[c].ProbeValid;
			if (!s_C[c].ProbeValid || (o - s_C[c].ProbeOrigin).LengthSquared() > 1e-6f)
			{
				moved[c] = true;
				any = true;
				s_C[c].ProbeOrigin = o;
				s_C[c].ProbeValid = true;
			}
		}
		if (any)
			Resample(moved, oldOrigin, oldValid);

		// 2) 복셀 짓기: 바뀐 단계 (장면이 바뀜 · 카메라를 따라 옮겨짐 · 아직 없음) 는 빠르게, 평소엔 한 판씩 천천히
		{
			PROFILE_SCOPE("Probe Volume Voxelize");
			GfxContext* ctx = Gfx::Context();
			for (int c = 0; c < s_Count; ++c)
			{
				Cascade& cs = s_C[c];
				if (!cs.VoxValid || (cs.ProbeOrigin - cs.VoxOrigin).LengthSquared() > 1e-6f)
					cs.Dirty = true;
				if (cs.Dirty || !cs.VoxValid)
					continue;
				const float vs = cs.Spacing * 0.5f;
				const Vec3 mn = cs.VoxOrigin, mx = cs.VoxOrigin + Vec3((float)kVX, (float)kVY, (float)kVZ) * vs;
				for (const auto& b : SceneCulling::ChangedBounds())
					if (b.first.x <= mx.x && b.second.x >= mn.x && b.first.y <= mx.y && b.second.y >= mn.y && b.first.z <= mx.z && b.second.z >= mn.z)
					{
						cs.Dirty = true;
						break;
					}
				// 발광 재질을 켜거나 바꿈 (물체 상자는 그대로) → 다시 짓는다
				if (fabsf(s_EmissiveSignature - s_PrevEmissiveSignature) > 1e-4f)
					cs.Dirty = true;
			}
			if (s_BuildCascade >= s_Count)
				s_BuildCascade = 0;
			if (s_C[s_BuildCascade].BuildStep < 0)
			{
				// 다음에 지을 단계: 바뀐 것 (가까운 단계부터), 없으면 돌아가며
				int pick = -1;
				for (int c = 0; c < s_Count && pick < 0; ++c)
					if (s_C[c].Dirty) pick = c;
				if (pick >= 0)
					s_BuildCascade = pick;
			}
			const int budget = s_C[s_BuildCascade].Dirty || s_C[s_BuildCascade].BuildStep >= 0 && s_C[s_BuildCascade].BuildFast ? kCapturesPerFrame : kBackgroundCaptures;
			for (int n = 0; n < budget; ++n)
			{
				if (s_BuildCascade >= s_Count)
					s_BuildCascade = 0;
				Cascade& cs = s_C[s_BuildCascade];
				if (cs.BuildStep < 0)
				{
					cs.BuildFast = cs.Dirty;
					cs.Dirty = false;
					cs.BuildOrigin = cs.ProbeOrigin;   // 프로브와 같은 자리 (복셀 = 프로브 칸의 절반)
					const float zero[4] = { 0, 0, 0, 0 };
					const int back = 1 - cs.Front;
					ctx->ClearRenderTargetView(s_VoxA[s_BuildCascade][back].All.Get(), zero);
					ctx->ClearRenderTargetView(s_VoxN[s_BuildCascade][back].All.Get(), zero);
					for (int s = 0; s < kPlaneSlots; ++s)
						ctx->ClearRenderTargetView(s_VoxPl[s_BuildCascade][back][s].All.Get(), zero);
					ctx->ClearRenderTargetView(s_VoxE[s_BuildCascade][back].All.Get(), zero);
					cs.BuildStep = 0;
				}
				CaptureStep(s_BuildCascade, s_Steps[cs.BuildStep]);
				if (++cs.BuildStep >= (int)s_Steps.size())
				{
					cs.Front = 1 - cs.Front;
					cs.VoxOrigin = cs.BuildOrigin;
					cs.VoxValid = true;
					cs.BuildStep = -1;
					++s_BuiltCascades;
					// 다음: 바뀐 단계가 있으면 그것, 없으면 돌아가며 천천히
					int pick = -1;
					for (int c2 = 0; c2 < s_Count && pick < 0; ++c2)
						if (s_C[c2].Dirty) pick = c2;
					s_BuildCascade = pick >= 0 ? pick : s_BuildCascade + 1;
					if (pick < 0)
						break;   // 바쁜 짓기는 끝 — 나머지는 다음 프레임부터 천천히
				}
			}
		}

		// 3) 다시 비추기 · 4) 프로브 갱신 — 프레임마다 한 단계씩 돌아가며
		const int c = (int)(s_Frame % (uint64_t)s_Count);
		Relight(c);
		UpdateProbes(c);
		Dilate(c);
	}

	void Bind(InstancedBasicEffect* effect)
	{
		FxEffect* fx = effect ? effect->GetFX() : nullptr;
		if (!fx)
			return;
		const int count = (s_Active && s_Ready) ? s_Count : 0;
		SetV(fx, "gGIParams", (float)count, s_Intensity, s_Capturing ? (float)s_CaptureMode : 0.0f, s_Local ? 1.0f : 0.0f);
		if (count <= 0)
			return;
		SetV(fx, "gGIBias", s_NormalBias, s_ViewBias, s_Capturing ? 0.0f : (float)s_DebugView, (float)kCascades);
		XMFLOAT4 cascades[4] = {};
		for (int c = 0; c < kCascades; ++c)
			cascades[c] = XMFLOAT4(s_C[c].ProbeOrigin.x, s_C[c].ProbeOrigin.y, s_C[c].ProbeOrigin.z, s_C[c].Spacing);
		if (FxVar* v = Var(fx, "gGICascade")) v->SetFloatVectorArray(&cascades[0].x, 0, 4);
		SetV(fx, "gGILocalMin", s_LocalMin.x, s_LocalMin.y, s_LocalMin.z, 0.0f);
		SetV(fx, "gGILocalMax", s_LocalMax.x, s_LocalMax.y, s_LocalMax.z, 0.0f);
		SetR(fx, "gGISH0", s_SH[0].SRV.Get());
		SetR(fx, "gGISH1", s_SH[1].SRV.Get());
		SetR(fx, "gGISH2", s_SH[2].SRV.Get());
		SetR(fx, "gGIValid", s_SH[3].SRV.Get());
		SetVoxAll(fx);
		SetR(fx, "gGIRadiance", s_Rad.SRV.Get());
		SetR(fx, "gGIPlanes", s_Planes.SRV.Get());
	}

	bool CascadeBox(int c, Vec3& min, Vec3& max)
	{
		if (c < 0 || c >= kCascades || !s_C[c].ProbeValid || c >= s_Count)
			return false;
		min = s_C[c].ProbeOrigin;
		max = min + Extent(s_C[c].Spacing);
		return true;
	}

	std::string StatusText()
	{
		if (s_Failed) return "GPU resources failed (see Editor.log)";
		if (!s_Active || !s_Ready) return std::string();
		int built = 0;
		for (int c = 0; c < s_Count; ++c) if (s_C[c].VoxValid) ++built;
		char buf[128];
		snprintf(buf, sizeof(buf), "%d / %d cascades live, %d probes", built, s_Count, s_Count * kPX * kPY * kPZ);
		return buf;
	}

	// 검사 · 진단용: 3D 텍스처 한 칸 읽기 (CPU 로 — DirectX 11 만)
	static bool ReadTexel(Tex3D& t, int x, int y, int z, float out[4])
	{
		DirectX::ScratchImage img;
		if (FAILED(Gfx::CaptureTexture(Gfx::Context(), t.Tex.Get(), img)))
			return false;
		const DirectX::Image* im = img.GetImage(0, 0, z);
		if (!im || x < 0 || y < 0 || x >= (int)im->width || y >= (int)im->height)
			return false;
		const uint8_t* row = im->pixels + (size_t)y * im->rowPitch;
		if (im->format == DXGI_FORMAT_R16G16B16A16_FLOAT)
		{
			const uint16_t* h = reinterpret_cast<const uint16_t*>(row) + x * 4;
			for (int i = 0; i < 4; ++i) out[i] = DirectX::PackedVector::XMConvertHalfToFloat(h[i]);
		}
		else
			for (int i = 0; i < 4; ++i) out[i] = row[x * 4 + i] / 255.0f;
		return true;
	}

	static int CountOccupied(Tex3D& t)
	{
		DirectX::ScratchImage img;
		if (FAILED(Gfx::CaptureTexture(Gfx::Context(), t.Tex.Get(), img)))
			return -1;
		int n = 0;
		for (size_t z = 0; z < img.GetMetadata().depth; ++z)
		{
			const DirectX::Image* im = img.GetImage(0, 0, z);
			for (size_t y = 0; y < im->height; ++y)
				for (size_t x = 0; x < im->width; ++x)
					if (im->pixels[y * im->rowPitch + x * 4 + 3] > 127) ++n;
		}
		return n;
	}

	void RegisterEditor()
	{
		// nova probevolume info | probe --position x,y,z | voxels
		CliServer::Register("probevolume", "adaptive probe volume op: {op: info | probe | voxels} (nova probevolume info)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
			const std::string op = args.value("op", std::string("info"));
			if (op == "debug")
			{
				s_DebugNoDirect = args.value("nodirect", s_DebugNoDirect);
				s_DebugView = args.value("view", s_DebugView);
				result = { { "nodirect", s_DebugNoDirect }, { "view", s_DebugView } };
				return true;
			}
			if (op == "voxels")
			{
				nlohmann::json list = nlohmann::json::array();
				for (int c = 0; c < s_Count; ++c)
					list.push_back({ { "occupied", s_C[c].VoxValid ? CountOccupied(s_VoxA[c][s_C[c].Front]) : 0 }, { "total", kVX * kVY * kVZ } });
				result = { { "cascades", list } };
				return true;
			}
			if (op == "probe")
			{
				if (!args.contains("position") || !args["position"].is_array() || args["position"].size() != 3)
				{
					error = "probe --position x,y,z";
					return false;
				}
				const Vec3 p(args["position"][0].get<float>(), args["position"][1].get<float>(), args["position"][2].get<float>());
				nlohmann::json list = nlohmann::json::array();
				for (int c = 0; c < s_Count; ++c)
				{
					const Cascade& cs = s_C[c];
					nlohmann::json e = { { "cascade", c } };
					// 가장 가까운 프로브
					const Vec3 l = (p - cs.ProbeOrigin) / cs.Spacing;
					const int ix = (int)floorf(l.x), iy = (int)floorf(l.y), iz = (int)floorf(l.z);
					if (ix >= 0 && iy >= 0 && iz >= 0 && ix < kPX && iy < kPY && iz < kPZ)
					{
						float r[4], g[4], b[4], v[4];
						if (ReadTexel(s_SH[0], ix, iy, c * kPZ + iz, r) && ReadTexel(s_SH[1], ix, iy, c * kPZ + iz, g) &&
							ReadTexel(s_SH[2], ix, iy, c * kPZ + iz, b) && ReadTexel(s_SH[3], ix, iy, c * kPZ + iz, v))
						{
							const float inv = 1.0f;   // SH 는 유효도를 곱하지 않은 값
							// 위 (+Y) 를 향한 면의 조도 / π
							auto up = [&](const float* s) { return (0.282095f * s[0] + 0.488603f * (2.0f / 3.0f) * s[2]) * inv; };
							e["probe"] = { { "index", { ix, iy, iz } }, { "validity", v[0] }, { "L0", { r[0] * inv, g[0] * inv, b[0] * inv } },
								{ "ambientUp", { up(r), up(g), up(b) } } };
						}
					}
					// 그 자리 복셀
					if (cs.VoxValid)
					{
						const Vec3 vl = (p - cs.VoxOrigin) / (cs.Spacing * 0.5f);
						const int vx = (int)floorf(vl.x), vy = (int)floorf(vl.y), vz = (int)floorf(vl.z);
						float a[4], n[4], rad[4];
						if (vx >= 0 && vy >= 0 && vz >= 0 && vx < kVX && vy < kVY && vz < kVZ &&
							ReadTexel(s_VoxA[c][cs.Front], vx, vy, vz, a) && ReadTexel(s_VoxN[c][cs.Front], vx, vy, vz, n) && ReadTexel(s_Rad, vx, vy, c * kVZ + vz, rad))
							e["voxel"] = { { "index", { vx, vy, vz } }, { "albedo", { a[0], a[1], a[2], a[3] } },
								{ "normal", { n[0] * 2 - 1, n[1] * 2 - 1, n[2] * 2 - 1 } }, { "radiance", { rad[0], rad[1], rad[2], rad[3] } } };
					}
					list.push_back(e);
				}
				result = { { "cascades", list } };
				return true;
			}
			if (op != "info" && op != "help")
			{
				error = "unknown op '" + op + "' (info | probe | voxels)";
				return false;
			}
			nlohmann::json cascades = nlohmann::json::array();
			for (int c = 0; c < kCascades && c < s_Count; ++c)
			{
				const Cascade& cs = s_C[c];
				cascades.push_back({ { "spacing", cs.Spacing }, { "origin", { cs.ProbeOrigin.x, cs.ProbeOrigin.y, cs.ProbeOrigin.z } },
					{ "voxelsLive", cs.VoxValid }, { "buildStep", cs.BuildStep }, { "steps", (int)s_Steps.size() } });
			}
			result = { { "active", s_Active != nullptr && s_Ready && s_Count > 0 }, { "failed", s_Failed },
				{ "volume", s_Active && s_Active->GetGameObject() ? s_Active->GetGameObject()->GetName() : std::string() },
				{ "mode", s_Local ? "Local" : "Global" }, { "cascades", cascades }, { "builtCascades", s_BuiltCascades },
				{ "frames", s_Frame }, { "raysPerProbe", s_Rays }, { "probesPerCascade", kPX * kPY * kPZ },
				{ "emissiveRenderers", s_Emissive.size() }, { "emissionCaptures", s_EmissionCaptures } };
			return true;
		});
	}
}
