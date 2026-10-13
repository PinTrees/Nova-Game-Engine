#include "pch.h"
#include "GrassField.h"
#include "WorldTerrain.h"
#include "TerrainData.h"
#include "TerrainBiomeTextures.h"
#include "TerrainTextureGen.h"
#include "Effects.h"
#include "RenderManager.h"
#include "UMaterial.h"
#include "Profiler.h"
#include "WeatherState.h"
#include "SceneCulling.h"
#include "GameObject.h"
#include <chrono>
#include <unordered_map>

namespace
{
	using Clock = std::chrono::steady_clock;

	// 바이옴 (TerrainBiomeTextures 순서) 마다 풀: x 밀도 배율, y 키 (m), z 마름 (끝이 짚 색), w 잎 너비 배율
	const XMFLOAT4 kBiomeGrass[TerrainBiomeTextures::kBiomes] = {
		{ 1.0f, 0.45f, 0.1f, 1.0f },    // Temperate Forest — 무릎 아래 풀밭
		{ 0.55f, 0.32f, 0.25f, 0.9f },  // Boreal — 이끼 사이 성긴 풀
		{ 0.6f, 0.18f, 0.45f, 0.8f },   // Tundra / Alpine — 낮고 마른 풀
		{ 0.07f, 0.25f, 0.9f, 0.9f },   // Hot Desert — 드문 마른 풀
		{ 0.95f, 0.8f, 0.85f, 1.0f },   // Savanna / Steppe — 키 큰 마른 풀
		{ 0.7f, 0.65f, 0.05f, 1.4f },   // Tropical Rainforest — 넓은 잎 풀
		{ 0.7f, 0.38f, 0.55f, 0.9f },   // Mediterranean — 여름에 마른 풀
		{ 0.85f, 0.7f, 0.15f, 1.2f },   // Wetland — 갈대 같은 풀
	};
	constexpr float kBaseSpacing = 0.4f;    // 가장 촘촘한 격자 (m) — 약 6 덩어리 / m² (덩어리마다 잎 9)
	constexpr float kNear = 10.0f;          // 이 거리 안은 가장 촘촘 (m)
	constexpr float kLevelSlope = 0.75f;    // 거리 단계: 0.75 · log2(d / 10) — 밀도 ∝ (10/d)^1.5
	constexpr float kMinPatch = 16.0f;
	constexpr int kMaxPoints = 6400;        // 패치 하나의 점 (덩어리) 수 위 한도
	constexpr int64_t kMaxVerticesPerFrame = 16'000'000;   // 프레임마다 풀 정점 셰이더 위 한도

	struct GrassVars
	{
		FxEffect* Fx = nullptr;
		FxTechnique* Tech = nullptr;
		FxVar *Patch = nullptr, *Level = nullptr, *Cam = nullptr, *Falloff = nullptr, *Wind = nullptr, *Biome = nullptr, *BiomeSlice = nullptr;
		FxVar *Size = nullptr, *Origin = nullptr, *HeightMap = nullptr, *Control = nullptr, *Climate = nullptr, *Albedos = nullptr;
		FxVar *TBiome = nullptr, *TBiomeClimate = nullptr, *TBiomeSlices = nullptr, *TSliceParams = nullptr, *TClimateRes = nullptr;
		bool Bound = false;
		bool Valid() const { return Tech && Tech->IsValid(); }
		void Bind()
		{
			Bound = true;
			Fx = Effects::InstancedBasicFX->GetFX();
			Tech = Fx->GetTechniqueByName("GrassFieldTech");
			auto vec = [&](const char* n) { return Fx->GetVariableByName(n)->AsVector(); };
			auto srv = [&](const char* n) { return Fx->GetVariableByName(n)->AsShaderResource(); };
			Patch = vec("gGrassPatch"); Level = vec("gGrassLevel"); Cam = vec("gGrassCam"); Falloff = vec("gGrassFalloff"); Wind = vec("gGrassWind");
			Biome = vec("gGrassBiome"); BiomeSlice = vec("gGrassBiomeSlice");
			Size = vec("gTerrainSize"); Origin = vec("gTerrainOrigin");
			HeightMap = srv("gTerrainHeightMap"); Control = srv("gTerrainControl"); Climate = srv("gTerrainClimate"); Albedos = srv("gTerrainAlbedos");
			TBiome = vec("gTerrainBiome"); TBiomeClimate = vec("gTerrainBiomeClimate"); TBiomeSlices = vec("gTerrainBiomeSlices");
			TSliceParams = vec("gTerrainSliceParams"); TClimateRes = vec("gTerrainClimateRes");
		}
	};
	GrassVars s_Vars;

