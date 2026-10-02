#pragma once
#include "Component.h"
#include "AnimatorIK.h"

// Dynamic Bone (Unity 의 Dynamic Bone 에셋 · VRM 의 Spring Bone): 머리카락 · 치마 · 꼬리 · 끈이 몸의 움직임 · 중력 · 바람에
// 따라 흔들리고 원래 자세로 돌아온다 (Play 중, Animator 와 같은 GameObject).
//  - 사슬 = 본 이름 목록 (뿌리 → 끝). 본마다 꼬리 = 다음 본, 끝 본은 꼬리로만 쓴다
//  - 알고리즘 = VRM Spring Bone (UniVRM 과 같음): 꼬리 위치를 Verlet 으로 — 관성 × (1 - drag) + 애니메이션 자세 쪽으로 stiffness
//    + 중력 · 바람, 본 길이 유지, 충돌체(구 · 캡슐) 밖으로 밀어냄 → 본이 꼬리를 보게 회전
//  - VRM 을 캐릭터로 만들면 파일의 Spring Bone 설정으로 자동으로 붙는다 (VrmImport::DynamicBoneJson)
class DynamicBone : public Component, public IAnimatorPoseModifier
{
public:
	struct Joint
	{
		std::string Bone;
		float Stiffness = 1.0f;     // 원래 자세로 돌아가는 힘
		float Drag = 0.4f;          // 0 = 계속 흔들림, 1 = 관성 없음
		float Gravity = 0.0f;       // 중력 세기 (m/s)
		Vec3 GravityDir = Vec3(0, -1, 0);
		float Radius = 0.02f;       // 충돌 반지름
	};
	struct Chain
	{
		std::string Name;
		std::vector<Joint> Joints;
		std::vector<int> Colliders;   // Colliders 번호 (이 사슬이 부딪히는 것)
	};
	struct Collider
	{
		std::string Bone;
		Vec3 Offset = Vec3::Zero;     // 본 로컬
		float Radius = 0.05f;
		bool Capsule = false;
		Vec3 Tail = Vec3::Zero;       // 캡슐 끝 (본 로컬)
	};

	std::vector<Chain> Chains;
	std::vector<Collider> Colliders;
	float Weight = 1.0f;              // 0 = 애니메이션 그대로
	float StiffnessScale = 1.0f, GravityScale = 1.0f, DragScale = 1.0f;
	Vec3 Wind = Vec3::Zero;           // 바람 (월드, m/s) — 모든 사슬에
	float WindTurbulence = 0.3f;      // 바람 흔들림 (0..1)
	bool ShowGizmos = true;

	DynamicBone();
	void ModifyPose(AnimatorPose& pose) override;
	int PoseOrder() const override { return 20; }   // 다리 · 손 · 시선 뒤
	void ResetSimulation() { m_State.clear(); }
	// 마지막 프레임의 꼬리 위치 (월드). 사슬 · 본 번호, 아직 없으면 false
	bool TailPosition(int chain, int joint, Vec3& out) const
	{
		if (chain < 0 || chain >= (int)m_State.size() || joint < 0 || joint >= (int)m_State[chain].size()) return false;
		out = Vec3(m_State[chain][joint].Cur.x, m_State[chain][joint].Cur.y, m_State[chain][joint].Cur.z);
		return true;
	}

	// 본 하나부터 자식을 따라 사슬을 만든다 (갈래마다 새 사슬). 스켈레톤 = 자식 SkinnedMeshRenderer 의 것. 만든 사슬 수
	int AddChainsFromBone(const std::string& rootBone, float stiffness, float drag, float gravity);

	void OnInspectorGUI() override;
	void OnDrawGizmos() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "animator"; }

	GENERATE_COMPONENT_BODY(DynamicBone)

private:
	struct TailState { XMFLOAT3 Prev, Cur; };
	std::vector<std::vector<TailState>> m_State;     // [사슬][본]
	std::vector<std::vector<int>> m_JointIndex;      // 본 번호 (스켈레톤)
	std::vector<int> m_ColliderIndex;
	const SkeletonAvataData* m_ResolvedFor = nullptr;
	size_t m_ResolvedChains = 0, m_ResolvedColliders = 0;
	XMFLOAT3 m_LastRoot = {};
	float m_Time = 0.0f;
	// 기즈모 (마지막 프레임, 월드)
	std::vector<std::vector<XMFLOAT3>> m_GizmoChains;
	struct GizmoCollider { XMFLOAT3 A, B; float R; };
	std::vector<GizmoCollider> m_GizmoColliders;
	void Resolve(const SkeletonAvataData& skeleton);
};

REGISTER_PACKAGE_COMPONENT(DynamicBone)
