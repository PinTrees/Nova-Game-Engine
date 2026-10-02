#pragma once
#include "Component.h"
#include "AnimatorIK.h"

// Legs Animator: 애니메이션은 평지 기준이지만 발을 실제 바닥(계단·경사·바위)에 붙인다 (Play 중, Animator 와 같은 GameObject).
//  - 발마다 위에서 아래로 레이캐스트 → 바닥 높이만큼 발을 올리고 내림 (두 뼈 IK, 무릎은 애니메이션이 굽은 쪽으로)
//  - Adjust Hips: 낮은 쪽 발이 닿도록 엉덩이를 내린다 (다리가 허공에 뜨지 않게)
//  - Align Feet: 발바닥을 바닥 기울기에 맞춘다
//  다리는 Humanoid 아바타(자동 매핑)로 찾는다.
class LegsAnimator : public Component, public IAnimatorPoseModifier
{
public:
	float Weight = 1.0f;
	float RayStartHeight = 0.6f;    // 발 위 어디서부터 쏘나 (m, 오브젝트 기준)
	float MaxStepDown = 0.6f;       // 발을 이만큼까지 내린다
	float MaxStepUp = 0.6f;         // 발을 이만큼까지 올린다
	bool AdjustHips = true;
	float HipsMaxDown = 0.45f;
	bool AlignFeet = true;
	float AlignWeight = 0.8f;
	float Smoothing = 14.0f;        // 따라가는 빠르기 (1/초)
	bool ShowGizmos = true;

	LegsAnimator();
	void ModifyPose(AnimatorPose& pose) override;
	int PoseOrder() const override { return 0; }

	void OnInspectorGUI() override;
	void OnDrawGizmos() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "animator"; }

	GENERATE_COMPONENT_BODY(LegsAnimator)

private:
	float m_FootOffset[2] = { 0.0f, 0.0f };   // 월드 위아래 (m)
	XMFLOAT3 m_Normal[2] = { { 0, 1, 0 }, { 0, 1, 0 } };
	float m_HipsOffset = 0.0f;
	XMFLOAT3 m_GizmoHit[2] = {};
	bool m_GizmoValid[2] = { false, false };
	bool Raycast(const XMFLOAT3& origin, float length, float& hitY, XMFLOAT3& normal) const;
};

REGISTER_PACKAGE_COMPONENT(LegsAnimator)
