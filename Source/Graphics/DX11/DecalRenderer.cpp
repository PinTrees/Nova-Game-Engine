#include "pch.h"
#include "DecalRenderer.h"
#include "DecalProjector.h"
#include "CustomShaders.h"
#include "UMaterial.h"
#include "Effects.h"
#include "Vertex.h"
#include "RenderStates.h"
#include "RenderLayers.h"
#include "GameObjectFactory.h"
#include "Mesh.h"
#include "Transform.h"
#include "Profiler.h"

namespace
{
	InstancedBasicEffect* s_Fx = nullptr;
	bool s_Tried = false;
	int s_Count[2] = { 0, 0 };

	FxVar* Var(FxEffect* fx, const char* name)
	{
		FxVar* v = fx->GetVariableByName(name);
		return v && v->IsValid() ? v : nullptr;
	}
	void SetM(FxEffect* fx, const char* name, CXMMATRIX m) { if (FxVar* v = Var(fx, name)) v->SetMatrix(reinterpret_cast<const float*>(&m)); }
	void SetV(FxEffect* fx, const char* name, float x, float y, float z, float w) { const float f[4] = { x, y, z, w }; if (FxVar* v = Var(fx, name)) v->SetFloatVector(f); }
	void SetV(FxEffect* fx, const char* name, FXMVECTOR x) { XMFLOAT4 f; XMStoreFloat4(&f, x); if (FxVar* v = Var(fx, name)) v->SetFloatVector(&f.x); }

	// 데칼 하나의 화면 · 상자 값 (엔진 이펙트 · Shader Graph 이펙트 모두 같은 이름 — 53. DecalCommon.fx)
	struct Params
	{
		XMMATRIX Box, WorldToBox, ViewProj, InvView, ViewProjTex;
		XMFLOAT4 Proj, ProjOffset, Viewport, AxisX, AxisY, AxisZ, UV, Fade;
		GfxShaderResourceView* NormalDepth = nullptr;
	};

	void SetDecalVars(FxEffect* fx, const Params& p)
	{
		SetM(fx, "gDecalBox", p.Box);
		SetM(fx, "gDecalWorldToBox", p.WorldToBox);
		SetM(fx, "gDecalViewProj", p.ViewProj);
		SetM(fx, "gDecalInvView", p.InvView);
		SetM(fx, "gViewProjTex", p.ViewProjTex);
		auto v4 = [&](const char* n, const XMFLOAT4& f) { if (FxVar* v = Var(fx, n)) v->SetFloatVector(&f.x); };
		v4("gDecalProj", p.Proj);
		v4("gDecalProjOffset", p.ProjOffset);
		v4("gDecalViewport", p.Viewport);
		v4("gDecalAxisX", p.AxisX);
		v4("gDecalAxisY", p.AxisY);
		v4("gDecalAxisZ", p.AxisZ);
		v4("gDecalUV", p.UV);
		v4("gDecalFade", p.Fade);
		if (FxVar* v = Var(fx, "gDecalNormalDepth")) v->SetResource(p.NormalDepth);
	}

	bool ActiveInHierarchy(GameObject* go)
	{
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
			if (!g->IsActive())
				return false;
		return true;
	}

	XMFLOAT4 Axis(FXMVECTOR v)
	{
		XMFLOAT4 f;
		XMStoreFloat4(&f, XMVector3Normalize(v));
		f.w = 0;
		return f;
	}