	void SetVec(FxVar* v, const XMFLOAT4& f)
	{
		if (v && v->IsValid())
			v->SetFloatVector(&f.x);
	}
	void SetSrv(FxVar* v, GfxShaderResourceView* s)
	{
		if (v && v->IsValid())
			v->SetResource(s);
	}

	// 타일의 풀 덮임 (32 × 32 칸마다 풀 레이어 무게의 최댓값) — 풀이 없는 패치를 건너뛴다
	struct Coverage
	{
		unsigned Revision = ~0u;
		int Channel = -1;
		float Max[32 * 32] = {};
		uint32_t LastUse = 0;
	};
	std::unordered_map<const TerrainData*, Coverage> s_Coverage;

	const Coverage& CoverageOf(const TerrainData& d, int channel, uint32_t frame)
	{
		Coverage& c = s_Coverage[&d];
		c.LastUse = frame;
		if (c.Revision == d.ControlRevision && c.Channel == channel)
			return c;
		c.Revision = d.ControlRevision;
		c.Channel = channel;
		const int res = d.ControlResolution;
		for (float& m : c.Max)
			m = 0.0f;
		if (res <= 0 || d.Control.size() < (size_t)res * res * 4)
		{
			for (float& m : c.Max)
				m = 1.0f;
			return c;
		}
		for (int z = 0; z < res; ++z)
			for (int x = 0; x < res; ++x)
			{
				const uint8_t* p = &d.Control[((size_t)z * res + x) * 4];
				const int sum = p[0] + p[1] + p[2] + p[3];
				const float w = sum > 0 ? p[channel] / (float)sum : 0.0f;
				float& m = c.Max[(size_t)(z * 32 / res) * 32 + x * 32 / res];
				m = (std::max)(m, w);
			}
		return c;
	}

	float LevelAt(float dist) { return (std::max)(0.0f, kLevelSlope * log2f((std::max)(dist, 1.0f) / kNear)); }

	GrassField::Stats s_Stats[2];
	float s_Time = 0.0f;
}

namespace GrassField
{
	const Stats& LastStats(bool editor) { return s_Stats[editor ? 1 : 0]; }

