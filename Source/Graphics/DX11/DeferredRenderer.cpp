#include "pch.h"
#include "DeferredRenderer.h"
#include "Effects.h"

// Rendering Path = Deferred — DeferredRenderer.h 의 설명
namespace DeferredRenderer
{
	namespace
	{
		// G0 알베도 (sRGB) + Occlusion, G1 Metallic · Smoothness · 표시 · 레이어, G2 월드 노멀, G3 발광 (HDR)
		const DXGI_FORMAT kFormats[4] = { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT };
		const char* kNames[4] = { "gGBuffer0", "gGBuffer1", "gGBuffer2", "gGBuffer3" };

		struct ViewTargets
		{
			UINT W = 0, H = 0;
			ComPtr<GfxTexture2D> Tex[4];
			ComPtr<GfxRenderTargetView> Rtv[4];
			ComPtr<GfxShaderResourceView> Srv[4];
		};
		ViewTargets s_Views[2];
		uint64_t s_Frames[2] = {};

		FxVar* Var(const char* name)
		{
			FxVar* v = Effects::InstancedBasicFX ? Effects::InstancedBasicFX->GetFX()->GetVariableByName(name) : nullptr;
			return v && v->IsValid() ? v : nullptr;
		}

		FxTechnique* LightTech()
		{
			FxTechnique* t = Effects::InstancedBasicFX ? Effects::InstancedBasicFX->GetFX()->GetTechniqueByName("DeferredLightTech") : nullptr;
			return t && t->IsValid() && t->GetPassByIndex(0)->IsUsable() ? t : nullptr;
		}

		bool Ensure(ViewTargets& v, UINT w, UINT h)
		{
			if (v.Tex[0] && v.W == w && v.H == h)
				return true;
			v = ViewTargets();
			auto dev = Gfx::Device();
			for (int i = 0; i < 4; ++i)
			{
				D3D11_TEXTURE2D_DESC d = {};
				d.Width = (std::max)(w, 1u);
				d.Height = (std::max)(h, 1u);
				d.MipLevels = 1;
				d.ArraySize = 1;
				d.Format = kFormats[i];
				d.SampleDesc.Count = 1;
				d.Usage = D3D11_USAGE_DEFAULT;
				d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				if (FAILED(dev->CreateTexture2D(&d, nullptr, v.Tex[i].GetAddressOf())) ||
					FAILED(dev->CreateRenderTargetView(v.Tex[i].Get(), nullptr, v.Rtv[i].GetAddressOf())) ||
					FAILED(dev->CreateShaderResourceView(v.Tex[i].Get(), nullptr, v.Srv[i].GetAddressOf())))
				{
					EditorLog::Write("Deferred", "G-buffer %d (%u x %u) could not be created", i, w, h);
					v = ViewTargets();
					return false;
				}
			}
			v.W = w;
			v.H = h;
			EditorLog::Write("Deferred", "G-buffer %u x %u (4 targets)", w, h);
			return true;
		}
	}

	bool Available()
	{
		return LightTech() != nullptr && Var("gGBuffer0") != nullptr;
	}

	bool BeginGBuffer(GfxContext* ctx, UINT width, UINT height, GfxDepthStencilView* dsv, const D3D11_VIEWPORT& viewport, int view)
	{
		ViewTargets& v = s_Views[view ? 1 : 0];
		if (!Available() || !Ensure(v, width, height))
			return false;
		// 표시 칸 (G1) 이 0 인 픽셀 = G-버퍼 물체 없음 (조명 패스가 건너뛴다). 나머지는 덮어쓴다
		const float zero[4] = { 0, 0, 0, 0 };
		ctx->ClearRenderTargetView(v.Rtv[1].Get(), zero);
		GfxRenderTargetView* rtvs[4] = { v.Rtv[0].Get(), v.Rtv[1].Get(), v.Rtv[2].Get(), v.Rtv[3].Get() };
		ctx->OMSetRenderTargets(4, rtvs, dsv);
		ctx->RSSetViewports(1, &viewport);
		ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		++s_Frames[view ? 1 : 0];
		return true;
	}

	void Light(GfxContext* ctx, GfxRenderTargetView* sceneTarget, GfxShaderResourceView* depthSrv, const Matrix& viewProj, const D3D11_VIEWPORT& viewport, int view)
	{
		ViewTargets& v = s_Views[view ? 1 : 0];
		FxTechnique* tech = LightTech();
		if (!tech || !v.Tex[0])
			return;
		GfxRenderTargetView* rt[1] = { sceneTarget };
		ctx->OMSetRenderTargets(1, rt, nullptr);
		ctx->RSSetViewports(1, &viewport);
		ctx->OMSetDepthStencilState(nullptr, 0);
		ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		ctx->RSSetState(nullptr);
		for (int i = 0; i < 4; ++i)
			if (FxVar* g = Var(kNames[i])) g->SetResource(v.Srv[i].Get());
		if (FxVar* d = Var("gDeferredDepth")) d->SetResource(depthSrv);
		XMFLOAT4X4 inv;
		XMStoreFloat4x4(&inv, XMMatrixInverse(nullptr, viewProj));
		if (FxVar* m = Var("gDeferredInvViewProj")) m->SetMatrix(&inv._11);
		// 재질마다의 켜기 값은 G-버퍼에 (그림자 = Receive Shadows, 안개) — 여기서는 모두 켠 값 (마지막 재질의 값이 남지 않게)
		ShaderSetting setting = {};
		setting.UseShadowMap = 1;
		setting.UseSsaoMap = 1;
		setting.FogEnabled = 1;
		Effects::InstancedBasicFX->SetShaderSetting(setting);
		ctx->IASetInputLayout(nullptr);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		tech->GetPassByIndex(0)->Apply(0, ctx);
		ctx->Draw(3, 0);
		// G-버퍼를 셰이더에서 푼다 (다음 프레임에 렌더 타깃으로 묶을 때 겹치지 않게)
		for (int i = 0; i < 4; ++i)
			if (FxVar* g = Var(kNames[i])) g->SetResource(nullptr);
		if (FxVar* d = Var("gDeferredDepth")) d->SetResource(nullptr);
		tech->GetPassByIndex(0)->Apply(0, ctx);
	}

	GfxShaderResourceView* GBufferSRV(int index, int view)
	{
		const ViewTargets& v = s_Views[view ? 1 : 0];
		return index >= 0 && index < 4 ? v.Srv[index].Get() : nullptr;
	}

	nlohmann::json Info()
	{
		nlohmann::json views = nlohmann::json::array();
		for (int i = 0; i < 2; ++i)
			views.push_back({ { "view", i ? "Scene" : "Game" }, { "size", { s_Views[i].W, s_Views[i].H } }, { "frames", s_Frames[i] } });
		return { { "available", Available() }, { "views", views } };
	}
}
