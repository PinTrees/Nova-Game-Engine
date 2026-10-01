#include "pch.h"
#include "AtmospherePass.h"
#include "Effects.h"
#include "VolumeProfile.h"
#include "LightHelper.h"

namespace
{
	std::unique_ptr<Effect> s_Effect;
	bool s_Failed = false;
	ComPtr<ID3D11Texture2D> s_Copy;
	ComPtr<ID3D11ShaderResourceView> s_CopySRV;
	D3D11_TEXTURE2D_DESC s_CopyDesc = {};

	float ToLinear(float c) { return powf((std::max)(c, 0.0f), 2.2f); }

	void SetVec(ID3DX11Effect* fx, const char* name, const XMFLOAT4& v)
	{
		if (auto* var = fx->GetVariableByName(name)->AsVector(); var && var->IsValid())
			var->SetFloatVector(&v.x);
	}

	ID3DX11Effect* Fx()
	{
		if (!s_Effect && !s_Failed)
		{
			s_Effect = std::make_unique<Effect>(Application::GetI()->GetDevice(), L"../Shaders/50. Atmosphere.fx");
			if (s_Effect->GetFX() == nullptr)
			{
				s_Effect.reset();
				s_Failed = true;
				EditorLog::Write("Atmosphere", "50. Atmosphere.fx failed to load - fog disabled");
			}
		}
		return s_Effect ? s_Effect->GetFX() : nullptr;
	}

	// 장면 색 사본 (타깃과 같은 형식·크기)
	ID3D11ShaderResourceView* CopyScene(ID3D11DeviceContext* dc, ID3D11RenderTargetView* target)
	{
		ComPtr<ID3D11Resource> res;
		target->GetResource(res.GetAddressOf());
		ComPtr<ID3D11Texture2D> tex;
		if (FAILED(res.As(&tex)))
			return nullptr;
		D3D11_TEXTURE2D_DESC desc;
		tex->GetDesc(&desc);
		if (!s_Copy || s_CopyDesc.Width != desc.Width || s_CopyDesc.Height != desc.Height || s_CopyDesc.Format != desc.Format || s_CopyDesc.SampleDesc.Count != desc.SampleDesc.Count)
		{
			s_Copy.Reset();
			s_CopySRV.Reset();
			D3D11_TEXTURE2D_DESC cd = desc;
			cd.MipLevels = 1;
			cd.ArraySize = 1;
			cd.Usage = D3D11_USAGE_DEFAULT;
			cd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			cd.CPUAccessFlags = 0;
			cd.MiscFlags = 0;
			auto device = Application::GetI()->GetDevice();
			if (desc.SampleDesc.Count > 1 || FAILED(device->CreateTexture2D(&cd, nullptr, s_Copy.GetAddressOf())))
				return nullptr;
			D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
			sd.Format = desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ? DXGI_FORMAT_R8G8B8A8_UNORM
				: desc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS ? DXGI_FORMAT_B8G8R8A8_UNORM
				: desc.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS ? DXGI_FORMAT_R16G16B16A16_FLOAT : desc.Format;
			sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			sd.Texture2D.MipLevels = 1;
			if (FAILED(device->CreateShaderResourceView(s_Copy.Get(), &sd, s_CopySRV.GetAddressOf())))
			{
				s_Copy.Reset();
				return nullptr;
			}
			s_CopyDesc = desc;
			EditorLog::Write("Atmosphere", "scene copy %u x %u (format %d)", desc.Width, desc.Height, (int)desc.Format);
		}
		if (desc.MipLevels == 1 && desc.ArraySize == 1)
			dc->CopyResource(s_Copy.Get(), tex.Get());
		else
			dc->CopySubresourceRegion(s_Copy.Get(), 0, 0, 0, 0, tex.Get(), 0, nullptr);
		return s_CopySRV.Get();
	}
}

