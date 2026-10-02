#pragma once
#include "Component.h"
#include "AnimatorIK.h"

// Hands Animator: 손을 목표에 (Play 중, Animator 와 같은 GameObject) — 무기 손잡이 잡기, 벽 · 난간 짚기, 문고리.
//  - 손마다 Target 오브젝트(위치 + 회전) 또는 스크립트 SetIKPosition / SetIKRotation (Unity 의 AvatarIKGoal 과 같은 생각)
//  - Hint: 팔꿈치가 향할 오브젝트 (없으면 애니메이션이 굽은 쪽)
//  - 위치 · 회전 가중치 따로, 목표가 생기고 없어질 때 Blend Speed 로 부드럽게
//  - 회전: 목표 회전이 없음(항등)이면 손이 T-포즈 방향 — 목표 오브젝트를 돌려 손 모양을 맞춘다
//  두 뼈 IK (위팔 → 아래팔 → 손). 본은 Humanoid 아바타로 찾는다. 다리(0) 다음, 시선(10) 전에 (5).
class HandsAnimator : public Component, public IAnimatorPoseModifier
{
public:
	struct Hand
	{
		uint64 Target = 0;
		uint64 Hint = 0;
		float PositionWeight = 1.0f;
		float RotationWeight = 1.0f;
	};
	Hand Hands[2];                 // 0 왼손, 1 오른손
	float Weight = 1.0f;
	float BlendSpeed = 8.0f;
	bool ShowGizmos = true;

	HandsAnimator();
	void ModifyPose(AnimatorPose& pose) override;
	int PoseOrder() const override { return 5; }

	// 스크립트 (hand 0 왼손 1 오른손): 오브젝트 대신 이 위치 · 회전 (Clear 까지)
	void SetIKPosition(int hand, const Vec3& p) { if (hand >= 0 && hand < 2) { m_Script[hand].Pos = p; m_Script[hand].UsePos = true; } }
	void SetIKRotation(int hand, const Quaternion& q) { if (hand >= 0 && hand < 2) { m_Script[hand].Rot = q; m_Script[hand].UseRot = true; } }
	void ClearIK(int hand) { if (hand >= 0 && hand < 2) m_Script[hand] = ScriptGoal(); }
	// 지난 포즈에서 손이 실제로 간 곳 (월드)
	Vec3 GetHandPosition(int hand) const { return hand >= 0 && hand < 2 ? Vec3(m_Result[hand].x, m_Result[hand].y, m_Result[hand].z) : Vec3::Zero; }

	void OnInspectorGUI() override;
	void OnDrawGizmos() override;
	void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "animator"; }

	GENERATE_COMPONENT_BODY(HandsAnimator)

private:
	struct ScriptGoal { Vec3 Pos = Vec3::Zero; Quaternion Rot = Quaternion::Identity; bool UsePos = false, UseRot = false; };
	ScriptGoal m_Script[2];
	float m_PosBlend[2] = { 0, 0 }, m_RotBlend[2] = { 0, 0 };
	XMFLOAT3 m_LastGoal[2] = {}, m_Result[2] = {};
	XMFLOAT4 m_LastRot[2] = { { 0, 0, 0, 1 }, { 0, 0, 0, 1 } };
	bool m_GizmoValid[2] = { false, false };
};

REGISTER_PACKAGE_COMPONENT(HandsAnimator)
