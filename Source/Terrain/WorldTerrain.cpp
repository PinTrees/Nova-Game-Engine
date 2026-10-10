#include "pch.h"
#include "WorldTerrain.h"
#include "Terrain.h"
#include "TerrainData.h"
#include "TerrainCollider.h"
#include "Transform.h"
#include "GameObject.h"
#include "Scene.h"
#include "SceneManager.h"
#include "RenderManager.h"
#include "JobSystem.h"
#include "UnityGUI.h"
#include "Profiler.h"
#include <chrono>
#include <map>
#include <mutex>

// 타일 높이 · 스플랫 결과: 작업 스레드 → 메인 (컴포넌트가 지워져도 큐는 남아 작업이 안전하게 끝난다)
struct WorldTerrainQueue
{
	std::mutex Lock;
	struct Item { int Tile; int Res; uint64_t Hash; std::vector<float> Heights; int ControlRes; std::vector<uint8_t> Control; };
	std::vector<Item> Done;
	int Running = 0;
};

namespace
{
	std::vector<WorldTerrain*>& Registry()
	{
		static std::vector<WorldTerrain*> all;
		return all;
	}

	// 만든 타일 캐시 (설정 · 타일 · 해상도): Play / Stop 으로 컴포넌트가 다시 만들어져도 다시 계산하지 않는다
	struct CacheKey
	{
		uint64_t Hash; int Tile; int Res;
		bool operator<(const CacheKey& o) const { return std::tie(Hash, Tile, Res) < std::tie(o.Hash, o.Tile, o.Res); }
	};
	struct CacheEntry { std::vector<float> Heights; int ControlRes = 0; std::vector<uint8_t> Control; uint64_t Use = 0; };
	std::mutex s_CacheLock;
	std::map<CacheKey, std::shared_ptr<CacheEntry>> s_Cache;
	uint64_t s_CacheUse = 0;
	size_t s_CacheBytes = 0;
	constexpr size_t kCacheBudget = 256ull << 20;   // 256 MB

	void CachePut(const CacheKey& key, const std::vector<float>& h, int controlRes, const std::vector<uint8_t>& c)
	{
		auto e = std::make_shared<CacheEntry>();
		e->Heights = h;
		e->ControlRes = controlRes;
		e->Control = c;
		std::lock_guard<std::mutex> lock(s_CacheLock);
		e->Use = ++s_CacheUse;
		auto& slot = s_Cache[key];
		if (slot)
			s_CacheBytes -= slot->Heights.size() * 4 + slot->Control.size();
		slot = e;
		s_CacheBytes += h.size() * 4 + c.size();
		while (s_CacheBytes > kCacheBudget && s_Cache.size() > 1)
		{
			auto oldest = s_Cache.begin();
			for (auto it = s_Cache.begin(); it != s_Cache.end(); ++it)
				if (it->second->Use < oldest->second->Use)
					oldest = it;
			s_CacheBytes -= oldest->second->Heights.size() * 4 + oldest->second->Control.size();
			s_Cache.erase(oldest);
		}
	}

	std::shared_ptr<CacheEntry> CacheGet(const CacheKey& key)
	{
		std::lock_guard<std::mutex> lock(s_CacheLock);
		auto it = s_Cache.find(key);
		if (it == s_Cache.end())
			return nullptr;
		it->second->Use = ++s_CacheUse;
		return it->second;
	}

	// 카메라 위치 (뷰 행렬의 역)
	Vec3 EyeOf(CXMMATRIX view)
	{
		const XMMATRIX inv = XMMatrixInverse(nullptr, view);
		return Vec3(XMVectorGetX(inv.r[3]), XMVectorGetY(inv.r[3]), XMVectorGetZ(inv.r[3]));
	}

