#include "pch.h"
#include "GfxTest.h"
#include "GeometryGenerator.h"
#include "LightHelper.h"
#include "PbrMaterial.h"
#include "MathHelper.h"
#include "PathManager.h"

namespace
{
	struct GfxVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT3 Normal;
		XMFLOAT2 Tex;
		XMFLOAT4 Tangent;
	};

	struct GfxMesh
	{
		ComPtr<GfxBuffer> Vb, Ib;
		UINT IndexCount = 0;
	};

	bool MakeMesh(GfxDevice* dev, const GeometryGenerator::MeshData& data, GfxMesh& m)
	{
		std::vector<GfxVertex> v;
		for (const auto& s : data.vertices)
			v.push_back({ s.position, s.normal, s.texC, XMFLOAT4(s.tangentU.x, s.tangentU.y, s.tangentU.z, 1.0f) });
		D3D11_BUFFER_DESC bd = {};
		bd.Usage = D3D11_USAGE_IMMUTABLE;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.ByteWidth = (UINT)(v.size() * sizeof(GfxVertex));
		D3D11_SUBRESOURCE_DATA sd = { v.data() };
		if (FAILED(dev->CreateBuffer(&bd, &sd, m.Vb.GetAddressOf()))) return false;
		bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
		bd.ByteWidth = (UINT)(data.indices.size() * 4);
		sd.pSysMem = data.indices.data();
		if (FAILED(dev->CreateBuffer(&bd, &sd, m.Ib.GetAddressOf()))) return false;
		m.IndexCount = (UINT)data.indices.size();
		return true;
	}

	void BuildMips(std::vector<std::vector<uint8_t>>& levels, UINT size)
	{
		for (UINT s = size; s > 1; s /= 2)
		{
			const std::vector<uint8_t>& src = levels.back();
			const UINT d = s / 2;
			std::vector<uint8_t> dst((size_t)d * d * 4);
			for (UINT y = 0; y < d; ++y)
				for (UINT x = 0; x < d; ++x)
					for (int c = 0; c < 4; ++c)
					{
						const int sum = src[((y * 2) * s + x * 2) * 4 + c] + src[((y * 2) * s + x * 2 + 1) * 4 + c] +
							src[((y * 2 + 1) * s + x * 2) * 4 + c] + src[((y * 2 + 1) * s + x * 2 + 1) * 4 + c];
						dst[((size_t)y * d + x) * 4 + c] = (uint8_t)((sum + 2) / 4);
					}
			levels.push_back(std::move(dst));
		}
	}

	ComPtr<GfxShaderResourceView> MakeChecker(GfxDevice* dev)
	{
		const UINT size = 256;
		std::vector<std::vector<uint8_t>> levels(1, std::vector<uint8_t>((size_t)size * size * 4));
		for (UINT y = 0; y < size; ++y)
			for (UINT x = 0; x < size; ++x)
			{
				const bool odd = ((x / 32) + (y / 32)) & 1;
				uint8_t r = odd ? 235 : 70, g = odd ? 235 : 75, b = odd ? 225 : 85;
				if (x < 64 && y < 64) { r = 220; g = 40; b = 40; }
				uint8_t* p = &levels[0][((size_t)y * size + x) * 4];
				p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
			}
		BuildMips(levels, size);
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = td.Height = size;
		td.MipLevels = (UINT)levels.size();
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_IMMUTABLE;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		std::vector<D3D11_SUBRESOURCE_DATA> sd;
		for (UINT m = 0; m < td.MipLevels; ++m)
			sd.push_back({ levels[m].data(), (size >> m) * 4, 0 });
		ComPtr<GfxTexture2D> tex;
		ComPtr<GfxShaderResourceView> srv;
		if (SUCCEEDED(dev->CreateTexture2D(&td, sd.data(), tex.GetAddressOf())))
			dev->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf());
		return srv;
	}

	XMFLOAT3 SkyColor(XMFLOAT3 dir)
	{
		XMFLOAT3 n;
		XMStoreFloat3(&n, XMVector3Normalize(XMLoadFloat3(&dir)));
		XMFLOAT3 c;
		if (n.y >= 0)
		{
			const float t = sqrtf(n.y);
			c = XMFLOAT3(0.78f + (0.28f - 0.78f) * t, 0.86f + (0.48f - 0.86f) * t, 0.95f + (0.88f - 0.95f) * t);
		}
		else
		{
			const float t = (std::min)(1.0f, -n.y * 3.0f);
			c = XMFLOAT3(0.55f + (0.32f - 0.55f) * t, 0.52f + (0.27f - 0.52f) * t, 0.48f + (0.2f - 0.48f) * t);
		}
		const float w = powf((std::max)(n.x, 0.0f), 4.0f) * 0.7f;
		return XMFLOAT3(c.x + (1.0f - c.x) * w, c.y + (0.55f - c.y) * w, c.z + (0.2f - c.z) * w);
	}

	ComPtr<GfxShaderResourceView> MakeSky(GfxDevice* dev)
	{
		const UINT size = 128;
		std::vector<std::vector<std::vector<uint8_t>>> faces(6);
		for (int f = 0; f < 6; ++f)
		{
			faces[f].assign(1, std::vector<uint8_t>((size_t)size * size * 4));
			for (UINT y = 0; y < size; ++y)
				for (UINT x = 0; x < size; ++x)
				{
					const float u = (x + 0.5f) / size * 2 - 1, v = (y + 0.5f) / size * 2 - 1;
					XMFLOAT3 d;
					switch (f)
					{
					case 0: d = XMFLOAT3(1, -v, -u); break;
					case 1: d = XMFLOAT3(-1, -v, u); break;
					case 2: d = XMFLOAT3(u, 1, v); break;
					case 3: d = XMFLOAT3(u, -1, -v); break;
					case 4: d = XMFLOAT3(u, -v, 1); break;
					default: d = XMFLOAT3(-u, -v, -1); break;
					}
					const XMFLOAT3 c = SkyColor(d);
					uint8_t* p = &faces[f][0][((size_t)y * size + x) * 4];
					p[0] = (uint8_t)(std::clamp(c.x, 0.0f, 1.0f) * 255 + 0.5f);
					p[1] = (uint8_t)(std::clamp(c.y, 0.0f, 1.0f) * 255 + 0.5f);
					p[2] = (uint8_t)(std::clamp(c.z, 0.0f, 1.0f) * 255 + 0.5f);
					p[3] = 255;
				}
			BuildMips(faces[f], size);
		}
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = td.Height = size;
		td.MipLevels = (UINT)faces[0].size();
		td.ArraySize = 6;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_IMMUTABLE;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		td.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
		std::vector<D3D11_SUBRESOURCE_DATA> sd;
		for (int f = 0; f < 6; ++f)
			for (UINT m = 0; m < td.MipLevels; ++m)
				sd.push_back({ faces[f][m].data(), (size >> m) * 4, 0 });
		ComPtr<GfxTexture2D> tex;
		ComPtr<GfxShaderResourceView> srv;
		if (SUCCEEDED(dev->CreateTexture2D(&td, sd.data(), tex.GetAddressOf())))
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
			vd.Format = td.Format;
			vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
			vd.TextureCube.MipLevels = td.MipLevels;
			dev->CreateShaderResourceView(tex.Get(), &vd, srv.GetAddressOf());
		}
		return srv;
	}
}

