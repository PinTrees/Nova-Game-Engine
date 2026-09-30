#include "pch.h"
#include "RigidBody.h"
#include "PhysicsManager.h"
#include "UnityGUI.h"

RigidBody::RigidBody()
{
	m_InspectorTitleName = "Rigidbody";
}

RigidBody::~RigidBody()
{
}

void RigidBody::SetMass(float mass)
{
	m_Mass = (std::max)(1e-7f, mass);
}

// ------------------------------------------------------------------ 속도
Vec3 RigidBody::GetVelocity()
{
	Vec3 v;
	if (PhysicsManager::GetI()->GetLinearVelocity(this, v))
		return v;
	return m_PendingVelocity;
}

void RigidBody::SetVelocity(const Vec3& velocity)
{
	if (!PhysicsManager::GetI()->SetLinearVelocity(this, velocity))
		m_PendingVelocity = velocity;
}

Vec3 RigidBody::GetAngularVelocity()
{
	Vec3 w;
	if (PhysicsManager::GetI()->GetAngularVelocity(this, w))
		return w;
	return m_PendingAngularVelocity;
}

void RigidBody::SetAngularVelocity(const Vec3& w)
{
	if (!PhysicsManager::GetI()->SetAngularVelocity(this, w))
		m_PendingAngularVelocity = w;
}

// ------------------------------------------------------------------ 힘
void RigidBody::AddForce(const Vec3& force, ForceMode mode)
{
	if (PhysicsManager::GetI()->AddForce(this, force, mode))
		return;
	// 바디가 아직 없으면 순간 속도 변화 모드만 보관한다 (Play 전 호출)
	if (mode == ForceMode::Impulse)
		m_PendingVelocity += force / m_Mass;
	else if (mode == ForceMode::VelocityChange)
		m_PendingVelocity += force;
}

void RigidBody::AddForce(const Vec3& force)
{
	AddForce(force, ForceMode::Force);
}

void RigidBody::AddTorque(const Vec3& torque, ForceMode mode)
{
	PhysicsManager::GetI()->AddTorque(this, torque, mode);
}

void RigidBody::AddTorque(const Vec3& torque)
{
	AddTorque(torque, ForceMode::Force);
}

void RigidBody::AddForceAtPosition(const Vec3& force, const Vec3& position, ForceMode mode)
{
	PhysicsManager::GetI()->AddForceAtPosition(this, force, position, mode);
}

void RigidBody::ApplyImpulse(const Vec3& impulse)
{
	AddForce(impulse, ForceMode::Impulse);
}

void RigidBody::MovePosition(const Vec3& position)
{
	if (!PhysicsManager::GetI()->MovePosition(this, position))
		GetGameObject()->GetTransform()->SetPosition(position);
}

void RigidBody::MoveRotation(const Quaternion& rotation)
{
	if (!PhysicsManager::GetI()->MoveRotation(this, rotation))
		GetGameObject()->GetTransform()->SetRotation(rotation);
}

bool RigidBody::IsSleeping() { return PhysicsManager::GetI()->IsSleeping(this); }
void RigidBody::Sleep() { PhysicsManager::GetI()->Sleep(this); }
void RigidBody::WakeUp() { PhysicsManager::GetI()->WakeUp(this); }

Vec3 RigidBody::GetWorldCenterOfMass()
{
	return PhysicsManager::GetI()->GetWorldCenterOfMass(this);
}