	// 타일 하나 만들기 (작업 스레드): 높이 (정규화) + 스플랫
	void BuildTile(const WorldGen::Settings& s, float ox, float oz, float tile, int res, std::vector<float>& heights, int& controlRes, std::vector<uint8_t>& control)
	{
		heights.resize((size_t)res * res);
		const float step = tile / (res - 1);
		for (int z = 0; z < res; ++z)
			for (int x = 0; x < res; ++x)
				heights[(size_t)z * res + x] = WorldGen::Height01(s, ox + x * step, oz + z * step);
		controlRes = (std::max)(16, (res - 1) / 2);
		control.resize((size_t)controlRes * controlRes * 4);
		const float cstep = tile / controlRes;
		const float hstep = tile / (res - 1);
		for (int z = 0; z < controlRes; ++z)
			for (int x = 0; x < controlRes; ++x)
			{
				const float wx = ox + (x + 0.5f) * cstep, wz = oz + (z + 0.5f) * cstep;
				// 높이 · 경사는 높이맵에서 (다시 계산하지 않는다)
				const float fx = (x + 0.5f) * cstep / hstep, fz = (z + 0.5f) * cstep / hstep;
				const int ix = std::clamp((int)fx, 0, res - 2), iz = std::clamp((int)fz, 0, res - 2);
				const float h00 = heights[(size_t)iz * res + ix], h10 = heights[(size_t)iz * res + ix + 1], h01 = heights[(size_t)(iz + 1) * res + ix];
				const float dx = (h10 - h00) * s.MaxHeight / hstep, dz = (h01 - h00) * s.MaxHeight / hstep;
				const float slopeCos = 1.0f / sqrtf(1.0f + dx * dx + dz * dz);
				const WorldGen::Biome b = WorldGen::BiomeAt(s, wx, wz, h00, slopeCos);
				WorldGen::Splat(b, wx, wz, s.Seed, &control[((size_t)z * controlRes + x) * 4]);
			}
	}
}

WorldTerrain::WorldTerrain()
{
	m_InspectorTitleName = "World Terrain";
	m_Queue = std::make_shared<WorldTerrainQueue>();
	Registry().push_back(this);
}

WorldTerrain::~WorldTerrain()
{
	auto& r = Registry();
	r.erase(std::remove(r.begin(), r.end(), this), r.end());
}

WorldTerrain* WorldTerrain::Active()
{
	for (WorldTerrain* w : Registry())
		if (w->IsEnabled() && w->GetGameObject() && w->GetGameObject()->IsActiveInHierarchy())
			return w;
	return nullptr;
}

void WorldTerrain::OnDestroy()
{
	DestroyTiles();
}

Vec3 WorldTerrain::TileOrigin(const Tile& t) const
{
	const float half = m_TilesPerSide * TileSize * 0.5f;
	return Vec3(-half + t.X * TileSize, 0.0f, -half + t.Z * TileSize);
}

void WorldTerrain::DestroyTiles()
{
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	// 씬이 지워지는 중이면 (~Scene 이 목록 표시를 먼저 푼다) 그 씬이 타일도 지운다 — 여기서 또 지우지 않는다
	for (Tile& t : m_Tiles)
		if (t.Object && scene && GameObject::IsAlive(t.Object) && t.Object->SceneListed && !t.Object->PendingDelete)
			scene->DestroyGameObject(t.Object);
	m_Tiles.clear();
	m_TilesPerSide = 0;
}

void WorldTerrain::EnsureTiles()
{
	const int side = (std::max)(1, (int)std::round(Settings.WorldSize / (std::max)(64.0f, TileSize)));
	if (side == m_TilesPerSide && !m_Tiles.empty())
		return;
	DestroyTiles();
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (scene == nullptr || m_pGameObject == nullptr)
		return;
	m_TilesPerSide = side;
	m_Tiles.resize((size_t)side * side);
	for (int z = 0; z < side; ++z)
		for (int x = 0; x < side; ++x)
		{
			Tile& t = m_Tiles[(size_t)z * side + x];
			t.X = x;
			t.Z = z;
		}
}