namespace AtmospherePass
{
	Params FromStack(const VolumeStack& stack, const DirectionalLight* sun)
	{
		Params p;
		XMFLOAT3 sunColor(1.0f, 0.96f, 0.9f);
		if (sun)
		{
			XMVECTOR L = XMVector3Normalize(XMVectorNegate(XMLoadFloat3(&sun->Direction)));
			XMStoreFloat4(&p.SunDir, L);
			sunColor = XMFLOAT3(ToLinear(sun->Diffuse.x), ToLinear(sun->Diffuse.y), ToLinear(sun->Diffuse.z));
		}
		p.SunColor = XMFLOAT4(sunColor.x, sunColor.y, sunColor.z, 0.0f);

		if (stack.IsActive("Fog"))
		{
			const VolumeComponent* f = stack.Get("Fog");
			p.Fog = true;
			const float base = f->F("baseHeight");
			// Maximum Height 에서 밀도 10 % → 척도 높이 = (최대 - 기준) / ln 10
			const float scaleH = (std::max)(f->F("maximumHeight") - base, 1.0f) / 2.302585f;
			p.FogA = XMFLOAT4(1.0f / (std::max)(f->F("meanFreePath"), 1.0f), base, 1.0f / scaleH, (std::max)(f->F("startDistance"), 0.0f));
			p.FogB = XMFLOAT4((std::max)(f->F("maxFogDistance"), 1.0f), std::clamp(f->F("maxOpacity"), 0.0f, 1.0f), (std::max)(f->F("sunScattering"), 0.0f), std::clamp(f->F("anisotropy"), 0.0f, 0.95f));
			const bool skyColor = f->I("colorMode") == 0;
			const float* c = skyColor ? f->V("tint") : f->V("color");
			p.FogColor = XMFLOAT4(skyColor ? (std::max)(c[0], 0.0f) : ToLinear(c[0]), skyColor ? (std::max)(c[1], 0.0f) : ToLinear(c[1]),
				skyColor ? (std::max)(c[2], 0.0f) : ToLinear(c[2]), skyColor ? 1.0f : 0.0f);
		}
		if (stack.IsActive("Atmosphere"))
		{
			const VolumeComponent* a = stack.Get("Atmosphere");
			p.Aerial = true;
			// 해수면 산란 계수 (1/m): 레일리 (680, 550, 440 nm), 미 (뿌연 날 기준)
			const float scale = (std::max)(a->F("distanceScale"), 0.0f);
			const float r = (std::max)(a->F("rayleigh"), 0.0f) * scale;
			p.Rayleigh = XMFLOAT4(5.8e-6f * r, 13.5e-6f * r, 33.1e-6f * r, 1.0f / (std::max)(a->F("rayleighHeight"), 1.0f));
			p.Mie = XMFLOAT4(21e-6f * (std::max)(a->F("mie"), 0.0f) * scale, 1.0f / (std::max)(a->F("mieHeight"), 1.0f),
				std::clamp(a->F("mieAnisotropy"), 0.0f, 0.99f), (std::max)(a->F("sunIntensity"), 0.0f));
		}
		return p;
	}

	void Bind(ID3DX11Effect* fx, const Params& p, const XMFLOAT3& eye, ID3D11ShaderResourceView* sky)
	{
		if (!fx)
			return;
		SetVec(fx, "gAtmFog", p.FogA);
		SetVec(fx, "gAtmFog2", p.FogB);
		SetVec(fx, "gAtmFogColor", p.FogColor);
		SetVec(fx, "gAtmRayleigh", p.Rayleigh);
		SetVec(fx, "gAtmMie", p.Mie);
		SetVec(fx, "gAtmSunDir", p.SunDir);
		SetVec(fx, "gAtmSunColor", p.SunColor);
		SetVec(fx, "gAtmEye", XMFLOAT4(eye.x, eye.y, eye.z, 1.0f));
		SetVec(fx, "gAtmFlags", XMFLOAT4(p.Fog ? 1.0f : 0.0f, p.Aerial ? 1.0f : 0.0f, sky ? 1.0f : 0.0f, 0.0f));
		if (auto* v = fx->GetVariableByName("gAtmSky")->AsShaderResource(); v && v->IsValid())
			v->SetResource(sky);
	}

	void Draw(ID3D11DeviceContext* dc, const Params& p, ID3D11RenderTargetView* target, ID3D11DepthStencilView* dsv, ID3D11ShaderResourceView* depthSRV,
		const D3D11_VIEWPORT& viewport, CXMMATRIX viewProj, const XMFLOAT3& eye, ID3D11ShaderResourceView* sky)
	{
		ID3DX11Effect* fx = Fx();
		if (!fx || !p.Active() || !target || !depthSRV)
			return;
		ID3DX11EffectTechnique* tech = fx->GetTechniqueByName("AtmosphereTech");
		if (!tech || !tech->IsValid())
			return;
		ID3D11ShaderResourceView* scene = CopyScene(dc, target);
		if (!scene)
			return;
		Bind(fx, p, eye, sky);
		const XMMATRIX inv = XMMatrixInverse(nullptr, viewProj);
		if (auto* v = fx->GetVariableByName("gAtmInvViewProj")->AsMatrix(); v && v->IsValid())
			v->SetMatrix(reinterpret_cast<const float*>(&inv));
		SetVec(fx, "gAtmViewport", XMFLOAT4(viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height));
		if (auto* v = fx->GetVariableByName("gAtmSceneColor")->AsShaderResource(); v && v->IsValid())
			v->SetResource(scene);
		if (auto* v = fx->GetVariableByName("gAtmDepth")->AsShaderResource(); v && v->IsValid())
			v->SetResource(depthSRV);

		// 깊이를 읽으므로 DSV 는 풀어 둔다
		dc->OMSetRenderTargets(1, &target, nullptr);
		dc->RSSetViewports(1, &viewport);
		dc->IASetInputLayout(nullptr);
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ID3D11Buffer* none = nullptr;
		UINT zero = 0;
		dc->IASetVertexBuffers(0, 1, &none, &zero, &zero);
		tech->GetPassByIndex(0)->Apply(0, dc);
		dc->Draw(3, 0);

		// 다음 단계(물)가 같은 깊이를 다시 쓰도록 바인딩을 풀고 상태를 되돌린다
		ID3D11ShaderResourceView* nullSRV[8] = {};
		dc->PSSetShaderResources(0, 8, nullSRV);
		dc->OMSetRenderTargets(1, &target, dsv);
		dc->OMSetDepthStencilState(nullptr, 0);
		dc->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		dc->RSSetState(nullptr);
	}
}
