#pragma once
#include "CinemachineCore.h"

// Position Control (Body) — Unity Cinemachine 3 와 같은 이름

// 대상에서 Follow Offset 만큼 떨어져 따라간다 (Binding Mode 가 오프셋의 축을 정한다)
class CinemachineFollow : public CinemachineComponentBase
{
public:
	Vec3 FollowOffset = Vec3(0.0f, 0.0f, -10.0f);
	CmTracker TrackerSettings;

	CinemachineFollow() { m_InspectorTitleName = "Cinemachine Follow"; }
	CmStage Stage() const override { return CmStage::Body; }
	void MutateCameraState(CinemachineCamera* vcam, CmState& state, float dt) override;
	void OnSnap() override { TrackerSettings.Reset(); }
	void OnInspectorGUI() override;

	Vec3* VecProp(int i) override;
	int* IntProp(int i) override;
	float* FloatProp(int i) override { return i == 0 ? &TrackerSettings.QuaternionDamping : nullptr; }

	GENERATE_COMPONENT_BODY(CinemachineFollow)
};
REGISTER_PACKAGE_COMPONENT(CinemachineFollow)

// 입력 축 (Unity InputAxis): 값 · 범위 · 감기 (Wrap) · 가운데
struct CmAxis
{
	float Value = 0.0f;
	float Min = -180.0f, Max = 180.0f;
	bool Wrap = false;
	float Center = 0.0f;
	void Validate();
	void Inspector(const char* label);
	json ToJson() const;
	void FromJson(const json& j, const CmAxis& fallback);
};

// 대상 둘레의 구 (Sphere) 또는 세 고리 (Three Ring — FreeLook) 위에 놓는다. 가로 · 세로 · 거리 축을 C# · 입력이 바꾼다
class CinemachineOrbitalFollow : public CinemachineComponentBase
{
public:
	struct Orbit { float Height, Radius; };
	int OrbitStyle = 0;   // 0 Sphere, 1 Three Ring
	float Radius = 10.0f;
	Orbit Top = { 5.0f, 2.0f }, Center = { 2.25f, 4.0f }, Bottom = { 0.1f, 2.5f };
	float SplineCurvature = 0.5f;
	Vec3 TargetOffset;
	CmTracker TrackerSettings;
	CmAxis HorizontalAxis, VerticalAxis, RadialAxis;

	CinemachineOrbitalFollow();
	CmStage Stage() const override { return CmStage::Body; }
	void MutateCameraState(CinemachineCamera* vcam, CmState& state, float dt) override;
	void OnSnap() override { TrackerSettings.Reset(); }
	void OnInspectorGUI() override;
	Vec3 OrbitOffset() const;   // 묶는 공간의 카메라 자리

	Vec3* VecProp(int i) override;
	int* IntProp(int i) override;
	float* FloatProp(int i) override;
	bool* BoolProp(int i) override;
	void Validate() override;

	GENERATE_COMPONENT_BODY(CinemachineOrbitalFollow)
};
REGISTER_PACKAGE_COMPONENT(CinemachineOrbitalFollow)

// 어깨 너머 3 인칭 (Unity ThirdPersonFollow): 대상 → 어깨 → 손 → 카메라. 회전은 대상의 회전 그대로 (조준용)
class CinemachineThirdPersonFollow : public CinemachineComponentBase
{
public:
	Vec3 Damping = Vec3(0.1f, 0.5f, 0.3f);
	Vec3 ShoulderOffset = Vec3(0.5f, -0.4f, 0.0f);
	float VerticalArmLength = 0.4f;
	float CameraSide = 1.0f;       // 0 왼쪽 어깨, 1 오른쪽
	float CameraDistance = 2.0f;
	// 벽 피하기 (Avoid Obstacles)
	bool AvoidObstacles = false;
	int CollisionFilter = 1;       // 레이어 마스크 (Default)
	std::string IgnoreTag;
	float CameraRadius = 0.2f;
	float DampingIntoCollision = 0.0f;
	float DampingFromCollision = 2.0f;

	CinemachineThirdPersonFollow() { m_InspectorTitleName = "Cinemachine Third Person Follow"; }
	CmStage Stage() const override { return CmStage::Body; }
	void MutateCameraState(CinemachineCamera* vcam, CmState& state, float dt) override;
	void OnSnap() override { m_HasPrevious = false; m_Pull = 0.0f; }
	void OnInspectorGUI() override;

	Vec3* VecProp(int i) override;
	float* FloatProp(int i) override;
	bool* BoolProp(int i) override { return i == 0 ? &AvoidObstacles : nullptr; }
	int* IntProp(int i) override { return i == 0 ? &CollisionFilter : nullptr; }
	void Validate() override;

	GENERATE_COMPONENT_BODY(CinemachineThirdPersonFollow)

private:
	bool m_HasPrevious = false;
	Vec3 m_PreviousTarget;
	float m_Pull = 0.0f;   // 벽 때문에 당긴 거리 (따라가기)
};
REGISTER_PACKAGE_COMPONENT(CinemachineThirdPersonFollow)
