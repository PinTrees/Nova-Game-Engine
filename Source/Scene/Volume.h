#pragma once
#include "Component.h"
#include "VolumeProfile.h"

// Unity URP 의 Volume 컴포넌트.
//  - Mode = Global: 어디서나 적용. Local: 이 GameObject 의 Box/Sphere Collider 안(+ Blend Distance)에서만.
//  - Weight(0..1), Priority(큰 값이 나중에 섞여 우선) 로 여러 Volume 을 섞는다 (VolumeManager).
//  - Profile(.volumeprofile) 에 Bloom, Tonemapping 등 효과와 "Override" 한 값이 들어 있다.
class Volume : public Component
{
public:
	enum class Mode { Global, Local };

private:
	Mode m_Mode = Mode::Global;
	float m_Weight = 1.0f;
	float m_Priority = 0.0f;
	float m_BlendDistance = 0.0f;
	std::string m_ProfilePath;
	std::shared_ptr<VolumeProfile> m_Profile;

	static std::vector<Volume*> s_All;

public:
	Volume();
	virtual ~Volume();

	Mode GetMode() const { return m_Mode; }
	void SetMode(Mode mode) { m_Mode = mode; }
	float GetWeight() const { return m_Weight; }
	void SetWeight(float w) { m_Weight = std::clamp(w, 0.0f, 1.0f); }
	float GetPriority() const { return m_Priority; }
	void SetPriority(float p) { m_Priority = p; }
	float GetBlendDistance() const { return m_BlendDistance; }

	void SetProfile(const std::string& path);
	std::shared_ptr<VolumeProfile> GetProfile();   // 파일이 밖에서 바뀌었으면 다시 읽는다
	const std::string& GetProfilePath() const { return m_ProfilePath; }

	// 이 점까지의 거리 (콜라이더 안이면 0, 콜라이더가 없으면 음수 = 적용 불가)
	float DistanceTo(const Vec3& point) const;
	bool HasCollider() const;

	static const std::vector<Volume*>& All() { return s_All; }

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "volume"; }

	GENERATE_COMPONENT_BODY(Volume)
};

REGISTER_COMPONENT(Volume)

// 카메라 위치에서 Volume 들을 섞어 VolumeStack 을 만든다 (Unity 의 VolumeManager.Update)
namespace VolumeManager
{
	// 결과 스택은 호출자(뷰)마다 따로 둔다 (Scene 뷰 / Game 뷰)
	void Update(VolumeStack& stack, const Vec3& cameraPosition);
	// 활성 Volume 이 하나라도 있는지 (없으면 후처리를 건너뛸 수 있다)
	bool AnyActiveVolume();
}
