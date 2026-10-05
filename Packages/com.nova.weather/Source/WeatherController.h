#pragma once
#include "Component.h"
#include "WeatherProfile.h"

class VisualEffect;
class AudioSource;
class AudioClip;
class LineRenderer;

// 오픈 월드 날씨 (Weather Controller). 장면에 하나 두면:
//  - 비 · 눈: 카메라 둘레에서만 태어나는 GPU 입자 (Visual Effect Graph) → 월드 크기와 상관없이 같은 비용
//  - 먹구름: 해 · 하늘 · 환경광을 어둡게, 날씨 안개, 바람 (나무 · 풀) 과 돌풍
//  - 번개 (빛줄기 + 하늘 번쩍임) 와 천둥 (거리만큼 늦게), 비 · 바람 소리 (Play 중)
//  Profile 을 바꾸면 Transition Time 동안 부드럽게 넘어간다. 값은 장면에 저장되지 않는다 (엔진의 WeatherState 로 그릴 때만 적용)
//  돕는 오브젝트 (입자 · 번개 선 · 소리) 는 장면 밖에 만든다 → Hierarchy 에 보이지 않고 저장되지 않는다
class WeatherController : public Component
{
public:
	std::string Profile = "Clear";   // 기본 프로필 이름 또는 .weather 경로
	float TransitionTime = 8.0f;     // 초
	float Density = 1.0f;            // 빗방울 · 눈송이 수 배율 (성능)
	bool Lightning = true;
	bool Sound = true;
	float Volume = 1.0f;
	float SnowDepth = 0.25f;         // 쌓인 눈 깊이 (m) — 발자국이 이만큼 꺼진다

	WeatherController();
	~WeatherController() override;

	void LastUpdate() override;
	void OnDestroy() override;
	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "volume"; }

	// 다른 프로필로 (seconds < 0 = Transition Time, 0 = 바로)
	bool SetProfile(const std::string& nameOrPath, float seconds, std::string* error = nullptr);
	void Strike();   // 번개 하나 (검사 · 연출)

	const WeatherParams& Current() const { return m_Current; }
	float TransitionProgress() const { return m_Duration > 0.0f ? std::clamp(m_Elapsed / m_Duration, 0.0f, 1.0f) : 1.0f; }
	const std::string& TargetProfile() const { return m_TargetName; }
	const std::string& ProfileError() const { return m_Error; }
	int StrikeCount() const { return m_Strikes; }
	float SurfaceWetness() const { return m_SurfaceWet; }   // 지금 표면 (비에 따라 천천히 젖고 마른다)
	float Puddles() const { return m_Puddles; }
	float SnowAmount() const { return m_Snow; }        // 쌓인 눈 (천천히 쌓이고 녹는다)
	// 소리 (Play 중): 비 약 · 비 강 · 바람 소리 크기, 재생 중이면 true
	bool SoundLevels(float out[3]) const;

	// 장면에 있는 것 (C# · CLI 용) — 꺼진 것도
	static WeatherController* Active();
	static int InstanceCount();   // 메모리에 있는 모든 인스턴스 (검사용)

	GENERATE_COMPONENT_BODY(WeatherController)

private:
	WeatherParams m_From, m_To, m_Current;
	std::string m_TargetName;        // 지금 향하는 프로필 (Profile 과 다르면 새로 시작)
	std::string m_Error;
	float m_Elapsed = 0.0f, m_Duration = 0.0f;
	bool m_Started = false;
	double m_Time = 0.0;             // 돌풍 · 번개 시계
	unsigned m_Rng = 0x9E3779B9u;
	bool m_WasOn = false;
	float m_SurfaceWet = 0.0f, m_Puddles = 0.0f, m_Snow = 0.0f;
	static float PuddleTarget(const WeatherParams& p);

	// 따라가는 카메라
	bool m_HaveViewer = false;
	Vec3 m_LastViewer, m_ViewerVelocity, m_ViewerForward = Vec3(0, 0, 1);

	// 입자
	class GameObject* m_FxObject = nullptr;
	VisualEffect* m_Fx = nullptr;

	// 번개
	struct Bolt
	{
		class GameObject* Object = nullptr;
		LineRenderer* Line = nullptr;
		float Age = 0.0f, Life = 0.0f, Width = 1.0f;
		bool Main = false;
	};
	std::vector<Bolt> m_Bolts;
	float m_NextStrike = -1.0f;      // 다음 번개까지 (초, < 0 = 아직 정하지 않음)
	float m_FlashAge = 10.0f, m_FlashPeak = 0.0f;
	int m_Strikes = 0;
	struct Thunder { float Delay; int Clip; float Volume; };
	std::vector<Thunder> m_Thunder;

	// 소리 (Play 중)
	class GameObject* m_AudioObject = nullptr;
	AudioSource* m_RainLight = nullptr;
	AudioSource* m_RainHeavy = nullptr;
	AudioSource* m_Wind = nullptr;
	AudioSource* m_OneShot = nullptr;
	std::shared_ptr<AudioClip> m_ThunderClips[3];

	float Random01();
	Vec3 ViewerPosition(Vec3* forward = nullptr) const;
	void Advance(float dt, bool running);
	void UpdateParticles(const Vec3& viewer, const Vec3& wind, bool on);
	void UpdateLightning(float dt, const Vec3& viewer, bool on);
	void UpdateAudio(float dt, float gust, bool on);
	void SpawnBolt(const Vec3& viewer, const Vec3& forward, float inFront);
	float FlashEnvelope() const;
	void ApplyState(float gust);
	void ReleaseHelpers();
};

REGISTER_PACKAGE_COMPONENT(WeatherController)
