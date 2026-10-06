#pragma once
#include "Component.h"
#include "Joint.h"

class RigidBody;
class Collider;

// Unity 의 WheelFrictionCurve: 미끄러짐 → 힘 (타이어 하중의 배수). 0 → Extremum 까지 오르고 Asymptote 까지 내려간 뒤 그대로
struct WheelFrictionCurveData
{
	float ExtremumSlip = 0.4f, ExtremumValue = 1.0f, AsymptoteSlip = 0.8f, AsymptoteValue = 0.5f, Stiffness = 1.0f;
	float Evaluate(float slip) const;   // |slip| → 배수 (Stiffness 포함)
};

// Unity 의 WheelCollider: 차량 바퀴 (Rigidbody 가 있는 부모 아래 자식 오브젝트에 붙인다).
//  - 서스펜션: 바퀴 중심 (Center = 가장 눌린 자리) 에서 로컬 -Y 로 레이를 쏴 바닥까지 거리 → 늘어난 길이. 힘 = 매달린 질량 × g + Spring × (쉬는 자리 - 길이) + Damper × 눌리는 속도
//    (PhysX 와 같이 Target Position 에서 정확히 멈춘다: 1 = 다 늘어남, 0 = 다 눌림)
//  - 타이어: 앞 · 옆 미끄러짐을 Forward · Sideways Friction 곡선에 넣어 하중 × 배수의 힘
//  - 바퀴 회전: Motor Torque · Brake Torque · Wheel Damping Rate · 마찰 반작용 (관성 = ½ m r²). Steer Angle = 로컬 Y 둘레 (도, + = 오른쪽)
// 콜라이더 형상은 만들지 않는다 (레이만 — Unity 와 같이 차체 콜라이더와 따로)
class WheelCollider : public Component
{
public:
	Vec3 Center = Vec3::Zero;
	float Radius = 0.5f;
	float Mass = 20.0f;
	float WheelDampingRate = 0.25f;
	float SuspensionDistance = 0.3f;
	float ForceAppPointDistance = 0.0f;
	JointSpringData SuspensionSpring{ 35000.0f, 4500.0f, 0.5f };
	WheelFrictionCurveData ForwardFriction{ 0.4f, 1.0f, 0.8f, 0.5f, 1.0f };
	WheelFrictionCurveData SidewaysFriction{ 0.2f, 1.0f, 0.5f, 0.75f, 1.0f };

	// 입력 (Play 중 스크립트) — 저장하지 않는다
	float MotorTorque = 0.0f, BrakeTorque = 0.0f, SteerAngle = 0.0f;

	struct GroundHit
	{
		Vec3 Point, Normal, ForwardDir, SidewaysDir;
		float Force = 0.0f, ForwardSlip = 0.0f, SidewaysSlip = 0.0f;
		GameObject* Object = nullptr;
	};

	WheelCollider();
	bool IsGrounded() const { return m_Grounded; }
	float Rpm() const;
	float SprungMass() const { return m_SprungMass; }
	// 바퀴 그림이 놓일 자리 (서스펜션 길이 · 조향 · 회전 포함)
	void GetWorldPose(Vec3& position, Quaternion& rotation);
	bool GetGroundHit(GroundHit& hit) const { if (m_Grounded) hit = m_Hit; return m_Grounded; }

	void FixedUpdate() override;
	void OnInspectorGUI() override;
	void OnDrawGizmos() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "sphere_collider"; }
	GENERATE_COMPONENT_BODY(WheelCollider)

private:
	float m_Omega = 0.0f;          // 바퀴 각속도 (rad/s)
	float m_Spin = 0.0f;           // 바퀴 회전 각 (rad, 그림)
	float m_Extension = -1.0f;     // 서스펜션이 늘어난 길이 (0 ~ Suspension Distance, -1 = 아직)
	float m_SprungMass = 0.0f;
	int m_WheelCount = 1;          // 같은 차의 바퀴 수 (마찰을 나눠 맡는다)
	bool m_Grounded = false;
	GroundHit m_Hit;
	RigidBody* Body() const;
};

REGISTER_COMPONENT(WheelCollider)
