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
#include "TerrainGraph.h"
#include <chrono>
#include <map>
#include <mutex>

// 타일 높이 · 스플랫 결과: 작업 스레드 → 메인 (컴포넌트가 지워져도 큐는 남아 작업이 안전하게 끝난다)
struct WorldTerrainQueue
{
	std::mutex Lock;
	struct Item { int Tile; int Res; uint64_t Hash; std::vector<float> Heights; int ControlRes; std::vector<uint8_t> Control; std::vector<uint8_t> Color; };
	std::vector<Item> Done;
	int Running = 0;
	std::atomic<bool> Cancel{ false };   // World Terrain 이 사라졌다 — 줄 선 타일 작업은 바로 끝낸다 (끄기 · 장면 닫기가 2049 타일을 기다리지 않게)
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
	struct CacheEntry { std::vector<float> Heights; int ControlRes = 0; std::vector<uint8_t> Control; std::vector<uint8_t> Color; uint64_t Use = 0; };
	std::mutex s_CacheLock;
	std::map<CacheKey, std::shared_ptr<CacheEntry>> s_Cache;
	uint64_t s_CacheUse = 0;
	size_t s_CacheBytes = 0;
	constexpr size_t kCacheBudget = 384ull << 20;   // 384 MB (2049 타일 = 높이 16.8 + 스플랫 4 MB → 18 개쯤)