	void DrawMain(bool editor)
	{
		WorldTerrain* world = WorldTerrain::Active();
		if (world && !world->Grass)
			world = nullptr;
		Stats st;
		if (!world)
		{
			s_Stats[editor ? 1 : 0] = st;
			return;
		}
		PROFILE_SCOPE("Grass Field");
		PROFILE_GPU("Grass Field");
		const auto t0 = Clock::now();
		if (!s_Vars.Bound)
			s_Vars.Bind();
		GrassVars& v = s_Vars;
		if (!v.Valid())
			return;
		RenderManager* rm = RenderManager::GetI();
		const XMMATRIX view = editor ? rm->EditorCameraViewMatrix : rm->CameraViewMatrix;
		const XMMATRIX viewProj = editor ? rm->EditorCameraViewProjectionMatrix : rm->CameraViewProjectionMatrix;
		const XMVECTOR camPosV = XMMatrixInverse(nullptr, view).r[3];
		const Vec3 cam(XMVectorGetX(camPosV), XMVectorGetY(camPosV), XMVectorGetZ(camPosV));
		const uint32_t frame = SceneCulling::FrameIndex();
		{
			static const auto start = Clock::now();
			s_Time = std::chrono::duration<float>(Clock::now() - start).count();
		}
		const float D = std::clamp(world->GrassDistance, 20.0f, 1500.0f);
		const float base = kBaseSpacing / sqrtf(std::clamp(world->GrassDensity, 0.1f, 4.0f));

		std::vector<WorldTerrain::TileView> tiles;
		world->NearTiles(cam.x, cam.z, D, tiles);
		if (tiles.empty())
		{
			s_Stats[editor ? 1 : 0] = st;
			return;
		}

		// 본 패스 조명 (DetailRenderer 와 같은 설정)
		{
			static const XMMATRIX toTex(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f);
			Effects::InstancedBasicFX->SetViewProj(viewProj);
			if (auto* m = v.Fx->GetVariableByName("gViewProjTex")->AsMatrix(); m && m->IsValid())
			{
				const XMMATRIX vt = viewProj * toTex;
				m->SetMatrix(reinterpret_cast<const float*>(&vt));
			}
			ShaderSetting setting = UMaterial::GetDefault()->GetShaderSetting();
			setting.UseShadowMap = 1;
			setting.UseSsaoMap = 1;
			Effects::InstancedBasicFX->SetShaderSetting(setting);
		}
		XMFLOAT4 planes[6];
		{
			XMFLOAT4X4 m;
			XMStoreFloat4x4(&m, viewProj);
			auto plane = [&](float a, float b, float c, float d) { const float l = sqrtf(a * a + b * b + c * c); return XMFLOAT4(a / l, b / l, c / l, d / l); };
			planes[0] = plane(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41);
			planes[1] = plane(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41);
			planes[2] = plane(m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42);
			planes[3] = plane(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42);
			planes[4] = plane(m._13, m._23, m._33, m._43);
			planes[5] = plane(m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43);
		}
		auto visible = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
			for (const XMFLOAT4& p : planes)
				if (p.x * (p.x >= 0 ? x1 : x0) + p.y * (p.y >= 0 ? y1 : y0) + p.z * (p.z >= 0 ? z1 : z0) + p.w < 0.0f)
					return false;
			return true;
		};

		const float yaw = XMConvertToRadians(30.0f);
		const float windK = 0.35f + 0.65f * std::clamp(WeatherState::Get().WindStrength, 0.0f, 3.0f);
		SetVec(v.Cam, XMFLOAT4(cam.x, cam.y, cam.z, D));
		SetVec(v.Falloff, XMFLOAT4(kNear, kLevelSlope, 0.0f, s_Time));
		SetVec(v.Wind, XMFLOAT4(sinf(yaw), cosf(yaw), 0.35f * windK, 1.0f / 9.0f));
		XMFLOAT4 biome[TerrainBiomeTextures::kBiomes];
		for (int i = 0; i < TerrainBiomeTextures::kBiomes; ++i)
		{
			biome[i] = kBiomeGrass[i];
			biome[i].y *= std::clamp(world->GrassHeight, 0.2f, 3.0f);
		}
		v.Biome->SetFloatVectorArray(&biome[0].x, 0, TerrainBiomeTextures::kBiomes);

		GfxContext* dc = Application::GetI()->GetDeviceContext();
		ComPtr<GfxDepthStencilState> prevDSS;
		UINT prevRef = 0;
		dc->OMGetDepthStencilState(prevDSS.GetAddressOf(), &prevRef);
		ComPtr<GfxRasterizerState> prevRS;
		dc->RSGetState(prevRS.GetAddressOf());
		dc->IASetInputLayout(nullptr);
		GfxBuffer* nullVB = nullptr;
		UINT zero = 0;
		dc->IASetVertexBuffers(0, 1, &nullVB, &zero, &zero);
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		const XMFLOAT4* biomeClimate = nullptr;
		XMFLOAT4 climate[TerrainBiomeTextures::kBiomes];
		{
			const TerrainBiomeTextures::Biome* bs = TerrainBiomeTextures::Biomes();
			for (int i = 0; i < TerrainBiomeTextures::kBiomes; ++i)
				climate[i] = XMFLOAT4(bs[i].Temperature, bs[i].Moisture, 1.0f / bs[i].TempWidth, 1.0f / bs[i].MoistWidth);
			biomeClimate = climate;
		}

