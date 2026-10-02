#pragma once
#include "Collider.h"

// Unity 의 CollisionFlags (Move 가 돌려주는 값, 비트 합)
enum class CollisionFlags : int
{
	None = 0,
	Sides = 1,
	Above = 2,
	Below = 4,
};

// OnControllerColliderHit 로 넘기는 한 번의 충돌 (Unity 의 ControllerColliderHit)
struct ControllerColliderHit
{
	Collider* collider = nullptr;
	GameObject* gameObject = nullptr;
	Vec3 point;
	Vec3 normal;
	Vec3 moveDirection;
	float moveLength = 0.0f;
};

// Unity 의 Character Controller: 리지드바디 없이 Move(이동량) 로 움직이며 벽을 따라 미끄러지고, 계단(Step Offset)을 오르고,
// Slope Limit 보다 가파른 경사는 오르지 못한다. 중력은 스크립트가 직접 더한다(SimpleMove 는 중력 포함).
// 실제 이동은 PhysicsManager 가 Jolt CharacterVirtual 로 한다(Play 중). 다른 물체에게는 키네마틱 캡슐로 보인다.
// 캡슐은 항상 세워져 있다(오브젝트 회전과 무관): 반지름 × max(|스케일 x|, |스케일 z|), 높이 × |스케일 y|.
class NOVA_API CharacterController
	: public Collider
{
private:
	float m_SlopeLimit = 45.0f;        // 도
	float m_StepOffset = 0.3f;         // m
	float m_SkinWidth = 0.08f;
	float m_MinMoveDistance = 0.001f;
	float m_Radius = 0.5f;
	float m_Height = 2.0f;
	bool m_DetectCollisions = true;    // 다른 물체가 이 캐릭터와 부딪히는지 (Unity 의 detectCollisions)

	// 실행 상태 (저장 안 함)
	Vec3 m_Velocity = Vec3::Zero;      // 마지막 Move 의 실제 이동량 / 시간
	int m_CollisionFlags = 0;
	bool m_IsGrounded = false;
	float m_FallSpeed = 0.0f;          // SimpleMove 가 쌓는 낙하 속도
	std::vector<ControllerColliderHit> m_Hits;   // 마지막 Move 에서 부딪힌 것

public:
	CharacterController();
	~CharacterController();

public:
	float GetSlopeLimit() const { return m_SlopeLimit; }
	void SetSlopeLimit(float v) { m_SlopeLimit = std::clamp(v, 0.0f, 180.0f); }
	float GetStepOffset() const { return m_StepOffset; }
	void SetStepOffset(float v) { m_StepOffset = (std::max)(0.0f, v); }
	float GetSkinWidth() const { return m_SkinWidth; }
	void SetSkinWidth(float v) { m_SkinWidth = (std::max)(0.0001f, v); }
	float GetMinMoveDistance() const { return m_MinMoveDistance; }
	void SetMinMoveDistance(float v) { m_MinMoveDistance = (std::max)(0.0f, v); }
	float GetRadius() const { return m_Radius; }
	void SetRadius(float r) { m_Radius = (std::max)(0.0f, r); }
	float GetHeight() const { return m_Height; }
	void SetHeight(float h) { m_Height = (std::max)(0.0f, h); }
	bool GetDetectCollisions() const { return m_DetectCollisions; }
	void SetDetectCollisions(bool v) { m_DetectCollisions = v; }

	// 월드 스케일을 적용한 반지름 / 원통부 절반 길이
	void GetScaledDimensions(const Vec3& lossyScale, float& radius, float& halfCylinder) const;

	// ---- Unity API ----
	int Move(const Vec3& motion);              // CollisionFlags 비트 합 (시간 = 이번 프레임)
	int Move(const Vec3& motion, float deltaTime);   // C# 이 Time.deltaTime 을 넘긴다 (FixedUpdate 안이면 고정 간격)
	bool SimpleMove(const Vec3& speed);        // 수평 속도(m/s, y 무시) + 중력. 땅에 닿아 있으면 true
	bool SimpleMove(const Vec3& speed, float deltaTime);
	bool IsGrounded() const { return m_IsGrounded; }
	Vec3 GetVelocity() const { return m_Velocity; }
	int GetCollisionFlags() const { return m_CollisionFlags; }
	const std::vector<ControllerColliderHit>& GetHits() const { return m_Hits; }

	// ---- PhysicsManager 전용 ----
	void _SetMoveResult(const Vec3& velocity, int flags, bool grounded) { m_Velocity = velocity; m_CollisionFlags = flags; m_IsGrounded = grounded; }
	std::vector<ControllerColliderHit>& _Hits() { return m_Hits; }

public:
	virtual void OnDrawGizmos() override;
	virtual void OnInspectorGUI() override;
	virtual void OnDestroy() override;
	virtual bool HasEnabledToggle() const override { return false; }   // Unity: Character Controller 는 끄는 체크박스가 없다
	virtual const char* InspectorIconName() const override { return "capsule_collider"; }

	GENERATE_COMPONENT_BODY(CharacterController)
};

REGISTER_COMPONENT(CharacterController)
