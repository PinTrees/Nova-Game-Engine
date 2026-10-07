#pragma once
#include "Component.h"

// 래그돌: 사람 뼈대 (Skinned Mesh) 의 본마다 물리 바디 (Rigidbody + Collider + Character Joint 가 붙은 GameObject) 를 잇는다.
//  - Active (켜짐): 바디가 뼈대 자세를 정한다 — Animator · Animation 을 끄고 바디를 다이내믹으로 (쓰러진다)
//  - 꺼짐: 바디가 애니메이션 자세를 따라간다 (키네마틱 — 다른 물체를 밀어낸다)
//  - 켜짐 → 꺼짐 (일어나기): 루트를 쓰러진 골반 자리 · 방향으로 옮기고, 쓰러진 자세에서 애니메이션 (일어나기 클립) 으로 Blend Time 동안 섞는다
// Unity 는 본이 Transform 이라 바디를 본 자체에 붙이지만, NOVA 의 본은 스키닝 자세 안에 있어 이 컴포넌트가 둘을 잇는다.
// 만들기: GameObject > 3D Object > Ragdoll (Unity 의 Ragdoll Wizard), CLI `nova ragdoll create <대상>`
class Ragdoll : public Component
{
public:
	struct Part
	{
		std::string Bone;             // 스켈레톤 노드 이름
		uint64 Body = 0;              // 바디 GameObject fileID
		Matrix Offset;                // 본 월드 = Offset × 바디 월드 (본의 크기 · 방향을 담는다)
	};
	std::vector<Part> Parts;
	bool Active = true;
	float BlendTime = 0.5f;           // 꺼질 때 쓰러진 자세 → 애니메이션으로 섞는 시간 (초, 0 = 바로)
	bool AlignRoot = true;            // 꺼질 때 루트를 골반 자리로 · 일어서는 방향 (등 = 발 쪽, 배 = 머리 쪽) 으로 돌린다

	Ragdoll();
	void LateUpdate() override;
	void OnInspectorGUI() override;
	void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "rigidbody"; }

	// Ragdoll Wizard: root 아래 휴머노이드 Skinned Mesh 의 지금 자세로 바디 11 개 (골반 · 척추 · 머리 · 팔 4 · 다리 4) 를 만든다.
	// 이미 래그돌이 있으면 실패. bodies = 만든 바디 수
	static Ragdoll* Build(GameObject* root, float totalMass, std::string& error);

	// 쓰러진 몸이 하늘을 보는가 (골반 바디의 배 쪽이 위) — 일어나기 클립 고르기 (GetUpBack · GetUpFront)
	bool IsFaceUp() const;
	bool IsBlending() const { return m_BlendLeft > 0.0f; }

	GENERATE_COMPONENT_BODY(Ragdoll)

private:
	bool m_Applied = false;          // Play 중 상태를 한 번이라도 적용했다
	bool m_AppliedActive = false;
	std::vector<Component*> m_DisabledAnimation;   // 켤 때 끈 Animator · Animation (끌 때 다시 켠다)
	std::vector<XMFLOAT4X4> m_BaseLocal;            // 켤 때 자세의 노드 로컬 (바디가 없는 노드 — 손 · 발 · 손가락 · 목)
	std::vector<std::vector<XMFLOAT4X4>> m_BlendFrom;   // 꺼질 때의 자세 (월드, Skinned Mesh 마다) — 애니메이션으로 섞는다
	float m_BlendLeft = 0.0f;
	void SetActiveState(bool active);
	GameObject* PelvisBody() const;
	void BeginRecover();
};

REGISTER_COMPONENT(Ragdoll)
