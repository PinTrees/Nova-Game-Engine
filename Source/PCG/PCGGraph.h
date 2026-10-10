#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// PCG (Procedural Content Generation) 그래프 — Unreal 의 PCG Graph 와 같은 생각.
//  노드를 이어 규칙을 만든다: 표면에서 점을 뽑고 (Surface Sampler) → 밀도 노이즈 · 높이 · 경사 · 바이옴으로 거르고 →
//  겹치지 않게 솎고 (Self Pruning) · 다른 규칙의 점 근처를 빼고 (Difference) → 돌리고 · 키우고 (Transform Points) →
//  메시를 놓는다 (Static Mesh Spawner). 그래프 = .pcg (JSON) 에셋, PCG Volume 이 월드에 적용한다.
//  값을 바꾸면 Revision 이 올라 PCG Volume 이 바뀐 규칙의 셀을 다시 만든다 (편집 중 바로)
namespace PCG
{
	enum class NodeType : int
	{
		SurfaceSampler = 0,
		DensityNoise,
		HeightFilter,
		SlopeFilter,
		BiomeFilter,
		DensityFilter,
		SelfPruning,
		Difference,
		TransformPoints,
		Merge,
		StaticMeshSpawner,
		Count
	};

	enum class ParamKind { Float, Int, Bool, Enum };
	struct ParamDesc
	{
		const char* Name;      // JSON 이름 (camelCase)
		const char* Label;     // 표시 이름
		ParamKind Kind;
		float Default;
		float Min, Max;
		const char* Options;   // Enum: "A|B|C"
		const char* Tooltip;
	};
	struct NodeDesc
	{
		NodeType Type;
		const char* Name;        // 타입 이름 (JSON)
		const char* Label;       // 표시 이름
		const char* Category;    // Sampler · Filter · Spatial · Spawner
		std::vector<const char*> Inputs;   // 입력 핀 이름 (비면 입력 없음)
		bool Output;
		std::vector<ParamDesc> Params;
		const char* Help;
	};
	const std::vector<NodeDesc>& NodeDescs();
	const NodeDesc* FindDesc(NodeType type);
	const NodeDesc* FindDesc(const std::string& name);

	struct MeshEntry
	{
		std::string Path;      // 모델 (FBX · GLB) — _LODn 노드가 LOD
		float Weight = 1.0f;
	};

	struct Node
	{
		int Id = 0;
		NodeType Type = NodeType::SurfaceSampler;
		float X = 0.0f, Y = 0.0f;           // 그래프 창 위치
		std::string Comment;                // 노드 이름 (표시)
		std::vector<float> Values;          // 파라미터 값 (NodeDesc::Params 차례)
		std::vector<MeshEntry> Meshes;      // Static Mesh Spawner
		bool Enabled = true;

		float Get(const char* name) const;  // 이름으로 (없으면 기본값)
		void Set(const char* name, float v);
	};

	struct Edge
	{
		int From = 0;      // 출력 노드
		int To = 0;        // 입력 노드
		int Pin = 0;       // 입력 핀 번호
	};

	struct Graph
	{
		std::vector<Node> Nodes;
		std::vector<Edge> Edges;
		int NextId = 1;
		uint64_t Revision = 1;      // 바뀔 때마다 (메모리 — PCG Volume 이 본다)

		Node* Find(int id);
		const Node* Find(int id) const;
		Node& Add(NodeType type, float x = 0.0f, float y = 0.0f);
		void Remove(int id);
		bool Connect(int from, int to, int pin);   // 입력 핀은 Merge 만 여럿, 그 밖은 하나 (새 연결이 바꾼다). 순환이면 false
		void Disconnect(int to, int pin, int from = 0);
		std::vector<int> InputsOf(int node, int pin) const;
		std::vector<int> Spawners() const;
		void Touch() { ++Revision; }
		uint64_t ContentHash() const;

		std::string ToJson() const;
		bool FromJson(const std::string& text, std::string* error = nullptr);
	};

	// 에셋 (.pcg): 경로마다 하나 — 그래프 창과 PCG Volume 이 같은 객체를 쓴다 (편집이 바로 보인다)
	std::shared_ptr<Graph> LoadGraph(const std::string& path);
	bool SaveGraph(const std::string& path, const Graph& graph);

	// 모델 경로 → 열린 월드 규칙의 역할 (BigTree · SmallTree · Bush · Fern · Grass · ForestCover · Rock · Debris · Mushroom · Desert · DesertGrass, 모르면 "")
	std::string ClassifyModel(const std::string& path);
	// 프로젝트 Assets 의 모델 (FBX · GLB) 을 역할로 나눈다 (MakeOpenWorldPreset 에 넣는다)
	std::vector<std::pair<std::string, std::string>> ScanProjectModels();

	// 열린 월드 기본 규칙 (숲 · 초원 · 사막 · 바위): models = (종류, 모델 경로) — Tree · Bush · Fern · Grass · Rock · Cactus · Debris · Mushroom
	Graph MakeOpenWorldPreset(const std::vector<std::pair<std::string, std::string>>& models);
}
