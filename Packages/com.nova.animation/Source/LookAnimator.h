#pragma once
#include "Component.h"
#include "AnimatorIK.h"

// Look Animator: 머리·목·가슴·척추가 대상 쪽으로 돌아간다 (Play 중, Animator 와 같은 GameObject).
//  - 회전을 여러 본에 나눠 자연스럽게 (가중치 = Spine / Chest / Neck / Head)
//  - 좌우(Max Yaw)·위아래(Max Pitch) 제한, 대상이 뒤쪽(Stop Angle 넘음)이면 정면으로 돌아온다
//  - 대상 = Target 오브젝트 (+ Target Offset) 또는 스크립트의 SetLookAtPosition
//  애니메이션 위에 더하는 회전이라 걷기·숨쉬기 동작은 그대로 남는다. 본은 Humanoid 아바타로 찾는다.
class LookAnimator : public Component, public IAnimatorPoseModifier
{
public:
	uint64 Target = 0;
	Vec3 TargetOffset = Vec3::Zero;
	float Weight = 1.0f;
	float MaxYaw = 70.0f;
	float MaxPitchUp = 35.0f;
	float MaxPitchDown = 45.0f;
	float StopAngle = 120.0f;      // 이보다 뒤쪽이면 보지 않는다
	float Speed = 6.0f;            // 따라 돌아가는 빠르기 (1/초)
	float SpineWeight = 0.1f, ChestWeight = 0.2f, NeckWeight = 0.3f, HeadWeight = 0.4f;
	bool ShowGizmos = true;

	LookAnimator();
	void ModifyPose(AnimatorPose& pose) override;
	int PoseOrder() const override { return 10; }

	// 스크립트: 오브젝트 대신 이 위치를 본다 (ClearLookAtPosition 까지)
	void SetLookAtPosition(const Vec3& p) { m_LookPosition = p; m_UseLookPosition = true; }
	void ClearLookAtPosition() { m_UseLookPosition = false; }

	void OnInspectorGUI() override;
	void OnDrawGizmos() override;
	void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "animator"; }

	GENERATE_COMPONENT_BODY(LookAnimator)

private:
	Vec3 m_LookPosition = Vec3::Zero;
	bool m_UseLookPosition = false;
	float m_Yaw = 0.0f, m_Pitch = 0.0f, m_Blend = 0.0f;
	XMFLOAT3 m_GizmoHead = {}, m_GizmoTarget = {};
	bool m_GizmoValid = false;
	bool TargetWorld(Vec3& out) const;
};

REGISTER_PACKAGE_COMPONENT(LookAnimator)