		// 가까운 타일부터 (예산을 가까운 풀에 먼저)
		std::sort(tiles.begin(), tiles.end(), [&](const WorldTerrain::TileView& a, const WorldTerrain::TileView& b) {
			auto dist = [&](const WorldTerrain::TileView& t) {
				const float dx = (std::max)({ t.Origin.x - cam.x, 0.0f, cam.x - (t.Origin.x + t.Size) }), dz = (std::max)({ t.Origin.z - cam.z, 0.0f, cam.z - (t.Origin.z + t.Size) });
				return dx * dx + dz * dz;
			};
			return dist(a) < dist(b);
		});
		for (const WorldTerrain::TileView& tv : tiles)
		{
			TerrainData& d = *tv.Data;
			if (!d.BiomeTextures || !d.ClimateSRV() || !d.HeightSRV() || !d.ControlSRV())
				continue;
			// 풀 역할 (레이어 이름이 Grass 인 것) · 바이옴 재질 세트
			std::array<int, 4> kinds = { -1, -1, -1, -1 };
			int grassRole = -1;
			const int layerCount = (std::min)((int)d.Layers.size(), TerrainData::kMaxLayers);
			for (int i = 0; i < layerCount; ++i)
			{
				kinds[(size_t)i] = d.Layers[i] ? (int)TerrainTextureGen::KindFromName(d.Layers[i]->Name()) : -1;
				if (kinds[(size_t)i] == (int)TerrainTextureGen::Kind::Grass && grassRole < 0)
					grassRole = i;
			}
			const TerrainBiomeTextures::Set* set = grassRole >= 0 ? TerrainBiomeTextures::Get(kinds, d.BiomeTextureSize) : nullptr;
			if (!set)
				continue;
			const Coverage& cov = CoverageOf(d, grassRole, frame);
			SetVec(v.Size, XMFLOAT4(d.Size.x, d.Size.y, d.Size.z, (float)d.HeightmapResolution));
			SetVec(v.Origin, XMFLOAT4(tv.Origin.x, tv.Origin.y, tv.Origin.z, 0.0f));
			SetSrv(v.HeightMap, d.HeightSRV());
			SetSrv(v.Control, d.ControlSRV());
			SetSrv(v.Climate, d.ClimateSRV());
			SetSrv(v.Albedos, set->Albedo);
			SetVec(v.TBiome, XMFLOAT4(1.0f, (float)TerrainBiomeTextures::kBiomes, 0.2f, 900.0f));
			v.TBiomeClimate->SetFloatVectorArray(&biomeClimate[0].x, 0, TerrainBiomeTextures::kBiomes);
			v.TBiomeSlices->SetFloatVectorArray(&set->Slices[0][0], 0, TerrainBiomeTextures::kBiomes);
			v.TSliceParams->SetFloatVectorArray(&set->Params[0][0], 0, TerrainBiomeTextures::kMaxSlices);
			SetVec(v.TClimateRes, XMFLOAT4((float)d.ClimateResolution, 0, 0, 0));
			SetVec(v.Falloff, XMFLOAT4(kNear, kLevelSlope, (float)grassRole, s_Time));
			SetVec(v.BiomeSlice, XMFLOAT4((float)grassRole, 0, 0, 0));
			const TerrainData::Node& root = d.GetNode(0, 0, 0);
			const float yMin = tv.Origin.y + root.MinHeight - 1.0f, yMax = tv.Origin.y + root.MaxHeight + 3.0f;

			// 쿼드트리: 가까울수록 작게 — 잎 패치는 그 거리의 단계로 (점 간격 2^Lp)
			struct Node { float X, Z, S; };
			struct Leaf { Node N; float Dist; int Lp, Side; };
			std::vector<Leaf> leaves;
			std::vector<Node> stack = { { tv.Origin.x, tv.Origin.z, tv.Size } };
			while (!stack.empty())
			{
				const Node n = stack.back();
				stack.pop_back();
				const float dx = (std::max)({ n.X - cam.x, 0.0f, cam.x - (n.X + n.S) }), dz = (std::max)({ n.Z - cam.z, 0.0f, cam.z - (n.Z + n.S) });
				const float nearDist = sqrtf(dx * dx + dz * dz);
				if (nearDist > D)
					continue;
				if (!visible(n.X, yMin, n.Z, n.X + n.S, yMax, n.Z + n.S))
				{
					++st.Culled;
					continue;
				}
				// 풀 덮임 (칸마다 최댓값)
				{
					const float cs = tv.Size / 32.0f;
					const int cx0 = std::clamp((int)((n.X - tv.Origin.x) / cs), 0, 31), cx1 = std::clamp((int)((n.X + n.S - tv.Origin.x) / cs - 0.001f), 0, 31);
					const int cz0 = std::clamp((int)((n.Z - tv.Origin.z) / cs), 0, 31), cz1 = std::clamp((int)((n.Z + n.S - tv.Origin.z) / cs - 0.001f), 0, 31);
					float m = 0.0f;
					for (int z = cz0; z <= cz1; ++z)
						for (int x = cx0; x <= cx1; ++x)
							m = (std::max)(m, cov.Max[z * 32 + x]);
					if (m < 0.15f)
					{
						++st.Culled;
						continue;
					}
				}
				const int lp = std::clamp((int)floorf(LevelAt(nearDist)), 0, 6);
				const float stepM = base * (float)(1 << lp);
				const int side = (int)ceilf(n.S / stepM) + 1;
				if (n.S > kMinPatch && (nearDist < n.S * 1.2f || side * side > kMaxPoints))
				{
					const float h = n.S * 0.5f;
					stack.push_back({ n.X, n.Z, h });
					stack.push_back({ n.X + h, n.Z, h });
					stack.push_back({ n.X, n.Z + h, h });
					stack.push_back({ n.X + h, n.Z + h, h });
					continue;
				}
				leaves.push_back({ n, nearDist, lp, side });
			}
			std::sort(leaves.begin(), leaves.end(), [](const Leaf& a, const Leaf& b) { return a.Dist < b.Dist; });
			for (const Leaf& lf : leaves)
			{
				const Node& n = lf.N;
				const int lp = lf.Lp, side = lf.Side;
				// 잎 수 · 마디: 가까운 단계일수록 많이
				const int blades = lp == 0 ? 9 : (lp == 1 ? 7 : (lp == 2 ? 5 : 4));
				const int segs = lp == 0 ? 3 : (lp <= 2 ? 2 : 1);
				SetVec(v.Patch, XMFLOAT4(n.X, n.Z, n.S, (float)side));
				SetVec(v.Level, XMFLOAT4((float)lp, base, (float)blades, (float)segs));
				v.Tech->GetPassByIndex(0)->Apply(0, dc);
				const UINT verts = (UINT)(blades * segs * 6);
				if ((int64_t)st.Vertices + (int64_t)verts * side * side > kMaxVerticesPerFrame)
				{
					++st.Culled;   // 프레임 예산 (정점 셰이더 수) — 넘으면 남은 패치는 그리지 않는다 (GPU 멈춤 방지)
					continue;
				}
				dc->DrawInstanced(verts, (UINT)(side * side), 0, 0);
				++st.Patches;
				st.Points += side * side;
				st.Vertices += (int)verts * side * side;
			}
		}
		// 오래 안 쓴 타일의 덮임
		if ((frame & 255u) == 0)
			for (auto it = s_Coverage.begin(); it != s_Coverage.end();)
				it = frame - it->second.LastUse > 600 ? s_Coverage.erase(it) : std::next(it);
		dc->OMSetDepthStencilState(prevDSS.Get(), prevRef);
		dc->RSSetState(prevRS.Get());
		SetSrv(v.Climate, nullptr);
		st.CpuMs = std::chrono::duration<float, std::milli>(Clock::now() - t0).count();
		s_Stats[editor ? 1 : 0] = st;
	}
}
