#pragma once
#include "CinemachineCore.h"

// 흔들림 · 충격 — Unity Cinemachine 3 와 같은 이름

// Noise 단계: 프로필 (손에 든 카메라 · 6D Shake …) 의 펄린 노이즈로 위치 · 회전을 흔든다 (출력에만, 가상 카메라 Transform 은 그대로)
class CinemachineBasicMultiChannelPerlin : public CinemachineComponentBase
{
public:
	int NoiseProfile = 2;            // CmCore::NoiseProfileNames (0 None, 2 Handheld_normal_mild)
	Vec3 PivotOffset;                // 이 점을 중심으로 돈다 (카메라 공간)
	float AmplitudeGain = 1.0f;
	float FrequencyGain = 1.0f;

	CinemachineBasicMultiChannelPerlin();
	CmStage Stage() const override { return CmStage::Noise; }
	void MutateCameraState(CinemachineCamera* vcam, CmState& state, float dt) override;
	void OnInspectorGUI() override;
	void ReSeed();

	int* IntProp(int i) override { return i == 0 ? &NoiseProfile : nullptr; }
	float* FloatProp(int i) override { return i == 0 ? &AmplitudeGain : i == 1 ? &FrequencyGain : nullptr; }
	Vec3* VecProp(int i) override { return i == 0 ? &PivotOffset : nullptr; }
	void Validate() override;

	GENERATE_COMPONENT_BODY(CinemachineBasicMultiChannelPerlin)

private:
	float m_Time = 0.0f;
	Vec3 m_Seed;
};
REGISTER_PACKAGE_COMPONENT(CinemachineBasicMultiChannelPerlin)

// 충격을 내는 곳 (Unity CinemachineImpulseSource): GenerateImpulse… 를 부르면 듣는 가상 카메라 (Impulse Listener) 가 흔들린다
class CinemachineImpulseSource : public Component, public ICmProperties
{
public:
	int ImpulseChannel = 1;          // 비트 마스크
	int ImpulseShape = CmCore::Bump;
	float ImpulseDuration = 0.2f;
	int ImpulseType = CmCore::Uniform;
	float DissipationRate = 0.25f;
	float DissipationDistance = 100.0f;
	float PropagationSpeed = 343.0f;
	Vec3 DefaultVelocity = Vec3(0.0f, -1.0f, 0.0f);

	CinemachineImpulseSource() { m_InspectorTitleName = "Cinemachine Impulse Source"; }
	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "camera"; }

	void GenerateImpulseAtPositionWithVelocity(const Vec3& position, const Vec3& velocity);
	void GenerateImpulseWithVelocity(const Vec3& velocity);
	void GenerateImpulseWithForce(float force) { GenerateImpulseWithVelocity(DefaultVelocity * force); }

	int* IntProp(int i) override;
	float* FloatProp(int i) override;
	Vec3* VecProp(int i) override { return i == 0 ? &DefaultVelocity : nullptr; }
	void Validate() override;

	GENERATE_COMPONENT_BODY(CinemachineImpulseSource)

private:
	float m_TestForce = 1.0f;
};
REGISTER_PACKAGE_COMPONENT(CinemachineImpulseSource)

// 확장 (Unity CinemachineImpulseListener): 가상 카메라에 붙이면 같은 채널의 충격만큼 흔들린다
class CinemachineImpulseListener : public CinemachineComponentBase
{
public:
	int ChannelMask = 1;
	float Gain = 1.0f;
	bool Use2DDistance = false;
	bool UseCameraSpace = true;      // 충격 방향을 카메라 공간으로 (아래 = 화면 아래)

	CinemachineImpulseListener() { m_InspectorTitleName = "Cinemachine Impulse Listener"; }
	CmStage Stage() const override { return CmStage::Finalize; }
	void MutateCameraState(CinemachineCamera* vcam, CmState& state, float dt) override;
	void OnInspectorGUI() override;

	int* IntProp(int i) override { return i == 0 ? &ChannelMask : nullptr; }
	float* FloatProp(int i) override { return i == 0 ? &Gain : nullptr; }
	bool* BoolProp(int i) override { return i == 0 ? &Use2DDistance : i == 1 ? &UseCameraSpace : nullptr; }

	GENERATE_COMPONENT_BODY(CinemachineImpulseListener)
};
REGISTER_PACKAGE_COMPONENT(CinemachineImpulseListener)