// ------------------------------------------------------------------ Inspector (Unity 6 Rigidbody)
void RigidBody::OnInspectorGUI()
{
	if (UnityGUI::Float("Mass", &m_Mass))
		SetMass(m_Mass);
	if (UnityGUI::Float("Linear Damping", &m_LinearDamping))
		SetLinearDamping(m_LinearDamping);
	if (UnityGUI::Float("Angular Damping", &m_AngularDamping))
		SetAngularDamping(m_AngularDamping);
	UnityGUI::Toggle("Automatic Center Of Mass", &m_AutomaticCenterOfMass);
	UnityGUI::Toggle("Automatic Tensor", &m_AutomaticTensor);
	UnityGUI::Toggle("Use Gravity", &m_UseGravity);
	UnityGUI::Toggle("Is Kinematic", &m_IsKinematic);

	static const char* interp[] = { "None", "Interpolate", "Extrapolate" };
	UnityGUI::Dropdown("Interpolate", &m_Interpolation, interp, 3);
	static const char* detect[] = { "Discrete", "Continuous", "Continuous Dynamic", "Continuous Speculative" };
	UnityGUI::Dropdown("Collision Detection", &m_CollisionDetection, detect, 4);

	if (UnityGUI::FoldoutPlain("Constraints", 0, false))
	{
		UnityGUI::Toggle3("Freeze Position", m_FreezePosition, 1);
		UnityGUI::Toggle3("Freeze Rotation", m_FreezeRotation, 1);
	}
	if (UnityGUI::FoldoutPlain("Layer Overrides", 0, false))
	{
		UnityGUI::ObjectField("Include Layers", "Nothing", 1);
		UnityGUI::ObjectField("Exclude Layers", "Nothing", 1);
	}
	if (UnityGUI::FoldoutPlain("Info", 0, false))
	{
		char buf[128];
		Vec3 v = GetVelocity();
		Vec3 w = GetAngularVelocity();
		sprintf_s(buf, "%.3f", v.Length());
		UnityGUI::ValueLabel("Speed", buf, 1);
		sprintf_s(buf, "X %.3f   Y %.3f   Z %.3f", v.x, v.y, v.z);
		UnityGUI::ValueLabel("Linear Velocity", buf, 1);
		sprintf_s(buf, "X %.3f   Y %.3f   Z %.3f", w.x, w.y, w.z);
		UnityGUI::ValueLabel("Angular Velocity", buf, 1);
		Vec3 com = GetWorldCenterOfMass();
		sprintf_s(buf, "X %.3f   Y %.3f   Z %.3f", com.x, com.y, com.z);
		UnityGUI::ValueLabel("World Center of Mass", buf, 1);
		UnityGUI::ValueLabel("Sleep State", PhysicsManager::GetI()->IsSimulating() ? (IsSleeping() ? "Asleep" : "Awake") : "-", 1);
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(RigidBody)
{
	json j = {};
	SERIALIZE_TYPE(j, RigidBody);
	j["mass"] = m_Mass;
	j["linearDamping"] = m_LinearDamping;
	j["angularDamping"] = m_AngularDamping;
	j["automaticCenterOfMass"] = m_AutomaticCenterOfMass;
	j["automaticTensor"] = m_AutomaticTensor;
	j["useGravity"] = m_UseGravity;
	j["isKinematic"] = m_IsKinematic;
	j["interpolation"] = m_Interpolation;
	j["collisionDetection"] = m_CollisionDetection;
	j["freezePosition"] = { m_FreezePosition[0], m_FreezePosition[1], m_FreezePosition[2] };
	j["freezeRotation"] = { m_FreezeRotation[0], m_FreezeRotation[1], m_FreezeRotation[2] };
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(RigidBody)
{
	SetMass(j.value("mass", 1.0f));
	// 이전 형식: m_LinearDamping / angularDamping 은 0.99 같은 "유지 비율"이었으므로 새 의미(감쇠 계수)로 옮기지 않는다
	m_LinearDamping = j.value("linearDamping", 0.0f);
	m_AngularDamping = j.contains("linearDamping") ? j.value("angularDamping", 0.05f) : 0.05f;
	m_AutomaticCenterOfMass = j.value("automaticCenterOfMass", true);
	m_AutomaticTensor = j.value("automaticTensor", true);
	m_UseGravity = j.value("useGravity", true);   // 이전 형식의 m_UseGravity 는 초기화되지 않은 값이 저장되었을 수 있어 무시한다
	m_IsKinematic = j.value("isKinematic", false);
	m_Interpolation = j.value("interpolation", 0);
	m_CollisionDetection = j.value("collisionDetection", 0);
	if (j.contains("freezePosition") && j["freezePosition"].is_array() && j["freezePosition"].size() == 3)
		for (int i = 0; i < 3; ++i) m_FreezePosition[i] = j["freezePosition"][i].get<bool>();
	if (j.contains("freezeRotation") && j["freezeRotation"].is_array() && j["freezeRotation"].size() == 3)
		for (int i = 0; i < 3; ++i) m_FreezeRotation[i] = j["freezeRotation"][i].get<bool>();
}