	// 엔진 데칼: 재질의 Lit / Unlit 값
	void SetMaterialVars(FxEffect* fx, UMaterial& m)
	{
		const PbrMaterial& p = m.GetPbr();
		SetV(fx, "gDecalColor", p.BaseColor.x, p.BaseColor.y, p.BaseColor.z, p.BaseColor.w);
		SetV(fx, "gDecalSurface", p.Metallic, p.Smoothness, p.NormalScale, 0);
		SetV(fx, "gDecalEmission", p.EmissionColor.x, p.EmissionColor.y, p.EmissionColor.z, 0);
		GfxShaderResourceView* base = m.GetBaseMapSRV();
		GfxShaderResourceView* normal = m.GetNormalMapSRV();
		SetV(fx, "gDecalMaps", base ? 1.0f : 0.0f, normal ? 1.0f : 0.0f, m.GetShader() == UMaterial::ShaderKind::Unlit ? 1.0f : 0.0f, 0);
		if (FxVar* v = Var(fx, "gDecalBaseMap")) v->SetResource(base);
		if (FxVar* v = Var(fx, "gDecalNormalMap")) v->SetResource(normal);
	}
}

namespace DecalRenderer
{
	int LastCount(bool editor) { return s_Count[editor ? 1 : 0]; }

	void Render(GfxContext* dc, GfxRenderTargetView* target, const D3D11_VIEWPORT& viewport, CXMMATRIX view, CXMMATRIX proj,
		GfxShaderResourceView* normalDepth, bool editor)
	{
		s_Count[editor ? 1 : 0] = 0;
		const std::vector<DecalProjector*>& all = DecalProjector::All();
		if (all.empty() || !normalDepth || !target)
			return;
		PROFILE_SCOPE("Decals");
		if (!s_Tried)
		{
			s_Tried = true;
			std::string error;
			s_Fx = CustomShaders::LoadEffect("engine:decal", L"../Shaders/52. Decal.fx", error);   // 프레임마다 빛 · 그림자 값을 받는다
			if (!s_Fx)
				EditorLog::Write("Decal", "52. Decal.fx failed to load: %s", error.c_str());
		}
		auto mesh = GameObjectFactory::GetPrimitiveMesh(PrimitiveType::Cube);
		if (!mesh)
			return;

		// 화면 값: 투영 행렬에서 원근 / 직교 · 중심 이동
		XMFLOAT4X4 P;
		XMStoreFloat4x4(&P, proj);
		const bool ortho = fabsf(P._34) < 1e-6f;
		Params base;
		base.ViewProj = view * proj;
		base.InvView = XMMatrixInverse(nullptr, view);
		static const XMMATRIX toTex(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f);
		base.ViewProjTex = base.ViewProj * toTex;
		base.Proj = XMFLOAT4(P._11, P._22, ortho ? 1.0f : 0.0f, 0);
		base.ProjOffset = XMFLOAT4(P._31, P._32, P._41, P._42);
		base.Viewport = XMFLOAT4(viewport.TopLeftX, viewport.TopLeftY, 1.0f / (std::max)(viewport.Width, 1.0f), 1.0f / (std::max)(viewport.Height, 1.0f));
		base.NormalDepth = normalDepth;
		XMFLOAT3 eye;
		XMStoreFloat3(&eye, base.InvView.r[3]);

		// 상태: 깊이 버퍼 없이 (재구성으로 판단), 상자 뒷면, RGB 만 섞기
		ComPtr<GfxRenderTargetView> oldRtv;
		ComPtr<GfxDepthStencilView> oldDsv;
		dc->OMGetRenderTargets(1, oldRtv.GetAddressOf(), oldDsv.GetAddressOf());
		dc->OMSetRenderTargets(1, &target, nullptr);
		dc->RSSetViewports(1, &viewport);
		const float blendFactor[4] = { 0, 0, 0, 0 };
		dc->OMSetBlendState(RenderStates::DecalBS.Get(), blendFactor, 0xFFFFFFFF);
		dc->OMSetDepthStencilState(nullptr, 0);
		dc->RSSetState(RenderStates::CullClockwiseRS.Get());
		dc->IASetInputLayout(InputLayouts::PosNormalTexTan2.Get());
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		auto drawBox = [&]() {
			dc->IASetInputLayout(InputLayouts::PosNormalTexTan2.Get());
			dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			mesh->ModelMesh.Draw(dc, 0);
		};

		int count = 0;
		for (DecalProjector* d : all)
		{
			GameObject* go = d ? d->GetGameObject() : nullptr;
			if (!go || !d->IsEnabled() || !ActiveInHierarchy(go) || !RenderLayers::Visible(go))
				continue;
			UMaterial* mat = d->GetMaterial();
			if (!mat)
				continue;
			// 거리 페이드 (Draw Distance · Start Fade)
			const Matrix box = d->BoxMatrix();
			const XMMATRIX boxM = XMLoadFloat4x4(&box);
			XMFLOAT3 center;
			XMStoreFloat3(&center, boxM.r[3]);
			const float dist = sqrtf((center.x - eye.x) * (center.x - eye.x) + (center.y - eye.y) * (center.y - eye.y) + (center.z - eye.z) * (center.z - eye.z));
			const float drawDist = d->GetDrawDistance();
			if (drawDist <= 0.0f || dist > drawDist)
				continue;
			const float start = d->GetStartFade();
			const float distFade = start >= 1.0f ? 1.0f : 1.0f - std::clamp((dist / drawDist - start) / (1.0f - start), 0.0f, 1.0f);

			Params p = base;
			p.Box = boxM;
			p.WorldToBox = XMMatrixInverse(nullptr, boxM);
			const Matrix world = go->GetTransform()->GetWorldMatrix();
			const XMMATRIX w = XMLoadFloat4x4(&world);
			p.AxisX = Axis(w.r[0]);
			p.AxisY = Axis(w.r[1]);
			p.AxisZ = Axis(w.r[2]);
			p.UV = XMFLOAT4(d->GetUVScale().x, d->GetUVScale().y, d->GetUVOffset().x, d->GetUVOffset().y);
			// Angle Fade: (0, 180) 이면 없음
			const Vec2 af = d->GetAngleFade();
			const bool angle = !(af.x <= 0.0f && af.y >= 180.0f);
			const float cosStart = cosf(XMConvertToRadians(af.x)), cosEnd = cosf(XMConvertToRadians(af.y));
			p.Fade = XMFLOAT4(d->GetFadeFactor(), angle ? cosStart : 0.0f, angle ? cosEnd : 1.0f, distFade);

			// Shader Graph 데칼 (재질의 셰이더가 DrawDecal 을 가지면) — 없거나 만드는 중이면 엔진 데칼이 재질의 Lit 값으로
			const CustomShaders::Shader* cs = mat->IsCustom() ? CustomShaders::Find(mat->CustomShader()) : nullptr;
			if (cs && cs->DrawDecal)
			{
				CustomShaders::DecalDraw dd;
				dd.Context = dc;
				dd.Material = mat;
				dd.SetDecalVars = [&](FxEffect* fx) { SetDecalVars(fx, p); };
				dd.Draw = drawBox;
				cs->DrawDecal(dd);
				++count;
				continue;
			}
			if (!s_Fx)
				continue;
			FxEffect* fx = s_Fx->GetFX();
			FxTechnique* tech = fx->GetTechniqueByName("DecalTech");
			if (!tech || !tech->IsValid())
				continue;
			SetDecalVars(fx, p);
			SetMaterialVars(fx, *mat);
			RenderLayers::SetObjectLayer(s_Fx, ~0u);
			tech->GetPassByIndex(0)->Apply(0, dc);
			drawBox();
			++count;
		}
		s_Count[editor ? 1 : 0] = count;

		// 되돌리기 (하늘 · 대기 · 물이 같은 타깃 · 깊이로 이어 그린다)
		GfxShaderResourceView* nullSRV[16] = {};
		dc->PSSetShaderResources(0, 16, nullSRV);
		dc->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
		dc->RSSetState(nullptr);
		GfxRenderTargetView* restore[1] = { oldRtv.Get() };
		dc->OMSetRenderTargets(1, restore, oldDsv.Get());
	}
}
