#include "pch.h"
#include "CapsuleCollider.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"

CapsuleCollider::CapsuleCollider()
{
	m_InspectorTitleName = "Capsule Collider";
}

CapsuleCollider::~CapsuleCollider()
{
}

void CapsuleCollider::GetScaledDimensions(const Vec3& s, float& radius, float& halfCylinder) const
{
	const float sc[3] = { fabsf(s.x), fabsf(s.y), fabsf(s.z) };
	const int a = m_Direction, b = (m_Direction + 1) % 3, c = (m_Direction + 2) % 3;
	radius = m_Radius * (std::max)(sc[b], sc[c]);
	const float height = m_Height * sc[a];
	halfCylinder = (std::max)(0.0f, height * 0.5f - radius);
}

void CapsuleCollider::OnDrawGizmos()
{
	if (!ShouldDrawGizmo())
		return;

	Transform* tr = m_pGameObject->GetTransform();
	Matrix world = tr->GetWorldMatrix();
	Vec3 axes[3] = { world.Right(), world.Up(), world.Backward() };
	for (auto& a : axes) a.Normalize();

	float r, half;
	GetScaledDimensions(tr->GetScale(), r, half);
	const Vec3 c = Vec3::Transform(m_Center, world);
	const Vec3 d = axes[m_Direction];
	const Vec3 u = axes[(m_Direction + 1) % 3];
	const Vec3 v = axes[(m_Direction + 2) % 3];
	const Vec3 top = c + d * half, bottom = c - d * half;

	// 위/아래 원, 옆면 선 4개, 반구 호 4개
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

void CapsuleCollider::OnInspectorGUI()
{
	DrawCommonInspector();
	if (UnityGUI::Float("Radius", &m_Radius)) SetRadius(m_Radius);
	if (UnityGUI::Float("Height", &m_Height)) SetHeight(m_Height);
	static const char* dirs[] = { "X-Axis", "Y-Axis", "Z-Axis" };
	UnityGUI::Dropdown("Direction", &m_Direction, dirs, 3);
	UnityGUI::FoldoutPlain("Layer Overrides", 0, false);
}

GENERATE_COMPONENT_FUNC_TOJSON(CapsuleCollider)
{
	json j;
	j["type"] = "CapsuleCollider";
	j["radius"] = m_Radius;
	j["height"] = m_Height;
	j["direction"] = m_Direction;
	SerializeCommon(j);
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CapsuleCollider)
{
	m_Radius = j.value("radius", 0.5f);
	m_Height = j.value("height", 2.0f);
	m_Direction = std::clamp(j.value("direction", 1), 0, 2);
	DeserializeCommon(j);
}
