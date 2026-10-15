#pragma once
#include "Component.h"
#include "AnimatorIK.h"

// Unity Animation Rigging 의 Two Bone IK Constraint: 뼈 셋 (Root → Mid → Tip) 의 끝이 Target 에 닿게, 가운데 관절은 Hint 쪽으로.
//  Humanoid 가 아닌 리그 (꼬리 · 기계 팔 · 무기) 에도 — 본은 노드 이름. Humanoid 면 Inspector 의 Auto Setup 이 팔 · 다리를 채운다.
//  Animator 와 같은 GameObject (Unity 는 Rig 아래 오브젝트 — NOVA 는 같은 오브젝트에 여러 개). Animator IK · Hands Animator 다음 (20).
//  Target 회전: Tip 의 월드 회전을 Target 의 월드 회전으로 (Maintain Target Offset 을 켜면 처음 차이를 지킨다)
class TwoBoneIKConstraint : public Component, public IAnimatorPoseModifier
{
public:
	std::string Root, Mid, Tip;   // 노드 이름
	uint64 Target = 0;
	uint64 Hint = 0;
	float Weight = 1.0f;
	float TargetPositionWeight = 1.0f;
	float TargetRotationWeight = 1.0f;
	float HintWeight = 1.0f;
	bool MaintainTargetPositionOffset = false;
	bool MaintainTargetRotationOffset = false;

	TwoBoneIKConstraint();
	void ModifyPose(AnimatorPose& pose) override;
	int PoseOrder() const override { return 20; }
	// Humanoid 팔 · 다리 (0 왼발 · 1 오른발 · 2 왼손 · 3 오른손 — AvatarIKGoal) 로 Root · Mid · Tip 채우기
	bool AutoSetup(int limb);

	void OnInspectorGUI() override;
	void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "animator"; }

	GENERATE_COMPONENT_BODY(TwoBoneIKConstraint)

private:
	const SkeletonAvataData* m_NodesFor = nullptr;
	std::string m_NodesKey;
	int m_Nodes[3] = { -1, -1, -1 };
	bool m_OffsetValid = false;
	XMFLOAT3 m_PosOffset = {};       // 모델 공간 (Tip − Target), Maintain Offset
	XMFLOAT4 m_RotOffset = { 0, 0, 0, 1 };
};

REGISTER_PACKAGE_COMPONENT(TwoBoneIKConstraint)