void WorldTerrain::Schedule(Tile& t, int res)
{
	const int index = (int)(&t - m_Tiles.data());
	const uint64_t hash = Settings.Hash() ^ ((uint64_t)std::llround(TileSize) << 1);
	t.Pending = true;
	// 캐시에 있으면 바로 (다음 ApplyResults)
	if (auto e = CacheGet({ hash, index, res }))
	{
		std::lock_guard<std::mutex> lock(m_Queue->Lock);
		m_Queue->Done.push_back({ index, res, hash, e->Heights, e->ControlRes, e->Control });
		return;
	}
	const Vec3 o = TileOrigin(t);
	const WorldGen::Settings s = Settings;
	const float tile = TileSize;
	std::shared_ptr<WorldTerrainQueue> q = m_Queue;
	{
		std::lock_guard<std::mutex> lock(q->Lock);
		++q->Running;
	}
	Jobs::Run([q, s, o, tile, res, index, hash]() {
		WorldTerrainQueue::Item item;
		item.Tile = index;
		item.Res = res;
		item.Hash = hash;
		BuildTile(s, o.x, o.z, tile, res, item.Heights, item.ControlRes, item.Control);
		CachePut({ hash, index, res }, item.Heights, item.ControlRes, item.Control);
		std::lock_guard<std::mutex> lock(q->Lock);
		q->Done.push_back(std::move(item));
		--q->Running;
	}, nullptr, Jobs::Priority::Background, "World Terrain Tile");
}

void WorldTerrain::ApplyResults()
{
	std::vector<WorldTerrainQueue::Item> done;
	{
		std::lock_guard<std::mutex> lock(m_Queue->Lock);
		done.swap(m_Queue->Done);
	}
	if (done.empty())
		return;
	// 가까운 것 (높은 해상도) 부터, 프레임마다 예산 (4 ms) — 남은 것은 다음 프레임
	std::sort(done.begin(), done.end(), [](const auto& a, const auto& b) { return a.Res > b.Res; });
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	const auto t0 = std::chrono::steady_clock::now();
	const uint64_t want = Settings.Hash() ^ ((uint64_t)std::llround(TileSize) << 1);
	if (m_LoadedLayers != LayerPaths)
	{
		m_Layers.clear();
		for (const std::string& p : LayerPaths)
			if (auto layer = TerrainLayer::Load(p))
				m_Layers.push_back(layer);
		m_LoadedLayers = LayerPaths;
		for (Tile& t : m_Tiles)
			if (t.Data)
				t.Data->Layers = m_Layers;
	}
	size_t i = 0;
	for (; i < done.size(); ++i)
	{
		if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() > 4.0)
			break;
		WorldTerrainQueue::Item& it = done[i];
		if (it.Tile < 0 || it.Tile >= (int)m_Tiles.size())
			continue;
		Tile& t = m_Tiles[(size_t)it.Tile];
		t.Pending = false;
		if (it.Hash != want)
			continue;   // 그사이 설정이 바뀌었다 — 다음 Tick 이 다시
		if (t.Object == nullptr || !GameObject::IsAlive(t.Object))
		{
			if (scene == nullptr)
				continue;
			t.Object = new GameObject("World Tile " + std::to_string(t.X) + "_" + std::to_string(t.Z));
			t.Object->SetHideAndDontSave(true);
			t.TerrainComp = t.Object->AddComponent<Terrain>();
			t.Data = std::make_shared<TerrainData>();
			t.Data->Layers = m_Layers;
			t.TerrainComp->SetTerrainData(t.Data);
			scene->AddRootGameObject(t.Object);
			t.Object->SetParent(m_pGameObject, false);
			t.Object->GetTransform()->SetLocalPosition(TileOrigin(t));
			t.Collider = false;
		}
		t.Data->SetGenerated(it.Res, Vec3(TileSize, Settings.MaxHeight, TileSize), std::move(it.Heights), it.ControlRes, std::move(it.Control));
		t.TerrainComp->SetPixelError(PixelError);
		t.TerrainComp->SetSkirtDepth((std::max)(4.0f, Settings.MaxHeight * 0.02f));
		t.Res = it.Res;
		t.Hash = it.Hash;
	}
	if (i < done.size())
	{
		// 못 한 것은 되돌려 놓는다 (다음 프레임 먼저)
		std::lock_guard<std::mutex> lock(m_Queue->Lock);
		for (; i < done.size(); ++i)
			m_Queue->Done.push_back(std::move(done[i]));
	}
	m_LastApplyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void WorldTerrain::Tick()
{
	// 한 프레임에 한 번 (Start · Update 가 같은 프레임에 부를 수 있다)
	const uint32_t frame = (uint32_t)ImGui::GetFrameCount();
	if (frame == m_LastTickFrame)
		return;
	m_LastTickFrame = frame;
	if (!IsEnabled() || m_pGameObject == nullptr)
		return;
	PROFILE_SCOPE("WorldTerrain.Update");
	EnsureTiles();
	ApplyResults();

	// 보는 곳: Scene 뷰 카메라 · Game 뷰 카메라 (편집 중에도 둘 다 그린다)
	RenderManager* rm = RenderManager::GetI();
	const Vec3 eyes[2] = { EyeOf(rm->EditorCameraViewMatrix), EyeOf(rm->CameraViewMatrix) };
	const uint64_t hash = Settings.Hash() ^ ((uint64_t)std::llround(TileSize) << 1);
	struct Want { int Tile; int Res; float Dist; };
	static std::vector<Want> wants;
	wants.clear();
	for (Tile& t : m_Tiles)
	{
		const Vec3 o = TileOrigin(t);
		float d = FLT_MAX;
		for (const Vec3& e : eyes)
		{
			const float dx = (std::max)({ o.x - e.x, 0.0f, e.x - (o.x + TileSize) });
			const float dz = (std::max)({ o.z - e.z, 0.0f, e.z - (o.z + TileSize) });
			d = (std::min)(d, sqrtf(dx * dx + dz * dz));
		}
		t.Wanted = d < NearDistance ? 513 : (d < MidDistance ? 129 : 33);
		if (!t.Pending && (t.Res != t.Wanted || t.Hash != hash))
			wants.push_back({ (int)(&t - m_Tiles.data()), t.Wanted, d });

		// 물리: 가까운 고해상도 타일만 Terrain Collider (Play)
		const bool wantCollider = Application::IsPlaying() && t.Res == 513 && d < ColliderDistance && t.Object && GameObject::IsAlive(t.Object);
		if (wantCollider && !t.Collider)
		{
			t.Object->AddComponent<TerrainCollider>();
			t.Collider = true;
		}
	}
	// 가까운 것부터, 동시에 작업 8 개까지 (나머지는 다음 프레임)
	std::sort(wants.begin(), wants.end(), [](const Want& a, const Want& b) { return a.Dist < b.Dist; });
	int running;
	{
		std::lock_guard<std::mutex> lock(m_Queue->Lock);
		running = m_Queue->Running;
	}
	for (const Want& w : wants)
	{
		if (running >= 8)
			break;
		Schedule(m_Tiles[(size_t)w.Tile], w.Res);
		++running;
	}
	m_BuiltForHash = hash;
}

