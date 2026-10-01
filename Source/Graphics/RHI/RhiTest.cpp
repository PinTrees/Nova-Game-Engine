#include "pch.h"
#include "RhiTest.h"
#include "GeometryGenerator.h"
#include "LightHelper.h"
#include "PbrMaterial.h"
#include "MathHelper.h"
#include "PathManager.h"

namespace
{
	struct TestVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT3 Normal;
		XMFLOAT2 Tex;
		XMFLOAT4 Tangent;
	};

	struct TestMesh
	{
		std::unique_ptr<Rhi::Buffer> Vb, Ib;
		uint32_t IndexCount = 0;
	};

	TestMesh MakeMesh(Rhi::Device& dev, const GeometryGenerator::MeshData& data)
	{
		std::vector<TestVertex> v;
		for (const auto& s : data.vertices)
			v.push_back({ s.position, s.normal, s.texC, XMFLOAT4(s.tangentU.x, s.tangentU.y, s.tangentU.z, 1.0f) });
		TestMesh m;
		m.Vb = dev.CreateBuffer({ (uint32_t)(v.size() * sizeof(TestVertex)), Rhi::BindVertex }, v.data());
		m.Ib = dev.CreateBuffer({ (uint32_t)(data.indices.size() * 4), Rhi::BindIndex }, data.indices.data());
		m.IndexCount = (uint32_t)data.indices.size();
		return m;
	}

	// RGBA8 밉 사슬 (2x2 평균)
	void BuildMips(std::vector<std::vector<uint8_t>>& levels, uint32_t size)
	{
		for (uint32_t s = size; s > 1; s /= 2)
		{
			const std::vector<uint8_t>& src = levels.back();
			const uint32_t d = s / 2;
			std::vector<uint8_t> dst((size_t)d * d * 4);
			for (uint32_t y = 0; y < d; ++y)
				for (uint32_t x = 0; x < d; ++x)
					for (int c = 0; c < 4; ++c)
					{
						const int sum = src[((y * 2) * s + x * 2) * 4 + c] + src[((y * 2) * s + x * 2 + 1) * 4 + c] +
							src[((y * 2 + 1) * s + x * 2) * 4 + c] + src[((y * 2 + 1) * s + x * 2 + 1) * 4 + c];
						dst[((size_t)y * d + x) * 4 + c] = (uint8_t)((sum + 2) / 4);
					}
			levels.push_back(std::move(dst));
		}
	}

	std::unique_ptr<Rhi::Texture> MakeChecker(Rhi::Device& dev)
	{
		const uint32_t size = 256;
		std::vector<std::vector<uint8_t>> levels(1, std::vector<uint8_t>((size_t)size * size * 4));
		for (uint32_t y = 0; y < size; ++y)
			for (uint32_t x = 0; x < size; ++x)
			{
				const bool odd = ((x / 32) + (y / 32)) & 1;
				uint8_t r = odd ? 235 : 70, g = odd ? 235 : 75, b = odd ? 225 : 85;
				if (x < 64 && y < 64) { r = 220; g = 40; b = 40; }   // 왼쪽 위 = 빨강 (uv (0,0))
				uint8_t* p = &levels[0][((size_t)y * size + x) * 4];
				p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
			}
		BuildMips(levels, size);
		Rhi::TextureDesc d;
		d.Width = d.Height = size;
		d.MipLevels = (uint32_t)levels.size();
		std::vector<Rhi::SubresourceData> sub;
		for (uint32_t m = 0; m < d.MipLevels; ++m)
			sub.push_back({ levels[m].data(), (size >> m) * 4 });
		return dev.CreateTexture(d, sub.data());
	}

	XMFLOAT3 SkyColor(XMFLOAT3 dir)
	{
		XMVECTOR v = XMVector3Normalize(XMLoadFloat3(&dir));
		XMFLOAT3 n;
		XMStoreFloat3(&n, v);
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
		// +X 쪽 주황 (큐브 면 방향 확인)
		const float w = powf((std::max)(n.x, 0.0f), 4.0f) * 0.7f;
		c = XMFLOAT3(c.x + (1.0f - c.x) * w, c.y + (0.55f - c.y) * w, c.z + (0.2f - c.z) * w);
		return c;
	}

	std::unique_ptr<Rhi::Texture> MakeSkyCube(Rhi::Device& dev)
	{
		const uint32_t size = 128;
		std::vector<std::vector<std::vector<uint8_t>>> faces(6);
		for (int f = 0; f < 6; ++f)
		{
			faces[f].assign(1, std::vector<uint8_t>((size_t)size * size * 4));
			for (uint32_t y = 0; y < size; ++y)
				for (uint32_t x = 0; x < size; ++x)
				{
					// D3D 큐브 면: u 오른쪽, v 아래
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
		Rhi::TextureDesc d;
		d.Type = Rhi::TextureType::Cube;
		d.Width = d.Height = size;
		d.ArraySize = 6;
		d.MipLevels = (uint32_t)faces[0].size();
		std::vector<Rhi::SubresourceData> sub;
		for (int f = 0; f < 6; ++f)
			for (uint32_t m = 0; m < d.MipLevels; ++m)
				sub.push_back({ faces[f][m].data(), (size >> m) * 4 });
		return dev.CreateTexture(d, sub.data());
	}
}

namespace RhiTest
{
	bool RenderLitScene(Rhi::Device& dev, int width, int height, Result& out, std::string& error)
	{
		const auto t0 = std::chrono::steady_clock::now();
		out.Device = dev.Description();
		out.Width = width;
		out.Height = height;
		const std::wstring fxPath = (std::filesystem::path(PathManager::GetI()->GetEnginePathW()) / L"Shaders" / L"32. InstancedBasic.fx").wstring();
		std::unique_ptr<Rhi::Effect> fx = dev.LoadEffect(fxPath, error);
		if (!fx) return false;
		const int tech = fx->FindTechnique("Tech");
		if (tech < 0) { error = "technique Tech not found"; return false; }

		const Rhi::VertexElement elements[] = {
			{ "POSITION", 0, Rhi::VertexFormat::Float3, 0 },
			{ "NORMAL", 0, Rhi::VertexFormat::Float3, 12 },
			{ "TEXCOORD", 0, Rhi::VertexFormat::Float2, 24 },
			{ "TANGENT", 0, Rhi::VertexFormat::Float4, 32 },
		};
		std::unique_ptr<Rhi::InputLayout> layout = dev.CreateInputLayout(elements, _countof(elements), fx.get(), tech, 0, error);
		if (!layout) return false;
		const std::wstring shadowPath = (std::filesystem::path(PathManager::GetI()->GetEnginePathW()) / L"Shaders" / L"26. BuildShadowMap.fx").wstring();
		std::unique_ptr<Rhi::Effect> sfx = dev.LoadEffect(shadowPath, error);
		if (!sfx) return false;
		const int stech = sfx->FindTechnique("BuildShadowMapTech");
		if (stech < 0) { error = "technique BuildShadowMapTech not found"; return false; }
		std::unique_ptr<Rhi::InputLayout> shadowLayout = dev.CreateInputLayout(elements, _countof(elements), sfx.get(), stech, 0, error);
		if (!shadowLayout) return false;

		GeometryGenerator gen;
		GeometryGenerator::MeshData sphereData, boxData, gridData;
		gen.CreateSphere(1.0f, 48, 24, sphereData);
		gen.CreateBox(1.5f, 1.5f, 1.5f, boxData);
		gen.CreateGrid(12.0f, 12.0f, 2, 2, gridData);
		TestMesh sphere = MakeMesh(dev, sphereData), box = MakeMesh(dev, boxData), grid = MakeMesh(dev, gridData);
		std::unique_ptr<Rhi::Texture> checker = MakeChecker(dev), sky = MakeSkyCube(dev);

		Rhi::TextureDesc cd;
		cd.Width = width;
		cd.Height = height;
		cd.RenderTarget = true;
		std::unique_ptr<Rhi::Texture> color = dev.CreateTexture(cd);
		cd.Format = Rhi::Format::D32_Float;
		std::unique_ptr<Rhi::Texture> depth = dev.CreateTexture(cd);
		const uint32_t shadowSize = 1024;
		Rhi::TextureDesc sd;
		sd.Type = Rhi::TextureType::Tex2DArray;
		sd.Width = sd.Height = shadowSize;
		sd.Format = Rhi::Format::D32_Float;
		sd.RenderTarget = true;
		std::unique_ptr<Rhi::Texture> shadow = dev.CreateTexture(sd);
		if (!checker || !sky || !color || !depth || !shadow) { error = "texture creation failed"; return false; }
		const auto t1 = std::chrono::steady_clock::now();

		// ---- 프레임 값
		// 빛 구조체는 생성자가 다 지우지 않는다 → 0 으로 (두 API 에 같은 바이트)
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
		const int dirCount = 1, pointCount = 1, spotCount = 0;
		fx->SetRaw(fx->FindVariable("gDirLights"), dirs, sizeof(dirs));
		fx->SetRaw(fx->FindVariable("gPointLights"), points, sizeof(points));
		fx->SetRaw(fx->FindVariable("gSpotLights"), spots, sizeof(spots));
		fx->SetInt(fx->FindVariable("gDirLightCount"), dirCount);
		fx->SetInt(fx->FindVariable("gPointLightCount"), pointCount);
		fx->SetInt(fx->FindVariable("gSpotLightCount"), spotCount);
		const XMFLOAT3 eye(0.0f, 2.4f, -5.6f);
		fx->SetRaw(fx->FindVariable("gEyePosW"), &eye, 12);
		fx->SetTexture(fx->FindVariable("gCubeMap"), sky.get());

		// ---- 방향광 그림자 맵 (정사영, 캐스케이드 1 개)
		const XMMATRIX sphereWorld = XMMatrixRotationY(0.6f) * XMMatrixTranslation(-1.35f, 1.0f, 0.0f);
		const XMMATRIX boxWorld = XMMatrixRotationY(0.5f) * XMMatrixTranslation(1.45f, 0.75f, 0.4f);
		const XMVECTOR lightDir = XMLoadFloat3(&dirs[0].Direction);
		const XMVECTOR center = XMVectorSet(0.0f, 0.5f, 0.0f, 1.0f);
		const XMMATRIX lightView = XMMatrixLookAtLH(XMVectorSubtract(center, XMVectorScale(lightDir, 12.0f)), center, XMVectorSet(0, 1, 0, 0));
		const XMMATRIX lightViewProj = lightView * XMMatrixOrthographicLH(14.0f, 14.0f, 0.5f, 30.0f);
		{
			XMFLOAT4 toLight;
			XMStoreFloat4(&toLight, XMVectorSetW(XMVectorNegate(lightDir), 0.0f));
			sfx->SetVector(sfx->FindVariable("gShadowLight"), &toLight.x);
			const float bias[2] = { 0.02f, 0.03f };
			sfx->SetRaw(sfx->FindVariable("gShadowBias"), bias, 8);
			XMFLOAT4X4 m;
			XMStoreFloat4x4(&m, lightViewProj);
			sfx->SetMatrix(sfx->FindVariable("gViewProj"), &m._11);
			XMStoreFloat4x4(&m, XMMatrixIdentity());
			sfx->SetMatrix(sfx->FindVariable("gTexTransform"), &m._11);
			dev.ResetState();
			dev.SetRenderTargets(nullptr, 0, shadow.get(), 0);
			dev.SetViewport(0, 0, (float)shadowSize, (float)shadowSize);
			dev.ClearDepth(shadow.get(), 1.0f);
			dev.SetInputLayout(shadowLayout.get());
			dev.SetTopology(Rhi::Topology::TriangleList);
			const Rhi::VarId sWorld = sfx->FindVariable("gWorld"), sWorldInvT = sfx->FindVariable("gWorldInvTranspose");
			for (const auto& [mesh, world] : { std::pair<const TestMesh*, XMMATRIX>{ &sphere, sphereWorld }, { &box, boxWorld } })
			{
				XMStoreFloat4x4(&m, world);
				sfx->SetMatrix(sWorld, &m._11);
				XMStoreFloat4x4(&m, MathHelper::InverseTranspose(world));
				sfx->SetMatrix(sWorldInvT, &m._11);
				sfx->Apply(stech, 0);
				dev.SetVertexBuffer(0, mesh->Vb.get(), sizeof(TestVertex));
				dev.SetIndexBuffer(mesh->Ib.get(), true);
				dev.DrawIndexed(mesh->IndexCount);
			}
		}
		// 텍스처 공간 (u 오른쪽, v 아래 — D3D 규칙 그대로)
		const XMMATRIX toTex(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f);
		{
			XMFLOAT4X4 m;
			XMStoreFloat4x4(&m, lightViewProj * toTex);
			fx->SetMatrixArray(fx->FindVariable("gDirShadowTransforms"), &m._11, 0, 1);
			const float spheres[16] = { 0, 0, 0, 10000.0f };
			fx->SetRaw(fx->FindVariable("gCascadeSpheres"), spheres, sizeof(spheres));
			const float params[4] = { 1.0f, 50.0f, 1000.0f, 1.0f };
			fx->SetRaw(fx->FindVariable("gShadowParams"), params, 16);
			const float dirData[16] = { 1.0f, 2.0f, 1.0f / 1024.0f };   // Strength 1, Medium (3x3), 1 / 맵 크기
			fx->SetRaw(fx->FindVariable("gDirShadowData"), dirData, sizeof(dirData));
			fx->SetTexture(fx->FindVariable("gDirShadowMaps"), shadow.get(), 0);
		}

		const XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMVectorSet(0.0f, 0.6f, 0.0f, 1), XMVectorSet(0, 1, 0, 0));
		const XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PI / 3.2f, (float)width / height, 0.1f, 100.0f);
		const XMMATRIX viewProj = view * proj;
		const Rhi::VarId vWorld = fx->FindVariable("gWorld"), vWorldInvT = fx->FindVariable("gWorldInvTranspose"), vWvp = fx->FindVariable("gWorldViewProj"),
			vTex = fx->FindVariable("gTexTransform"), vView = fx->FindVariable("gView"), vProj = fx->FindVariable("gProj"), vViewProj = fx->FindVariable("gViewProj"),
			vPbr = fx->FindVariable("gPbr"), vSetting = fx->FindVariable("gShaderSetting"), vDiffuse = fx->FindVariable("gDiffuseMap");

		dev.ResetState();
		Rhi::Texture* targets[] = { color.get() };
		dev.SetRenderTargets(targets, 1, depth.get());
		dev.SetViewport(0, 0, (float)width, (float)height);
		const float clear[4] = { 0.08f, 0.09f, 0.11f, 1.0f };
		dev.ClearColor(color.get(), clear);
		dev.ClearDepth(depth.get(), 1.0f);
		dev.SetInputLayout(layout.get());
		dev.SetTopology(Rhi::Topology::TriangleList);

		auto draw = [&](const TestMesh& mesh, CXMMATRIX world, const PbrMaterial& pbr) {
			XMFLOAT4X4 m;
			XMStoreFloat4x4(&m, world);
			fx->SetMatrix(vWorld, &m._11);
			XMStoreFloat4x4(&m, MathHelper::InverseTranspose(world));
			fx->SetMatrix(vWorldInvT, &m._11);
			XMStoreFloat4x4(&m, world * viewProj);
			fx->SetMatrix(vWvp, &m._11);
			XMStoreFloat4x4(&m, XMMatrixIdentity());
			fx->SetMatrix(vTex, &m._11);
			XMStoreFloat4x4(&m, view);
			fx->SetMatrix(vView, &m._11);
			XMStoreFloat4x4(&m, proj);
			fx->SetMatrix(vProj, &m._11);
			XMStoreFloat4x4(&m, viewProj);
			fx->SetMatrix(vViewProj, &m._11);
			fx->SetRaw(vPbr, &pbr, sizeof(pbr));
			const int setting[8] = { 0, 0, 0, 1 };   // gUseShadowMap
			fx->SetRaw(vSetting, setting, sizeof(setting));
			fx->SetTexture(vDiffuse, checker.get());
			fx->Apply(tech, 0);
			dev.SetVertexBuffer(0, mesh.Vb.get(), sizeof(TestVertex));
			dev.SetIndexBuffer(mesh.Ib.get(), true);
			dev.DrawIndexed(mesh.IndexCount);
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

		dev.Finish();
		const auto t2 = std::chrono::steady_clock::now();
		out.LoadMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
		out.DrawMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
		dev.SetRenderTargets(nullptr, 0, nullptr);
		return dev.ReadPixels(color.get(), out.Rgba, error);
	}
}
