#pragma once
#include <string>
#include <vector>

// 지형 디테일 한 종류 (Unity 의 DetailPrototype): 풀 / 꽃 / 작은 돌.
//  메시는 설정으로 절차 생성한다 (DetailRenderer): 풀 = 휘어진 잎 여러 장의 덩어리, 꽃 = 잎 + 줄기 끝 꽃송이, 돌 = 찌그러진 구 몇 개.
//  지형의 밀도 맵(0~255)이 칠해진 곳에 Density(개/m²) 만큼 덩어리를 흩뿌린다. 색은 감마 공간 0~1
struct DetailPrototype
{
	enum class Kind { Grass, Flower, Pebble };

	std::string Name = "Grass";
	Kind Type = Kind::Grass;
	int Seed = 1;

	// ---- 모양 (덩어리 하나)
	float Height = 0.45f;          // m: 잎 / 줄기 높이
	float Radius = 0.18f;          // m: 덩어리 반지름 (잎·돌이 퍼지는 범위)
	int Blades = 14;               // 잎 수 (돌 = 돌 수)
	float BladeWidth = 0.022f;     // m (돌 = 돌 크기)
	float Bend = 0.35f;            // 잎이 휘는 정도
	float Lean = 0.35f;            // 바깥으로 기우는 정도
	int Heads = 2;                 // 꽃송이 수
	int Petals = 6;
	float HeadSize = 0.035f;       // m: 꽃잎 길이
	float MinScale = 0.75f, MaxScale = 1.2f;   // 덩어리마다 크기 (높이)
	float WidthVariation = 0.2f;   // 덩어리마다 폭 차이

	// ---- 색
	XMFLOAT4 HealthyColor = { 0.33f, 0.52f, 0.18f, 1.0f };
	XMFLOAT4 DryColor = { 0.62f, 0.58f, 0.30f, 1.0f };
	XMFLOAT4 FlowerColor = { 0.95f, 0.82f, 0.20f, 1.0f };
	XMFLOAT4 CenterColor = { 0.55f, 0.35f, 0.10f, 1.0f };
	float NoiseSpread = 0.08f;     // 1/m: 건강 ↔ 마름 얼룩 크기 (Unity 의 Noise Spread)
	float DryAmount = 0.35f;       // 마른 색이 섞이는 정도
	float ColorVariation = 0.15f;  // 덩어리마다 밝기 차이
	float TipColor = 0.25f;        // 잎 끝이 마른 색으로
	float Translucency = 0.6f;
	float Smoothness = 0.25f;

	// ---- 배치
	float Density = 8.0f;          // 덩어리 수 / m² (밀도 맵 255 일 때)
	float MaxSlope = 45.0f;        // 도: 이보다 가파르면 안 자람
	uint32_t LayerMask = 0xF;      // 자라는 지형 레이어 (비트). 칠한 레이어 비중만큼 밀도가 준다
	float GroundAlign = 0.15f;     // 0 = 똑바로 위, 1 = 지면 법선을 따라
	float WindResponse = 1.0f;     // 0 = 바람에 안 흔들림 (돌)
	bool CastShadows = true;

	static std::vector<std::string> ListPresets();          // Resources/Packages/Terrain/Details/*.detail
	static bool LoadPreset(const std::string& path, DetailPrototype& out);
	nlohmann::json ToJson() const;
	void FromJson(const nlohmann::json& j);
	bool DrawInspector();    // 바뀌면 true

	const std::string& MeshKey() const;   // 메시를 바꾸는 값만
	size_t Hash() const;                  // 배치·색 포함 전부
	void Invalidate() { m_KeyDirty = true; }

private:
	mutable std::string m_MeshKey;
	mutable size_t m_Hash = 0;
	mutable bool m_KeyDirty = true;
};

// 지형 전체의 디테일 설정 (Unity Terrain Settings 의 Detail Objects + Wind Settings for Grass)
struct DetailSettings
{
	float Distance = 90.0f;        // m: 이보다 멀면 안 그림 (멀수록 성기게)
	float DensityScale = 1.0f;     // 전체 밀도 배율
	float ShadowDistance = 35.0f;  // m: 그림자를 드리우는 거리
	float WindSpeed = 0.6f;
	float WindSize = 22.0f;        // m: 바람 물결 크기
	float WindBending = 0.5f;
	float WindDirection = 35.0f;   // 도 (+Z 기준 시계 방향)

	nlohmann::json ToJson() const;
	void FromJson(const nlohmann::json& j);
	size_t Hash() const;           // 배치에 영향을 주는 값 (밀도)
};
