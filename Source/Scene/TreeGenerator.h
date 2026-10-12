#pragma once
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

// NOVA 나무 생성기 (SpeedTree / Weber-Penn 방식의 절차적 나무)
//  - 줄기 → 가지(1~3 단계)를 재귀로 키운다. 가지는 휘어진 관(링을 이은 원통), 자식 가지는 황금각(137.5°)으로 돌려 붙인다
//  - 수관(Crown) 모양 함수가 높이에 따라 가지 길이를 정한다 (원뿔, 구, 반구, 원기둥, 불꽃 …)
//  - 마지막 단계 가지에 잎 카드를 붙인다. 잎 모양은 셰이더가 식(SDF)으로 그리므로 텍스처가 없다
//  - 정점마다 바람 가중치/위상을 넣어 셰이더가 계층 흔들림을 계산한다 (44. TreeCommon.fx)
//  - 자라는 꼴 (Form): 나무 · 덤불 (Tree) 말고도 야자 (줄기 끝 잎 다발), 풀 · 고사리 · 로제트 (밑동에서 잎 카드), 꽃 (꽃대 + 꽃) —
//    모든 식생을 같은 생성기 · 셰이더 · LOD · 바람으로 (PlantLibrary 의 기후대별 종 100 여 개)
struct TreeParams
{
	enum class Crown { Conical, Spherical, Hemispherical, Cylindrical, TaperedCylindrical, Flame, InverseConical };
	// Blade = 풀잎 (밑에서 부채꼴), Frond = 깃꼴 잎 (가운데 잎줄기 + 작은 잎 — 고사리 · 야자), Round = 둥근 잎, Lobed = 갈래 잎 (단풍 · 참나무), Scale = 비늘잎 (측백)
	enum class LeafShape { Broad, Oval, Needle, Blade, Frond, Round, Lobed, Scale };
	static constexpr int kLeafShapes = 8;
	enum class Form { Tree, Palm, Grass, Fern, Rosette, Flower };
	static constexpr int kForms = 6;

	int Seed = 1;
	Form Shape = Form::Tree;
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
	// 저장하지 않는 입력: 잎 텍스처의 잎 길이(TreeDesc::LeafLength). 카드를 잎이 덮인 모양(TreeTextures::LeafCardHull)으로 자를 때 쓴다
	float LeafLengthForHull = -1.0f;   // < 0 이면 사각형 카드
	float LeafStart = 0.3f;        // 가지 길이의 이 비율부터 잎
	int CardSegments = 1;          // 잎 카드 마디 (2 이상이면 길이 방향으로 휘는 띠 — 풀잎 · 고사리 · 야자 잎)
	float Droop = 0.0f;            // 카드 끝이 아래로 휘는 정도 (라디안, 마디마다 나눠 굽힌다)
	float CardSpread = 45.0f;      // 밑동 카드 (Grass · Fern · Rosette · Flower 잎) 가 세로에서 벌어지는 각 (도) / Palm 잎이 위로 서는 각

	// ---- 꽃 (잎 아틀라스 칸 3 = 꽃잎 무늬, 꽃 카드는 그 칸만 쓴다)
	int FlowerPetals = 0;          // 0 = 꽃 없음
	int FlowerCards = 0;           // Tree: 마지막 단계 가지마다, Flower: 꽃대마다 꽃 카드 수
	float FlowerSize = 0.12f;      // 꽃 카드 한 변 (m)
	int Stems = 0;                 // Flower: 꽃대 수 (밑동에서)

	void ApplyPreset(int preset);  // PlantLibrary 의 종 (모양만, seed 는 그대로)
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
	// lod 0 = 전체, 1 = 중간 단계 (링 변·잔가지·잎 카드를 줄임, 같은 seed 면 모양은 같다)
	void Generate(const TreeParams& params, TreeMeshData& out, int lod = 0);
}