namespace GfxTest
{
	bool Render(GfxDevice* dev, GfxContext* ctx, Rhi::Device* rhi, int width, int height, Result& out, std::string& error)
	{
		const auto t0 = std::chrono::steady_clock::now();
		out.Width = width;
		out.Height = height;
		const std::filesystem::path shaders = std::filesystem::path(PathManager::GetI()->GetEnginePathW()) / L"Shaders";
		ComPtr<FxEffect> fx = FxEffect::Load((shaders / L"32. InstancedBasic.fx").wstring(), error, rhi);
		if (!fx) return false;
		ComPtr<FxEffect> sfx = FxEffect::Load((shaders / L"26. BuildShadowMap.fx").wstring(), error, rhi);
		if (!sfx) return false;
		FxTechnique* tech = fx->GetTechniqueByName("Tech");
		FxTechnique* stech = sfx->GetTechniqueByName("BuildShadowMapTech");
		if (!tech->IsValid() || !stech->IsValid()) { error = "technique not found"; return false; }

		// 입력 배치 = pass 의 입력 서명으로 (엔진 Vertex.cpp 와 같은 방식)
		const D3D11_INPUT_ELEMENT_DESC elements[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		ComPtr<GfxInputLayout> layout, shadowLayout;
		D3DX11_PASS_DESC pd;
		tech->GetPassByIndex(0)->GetDesc(&pd);
		if (FAILED(dev->CreateInputLayout(elements, _countof(elements), pd.pIAInputSignature, pd.IAInputSignatureSize, layout.GetAddressOf()))) { error = "input layout"; return false; }
		stech->GetPassByIndex(0)->GetDesc(&pd);
		if (FAILED(dev->CreateInputLayout(elements, _countof(elements), pd.pIAInputSignature, pd.IAInputSignatureSize, shadowLayout.GetAddressOf()))) { error = "shadow input layout"; return false; }

		GeometryGenerator gen;
		GeometryGenerator::MeshData sphereData, boxData, gridData;
		gen.CreateSphere(1.0f, 48, 24, sphereData);
		gen.CreateBox(1.5f, 1.5f, 1.5f, boxData);
		gen.CreateGrid(12.0f, 12.0f, 2, 2, gridData);
		GfxMesh sphere, box, grid;
		if (!MakeMesh(dev, sphereData, sphere) || !MakeMesh(dev, boxData, box) || !MakeMesh(dev, gridData, grid)) { error = "mesh buffers"; return false; }
		ComPtr<GfxShaderResourceView> checker = MakeChecker(dev), sky = MakeSky(dev);
		if (!checker || !sky) { error = "textures"; return false; }

		// 색·깊이 타깃 (깊이 = 물이 읽는 엔진 장면 깊이와 같은 R24G8 타입 없는 형식)
		D3D11_TEXTURE2D_DESC cd = {};
		cd.Width = width;
		cd.Height = height;
		cd.MipLevels = 1;
		cd.ArraySize = 1;
		cd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		cd.SampleDesc.Count = 1;
		cd.Usage = D3D11_USAGE_DEFAULT;
		cd.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		ComPtr<GfxTexture2D> colorTex, depthTex, shadowTex;
		ComPtr<GfxRenderTargetView> rtv;
		ComPtr<GfxDepthStencilView> dsv, shadowDsv;
		ComPtr<GfxShaderResourceView> shadowSrv;
		if (FAILED(dev->CreateTexture2D(&cd, nullptr, colorTex.GetAddressOf())) || FAILED(dev->CreateRenderTargetView(colorTex.Get(), nullptr, rtv.GetAddressOf()))) { error = "color target"; return false; }
		cd.Format = DXGI_FORMAT_R24G8_TYPELESS;
		cd.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
		dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		if (FAILED(dev->CreateTexture2D(&cd, nullptr, depthTex.GetAddressOf())) || FAILED(dev->CreateDepthStencilView(depthTex.Get(), &dd, dsv.GetAddressOf()))) { error = "depth target"; return false; }
		// 그림자 맵: ShadowMap.cpp 와 같은 모양 (배열 1 조각)
		const UINT shadowSize = 1024;
		cd.Width = cd.Height = shadowSize;
		if (FAILED(dev->CreateTexture2D(&cd, nullptr, shadowTex.GetAddressOf()))) { error = "shadow map"; return false; }
		D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
		sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		sv.Texture2DArray.MipLevels = 1;
		sv.Texture2DArray.ArraySize = 1;
		dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		dd.Texture2DArray.ArraySize = 1;
		if (FAILED(dev->CreateShaderResourceView(shadowTex.Get(), &sv, shadowSrv.GetAddressOf())) || FAILED(dev->CreateDepthStencilView(shadowTex.Get(), &dd, shadowDsv.GetAddressOf()))) { error = "shadow views"; return false; }
		// 상태 객체 (기본과 같은 값을 일부러 만들어 넣어 본다)
		D3D11_RASTERIZER_DESC rd = {};
		rd.FillMode = D3D11_FILL_SOLID;
		rd.CullMode = D3D11_CULL_BACK;
		rd.DepthClipEnable = TRUE;
		ComPtr<GfxRasterizerState> rs;
		dev->CreateRasterizerState(&rd, rs.GetAddressOf());
		const auto t1 = std::chrono::steady_clock::now();

		// ---- 프레임 값
		DirectionalLight dirs[4];
		PointLight points[4];
		SpotLight spots[4];
		memset(dirs, 0, sizeof(dirs));
		memset(points, 0, sizeof(points));
		memset(spots, 0, sizeof(spots));
		dirs[0].Init();
		dirs[0].Diffuse = XMFLOAT4(1.0f, 0.95f, 0.85f, 1.0f);
		XMStoreFloat3(&dirs[0].Direction, XMVector3Normalize(XMVectorSet(-0.45f, -0.8f, 0.4f, 0)));
		points[0].Init();
		points[0].Diffuse = XMFLOAT4(0.2f, 0.6f, 1.0f, 1.0f);
		points[0].Position = XMFLOAT3(0.2f, 0.6f, -1.6f);
		points[0].Range = 5.0f;
		points[0].Att = XMFLOAT3(0.0f, 0.4f, 0.0f);
		fx->GetVariableByName("gDirLights")->SetRawValue(dirs, 0, sizeof(dirs));
		fx->GetVariableByName("gPointLights")->SetRawValue(points, 0, sizeof(points));
		fx->GetVariableByName("gSpotLights")->SetRawValue(spots, 0, sizeof(spots));
		fx->GetVariableByName("gDirLightCount")->AsScalar()->SetInt(1);
		fx->GetVariableByName("gPointLightCount")->AsScalar()->SetInt(1);
		fx->GetVariableByName("gSpotLightCount")->AsScalar()->SetInt(0);
		const XMFLOAT3 eye(0.0f, 2.4f, -5.6f);
		fx->GetVariableByName("gEyePosW")->SetRawValue(&eye, 0, 12);
		fx->GetVariableByName("gCubeMap")->AsShaderResource()->SetResource(sky.Get());

		const XMMATRIX sphereWorld = XMMatrixRotationY(0.6f) * XMMatrixTranslation(-1.35f, 1.0f, 0.0f);
		const XMMATRIX boxWorld = XMMatrixRotationY(0.5f) * XMMatrixTranslation(1.45f, 0.75f, 0.4f);
		const XMVECTOR lightDir = XMLoadFloat3(&dirs[0].Direction);
		const XMVECTOR center = XMVectorSet(0.0f, 0.5f, 0.0f, 1.0f);
		const XMMATRIX lightView = XMMatrixLookAtLH(XMVectorSubtract(center, XMVectorScale(lightDir, 12.0f)), center, XMVectorSet(0, 1, 0, 0));
		const XMMATRIX lightViewProj = lightView * XMMatrixOrthographicLH(14.0f, 14.0f, 0.5f, 30.0f);
		const UINT stride = sizeof(GfxVertex), offset = 0;

		// ---- 그림자 맵
		{
			XMFLOAT4 toLight;
			XMStoreFloat4(&toLight, XMVectorSetW(XMVectorNegate(lightDir), 0.0f));
			sfx->GetVariableByName("gShadowLight")->AsVector()->SetFloatVector(&toLight.x);
			const float bias[2] = { 0.02f, 0.03f };
			sfx->GetVariableByName("gShadowBias")->SetRawValue(bias, 0, 8);
			sfx->GetVariableByName("gViewProj")->AsMatrix()->SetMatrix(reinterpret_cast<const float*>(&lightViewProj));
			const XMMATRIX identity = XMMatrixIdentity();
			sfx->GetVariableByName("gTexTransform")->AsMatrix()->SetMatrix(reinterpret_cast<const float*>(&identity));
			ctx->OMSetRenderTargets(0, nullptr, shadowDsv.Get());
			const D3D11_VIEWPORT vp = { 0, 0, (float)shadowSize, (float)shadowSize, 0, 1 };
			ctx->RSSetViewports(1, &vp);
			ctx->ClearDepthStencilView(shadowDsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
			ctx->RSSetState(rs.Get());
			ctx->OMSetDepthStencilState(nullptr, 0);
			ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
			ctx->IASetInputLayout(shadowLayout.Get());
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			for (const auto& [mesh, world] : { std::pair<const GfxMesh*, XMMATRIX>{ &sphere, sphereWorld }, { &box, boxWorld } })
			{
				sfx->GetVariableByName("gWorld")->AsMatrix()->SetMatrix(reinterpret_cast<const float*>(&world));
				const XMMATRIX invT = MathHelper::InverseTranspose(world);
				sfx->GetVariableByName("gWorldInvTranspose")->AsMatrix()->SetMatrix(reinterpret_cast<const float*>(&invT));
				stech->GetPassByIndex(0)->Apply(0, ctx);
				ctx->IASetVertexBuffers(0, 1, mesh->Vb.GetAddressOf(), &stride, &offset);
				ctx->IASetIndexBuffer(mesh->Ib.Get(), DXGI_FORMAT_R32_UINT, 0);
				ctx->DrawIndexed(mesh->IndexCount, 0, 0);
			}
		}
		const XMMATRIX toTex(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f);
		{
			const XMMATRIX shadowT = lightViewProj * toTex;
			fx->GetVariableByName("gDirShadowTransforms")->AsMatrix()->SetMatrixArray(reinterpret_cast<const float*>(&shadowT), 0, 1);
			const float spheres[16] = { 0, 0, 0, 10000.0f };
			fx->GetVariableByName("gCascadeSpheres")->SetRawValue(spheres, 0, sizeof(spheres));
			const float params[4] = { 1.0f, 50.0f, 1000.0f, 1.0f };
			fx->GetVariableByName("gShadowParams")->AsVector()->SetFloatVector(params);
			const float dirData[16] = { 1.0f, 2.0f };
			fx->GetVariableByName("gDirShadowData")->AsVector()->SetFloatVectorArray(dirData, 0, 4);
			GfxShaderResourceView* maps[1] = { shadowSrv.Get() };
			fx->GetVariableByName("gDirShadowMaps")->AsShaderResource()->SetResourceArray(maps, 0, 1);
		}

		// ---- 장면
		const XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMVectorSet(0.0f, 0.6f, 0.0f, 1), XMVectorSet(0, 1, 0, 0));
		const XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PI / 3.2f, (float)width / height, 0.1f, 100.0f);
		const XMMATRIX viewProj = view * proj;
		GfxRenderTargetView* rtvs[1] = { rtv.Get() };
		ctx->OMSetRenderTargets(1, rtvs, dsv.Get());
		const D3D11_VIEWPORT vp = { 0, 0, (float)width, (float)height, 0, 1 };
		ctx->RSSetViewports(1, &vp);
		const float clear[4] = { 0.08f, 0.09f, 0.11f, 1.0f };
		ctx->ClearRenderTargetView(rtv.Get(), clear);
		ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		ctx->IASetInputLayout(layout.Get());
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		auto draw = [&](const GfxMesh& mesh, CXMMATRIX world, const PbrMaterial& pbr) {
			const XMMATRIX invT = MathHelper::InverseTranspose(world), wvp = world * viewProj, identity = XMMatrixIdentity();
			fx->GetVariableByName("gWorld")->AsMatrix()->SetMatrix(reinterpret_cast<const float*>(&world));
			fx->GetVariableByName("gWorldInvTranspose")->AsMatrix()->SetMatrix(reinterpret_cast<const float*>(&invT));
			fx->GetVariableByName("gWorldViewProj")->AsMatrix()->SetMatrix(reinterpret_cast<const float*>(&wvp));
			fx->GetVariableByName("gTexTransform")->AsMatrix()->SetMatrix(reinterpret_cast<const float*>(&identity));
			fx->GetVariableByName("gView")->AsMatrix()->SetMatrix(reinterpret_cast<const float*>(&view));
			fx->GetVariableByName("gProj")->AsMatrix()->SetMatrix(reinterpret_cast<const float*>(&proj));
			fx->GetVariableByName("gViewProj")->AsMatrix()->SetMatrix(reinterpret_cast<const float*>(&viewProj));
			fx->GetVariableByName("gPbr")->SetRawValue(&pbr, 0, sizeof(pbr));
			const int setting[8] = { 0, 0, 0, 1 };
			fx->GetVariableByName("gShaderSetting")->SetRawValue(setting, 0, sizeof(setting));
			fx->GetVariableByName("gDiffuseMap")->AsShaderResource()->SetResource(checker.Get());
			tech->GetPassByIndex(0)->Apply(0, ctx);
			ctx->IASetVertexBuffers(0, 1, mesh.Vb.GetAddressOf(), &stride, &offset);
			ctx->IASetIndexBuffer(mesh.Ib.Get(), DXGI_FORMAT_R32_UINT, 0);
			ctx->DrawIndexed(mesh.IndexCount, 0, 0);
		};
		PbrMaterial floor;
		floor.BaseColor = XMFLOAT4(0.85f, 0.85f, 0.85f, 1);
		floor.UseBaseMap = 1;
		floor.Tiling = XMFLOAT2(3, 3);
		floor.Smoothness = 0.35f;
		draw(grid, XMMatrixIdentity(), floor);
		PbrMaterial ball;
		ball.UseBaseMap = 1;
		ball.Smoothness = 0.85f;
		draw(sphere, sphereWorld, ball);
		PbrMaterial metal;
		metal.BaseColor = XMFLOAT4(0.95f, 0.55f, 0.35f, 1);
		metal.Metallic = 1.0f;
		metal.Smoothness = 0.7f;
		draw(box, boxWorld, metal);

		// 다음 사용을 위해 풀기 (엔진 렌더러처럼)
		ctx->OMSetRenderTargets(0, nullptr, nullptr);
		fx->GetVariableByName("gDirShadowMaps")->AsShaderResource()->SetResourceArray(std::array<GfxShaderResourceView*, 1>{ nullptr }.data(), 0, 1);
		ctx->Flush();
		const auto t2 = std::chrono::steady_clock::now();
		out.LoadMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
		out.DrawMs = std::chrono::duration<double, std::milli>(t2 - t1).count();

		DirectX::ScratchImage captured;
		if (FAILED(Gfx::CaptureTexture(ctx, colorTex.Get(), captured)))
		{
			error = "capture failed";
			return false;
		}
		const DirectX::Image* img = captured.GetImage(0, 0, 0);
		out.Rgba.resize((size_t)width * height * 4);
		for (int y = 0; y < height; ++y)
			memcpy(out.Rgba.data() + (size_t)y * width * 4, img->pixels + y * img->rowPitch, (size_t)width * 4);
		return true;
	}
}
