#include "pch.h"
#include "MeshPreview.h"
#include "Effects.h"
#include "Vertex.h"
#include "MathHelper.h"
#include "Mesh.h"
#include "UMaterial.h"

namespace MeshPreview
{
	XMFLOAT4X4 Identity()
	{
		XMFLOAT4X4 m;
		XMStoreFloat4x4(&m, XMMatrixIdentity());
		return m;
	}

	bool Target::Ensure(int w, int h)
	{
		if (W == w && H == h && Rtv)
			return true;
		auto device = Application::GetI()->GetDevice();
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = (UINT)w;
		td.Height = (UINT)h;
		td.MipLevels = td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(device->CreateTexture2D(&td, nullptr, Color.ReleaseAndGetAddressOf())))
			return false;
		device->CreateRenderTargetView(Color.Get(), nullptr, Rtv.ReleaseAndGetAddressOf());
		device->CreateShaderResourceView(Color.Get(), nullptr, Srv.ReleaseAndGetAddressOf());
		td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if (FAILED(device->CreateTexture2D(&td, nullptr, Depth.ReleaseAndGetAddressOf())))
			return false;
		device->CreateDepthStencilView(Depth.Get(), nullptr, Dsv.ReleaseAndGetAddressOf());
		W = w;
		H = h;
		return true;
	}

	void Render(Target& t, const std::vector<Item>& items, const View& v)
	{
		if (!t.Rtv || Effects::InstancedBasicFX == nullptr)
			return;
		auto ctx = Application::GetI()->GetDeviceContext();
		ComPtr<GfxRenderTargetView> oldRtv;
		ComPtr<GfxDepthStencilView> oldDsv;
		ctx->OMGetRenderTargets(1, oldRtv.GetAddressOf(), oldDsv.GetAddressOf());
		UINT vpCount = 1;
		D3D11_VIEWPORT oldVp = {};
		ctx->RSGetViewports(&vpCount, &oldVp);

		GfxRenderTargetView* rtvs[1] = { t.Rtv.Get() };
		ctx->OMSetRenderTargets(1, rtvs, t.Dsv.Get());
		const D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)t.W, (float)t.H, 0.0f, 1.0f };
		ctx->RSSetViewports(1, &vp);
		ctx->ClearRenderTargetView(t.Rtv.Get(), v.Clear);
		ctx->ClearDepthStencilView(t.Dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		ctx->OMSetDepthStencilState(nullptr, 0);
		ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		ctx->RSSetState(nullptr);

		// 카메라: 상자를 화면에 맞춘다
		const XMFLOAT3 c((v.Min.x + v.Max.x) * 0.5f, (v.Min.y + v.Max.y) * 0.5f, (v.Min.z + v.Max.z) * 0.5f);
		const float dx = v.Max.x - v.Min.x, dy = v.Max.y - v.Min.y, dz = v.Max.z - v.Min.z;
		const float radius = (std::max)(0.5f, 0.5f * sqrtf(dx * dx + dy * dy + dz * dz));
		const float aspect = (float)t.W / (std::max)(1, t.H);
		const float fov = XMConvertToRadians(v.Fov);
		const float fit = radius / sinf(fov * 0.5f * (std::min)(1.0f, aspect));
		const float dist = fit * 0.92f / (std::max)(0.2f, v.Zoom);
		const float yr = XMConvertToRadians(v.Yaw), pr = XMConvertToRadians(v.Pitch);
		const XMFLOAT3 eye(c.x + sinf(yr) * cosf(pr) * dist, c.y + sinf(pr) * dist, c.z - cosf(yr) * cosf(pr) * dist);
		const XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMVectorSet(c.x, c.y, c.z, 1.0f), XMVectorSet(0, 1, 0, 0));
		const XMMATRIX proj = XMMatrixPerspectiveFovLH(fov, aspect, (std::max)(0.05f, dist * 0.02f), dist * 20.0f + 400.0f);
		const XMMATRIX toTex(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f);
		auto fx = Effects::InstancedBasicFX;
		fx->SetViewProj(view * proj);
		fx->SetTexTransform(XMMatrixIdentity());
		fx->SetEyePosW(eye);
		// 해 (왼쪽 앞 위) + 하늘 보조광
		DirectionalLight lights[2];
		lights[0].Ambient = XMFLOAT4(0.36f, 0.38f, 0.42f, 1.0f);
		lights[0].Diffuse = XMFLOAT4(1.05f, 0.98f, 0.88f, 1.0f);
		lights[0].Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		XMStoreFloat3(&lights[0].Direction, XMVector3Normalize(XMVectorSet(0.55f, -0.62f, 0.56f, 0)));
		lights[1].Ambient = XMFLOAT4(0, 0, 0, 1);
		lights[1].Diffuse = XMFLOAT4(0.22f, 0.26f, 0.34f, 1.0f);
		lights[1].Specular = XMFLOAT4(0.1f, 0.1f, 0.1f, 1.0f);
		XMStoreFloat3(&lights[1].Direction, XMVector3Normalize(XMVectorSet(-0.6f, -0.3f, -0.4f, 0)));
		PointLight noPoint;
		SpotLight noSpot;
		fx->SetDirLights(lights, 2);
		fx->SetPointLights(&noPoint, 0);
		fx->SetSpotLights(&noSpot, 0);

		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->IASetInputLayout(InputLayouts::PosNormalTexTan.Get());
		for (const Item& it : items)
		{
			if (!it.Model)
				continue;
			const XMMATRIX world = XMLoadFloat4x4(&it.World);
			fx->SetWorld(world);
			fx->SetWorldInvTranspose(MathHelper::InverseTranspose(world));
			fx->SetWorldViewProj(world * view * proj);
			fx->SetWorldViewProjTex(world * view * proj * toTex);
			for (int i = 0; i < (int)it.Model->Subsets.size(); ++i)
			{
				const UINT mi = it.Model->Subsets[i].MaterialIndex;
				const std::shared_ptr<UMaterial>& m = mi < it.Materials.size() ? it.Materials[mi] : nullptr;
				(m ? m : UMaterial::GetDefault())->Apply(fx.get(), true);   // 그림자 · SSAO · 안개 없이
				fx->Tech->GetPassByIndex(0)->Apply(0, ctx);
				it.Model->ModelMesh.Draw(ctx, i);
			}
		}

		GfxShaderResourceView* nullSRV[16] = {};
		ctx->PSSetShaderResources(0, 16, nullSRV);
		GfxRenderTargetView* restore[1] = { oldRtv.Get() };
		ctx->OMSetRenderTargets(1, restore, oldDsv.Get());
		if (vpCount > 0)
			ctx->RSSetViewports(1, &oldVp);
	}
}
