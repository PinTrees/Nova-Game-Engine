#include "pch.h"
#include "ScreenSpaceReflection.h"
#include "Effects.h"
#include "VolumeProfile.h"
#include "EditorLog.h"

namespace
{
	// 뷰마다 (0 = Game, 1 = Scene): 지난 프레임 장면 색 (밉까지) 과 그때의 ViewProj
	struct History
	{
		ComPtr<GfxTexture2D> Tex;
		ComPtr<GfxShaderResourceView> SRV;
		UINT W = 0, H = 0, Mips = 1;
		DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
		XMFLOAT4X4 ViewProj = {};        // 저장한 장면을 그린 카메라
		XMFLOAT4X4 PendingViewProj = {}; // 이번 뷰 (StoreHistory 때 ViewProj 로)
		bool Valid = false;
	};
	History s_History[2];

	// 마지막 Prepare 의 값 (Bind 가 넣는다)
	struct State
	{
		bool On = false;
		XMFLOAT4X4 View = {}, Proj = {}, PrevViewProj = {};
		float Params[4] = {};    // 켜짐, 걸음 수, 최대 거리 (m), Object Thickness
		float Params2[4] = {};   // Minimum Smoothness, Smoothness Fade Start, Screen Edge Fade Distance, 지난 색 밉 수
		float Size[4] = {};      // 깊이 프리패스 크기 (w, h, 1/w, 1/h)
		GfxShaderResourceView* NormalDepth = nullptr;
		GfxShaderResourceView* HistorySRV = nullptr;
	};
	State s_State;
	bool s_Active[2] = {};

	constexpr float kMaxDistance = 40.0f;   // 광선 최대 길이 (m)

	FxVar* Var(FxEffect* fx, const char* name)
	{
		FxVar* v = fx ? fx->GetVariableByName(name) : nullptr;
		return v && v->IsValid() ? v : nullptr;
	}

	DXGI_FORMAT Typed(DXGI_FORMAT f)
	{
		switch (f)
		{
		case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
		default: return f;
		}
	}
}

namespace ScreenSpaceReflection
{
	bool Active(bool editor) { return s_Active[editor ? 1 : 0]; }

	void Prepare(const VolumeStack& stack, bool editor, bool capture, GfxShaderResourceView* normalDepth, CXMMATRIX view, CXMMATRIX proj)
	{
		History& h = s_History[editor ? 1 : 0];
		State& s = s_State;
		s = State{};
		XMStoreFloat4x4(&h.PendingViewProj, XMMatrixMultiply(view, proj));
		const VolumeComponent* c = stack.Get("ScreenSpaceReflection");
		const bool want = !capture && c && stack.IsActive("ScreenSpaceReflection") && normalDepth;
		if (!capture)
			s_Active[editor ? 1 : 0] = want;
		if (!want || !h.Valid || !h.SRV)
			return;   // 지난 프레임 색이 생기기 전 (켠 첫 프레임) 은 프로브 · 하늘만
		ComPtr<GfxResource> res;
		normalDepth->GetResource(res.GetAddressOf());
		ComPtr<GfxTexture2D> tex;
		if (FAILED(res.As(&tex)))
			return;
		D3D11_TEXTURE2D_DESC nd;
		tex->GetDesc(&nd);
		static const float kSteps[3] = { 16.0f, 32.0f, 64.0f };   // Low · Medium · High (HDRP 의 Max Ray Steps)
		s.On = true;
		XMStoreFloat4x4(&s.View, view);
		XMStoreFloat4x4(&s.Proj, proj);
		s.PrevViewProj = h.ViewProj;
		const float minS = std::clamp(c->F("minSmoothness"), 0.0f, 1.0f);
		s.Params[0] = 1.0f;
		s.Params[1] = kSteps[std::clamp(c->I("quality"), 0, 2)];
		s.Params[2] = kMaxDistance;
		s.Params[3] = std::clamp(c->F("depthBufferThickness"), 0.0f, 1.0f);
		s.Params2[0] = minS;
		s.Params2[1] = (std::max)(std::clamp(c->F("smoothnessFadeStart"), 0.0f, 1.0f), minS);
		s.Params2[2] = std::clamp(c->F("screenFadeDistance"), 0.0f, 1.0f);
		s.Params2[3] = (float)h.Mips;
		s.Size[0] = (float)nd.Width;
		s.Size[1] = (float)nd.Height;
		s.Size[2] = 1.0f / (std::max)((float)nd.Width, 1.0f);
		s.Size[3] = 1.0f / (std::max)((float)nd.Height, 1.0f);
		s.NormalDepth = normalDepth;
		s.HistorySRV = h.SRV.Get();
	}

