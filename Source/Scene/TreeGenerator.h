#pragma once
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

// NOVA 나무 생성기 (SpeedTree / Weber-Penn 방식의 절차적 나무)
//  - 줄기 → 가지(1~3 단계)를 재귀로 키운다. 가지는 휘어진 관(링을 이은 원통), 자식 가지는 황금각(137.5°)으로 돌려 붙인다
//  - 수관(Crown) 모양 함수가 높이에 따라 가지 길이를 정한다 (원뿔, 구, 반구, 원기둥, 불꽃 …)
//  - 마지막 단계 가지에 잎 카드를 붙인다. 잎 모양은 셰이더가 식(SDF)으로 그리므로 텍스처가 없다
//  - 정점마다 바람 가중치/위상을 넣어 셰이더가 계층 흔들림을 계산한다 (44. TreeCommon.fx)
struct TreeParams
{
	enum class Crown { Conical, Spherical, Hemispherical, Cylindrical, TaperedCylindrical, Flame, InverseConical };
	enum class LeafShape { Broad, Oval, Needle };

	int Seed = 1;
	// ---- 줄기
	float Height = 9.0f;
	float Radius = 0.32f;
	float TipRadius = 0.25f;       // 끝 반지름 / 밑동 반지름
	float Flare = 0.6f;            // 뿌리 쪽 퍼짐
	float Gnarl = 0.2f;            // 휘어짐
	float Lean = 0.0f;             // 기울기 (도)
	int RadialSegments = 10;
	Crown CrownShape = Crown::Spherical;

	// ---- 가지 단계 (1 = 줄기에서 난 가지, 2 = 그 가지에서 난 가지 …)
	struct Level
	{
		int Count = 10;            // 부모 하나에 붙는 수
		float Start = 0.3f;        // 부모 길이의 이 비율부터 붙는다
		float Angle = 50.0f;       // 부모와의 각 (도)
		float AngleVariance = 12.0f;
		float Length = 0.5f;       // 부모 길이에 대한 비율
		float LengthVariance = 0.2f;
		float Gravity = 0.15f;     // 아래로 처짐
		float Up = 0.1f;           // 위로 향하는 경향
		float Radius = 0.5f;       // 부모의 그 지점 반지름에 대한 비율
		float Gnarl = 0.25f;
	};
	int Levels = 2;
	Level L[3];

	// ---- 잎 (카드)
	LeafShape Leaf = LeafShape::Broad;
	int LeafCards = 7;             // 마지막 단계 가지 하나에 붙는 카드 수
	int LeavesPerCard = 5;         // 카드 한 장에 그리는 잎 수 (셰이더)
	float LeafCardSize = 0.8f;     // 카드 한 변 (m)
	float LeafStart = 0.3f;        // 가지 길이의 이 비율부터 잎

	void ApplyPreset(int preset);  // 0 Oak, 1 Pine, 2 Birch, 3 Bush
	static const char* const* PresetNames(int& count);

	nlohmann::json ToJson() const;
	void FromJson(const nlohmann::json& j);
};

// 나무 한 그루의 정점 (80 바이트). 셰이더 TreeVertexIn 과 같은 배치
struct TreeVertex
{
	XMFLOAT3 Pos;
	XMFLOAT3 Normal;
	XMFLOAT2 UV;
	XMFLOAT4 Wind;      // x 줄기 가중, y 1차 가지 가중, z 2차 가지 가중, w 잎 떨림 가중
	XMFLOAT4 Axis;      // xyz 가지 방향 / 카드 위쪽, w AO
	XMFLOAT4 Phase;     // x 1차 위상, y 2차 위상, z 잎 무늬 시드
};

struct TreeMeshData
{
	std::vector<TreeVertex> Vertices;   // 수피 정점 다음에 잎 정점
	std::vector<uint32_t> Indices;      // 수피 삼각형 다음에 잎 삼각형
	uint32_t BarkIndexCount = 0;
	uint32_t LeafIndexCount = 0;
	int BranchCount = 0;
	int LeafCardCount = 0;
	XMFLOAT3 BoundsMin = { 0, 0, 0 }, BoundsMax = { 0, 0, 0 };
};

namespace TreeGenerator
{
	void Generate(const TreeParams& params, TreeMeshData& out);
}
