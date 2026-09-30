#pragma once
#include "Component.h"

enum class ForceMode;

// Unity 의 Rigidbody. 실제 시뮬레이션은 PhysicsManager(Jolt Physics)가 담당하고,
// 이 컴포넌트는 설정 값과 Unity 스타일 API(velocity, AddForce, MovePosition ...)를 제공한다.
class RigidBody : public Component
{
public:
	enum class Interpolation { None, Interpolate, Extrapolate };
	enum class CollisionDetection { Discrete, Continuous, ContinuousDynamic, ContinuousSpeculative };

private:
	float m_Mass = 1.0f;
	float m_LinearDamping = 0.0f;      // Unity 6: Linear Damping (구 Drag)
	float m_AngularDamping = 0.05f;    // Unity 6: Angular Damping (구 Angular Drag)
	bool m_AutomaticCenterOfMass = true;
	bool m_AutomaticTensor = true;
	bool m_UseGravity = true;
	bool m_IsKinematic = false;
	int m_Interpolation = 0;           // Interpolation
	int m_CollisionDetection = 0;      // CollisionDetection
	bool m_FreezePosition[3] = { false, false, false };
	bool m_FreezeRotation[3] = { false, false, false };

	// Play 전에 설정된 속도 (바디가 만들어질 때 적용)
	Vec3 m_PendingVelocity = Vec3::Zero;
	Vec3 m_PendingAngularVelocity = Vec3::Zero;

	// PhysicsManager 가 관리하는 Jolt 바디 ID (없으면 0xffffffff)
	uint32 m_BodyId = 0xffffffff;

public:
	RigidBody();
	virtual ~RigidBody();

public:
	// ---- 설정 ----
	void SetMass(float mass);
	float GetMass() const { return m_Mass; }
	float GetInverseMass() const { return m_Mass > 0.0f ? 1.0f / m_Mass : 0.0f; }
	void SetLinearDamping(float d) { m_LinearDamping = (std::max)(0.0f, d); }
	float GetLinearDamping() const { return m_LinearDamping; }
	void SetAngularDamping(float d) { m_AngularDamping = (std::max)(0.0f, d); }
	float GetAngularDamping() const { return m_AngularDamping; }
	void SetUseGravity(bool use) { m_UseGravity = use; }
	bool GetUseGravity() const { return m_UseGravity; }
	void SetKinematic(bool isKinematic) { m_IsKinematic = isKinematic; }
	bool IsKinematic() const { return m_IsKinematic; }
	Interpolation GetInterpolation() const { return (Interpolation)m_Interpolation; }
	void SetInterpolation(Interpolation i) { m_Interpolation = (int)i; }
	CollisionDetection GetCollisionDetection() const { return (CollisionDetection)m_CollisionDetection; }
	void SetCollisionDetection(CollisionDetection c) { m_CollisionDetection = (int)c; }
	bool IsPositionFrozen(int axis) const { return m_FreezePosition[axis]; }
	bool IsRotationFrozen(int axis) const { return m_FreezeRotation[axis]; }
	void SetFreezePosition(int axis, bool freeze) { m_FreezePosition[axis] = freeze; }
	void SetFreezeRotation(int axis, bool freeze) { m_FreezeRotation[axis] = freeze; }

	// ---- Unity API ----
	Vec3 GetVelocity();                           // Rigidbody.linearVelocity
	void SetVelocity(const Vec3& velocity);
	Vec3 GetAngularVelocity();                    // Rigidbody.angularVelocity (rad/s, 월드)
	void SetAngularVelocity(const Vec3& w);
	void AddForce(const Vec3& force, ForceMode mode);
	void AddForce(const Vec3& force);             // ForceMode.Force
	void AddTorque(const Vec3& torque, ForceMode mode);
	void AddTorque(const Vec3& torque);
	void AddForceAtPosition(const Vec3& force, const Vec3& position, ForceMode mode);
	void MovePosition(const Vec3& position);
	void MoveRotation(const Quaternion& rotation);
	bool IsSleeping();
	void Sleep();
	void WakeUp();
	Vec3 GetWorldCenterOfMass();

	// 이전 API 호환
	void ApplyForce(const Vec3& force) { AddForce(force); }
	void ApplyImpulse(const Vec3& impulse);
	void ApplyTorque(const Vec3& torque) { AddTorque(torque); }
	Vec3 GetRotationVelocity() { return GetAngularVelocity(); }
	void SetRotationVelocity(const Vec3& w) { SetAngularVelocity(w); }

	// ---- PhysicsManager 전용 ----
	uint32 _GetBodyId() const { return m_BodyId; }
	void _SetBodyId(uint32 id) { m_BodyId = id; }
	Vec3 _TakePendingVelocity() { Vec3 v = m_PendingVelocity; m_PendingVelocity = Vec3::Zero; return v; }
	Vec3 _TakePendingAngularVelocity() { Vec3 v = m_PendingAngularVelocity; m_PendingAngularVelocity = Vec3::Zero; return v; }

public:
	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual bool HasEnabledToggle() const override { return false; }
	virtual const char* InspectorIconName() const override { return "rigidbody"; }

	GENERATE_COMPONENT_BODY(RigidBody)
};

REGISTER_COMPONENT(RigidBody);