	void Bind(InstancedBasicEffect* effect)
	{
		FxEffect* fx = effect ? effect->GetFX() : nullptr;
		if (!fx)
			return;
		const State& s = s_State;
		if (FxVar* v = Var(fx, "gSsrParams")) v->SetFloatVector(s.Params);   // 꺼짐이면 0 — 셰이더가 건너뛴다
		if (!s.On)
		{
			if (FxVar* v = Var(fx, "gSsrNormalDepth")) v->SetResource(nullptr);
			if (FxVar* v = Var(fx, "gSsrHistory")) v->SetResource(nullptr);
			return;
		}
		if (FxVar* v = Var(fx, "gSsrParams2")) v->SetFloatVector(s.Params2);
		if (FxVar* v = Var(fx, "gSsrSize")) v->SetFloatVector(s.Size);
		if (FxVar* v = Var(fx, "gSsrView")) v->SetMatrix(&s.View._11);
		if (FxVar* v = Var(fx, "gSsrProj")) v->SetMatrix(&s.Proj._11);
		if (FxVar* v = Var(fx, "gSsrPrevViewProj")) v->SetMatrix(&s.PrevViewProj._11);
		if (FxVar* v = Var(fx, "gSsrNormalDepth")) v->SetResource(s.NormalDepth);
		if (FxVar* v = Var(fx, "gSsrHistory")) v->SetResource(s.HistorySRV);
	}

	void StoreHistory(GfxContext* dc, GfxRenderTargetView* sceneTarget, bool editor)
	{
		History& h = s_History[editor ? 1 : 0];
		if (!s_Active[editor ? 1 : 0] || !sceneTarget)
		{
			h.Valid = false;   // 다시 켜면 그 프레임의 색부터 (예전 색 · 카메라는 버림)
			return;
		}
		ComPtr<GfxResource> res;
		sceneTarget->GetResource(res.GetAddressOf());
		ComPtr<GfxTexture2D> src;
		if (FAILED(res.As(&src)))
			return;
		D3D11_TEXTURE2D_DESC desc;
		src->GetDesc(&desc);
		if (desc.SampleDesc.Count > 1)
			return;
		const DXGI_FORMAT format = Typed(desc.Format);
		if (!h.Tex || h.W != desc.Width || h.H != desc.Height || h.Format != format)
		{
			h = History{};
			UINT mips = 1;
			for (UINT m = (std::max)(desc.Width, desc.Height); m > 1 && mips < 8; m >>= 1)
				++mips;
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = desc.Width;
			td.Height = desc.Height;
			td.MipLevels = mips;
			td.ArraySize = 1;
			td.Format = format;
			td.SampleDesc.Count = 1;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
			auto device = Application::GetI()->GetDevice();
			if (FAILED(device->CreateTexture2D(&td, nullptr, h.Tex.GetAddressOf())) ||
				FAILED(device->CreateShaderResourceView(h.Tex.Get(), nullptr, h.SRV.GetAddressOf())))
			{
				h = History{};
				return;
			}
			h.W = desc.Width;
			h.H = desc.Height;
			h.Mips = mips;
			h.Format = format;
			EditorLog::Write("SSR", "%s history %u x %u (format %d, %u mips)", editor ? "scene" : "game", desc.Width, desc.Height, (int)format, mips);
		}
		dc->CopySubresourceRegion(h.Tex.Get(), 0, 0, 0, 0, src.Get(), 0, nullptr);
		dc->GenerateMips(h.SRV.Get());
		h.ViewProj = h.PendingViewProj;
		h.Valid = true;
	}
}
