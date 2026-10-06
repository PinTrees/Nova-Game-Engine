#pragma once
#include "Component.h"

// 래그돌: 사람 뼈대 (Skinned Mesh) 의 본마다 물리 바디 (Rigidbody + Collider + Character Joint 가 붙은 GameObject) 를 잇는다.
//  - Active (켜짐): 바디가 뼈대 자세를 정한다 — Animator · Animation 을 끄고 바디를 다이내믹으로 (쓰러진다)
//  - 꺼짐: 바디가 애니메이션 자세를 따라간다 (키네마틱 — 다른 물체를 밀어낸다)
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

	Ragdoll();
	void LateUpdate() override;
	void OnInspectorGUI() override;
	void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "rigidbody"; }

	// Ragdoll Wizard: root 아래 휴머노이드 Skinned Mesh 의 지금 자세로 바디 11 개 (골반 · 척추 · 머리 · 팔 4 · 다리 4) 를 만든다.
	// 이미 래그돌이 있으면 실패. bodies = 만든 바디 수
	static Ragdoll* Build(GameObject* root, float totalMass, std::string& error);

	GENERATE_COMPONENT_BODY(Ragdoll)

private:
	bool m_Applied = false;          // Play 중 상태를 한 번이라도 적용했다
	bool m_AppliedActive = false;
	std::vector<Component*> m_DisabledAnimation;   // 켤 때 끈 Animator · Animation (끌 때 다시 켠다)
	std::vector<XMFLOAT4X4> m_BaseLocal;            // 켤 때 자세의 노드 로컬 (바디가 없는 노드 — 손 · 발 · 손가락 · 목)
	void SetActiveState(bool active);
};

REGISTER_COMPONENT(Ragdoll)
