#pragma once
#include "Component.h"

// Unity 의 JointSpring / JointMotor / JointLimits (각도는 도)
struct JointSpringData { float Spring = 0.0f, Damper = 0.0f, TargetPosition = 0.0f; };
struct JointMotorData { float TargetVelocity = 0.0f, Force = 0.0f; bool FreeSpin = false; };
struct JointLimitsData { float Min = 0.0f, Max = 0.0f, Bounciness = 0.0f; };

// Unity 의 Joint 공통 (Fixed / Hinge / Spring). Rigidbody 가 있는 GameObject 에 붙이고, Connected Body(다른 Rigidbody 의 GameObject,
// 없으면 월드)와 잇는다. 실제 구속은 PhysicsManager 가 Play 중 Jolt Constraint 로 만든다.
// Break Force / Break Torque 를 넘으면 끊어지고(OnJointBreak) 컴포넌트가 지워진다 (Unity 와 같음).
class Joint
	: public Component
{
protected:
	uint64 m_ConnectedBody = 0;            // GameObject fileID (0 = 월드)
	Vec3 m_Anchor = Vec3::Zero;            // 내 로컬 좌표
	Vec3 m_Axis = Vec3(1, 0, 0);           // 내 로컬 좌표 (Hinge)
	bool m_AutoConfigureConnectedAnchor = true;
	Vec3 m_ConnectedAnchor = Vec3::Zero;   // 상대의 로컬 좌표 (상대가 없으면 월드)
	float m_BreakForce = std::numeric_limits<float>::infinity();
	float m_BreakTorque = std::numeric_limits<float>::infinity();
	bool m_EnableCollision = false;        // 이은 두 바디끼리 부딪힐지
	bool m_Broken = false;

	void SerializeCommon(json& j) const;
	void DeserializeCommon(const json& j);
	void DrawCommonTop(bool withAxis);     // Connected Body, Anchor, (Axis), Auto Configure, Connected Anchor
	void DrawCommonBottom();               // Break Force, Break Torque, Enable Collision
	void DrawAnchorGizmo(bool withAxis);

public:
	Joint();

	virtual int JointKind() const = 0;     // 0 Fixed, 1 Hinge, 2 Spring

	uint64 GetConnectedBody() const { return m_ConnectedBody; }
	void SetConnectedBody(uint64 fileID) { m_ConnectedBody = fileID; }
	Vec3 GetAnchor() const { return m_Anchor; }
	void SetAnchor(const Vec3& a) { m_Anchor = a; }
	Vec3 GetAxis() const { return m_Axis; }
	void SetAxis(const Vec3& a) { m_Axis = a; }
	bool GetAutoConfigureConnectedAnchor() const { return m_AutoConfigureConnectedAnchor; }
	void SetAutoConfigureConnectedAnchor(bool v) { m_AutoConfigureConnectedAnchor = v; }
	Vec3 GetConnectedAnchor() const { return m_ConnectedAnchor; }
	void SetConnectedAnchor(const Vec3& a) { m_ConnectedAnchor = a; }
	float GetBreakForce() const { return m_BreakForce; }
	void SetBreakForce(float f) { m_BreakForce = f < 0.0f ? 0.0f : f; }
	float GetBreakTorque() const { return m_BreakTorque; }
	void SetBreakTorque(float t) { m_BreakTorque = t < 0.0f ? 0.0f : t; }
	bool GetEnableCollision() const { return m_EnableCollision; }
	void SetEnableCollision(bool v) { m_EnableCollision = v; }
	bool IsBroken() const { return m_Broken; }
	void _SetBroken() { m_Broken = true; }

	// 구속 모양을 바꾸는 값의 해시 (바뀌면 PhysicsManager 가 다시 만든다)
	virtual size_t ParamsHash() const;

	void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;
	void OnDestroy() override;
	bool UsesUnityInspector() const override { return true; }
	bool HasEnabledToggle() const override { return false; }
};

class FixedJoint : public Joint
{
public:
	FixedJoint();
	int JointKind() const override { return 0; }
	void OnInspectorGUI() override;
	void OnDrawGizmos() override { DrawAnchorGizmo(false); }
	const char* InspectorIconName() const override { return "rigidbody"; }
	GENERATE_COMPONENT_BODY(FixedJoint)
};

class HingeJoint : public Joint
{
public:
	bool UseSpring = false;
	JointSpringData Spring;
	bool UseMotor = false;
	JointMotorData Motor;
	bool UseLimits = false;
	JointLimitsData Limits;

	HingeJoint();
	int JointKind() const override { return 1; }
	size_t ParamsHash() const override;
	void OnInspectorGUI() override;
	void OnDrawGizmos() override;
	const char* InspectorIconName() const override { return "rigidbody"; }

	// Play 중 현재 각도(도)·각속도(도/초) — Unity 의 HingeJoint.angle / velocity
	float GetAngle() const;
	float GetVelocity() const;
	GENERATE_COMPONENT_BODY(HingeJoint)
};

class SpringJoint : public Joint
{
public:
	float SpringValue = 10.0f;
	float Damper = 0.2f;
	float MinDistance = 0.0f;
	float MaxDistance = 0.0f;

	SpringJoint();
	int JointKind() const override { return 2; }
	size_t ParamsHash() const override;
	void OnInspectorGUI() override;
	void OnDrawGizmos() override { DrawAnchorGizmo(false); }
	const char* InspectorIconName() const override { return "rigidbody"; }
	GENERATE_COMPONENT_BODY(SpringJoint)
};

REGISTER_COMPONENT(FixedJoint)
REGISTER_COMPONENT(HingeJoint)
REGISTER_COMPONENT(SpringJoint)
