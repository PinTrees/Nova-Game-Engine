#pragma once
#include "Component.h"
#include "WorldGen.h"
#include <array>
#include <memory>
#include <vector>

class Terrain;
class TerrainData;
class TerrainLayer;

// 초대형 월드 지형 (World Terrain): WorldGen 의 높이 · 바이옴 함수를 타일 (기본 1 km) 지형으로 보여 준다.
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
	float TileSize = 1024.0f;
	float NearDistance = 1300.0f;     // 이 거리 (타일 사각형까지) 안 = 513
	float MidDistance = 4500.0f;      // 이 거리 안 = 129, 바깥 = 33
	float ColliderDistance = 700.0f;  // 이 거리 안의 513 타일에 Terrain Collider (Play)
	float PixelError = 6.0f;
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

	struct Info { int Tiles = 0, Built = 0, Pending = 0, Res513 = 0, Res129 = 0, Res33 = 0, Colliders = 0; double LastApplyMs = 0; };
	Info GetInfo() const;
	void RegenerateAll();   // 캐시를 버리고 모두 다시

private:
	struct Tile
	{
		int X = 0, Z = 0;
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

	std::vector<Tile> m_Tiles;
	int m_TilesPerSide = 0;
	uint64_t m_BuiltForHash = 0;
	std::vector<std::shared_ptr<TerrainLayer>> m_Layers;
	std::array<std::string, 4> m_LoadedLayers;
	std::shared_ptr<struct WorldTerrainQueue> m_Queue;
	double m_LastApplyMs = 0.0;
	uint32_t m_LastTickFrame = ~0u;
};
REGISTER_COMPONENT(WorldTerrain)
