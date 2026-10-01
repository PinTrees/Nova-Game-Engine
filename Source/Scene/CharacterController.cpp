#include "pch.h"
#include "CharacterController.h"
#include "PhysicsManager.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"

CharacterController::CharacterController()
{
	m_InspectorTitleName = "Character Controller";
	m_Center = Vec3::Zero;
}

CharacterController::~CharacterController()
{
}

void CharacterController::GetScaledDimensions(const Vec3& s, float& radius, float& halfCylinder) const
{
	radius = m_Radius * (std::max)(fabsf(s.x), fabsf(s.z));
	const float height = m_Height * fabsf(s.y);
	halfCylinder = (std::max)(0.0f, height * 0.5f - radius);
}

int CharacterController::Move(const Vec3& motion)
{
	return Move(motion, (float)DT);
}

int CharacterController::Move(const Vec3& motion, float deltaTime)
{
	return PhysicsManager::GetI()->MoveCharacter(this, motion, deltaTime);
}

bool CharacterController::SimpleMove(const Vec3& speed)
{
	return SimpleMove(speed, (float)DT);
}

bool CharacterController::SimpleMove(const Vec3& speed, float deltaTime)
{
	// Unity: y 는 무시하고, 땅에 있지 않으면 중력으로 떨어지는 속도를 쌓는다
	const float dt = (std::max)(deltaTime, 1e-4f);
	if (m_IsGrounded)
		m_FallSpeed = 0.0f;
	m_FallSpeed += PhysicsManager::GetI()->GetGravity().y * dt;
	Move(Vec3(speed.x * dt, m_FallSpeed * dt, speed.z * dt), dt);
	return m_IsGrounded;
}

void CharacterController::OnDestroy()
{
	PhysicsManager::GetI()->RemoveCharacter(this);
}

void CharacterController::OnDrawGizmos()
{
	if (!ShouldDrawGizmo())
		return;
	Transform* tr = m_pGameObject->GetTransform();
	float r, half;
	GetScaledDimensions(tr->GetScale(), r, half);
	// 캡슐은 항상 세로 (월드 축)
	const Vec3 c = Vec3::Transform(m_Center, tr->GetWorldMatrix());
	const Vec3 d(0, 1, 0), u(1, 0, 0), v(0, 0, 1);
	const Vec3 top = c + d * half, bottom = c - d * half;
	GizmoCircle(top, u, v, r);
	GizmoCircle(bottom, u, v, r);
	const Vec3 sides[4] = { u, -u, v, -v };
	for (const Vec3& sdir : sides)
	{
		Vec3 a = top + sdir * r, b = bottom + sdir * r;
		SceneViewOverlay::DrawLine(XMFLOAT3(a.x, a.y, a.z), XMFLOAT3(b.x, b.y, b.z), GizmoColor(), 1.0f);
	}
	GizmoCircle(top, u, d, r, 0.0f, XM_PI, 24);
	GizmoCircle(top, v, d, r, 0.0f, XM_PI, 24);
	GizmoCircle(bottom, u, -d, r, 0.0f, XM_PI, 24);
	GizmoCircle(bottom, v, -d, r, 0.0f, XM_PI, 24);
}

void CharacterController::OnInspectorGUI()
{
	// Unity 의 Character Controller 인스펙터 순서
	if (UnityGUI::Float("Slope Limit", &m_SlopeLimit)) SetSlopeLimit(m_SlopeLimit);
	if (UnityGUI::Float("Step Offset", &m_StepOffset)) SetStepOffset(m_StepOffset);
	if (UnityGUI::Float("Skin Width", &m_SkinWidth)) SetSkinWidth(m_SkinWidth);
	if (UnityGUI::Float("Min Move Distance", &m_MinMoveDistance)) SetMinMoveDistance(m_MinMoveDistance);
	UnityGUI::Vector3("Center", &m_Center.x);
	if (UnityGUI::Float("Radius", &m_Radius)) SetRadius(m_Radius);
	if (UnityGUI::Float("Height", &m_Height)) SetHeight(m_Height);
	if (m_StepOffset > m_Height + 2.0f * m_Radius)
		UnityGUI::HelpBox("Step Offset must be less or equal to Height + Radius * 2.");
	DrawLayerOverrides();
}

GENERATE_COMPONENT_FUNC_TOJSON(CharacterController)
{
	json j;
	j["type"] = "CharacterController";
	j["slopeLimit"] = m_SlopeLimit;
	j["stepOffset"] = m_StepOffset;
	j["skinWidth"] = m_SkinWidth;
	j["minMoveDistance"] = m_MinMoveDistance;
	j["radius"] = m_Radius;
	j["height"] = m_Height;
	j["detectCollisions"] = m_DetectCollisions;
	SerializeCommon(j);
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CharacterController)
{
	SetSlopeLimit(j.value("slopeLimit", 45.0f));
	SetStepOffset(j.value("stepOffset", 0.3f));
	SetSkinWidth(j.value("skinWidth", 0.08f));
	SetMinMoveDistance(j.value("minMoveDistance", 0.001f));
	SetRadius(j.value("radius", 0.5f));
	SetHeight(j.value("height", 2.0f));
	m_DetectCollisions = j.value("detectCollisions", true);
	DeserializeCommon(j);
	m_IsTrigger = false;
}
