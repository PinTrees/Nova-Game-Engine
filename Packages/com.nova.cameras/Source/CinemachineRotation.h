#pragma once
#include "CinemachineCore.h"

// Rotation Control (Aim) — Unity Cinemachine 3 와 같은 이름

// 보는 대상을 화면의 Screen Position 에 둔다 (Dead Zone 안이면 돌지 않고, Hard Limits 밖으로는 나가지 않게)
//  화면 자리는 가운데 0, 가장자리 ±0.5 (Unity 와 같이 y + = 아래). 크기는 화면 전체 = 1
class CinemachineRotationComposer : public CinemachineComponentBase
{
public:
	Vec3 TargetOffset;                       // 대상 자기 공간
	// 아래 2 차원 값은 x · y 만 쓴다 (C# 와 Vector3 로 주고받으려고 Vec3)
	Vec3 Damping = Vec3(0.5f, 0.5f, 0.0f);   // 가로 · 세로 (초)
	Vec3 ScreenPosition;
	bool DeadZoneEnabled = false;
	Vec3 DeadZoneSize = Vec3(0.2f, 0.2f, 0.0f);
	bool HardLimitsEnabled = true;
	Vec3 HardLimitsSize = Vec3(0.8f, 0.8f, 0.0f);
	Vec3 HardLimitsOffset;
	bool CenterOnActivate = true;

	CinemachineRotationComposer() { m_InspectorTitleName = "Cinemachine Rotation Composer"; }
	CmStage Stage() const override { return CmStage::Aim; }
	void MutateCameraState(CinemachineCamera* vcam, CmState& state, float dt) override;
	void OnInspectorGUI() override;

	Vec3* VecProp(int i) override;
	bool* BoolProp(int i) override;
	void Validate() override;

	GENERATE_COMPONENT_BODY(CinemachineRotationComposer)
};
REGISTER_PACKAGE_COMPONENT(CinemachineRotationComposer)

// 대상을 화면 가운데에 바로 (따라가기 없음)
class CinemachineHardLookAt : public CinemachineComponentBase
{
public:
	Vec3 LookAtOffset;   // 대상 자기 공간

	CinemachineHardLookAt() { m_InspectorTitleName = "Cinemachine Hard Look At"; }
	CmStage Stage() const override { return CmStage::Aim; }
	void MutateCameraState(CinemachineCamera* vcam, CmState& state, float dt) override;
	void OnInspectorGUI() override;
	Vec3* VecProp(int i) override { return i == 0 ? &LookAtOffset : nullptr; }

	GENERATE_COMPONENT_BODY(CinemachineHardLookAt)
};
REGISTER_PACKAGE_COMPONENT(CinemachineHardLookAt)

// Tracking Target 의 회전을 따라 돈다
class CinemachineRotateWithFollowTarget : public CinemachineComponentBase
{
public:
	float Damping = 0.0f;

	CinemachineRotateWithFollowTarget() { m_InspectorTitleName = "Cinemachine Rotate With Follow Target"; }
	CmStage Stage() const override { return CmStage::Aim; }
	void MutateCameraState(CinemachineCamera* vcam, CmState& state, float dt) override;
	void OnInspectorGUI() override;
	float* FloatProp(int i) override { return i == 0 ? &Damping : nullptr; }
	void Validate() override { Damping = (std::max)(0.0f, Damping); }

	GENERATE_COMPONENT_BODY(CinemachineRotateWithFollowTarget)
};
REGISTER_PACKAGE_COMPONENT(CinemachineRotateWithFollowTarget)
