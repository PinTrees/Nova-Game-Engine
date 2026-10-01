#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// NOVA 지형 생성기 설정 (World Creator 의 레이어 스택 + Atlas 의 비파괴 스탬프 방식).
//  높이 = Base(노이즈) → 스탬프(씬의 TerrainStamp 오브젝트, Order 순) → 필터 스택(위에서 아래로) 순서로 만든다.
//  재질 규칙은 마지막 높이에서 높이·경사(·침식 퇴적)를 보고 지형 레이어(스플랫)를 칠한다.
//  TerrainData 에 저장되며(.terraindata v3), 값이나 스탬프가 바뀌면 다시 생성한다 (손으로 칠한 높이는 덮어쓴다)
struct TerrainGenBase
{
	enum class Type { Flat, Classic, Ridged, Billow, Eroded, CurrentTerrain, Dunes };
	Type NoiseType = Type::Classic;
	int Seed = 1;
	float Scale = 600.0f;          // 첫 옥타브 파장 (m)
	float MinHeight = 20.0f;       // m
	float MaxHeight = 220.0f;      // m
	float ShapePower = 1.4f;       // > 1 = 골짜기는 넓고 봉우리는 뾰족하게
	float OffsetX = 0.0f, OffsetZ = 0.0f;
	float WindAngle = 30.0f;       // Dunes: 바람 방향 (도)
	static constexpr int kOctaves = 8;
	float Octaves[kOctaves] = { 1.0f, 0.55f, 0.32f, 0.18f, 0.1f, 0.06f, 0.035f, 0.02f };   // 옥타브별 세기 (World Creator 의 Base Noise 막대)
};

struct TerrainGenFilter
{
	enum class Type { HydraulicErosion, ThermalErosion, Terrace, Smooth, HeightCurve, DetailNoise, Count };
	Type FilterType = Type::HydraulicErosion;
	bool Enabled = true;
	float Strength = 1.0f;         // 결과를 이전 높이와 섞는 비율
	float P[6] = {};               // 종류별 값 (ParamInfo 참고)

	static const char* Name(Type t);
	struct ParamInfo { const char* Label; float Min, Max, Default; bool Integer; };
	// 종류의 값 설명 (Label 이 nullptr 이면 그 칸은 안 씀)
	static const ParamInfo* Params(Type t);
	static TerrainGenFilter Make(Type t);
	static bool IsHeavy(Type t) { return t == Type::HydraulicErosion || t == Type::ThermalErosion; }   // 끄는 중에는 건너뜀
};

// 재질 규칙 (World Creator 의 Material 레이어): 분포(높이·경사·퇴적·흐름·오목함·노이즈)로 정한 곳에
//  지형 레이어(텍스처)를 칠하고, 색을 정하면 컬러 맵에 그 색을 칠한다 (텍스처는 명암 디테일만 쓴다)
struct TerrainGenGradientStop
{
	float Pos = 0.0f;
	float Color[3] = { 1, 1, 1 };   // sRGB 0~1
};

struct TerrainGenMaterialRule
{
	enum class ColorMode { Texture, Color, Gradient };
	enum class Input { Height, Slope, Flow, Sediment, Cavity, Noise };   // 그라디언트를 읽는 값

	bool Enabled = true;
	std::string Name;
	int Layer = 1;                 // 지형 레이어 번호 (0~3)
	float HeightMin = 0.0f, HeightMax = 10000.0f, HeightBlend = 10.0f;   // m
	float SlopeMin = 0.0f, SlopeMax = 90.0f, SlopeBlend = 5.0f;          // 도
	float Sediment = 0.0f;         // 침식 퇴적 마스크를 얼마나 따를지 (0 = 무시, 1 = 퇴적된 곳에만)
	float Flow = 0.0f;             // 물길(수력 침식 흐름) 마스크를 얼마나 따를지
	float Cavity = 0.0f;           // > 0 = 오목한 곳(골)에, < 0 = 볼록한 곳(능선)에
	float Noise = 0.25f;           // 경계를 흐트러뜨리는 노이즈
	float Opacity = 1.0f;

	ColorMode Mode = ColorMode::Texture;
	float Color[3] = { 0.5f, 0.45f, 0.4f };            // sRGB
	Input GradientInput = Input::Height;
	std::vector<TerrainGenGradientStop> Gradient;    // Pos 순
	float ColorVariation = 0.15f;  // 큰·작은 노이즈로 밝기·색을 흔든다 (단색이 밋밋하지 않게)
};

nlohmann::json MaterialRuleToJson(const TerrainGenMaterialRule& r);
TerrainGenMaterialRule MaterialRuleFromJson(const nlohmann::json& j);

struct TerrainGenSettings
{
	bool Enabled = false;          // 켜면 이 지형의 높이를 생성기가 만든다
	bool AutoUpdate = true;        // 바뀌면 바로 다시 생성
	TerrainGenBase Base;
	std::vector<TerrainGenFilter> Filters;
	bool PaintMaterials = false;
	std::vector<TerrainGenMaterialRule> Materials;
	std::string BiomePreset;       // 마지막으로 적용한 바이옴 프리셋 이름 (표시용)

	nlohmann::json ToJson() const;
	void FromJson(const nlohmann::json& j);
	static TerrainGenSettings MakeDefault();   // 기본 스택: Classic 노이즈 + 수력 침식 + 열 침식, 재질 규칙 3 개
};