WorldTerrain::Info WorldTerrain::GetInfo() const
{
	Info info;
	info.Tiles = (int)m_Tiles.size();
	for (const Tile& t : m_Tiles)
	{
		info.Built += t.Res > 0;
		info.Pending += t.Pending;
		info.Res513 += t.Res == 513;
		info.Res129 += t.Res == 129;
		info.Res33 += t.Res == 33;
		info.Colliders += t.Collider;
	}
	info.LastApplyMs = m_LastApplyMs;
	return info;
}

void WorldTerrain::RegenerateAll()
{
	{
		std::lock_guard<std::mutex> lock(s_CacheLock);
		s_Cache.clear();
		s_CacheBytes = 0;
	}
	for (Tile& t : m_Tiles)
		t.Hash = 0;
}

void WorldTerrain::OnInspectorGUI()
{
	using namespace UnityGUI;
	WorldGen::Settings& s = Settings;
	Int("Seed", &s.Seed);
	Slider("World Size (m)", &s.WorldSize, 2048.0f, 65536.0f);
	Slider("Max Height (m)", &s.MaxHeight, 50.0f, 4000.0f);
	if (Foldout("Shape", 1, true, false))
	{
		Slider("Continent Scale", &s.ContinentScale, 1000.0f, 40000.0f);
		Slider("Mountains", &s.Mountains, 0.0f, 1.0f);
		Slider("Mountain Scale", &s.MountainScale, 300.0f, 10000.0f);
		Slider("Hills", &s.Hills, 0.0f, 1.0f);
		Slider("Hill Scale", &s.HillScale, 50.0f, 3000.0f);
		Slider("Detail", &s.Detail, 0.0f, 0.05f);
		Slider("Detail Scale", &s.DetailScale, 5.0f, 500.0f);
		Slider("Warp", &s.Warp, 0.0f, 1.0f);
	}
	if (Foldout("Biomes", 2, true, false))
	{
		Slider("Biome Scale", &s.BiomeScale, 500.0f, 20000.0f);
		Slider("Forest", &s.Forest, 0.0f, 1.0f);
		Slider("Desert", &s.Desert, 0.0f, 1.0f);
	}
	if (Foldout("Streaming", 3, true, false))
	{
		Slider("Tile Size", &TileSize, 256.0f, 4096.0f);
		Slider("Near (513)", &NearDistance, 200.0f, 5000.0f);
		Slider("Mid (129)", &MidDistance, 500.0f, 20000.0f);
		Slider("Collider Distance", &ColliderDistance, 0.0f, 3000.0f);
		Slider("Pixel Error", &PixelError, 1.0f, 50.0f);
	}
	const Info info = GetInfo();
	char text[256];
	snprintf(text, sizeof(text), "%d tiles (%.0f x %.0f km): built %d, generating %d — 513: %d, 129: %d, 33: %d, colliders %d",
		info.Tiles, s.WorldSize / 1000.0f, s.WorldSize / 1000.0f, info.Built, info.Pending, info.Res513, info.Res129, info.Res33, info.Colliders);
	HelpBox(text, false);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
	if (ImGui::Button("Regenerate", ImVec2(140, 0)))
		RegenerateAll();
}

