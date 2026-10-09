#include "pch.h"
#include "SphereCollider.h"
#include "UnityGUI.h"

SphereCollider::SphereCollider()
    : m_Radius(0.5f)
{
	m_InspectorTitleName = "Sphere Collider";
}

SphereCollider::~SphereCollider()
{
}

float SphereCollider::GetRadius()
{
    return m_Radius;
}

void SphereCollider::OnDrawGizmos()
{
	if (!ShouldDrawGizmo())
		return;
	// Unity 와 같이 반지름은 가장 큰 스케일 축을 따른다
	Transform* transform = m_pGameObject->GetTransform();
	Matrix world = transform->GetWorldMatrix();
	Vec3 s = transform->GetScale();
	float r = m_Radius * (std::max)((std::max)(fabsf(s.x), fabsf(s.y)), fabsf(s.z));
	Vec3 c = Vec3::Transform(m_Center, world);
	Vec3 ax = world.Right(), ay = world.Up(), az = world.Backward();
	ax.Normalize(); ay.Normalize(); az.Normalize();
	GizmoCircle(c, ax, ay, r);
	GizmoCircle(c, ay, az, r);
	GizmoCircle(c, az, ax, r);
}

void SphereCollider::OnInspectorGUI()
{
	DrawCommonInspector();
	UnityGUI::Float("Radius", &m_Radius);
	DrawLayerOverrides();
}

GENERATE_COMPONENT_FUNC_TOJSON(SphereCollider)
{
    json j;
    j["type"] = "SphereCollider";
    j["radius"] = m_Radius;
    SerializeCommon(j);
    return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(SphereCollider)
{
	MarkPhysicsDirty();   // 물리 동기화가 이 소유자를 다시 본다
    if (j.contains("radius"))
        m_Radius = j["radius"].get<float>();
    DeserializeCommon(j);
}
