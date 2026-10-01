#pragma once

// 물 프로파일 (Resources/Packages/Water/Profiles/*.waterprofile, JSON).
//  한 프로파일 = 물 색(흡수·산란) + 파도(Gerstner 스펙트럼) + 잔물결 노멀 + 거품 + 반사·굴절 + 코스틱 + 흐름.
//  색은 감마 공간 0~1, 길이는 미터. WaterBody 가 이름으로 고른다
struct WaterProfile
{
	std::string Name = "Default";
	std::string Description;
	std::string Path;                 // 프로젝트 기준 경로 (패키지에서 읽었으면)

	// ---- 색 (Crest / KWS 식: 깊이에 따른 흡수 + 산란)
	float Absorption[3] = { 0.30f, 0.80f, 0.90f };   // Clarity 만큼 지나간 빛이 남는 비율 (빨강이 먼저 사라진다)
	float Clarity = 8.0f;             // m. 위 비율이 되는 거리 (클수록 맑다)
	float ScatterColor[3] = { 0.04f, 0.22f, 0.30f };  // 깊은 물이 내는 색 (햇빛이 물속에서 흩어져 나오는 색)
	float Turbidity = 0.0f;           // 0 = 맑음, 1 = 흙탕 (산란이 얕은 곳에서도 바닥을 가린다)
	float SubsurfaceColor[3] = { 0.10f, 0.55f, 0.50f };   // 파도 마루를 비쳐 나오는 빛 (SSS)
	float Subsurface = 0.6f;

	// ---- 파도 (Gerstner 합, 바람에서 스펙트럼)
	float WindSpeed = 8.0f;           // m/s (최대 파장·높이)
	float WindDirection = 30.0f;      // 도 (+x 기준, 반시계)
	float Choppiness = 0.6f;          // 0 = 둥근 파도, 1 = 뾰족한 마루
	float WaveScale = 1.0f;           // 높이 배율
	float MinWavelength = 0.8f;       // m
	float MaxWavelength = 0.0f;       // m (0 = 바람에서)
	float Spread = 50.0f;             // 도. 바람 방향에서 퍼지는 정도
	int WaveCount = 24;               // 최대 32
	int Seed = 7;

	// ---- 잔물결 노멀 (텍스처 2 장을 바람 방향으로 흘림)
	float NormalStrength = 0.6f;
	float NormalTiling = 9.0f;        // m (A 텍스처 한 장 크기, B 는 0.37 배)
	float NormalSpeed = 0.6f;         // m/s

	// ---- 거품
	float FoamAmount = 0.5f;          // 파도 마루(흰 물결)
	float ShoreFoam = 1.2f;           // m. 물가 거품 띠 폭 (수심)
	float FoamTiling = 6.0f;          // m

	// ---- 빛
	float Smoothness = 0.92f;         // 해 반사 날카로움
	float Reflection = 1.0f;          // 하늘 반사 세기
	float Refraction = 0.6f;          // 굴절 흔들림
	float Caustics = 0.8f;            // 바닥 코스틱 세기
	float CausticsDepth = 6.0f;       // m. 이 깊이까지 보인다
	float CausticsTiling = 5.0f;      // m

	// ---- 흐름 (강)
	float FlowSpeed = 1.2f;           // m/s 기본 유속

	nlohmann::json ToJson() const;
	void FromJson(const nlohmann::json& j);
};

namespace WaterProfiles
{
	const std::vector<WaterProfile>& List();
	const WaterProfile* Find(const std::string& name);   // 없으면 nullptr
	const WaterProfile& Get(const std::string& name);    // 없으면 기본값
	void Reload();
}
