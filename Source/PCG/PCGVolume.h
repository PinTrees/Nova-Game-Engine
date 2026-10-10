#pragma once
#include "Component.h"
#include "PCGGraph.h"
#include <functional>
#include <memory>
#include <unordered_map>

// PCG Volume (Unreal 의 PCG Volume + PCG Component): PCG 그래프를 월드에 적용한다.
//  - 영역 = World Terrain 전체 (Whole World) 또는 Transform 위치 · 크기 (x · z) 의 상자
//  - Runtime Generation (Partitioned): 스포너마다 격자 셀 (Cell Size), 카메라 (Scene 뷰 · Game 뷰) 에서 Cull Distance 안의 셀만
//    작업 스레드에서 만든다 — 가까운 것부터. 멀어진 셀은 버린다. 32 km 월드도 보이는 곳만
//  - 규칙 (그래프) · World Terrain 값 · Seed 가 바뀌면 셀을 다시 만든다 (그동안 예전 셀을 그린다 — 깜빡임 없이 바뀐다)
//  - 그리기: GameObject 없이 셀마다 GPU 인스턴스 버퍼 (한 번 올림), LOD 는 셀 거리로. Mesh Renderer 묶음과 같은 길 (그림자 · 깊이 · 재질)
class PCGVolume : public Component
{
	GENERATE_COMPONENT_BODY(PCGVolume)
public:
	std::string GraphPath;          // .pcg
	int Seed = 1;
	bool WholeWorld = true;
	float DistanceScale = 1.0f;     // 모든 스포너 Cull Distance 배율 (품질 · 성능)

	PCGVolume();
	~PCGVolume() override;

	void Start() override { Tick(); }
	void Update() override { Tick(); }
	void _Editor_Update() override { Tick(); }
	void OnDestroy() override;
	void OnInspectorGUI() override;
	void OnDrawGizmos() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "terrain_paint"; }

	static const std::vector<PCGVolume*>& All();
	std::shared_ptr<PCG::Graph> GetGraph() const;
	void Regenerate();               // 모든 셀을 버리고 다시
	nlohmann::json Info() const;

	struct CellKey
	{
		int Spawner = 0, CX = 0, CZ = 0;
		bool operator==(const CellKey& o) const { return Spawner == o.Spawner && CX == o.CX && CZ == o.CZ; }
	};
	struct CellKeyHash { size_t operator()(const CellKey& k) const { return ((size_t)k.Spawner * 73856093u) ^ ((size_t)(uint32_t)k.CX * 19349663u) ^ ((size_t)(uint32_t)k.CZ * 83492791u); } };
	struct Cell;
	struct SpawnerInfo
	{
		int Id = 0;
		std::string Name;
		float Cull = 300.0f, CellSize = 64.0f, ShadowDistance = 150.0f, LodBias = 1.0f;
		bool Shadows = true;
		std::vector<std::string> Meshes;
	};

	// 그래프 창 열기 (편집기가 넣는다 — 게임 빌드에는 없음)
	static std::function<void(const std::string&)> s_OpenGraphWindow;

	// 그리기 (MeshBatcher 바깥 묶음) — 모든 PCG Volume
	static void EmitBatches(int pass, bool editor, void* out);

private:
	void Tick();
	void Clear();
	void ApplyResults();
	bool InBounds(float x0, float z0, float x1, float z1) const;

	std::unordered_map<CellKey, std::unique_ptr<Cell>, CellKeyHash> m_Cells;
	std::vector<SpawnerInfo> m_Spawners;
	std::shared_ptr<const PCG::Graph> m_Snapshot;
	std::shared_ptr<PCG::Graph> m_Graph;
	std::string m_GraphFor;
	uint64_t m_GraphRevision = 0;
	uint64_t m_Key = 0;
	std::shared_ptr<struct PCGQueue> m_Queue;
	uint32_t m_Frame = 0;
	uint32_t m_LastTickFrame = ~0u;
	double m_GenMs = 0.0;
	int m_Generated = 0;
	float m_BoundsX0 = 0, m_BoundsZ0 = 0, m_BoundsX1 = 0, m_BoundsZ1 = 0;
	mutable int m_LastDrawn = 0, m_LastBatches = 0;
};
REGISTER_COMPONENT(PCGVolume)
