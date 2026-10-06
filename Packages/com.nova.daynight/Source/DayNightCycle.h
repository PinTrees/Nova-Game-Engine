#pragma once
#include "Component.h"

// 낮 · 밤 순환 (Day Night Cycle). 장면에 하나 두면 전체를 다룬다:
//  - 시각 (Time Of Day, 0 ~ 24 시) 이 Play 중 Day Length 만큼에 하루씩 흐른다 (편집 중에는 멈춰 그 시각을 보여 준다)
//  - 단계: 새벽 → 아침 → 낮 → 저녁 → 노을 → 밤 → 은하수 → 새벽 … 단계마다 모습 (Look) 이 있고 사이는 부드럽게 섞는다
//  - 장면의 Directional Light 를 해로 돌린다 (6 시에 동쪽에서 떠서 18 시에 서쪽으로 진다). 해가 지면 반대편 달빛 (그림자도 따라온다)
//  - 빛 · 환경광 · 반사 · 안개 색 · 하늘 (그라데이션 · 노을 빛 · 해 · 달 · 별 · 은하수) 은 엔진의 DayNightState 로 (장면에 저장되지 않는다)
class DayNightCycle : public Component
{
public:
	enum Phase { Dawn = 0, Morning, Day, Evening, Sunset, Night, MilkyWay, PhaseCount };
	static const char* PhaseName(int phase);       // "Dawn" …
	static float PhaseStart(int phase);            // 시작 시각 (시)

	// 단계의 모습
	struct Look
	{
		XMFLOAT3 SunColor = { 1, 1, 1 };   float SunIntensity = 1.0f;     // 해 (밤 = 달빛)
		XMFLOAT3 Ambient = { 1, 1, 1 };    float AmbientIntensity = 1.0f;
		XMFLOAT3 Zenith = { 0.3f, 0.5f, 0.95f };
		XMFLOAT3 Horizon = { 0.75f, 0.85f, 1.0f };
		float SkyBrightness = 1.0f;        // 스카이박스 밝기
		float GradientMix = 0.0f;          // 스카이박스 → 그라데이션 (0 = 스카이박스 그대로)
		XMFLOAT3 Glow = { 1, 0.6f, 0.3f }; float GlowStrength = 0.0f;  // 해 쪽 지평 빛
		float Stars = 0.0f, MilkyWay = 0.0f;
		XMFLOAT3 Fog = { 1, 1, 1 };        // 안개 색 배율
	};

	float TimeOfDay = 9.0f;          // 시 (0 ~ 24)
	float DayLengthMinutes = 24.0f;  // 실제 몇 분에 하루 (Play 중)
	bool Paused = false;
	float SunAzimuth = 0.0f;         // 해가 뜨는 쪽 (도, 0 = +X 동쪽에서 떠서 -X 로 진다)
	float MaxElevation = 60.0f;      // 정오의 해 높이 (도)
	uint64 SunLight = 0;             // 해로 쓸 Directional Light (0 = 장면의 첫 Directional Light)
	float StarBrightness = 1.0f;
	float MilkyWayBrightness = 1.0f;
	Look Looks[PhaseCount];

	DayNightCycle();
	~DayNightCycle() override;

	void LastUpdate() override;
	void OnDestroy() override;
	void OnInspectorGUI() override;
	void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "light_directional"; }

	int CurrentPhase() const;                 // TimeOfDay 가 든 단계
	Vec3 SunDirection() const;                // 해가 있는 쪽 (단위)
	float SunElevation() const;               // 도 (밤 = 음수)
	const Look& Blended() const { return m_Blended; }
	class GameObject* LightObject() const;    // 돌리는 Directional Light 의 오브젝트

	static DayNightCycle* Active();           // 장면에서 켜진 것 (C# · CLI 용)
	void Apply();                             // 지금 시각의 빛 · 하늘 · 해 방향을 바로 (CLI 가 값을 바꾼 뒤 — 다음 프레임을 기다리지 않게)
	static void ResetLooks(Look looks[PhaseCount]);   // 기본 모습

	GENERATE_COMPONENT_BODY(DayNightCycle)

private:
	Look m_Blended;
};

REGISTER_PACKAGE_COMPONENT(DayNightCycle)