GENERATE_COMPONENT_FUNC_TOJSON(WorldTerrain)
{
	json j;
	SERIALIZE_TYPE(j, WorldTerrain);
	j["enabled"] = m_Enabled;
	const WorldGen::Settings& s = Settings;
	j["seed"] = s.Seed;
	j["worldSize"] = s.WorldSize;
	j["maxHeight"] = s.MaxHeight;
	j["continentScale"] = s.ContinentScale;
	j["mountainScale"] = s.MountainScale;
	j["mountains"] = s.Mountains;
	j["hillScale"] = s.HillScale;
	j["hills"] = s.Hills;
	j["detailScale"] = s.DetailScale;
	j["detail"] = s.Detail;
	j["warp"] = s.Warp;
	j["biomeScale"] = s.BiomeScale;
	j["desert"] = s.Desert;
	j["forest"] = s.Forest;
	j["tileSize"] = TileSize;
	j["nearDistance"] = NearDistance;
	j["midDistance"] = MidDistance;
	j["colliderDistance"] = ColliderDistance;
	j["pixelError"] = PixelError;
	j["layers"] = { LayerPaths[0], LayerPaths[1], LayerPaths[2], LayerPaths[3] };
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(WorldTerrain)
{
	m_Enabled = j.value("enabled", true);
	WorldGen::Settings& s = Settings;
	s.Seed = j.value("seed", s.Seed);
	s.WorldSize = j.value("worldSize", s.WorldSize);
	s.MaxHeight = j.value("maxHeight", s.MaxHeight);
	s.ContinentScale = j.value("continentScale", s.ContinentScale);
	s.MountainScale = j.value("mountainScale", s.MountainScale);
	s.Mountains = j.value("mountains", s.Mountains);
	s.HillScale = j.value("hillScale", s.HillScale);
	s.Hills = j.value("hills", s.Hills);
	s.DetailScale = j.value("detailScale", s.DetailScale);
	s.Detail = j.value("detail", s.Detail);
	s.Warp = j.value("warp", s.Warp);
	s.BiomeScale = j.value("biomeScale", s.BiomeScale);
	s.Desert = j.value("desert", s.Desert);
	s.Forest = j.value("forest", s.Forest);
	TileSize = j.value("tileSize", TileSize);
	NearDistance = j.value("nearDistance", NearDistance);
	MidDistance = j.value("midDistance", MidDistance);
	ColliderDistance = j.value("colliderDistance", ColliderDistance);
	PixelError = j.value("pixelError", PixelError);
	if (j.contains("layers") && j["layers"].is_array())
		for (size_t i = 0; i < 4 && i < j["layers"].size(); ++i)
			LayerPaths[i] = j["layers"][i].get<std::string>();
}
