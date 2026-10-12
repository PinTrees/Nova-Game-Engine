#pragma once
#include "Component.h"
#include "WorldGen.h"
#include <array>
#include <functional>
#include <string>
#include <memory>
#include <vector>

class Terrain;
class TerrainData;
class TerrainLayer;

// 초대형 월드 지형 (World Terrain): WorldGen 의 높이 · 바이옴 함수를 타일 (기본 1 km) 지형으로 보여 준다.
//  Graph (.terraingraph — Gaea 식 지형 그래프) 를 정하면 그 빌드가 높이 · 스플랫을 정한다: 월드 크기 · 타일 크기 · 가까운 타일 해상도 (예: 50 km · 1 km · 2049)
//  - 32 km 면 32 x 32 = 1024 타일. 카메라 (Scene 뷰 · Game 뷰 둘 다) 와의 거리로 해상도: 가까이 513 (2 m), 중간 129 (8 m), 멀리 33 (32 m)
//  - 높이 · 스플랫은 작업 스레드에서 만들고 (같은 위치 = 같은 값 — 타일 경계가 맞는다), 메인은 프레임마다 예산만큼 바꿔 끼운다.
//    해상도가 다른 이웃 타일과의 틈은 스커트로 가린다
//  - 타일 오브젝트는 숨김 · 저장 안 함 (HideAndDontSave) — 씬에는 이 컴포넌트 (설정) 만 저장된다
//  - 값을 바꾸면 모든 타일을 가까운 것부터 다시 만든다. PCG 는 같은 함수로 높이 · 바이옴을 읽는다 (Revision)
class WorldTerrain : public Component
{
	GENERATE_COMPONENT_BODY(WorldTerrain)
public:
	WorldGen::Settings Settings;
	std::string GraphPath;            // .terraingraph (비우면 위 노이즈 월드)
	float TileSize = 1024.0f;
	float NearDistance = 1300.0f;     // 이 거리 (타일 사각형까지) 안 = 가까운 해상도 (513, 그래프면 그 Tile Resolution)
	float MidDistance = 4500.0f;      // 이 거리 안 = 129, 바깥 = 33
	float ColliderDistance = 700.0f;  // 이 거리 안의 513 타일에 Terrain Collider (Play)
	float PixelError = 6.0f;
	// 먼 타일 묶기: 모든 타일이 Mid Distance 너머인 무리 (G x G 타일 — 50 km 면 5 x 5 km) 는 큰 타일 하나 (257²) 로 그린다.
	//  50 km 월드에서 그리는 지형이 2500 개 → 300 개쯤 (CPU — 타일 · 패스마다 준비). 무리 안 타일은 만들지도 않는다
	bool MergeFarTiles = true;
	int SuperResolution = 257;
	std::array<std::string, 4> LayerPaths = { "Resources\\Packages\\Terrain\\Layers\\Grass.terrainlayer", "Resources\\Packages\\Terrain\\Layers\\Dirt.terrainlayer",
		"Resources\\Packages\\Terrain\\Layers\\Rock.terrainlayer", "Resources\\Packages\\Terrain\\Layers\\Sand.terrainlayer" };

	WorldTerrain();
	~WorldTerrain() override;

	void Start() override { Tick(); }
	void Update() override { Tick(); }
	void _Editor_Update() override { Tick(); }
	void OnDestroy() override;
	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "terrain"; }

	// 설정이 바뀐 번호 (PCG 셀 다시 만들기)
	uint64_t Revision() const { return Settings.Hash(); }
	// 지금 씬의 World Terrain (없으면 nullptr) — PCG 가 높이 · 바이옴을 읽는다
	static WorldTerrain* Active();

	struct Info { int Tiles = 0, Built = 0, Pending = 0, Res513 = 0, Res129 = 0, Res33 = 0, Colliders = 0, NearRes = 513; double LastApplyMs = 0; bool WaitingForBuild = false;
		size_t CpuBytes = 0, GpuBytes = 0; int Groups = 0, SuperShown = 0, Drawn = 0; };   // Res513 = 가까운 해상도 타일 수 (NearRes). Built = 보이는 (만든 타일 또는 큰 타일이 덮은) 타일 수
	Info GetInfo() const;
	void RegenerateAll();   // 캐시를 버리고 모두 다시
	int NearResolution() const { return m_NearRes; }   // 가까운 타일 높이맵 (513 · 그래프면 2049 등)
	// 진단: 그 자리 타일 (있으면) 의 높이 · 해상도 — 높이 함수와 견주기 (CLI world probe)
	bool ProbeTile(float x, float z, float& tileHeight, int& tileRes) const;
	// 그래프 창 열기 (편집기가 넣는다)
	static std::function<void(const std::string&)> s_OpenGraphWindow;

private:
	struct Tile
	{
		int X = 0, Z = 0;
		Vec3 Origin;              // 월드 (모서리)
		float Size = 0.0f;        // 한 변 (m)
		bool Super = false;       // 먼 무리를 덮는 큰 타일
		int Group = -1;           // 보통 타일: 속한 무리 (없으면 -1), 큰 타일: 무리 번호
		bool Shown = false;       // 지금 그린다
		GameObject* Object = nullptr;
		Terrain* TerrainComp = nullptr;
		std::shared_ptr<TerrainData> Data;
		int Res = 0;              // 지금 보이는 해상도 (0 = 아직)
		int Wanted = 0;
		uint64_t Hash = 0;        // 만든 설정
		bool Pending = false;     // 작업 중
		bool Collider = false;
	};
	struct Result;
	void Tick();
	void EnsureTiles();
	void DestroyTiles();
	void Schedule(Tile& t, int res);
	void ApplyResults();
	Vec3 TileOrigin(const Tile& t) const;

	std::vector<Tile> m_Tiles;          // 보통 타일 (side²) 다음에 큰 타일 (무리마다 하나)
	int m_TilesPerSide = 0;
	int m_GroupTiles = 0, m_GroupsPerSide = 0, m_SuperBase = 0;   // 무리 한 변 타일 수 · 무리 수 · 큰 타일 시작 번호
	std::vector<char> m_GroupFar;       // 무리의 모든 타일이 먼가 (프레임마다)
	void SetShown(Tile& t, bool shown);
	uint64_t m_BuiltForHash = 0;
	std::vector<std::shared_ptr<TerrainLayer>> m_Layers;
	std::array<std::string, 4> m_LoadedLayers;
	std::shared_ptr<struct WorldTerrainQueue> m_Queue;
	double m_LastApplyMs = 0.0;
	uint32_t m_LastTickFrame = ~0u;
	int m_NearRes = 513;
	bool m_WaitingForBuild = false;
	void SyncGraph();
};
REGISTER_COMPONENT(WorldTerrain)