	void CachePut(const CacheKey& key, const std::vector<float>& h, int controlRes, const std::vector<uint8_t>& c, const std::vector<uint8_t>& color)
	{
		auto e = std::make_shared<CacheEntry>();
		e->Heights = h;
		e->ControlRes = controlRes;
		e->Control = c;
		e->Color = color;
		std::lock_guard<std::mutex> lock(s_CacheLock);
		e->Use = ++s_CacheUse;
		auto& slot = s_Cache[key];
		if (slot)
			s_CacheBytes -= slot->Heights.size() * 4 + slot->Control.size() + slot->Color.size();
		slot = e;
		s_CacheBytes += h.size() * 4 + c.size() + color.size();
		while (s_CacheBytes > kCacheBudget && s_Cache.size() > 1)
		{
			auto oldest = s_Cache.begin();
			for (auto it = s_Cache.begin(); it != s_Cache.end(); ++it)
				if (it->second->Use < oldest->second->Use)
					oldest = it;
			s_CacheBytes -= oldest->second->Heights.size() * 4 + oldest->second->Control.size() + oldest->second->Color.size();
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

	// 줄 범위 [b, e) 를 (큰 타일은 작업 스레드 여럿으로 — 2049 타일 하나 = 420 만 표본)
	template <class F>
	void ForRows(int rows, bool parallel, F&& fn)
	{
		if (parallel)
			Jobs::ParallelFor(rows, 16, fn, "World Terrain Rows", Jobs::Priority::Normal);
		else
			fn(0, rows);
	}

	// 타일 하나 만들기 (작업 스레드): 높이 (정규화) + 스플랫. 표본 간격 (step) 보다 잔 디테일은 건너뛴다 (먼 타일)
	bool BuildTile(const WorldGen::Settings& s, float ox, float oz, float tile, int res, std::vector<float>& heights, int& controlRes, std::vector<uint8_t>& control,
		std::vector<uint8_t>& color, const std::atomic<bool>& cancel)
	{
		heights.resize((size_t)res * res);
		const float step = tile / (res - 1);
		const bool big = res >= 1025;
		// 끄는 중이면 (Jobs::Shutdown 이 남은 타일 잡을 메인 스레드에서 돈다 — 2049² 타일 여럿에 1 분 걸렸다) 줄마다 보고 바로 끝낸다
		ForRows(res, big, [&](int b, int e) {
			for (int z = b; z < e; ++z)
			{
				if (cancel || Jobs::ShuttingDown())
					return;
				for (int x = 0; x < res; ++x)
					heights[(size_t)z * res + x] = WorldGen::Height01(s, ox + x * step, oz + z * step, step);
			}
		});
		if (cancel || Jobs::ShuttingDown())
			return false;
		controlRes = (std::max)(16, (res - 1) / 2);
		control.resize((size_t)controlRes * controlRes * 4);
		{
			uint8_t probe[4];
			if (s.External && s.External->Color(ox, oz, probe))
				color.assign((size_t)controlRes * controlRes * 4, 0);
			else
				color.clear();
		}
		const float cstep = tile / controlRes;
		const float hstep = tile / (res - 1);
		ForRows(controlRes, big, [&](int b, int e) {
		if (cancel)
			return;
		for (int z = b; z < e; ++z)
			for (int x = 0; x < controlRes; ++x)
			{
				const float wx = ox + (x + 0.5f) * cstep, wz = oz + (z + 0.5f) * cstep;
				// 높이 · 경사는 높이맵에서 (다시 계산하지 않는다)
				const float fx = (x + 0.5f) * cstep / hstep, fz = (z + 0.5f) * cstep / hstep;
				const int ix = std::clamp((int)fx, 0, res - 2), iz = std::clamp((int)fz, 0, res - 2);
				const float h00 = heights[(size_t)iz * res + ix], h10 = heights[(size_t)iz * res + ix + 1], h01 = heights[(size_t)(iz + 1) * res + ix];
				const float dx = (h10 - h00) * s.MaxHeight / hstep, dz = (h01 - h00) * s.MaxHeight / hstep;
				const float slopeCos = 1.0f / sqrtf(1.0f + dx * dx + dz * dz);
				if (s.External)
				{
					s.External->Splat(wx, wz, h00, slopeCos, &control[((size_t)z * controlRes + x) * 4]);
					// 컬러 맵 (Terrain Graph 의 SatMap) — 있는지는 위에서 한 번 (줄마다 병렬이라 여기서 바꾸지 않는다)
					if (!color.empty())
						s.External->Color(wx, wz, &color[((size_t)z * controlRes + x) * 4]);
				}
				else
				{
					const WorldGen::Biome bio = WorldGen::BiomeAt(s, wx, wz, h00, slopeCos);
					WorldGen::Splat(bio, wx, wz, s.Seed, &control[((size_t)z * controlRes + x) * 4]);
				}
			}
		});
		return !cancel;
	}
}

std::function<void(const std::string&)> WorldTerrain::s_OpenGraphWindow;

// 그래프 빌드를 따른다: 월드 · 타일 크기, 가까운 해상도, 레이어. 빌드가 없으면 타일을 만들지 않는다 (빌드를 기다린다)
void WorldTerrain::SyncGraph()
{
	if (GraphPath.empty())
	{
		if (Settings.External)
			Settings.External.reset();
		m_NearRes = 513;
		m_WaitingForBuild = false;
		return;
	}
	std::shared_ptr<const TerrainGraph::Built> built = TerrainGraph::GetBuilt(GraphPath, false);
	m_WaitingForBuild = built == nullptr;
	if (!built)
	{
		Settings.External.reset();
		return;
	}
	if (Settings.External != built)
	{
		Settings.External = built;
		Settings.WorldSize = built->World.WorldSize;
		Settings.MaxHeight = built->World.MaxHeight;
		TileSize = built->World.TileSize;
		const int r = built->World.TileResolution;
		m_NearRes = (r == 4097 || r == 2049 || r == 1025 || r == 513 || r == 257) ? r : 2049;
		for (int i = 0; i < 4; ++i)
			if (!built->Layers[i].Layer.empty())
				LayerPaths[(size_t)i] = built->Layers[i].Layer;
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
	m_Queue->Cancel = true;
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

bool WorldTerrain::ProbeTile(float x, float z, float& tileHeight, int& tileRes) const
{
	for (const Tile& t : m_Tiles)
	{
		const Vec3 o = TileOrigin(t);
		if (x < o.x || z < o.z || x >= o.x + t.Size || z >= o.z + t.Size || !t.Data || t.Res == 0 || !t.Shown)
			continue;
		tileHeight = o.y + t.Data->GetHeight(x - o.x, z - o.z);
		tileRes = t.Res;
		return true;
	}
	return false;
}

Vec3 WorldTerrain::TileOrigin(const Tile& t) const
{
	return t.Origin;
}

void WorldTerrain::SetShown(Tile& t, bool shown)
{
	t.Shown = shown;
	if (t.TerrainComp && t.Object && GameObject::IsAlive(t.Object))
		t.TerrainComp->SetDraw(shown);
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
	m_GroupTiles = m_GroupsPerSide = m_SuperBase = 0;
	m_GroupFar.clear();
}

void WorldTerrain::EnsureTiles()
{
	const int side = (std::max)(1, (int)std::round(Settings.WorldSize / (std::max)(64.0f, TileSize)));
	// 무리 한 변: 한 변 타일 수를 나누는 5 · 4 · 6 · 8 · 3 중 먼저 (50 → 5, 32 → 4). 없으면 4 (끝 줄 타일은 무리 없이)
	int group = 0;
	if (MergeFarTiles && side >= 6)
	{
		for (int g : { 5, 4, 6, 8, 3 })
			if (side % g == 0) { group = g; break; }
		if (group == 0)
			group = 4;
	}
	const size_t wantCount = (size_t)side * side + (group ? (size_t)(side / group) * (side / group) : 0);
	if (side == m_TilesPerSide && group == m_GroupTiles && m_Tiles.size() == wantCount && fabsf(m_Tiles[0].Size - TileSize) < 0.01f)
		return;
	DestroyTiles();
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (scene == nullptr || m_pGameObject == nullptr)
		return;
	m_TilesPerSide = side;
	m_GroupTiles = group;
	m_GroupsPerSide = group ? side / group : 0;
	m_SuperBase = side * side;
	m_Tiles.resize(wantCount);
	const float half = side * TileSize * 0.5f;
	for (int z = 0; z < side; ++z)
		for (int x = 0; x < side; ++x)
		{
			Tile& t = m_Tiles[(size_t)z * side + x];
			t.X = x;
			t.Z = z;
			t.Size = TileSize;
			t.Origin = Vec3(-half + x * TileSize, 0.0f, -half + z * TileSize);
			t.Group = group && x / group < m_GroupsPerSide && z / group < m_GroupsPerSide ? (z / group) * m_GroupsPerSide + x / group : -1;
		}
	for (int gz = 0; gz < m_GroupsPerSide; ++gz)
		for (int gx = 0; gx < m_GroupsPerSide; ++gx)
		{
			Tile& t = m_Tiles[(size_t)m_SuperBase + gz * m_GroupsPerSide + gx];
			t.X = gx;
			t.Z = gz;
			t.Super = true;
			t.Group = gz * m_GroupsPerSide + gx;
			t.Size = TileSize * group;
			t.Origin = Vec3(-half + gx * group * TileSize, 0.0f, -half + gz * group * TileSize);
		}
	m_GroupFar.assign((size_t)m_GroupsPerSide * m_GroupsPerSide, 0);
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
		m_Queue->Done.push_back({ index, res, hash, e->Heights, e->ControlRes, e->Control, e->Color });
		return;
	}
	const Vec3 o = TileOrigin(t);
	const WorldGen::Settings s = Settings;
	const float tile = t.Size;
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
		if (!q->Cancel && !Jobs::ShuttingDown() && BuildTile(s, o.x, o.z, tile, res, item.Heights, item.ControlRes, item.Control, item.Color, q->Cancel) && !q->Cancel)
			CachePut({ hash, index, res }, item.Heights, item.ControlRes, item.Control, item.Color);
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
			t.Object = new GameObject((t.Super ? "World Far Tile " : "World Tile ") + std::to_string(t.X) + "_" + std::to_string(t.Z));
			t.Object->SetHideAndDontSave(true);
			t.TerrainComp = t.Object->AddComponent<Terrain>();
			t.Data = std::make_shared<TerrainData>();
			t.Data->Layers = m_Layers;
			t.TerrainComp->SetTerrainData(t.Data);
			scene->AddRootGameObject(t.Object);
			t.Object->SetParent(m_pGameObject, false);
			t.Object->GetTransform()->SetLocalPosition(TileOrigin(t));
			t.Collider = false;
			t.TerrainComp->SetDraw(false);   // 보이기는 Tick 이 정한다 (무리 · 큰 타일 바꿔 끼우기)
			t.Shown = false;
		}
		t.Data->SetGenerated(it.Res, Vec3(t.Size, Settings.MaxHeight, t.Size), std::move(it.Heights), it.ControlRes, std::move(it.Control));
		// 컬러 맵: 해상도가 바뀌면 텍스처를 새로 (비웠다 넣는다)
		t.Data->SetColorMap({});
		if (!it.Color.empty())
			t.Data->SetColorMap(std::move(it.Color));
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
	SyncGraph();
	if (m_WaitingForBuild)
	{
		DestroyTiles();
		return;
	}
	EnsureTiles();
	ApplyResults();

	// 보는 곳: Scene 뷰 카메라 · Game 뷰 카메라 (편집 중에도 둘 다 그린다)
	RenderManager* rm = RenderManager::GetI();
	const Vec3 eyes[2] = { EyeOf(rm->EditorCameraViewMatrix), EyeOf(rm->CameraViewMatrix) };
	const uint64_t hash = Settings.Hash() ^ ((uint64_t)std::llround(TileSize) << 1);
	struct Want { int Tile; int Res; float Dist; };
	static std::vector<Want> wants;
	wants.clear();
	auto distOf = [&](const Tile& t) {
		const Vec3 o = TileOrigin(t);
		float d = FLT_MAX;
		for (const Vec3& e : eyes)
		{
			const float dx = (std::max)({ o.x - e.x, 0.0f, e.x - (o.x + t.Size) });
			const float dz = (std::max)({ o.z - e.z, 0.0f, e.z - (o.z + t.Size) });
			d = (std::min)(d, sqrtf(dx * dx + dz * dz));
		}
		return d;
	};
	// 무리가 먼가: 큰 타일 사각형까지 거리가 Mid Distance 너머 = 안의 모든 타일이 33 일 것
	for (int gi = 0; gi < (int)m_GroupFar.size(); ++gi)
		m_GroupFar[(size_t)gi] = distOf(m_Tiles[(size_t)m_SuperBase + gi]) >= MidDistance ? 1 : 0;
	// 보이기: 먼 무리는 큰 타일이 다 만들어졌으면 큰 타일, 아니면 안 타일. 가까워진 무리는 안 타일이 다 만들어질 때까지 큰 타일을 둔다
	for (int gi = 0; gi < (int)m_GroupFar.size(); ++gi)
	{
		Tile& sup = m_Tiles[(size_t)m_SuperBase + gi];
		const bool superReady = sup.Res > 0 && sup.Hash == hash;
		bool membersReady = true;
		const int gx = gi % m_GroupsPerSide, gz = gi / m_GroupsPerSide;
		for (int z = 0; z < m_GroupTiles && membersReady; ++z)
			for (int x = 0; x < m_GroupTiles && membersReady; ++x)
			{
				const Tile& t = m_Tiles[(size_t)(gz * m_GroupTiles + z) * m_TilesPerSide + gx * m_GroupTiles + x];
				membersReady = t.Res > 0 && t.Hash == hash;
			}
		const bool useSuper = superReady && (m_GroupFar[(size_t)gi] || !membersReady);
		if (sup.Shown != useSuper)
			SetShown(sup, useSuper);
		for (int z = 0; z < m_GroupTiles; ++z)
			for (int x = 0; x < m_GroupTiles; ++x)
			{
				Tile& t = m_Tiles[(size_t)(gz * m_GroupTiles + z) * m_TilesPerSide + gx * m_GroupTiles + x];
				const bool show = !useSuper && t.Res > 0;
				if (t.Shown != show)
					SetShown(t, show);
			}
	}
	for (Tile& t : m_Tiles)
	{
		if (t.Super)
		{
			// 큰 타일은 늘 만들어 둔다 (멀어지면 바로 바꿔 끼우게 — 무리마다 257² 하나, 모두 합쳐 수십 MB)
			t.Wanted = SuperResolution;
			if (!t.Pending && (t.Res != t.Wanted || t.Hash != hash))
				wants.push_back({ (int)(&t - m_Tiles.data()), t.Wanted, distOf(t) + MidDistance });
			continue;
		}
		const float d = distOf(t);
		const bool inFarGroup = t.Group >= 0 && m_GroupFar[(size_t)t.Group];
		t.Wanted = d < NearDistance ? m_NearRes : (d < MidDistance ? 129 : 33);
		if (t.Group < 0 && !t.Shown && t.Res > 0)
			SetShown(t, true);   // 무리 없는 끝 줄 타일
		if (inFarGroup)
		{
			t.Wanted = t.Res;     // 큰 타일이 덮는다 — 만들지 않는다
			// 큰 타일이 보이면 안 타일은 놓는다 (카메라가 돌아다녀도 메모리가 늘지 않게 — 다시 오면 캐시에서)
			if (!t.Pending && t.Res > 0 && m_Tiles[(size_t)m_SuperBase + t.Group].Shown && t.Object)
			{
				Scene* scene = SceneManager::GetI()->GetCurrentScene();
				if (scene && GameObject::IsAlive(t.Object) && t.Object->SceneListed && !t.Object->PendingDelete)
					scene->DestroyGameObject(t.Object);
				t.Object = nullptr;
				t.TerrainComp = nullptr;
				t.Data.reset();
				t.Res = t.Wanted = 0;
				t.Shown = false;
				t.Collider = false;
			}
		}
		if (!t.Pending && t.Wanted > 0 && (t.Res != t.Wanted || t.Hash != hash))
			wants.push_back({ (int)(&t - m_Tiles.data()), t.Wanted, d });

		// 물리: 가까운 고해상도 타일만 Terrain Collider (Play)
		const bool wantCollider = Application::IsPlaying() && t.Res == m_NearRes && d < ColliderDistance && t.Object && GameObject::IsAlive(t.Object);
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
	info.Tiles = m_SuperBase > 0 ? m_SuperBase : (int)m_Tiles.size();
	info.Groups = m_GroupsPerSide * m_GroupsPerSide;
	for (const Tile& t : m_Tiles)
	{
		info.Drawn += t.Shown;
		if (t.Super)
		{
			info.SuperShown += t.Shown;
			info.Pending += t.Pending;
			if (t.Shown)
				info.Built += m_GroupTiles * m_GroupTiles;   // 큰 타일이 덮은 타일
			if (t.Data)
			{
				info.CpuBytes += t.Data->CpuBytes();
				info.GpuBytes += t.Data->GpuBytes();
			}
			continue;
		}
		const bool covered = t.Group >= 0 && m_SuperBase + t.Group < (int)m_Tiles.size() && m_Tiles[(size_t)m_SuperBase + t.Group].Shown;
		info.Built += t.Res > 0 && !covered;
		info.Pending += t.Pending;
		info.Res513 += t.Res == m_NearRes;
		info.Res129 += t.Res == 129;
		info.Res33 += t.Res == 33;
		info.Colliders += t.Collider;
		if (t.Data)
		{
			info.CpuBytes += t.Data->CpuBytes();
			info.GpuBytes += t.Data->GpuBytes();
		}
	}
	info.NearRes = m_NearRes;
	info.WaitingForBuild = m_WaitingForBuild;
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
	TextField("Graph", &GraphPath);
	if (!GraphPath.empty())
	{
		const TerrainGraph::BuildStatus st = TerrainGraph::GetBuildStatus(GraphPath);
		char text[256];
		if (st.Running)
			snprintf(text, sizeof(text), "Building %d²: %s %.0f %%", st.Resolution, st.Stage.c_str(), st.Progress * 100.0f);
		else if (m_WaitingForBuild)
			snprintf(text, sizeof(text), "The graph is not built yet - open it and press Build.");
		else
			snprintf(text, sizeof(text), "Terrain Graph world: %.0f x %.0f km, tiles %.0f m, near %d²", s.WorldSize / 1000.0f, s.WorldSize / 1000.0f, TileSize, m_NearRes);
		HelpBox(text, false);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
		if (ImGui::Button("Open Terrain Graph", ImVec2(160, 0)) && s_OpenGraphWindow)
			s_OpenGraphWindow(GraphPath);
	}
	if (!GraphPath.empty())
		ImGui::BeginDisabled();
	Int("Seed", &s.Seed);
	Slider("World Size (m)", &s.WorldSize, 2048.0f, 65536.0f);
	Slider("Max Height (m)", &s.MaxHeight, 50.0f, 4000.0f);
	// 노이즈 월드의 모양 · 바이옴 값 — 그래프 월드면 숨긴다 (그래프 창에서)
	if (GraphPath.empty() && Foldout("Shape", 1, true, false))
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
	if (GraphPath.empty() && Foldout("Biomes", 2, true, false))
	{
		Slider("Biome Scale", &s.BiomeScale, 500.0f, 20000.0f);
		Slider("Forest", &s.Forest, 0.0f, 1.0f);
		Slider("Desert", &s.Desert, 0.0f, 1.0f);
	}
	if (!GraphPath.empty())
		ImGui::EndDisabled();
	if (Foldout("Streaming", 3, true, false))
	{
		Slider("Tile Size", &TileSize, 256.0f, 4096.0f);
		Slider("Near Distance", &NearDistance, 200.0f, 5000.0f);
		Slider("Mid (129)", &MidDistance, 500.0f, 20000.0f);
		Slider("Collider Distance", &ColliderDistance, 0.0f, 3000.0f);
		Slider("Pixel Error", &PixelError, 1.0f, 50.0f);
		Toggle("Merge Far Tiles", &MergeFarTiles);   // 먼 무리 = 큰 타일 하나
	}
	const Info info = GetInfo();
	char text[256];
	snprintf(text, sizeof(text), "%d tiles (%.0f x %.0f km): built %d, generating %d — %d: %d, 129: %d, 33: %d, colliders %d. Memory %.0f MB, GPU %.0f MB",
		info.Tiles, s.WorldSize / 1000.0f, s.WorldSize / 1000.0f, info.Built, info.Pending, info.NearRes, info.Res513, info.Res129, info.Res33, info.Colliders,
		info.CpuBytes / 1048576.0, info.GpuBytes / 1048576.0);
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
	j["mergeFarTiles"] = MergeFarTiles;
	j["layers"] = { LayerPaths[0], LayerPaths[1], LayerPaths[2], LayerPaths[3] };
	if (!GraphPath.empty())
		j["graph"] = GraphPath;
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
	MergeFarTiles = j.value("mergeFarTiles", MergeFarTiles);
	GraphPath = j.value("graph", std::string());
	if (j.contains("layers") && j["layers"].is_array())
		for (size_t i = 0; i < 4 && i < j["layers"].size(); ++i)
			LayerPaths[i] = j["layers"][i].get<std::string>();
}
