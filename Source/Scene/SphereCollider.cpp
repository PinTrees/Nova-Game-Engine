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
	Transform* transform = m_pGameObject->GetTransform();
	XMMATRIX world = transform->GetWorldMatrix();
	XMMATRIX centered = XMMatrixTranslation(m_Center.x, m_Center.y, m_Center.z) * world;
    Gizmo::DrawSphere(centered, m_Radius);
}

void SphereCollider::OnInspectorGUI()
{
	DrawCommonInspector();
	UnityGUI::Float("Radius", &m_Radius);
	UnityGUI::FoldoutPlain("Layer Overrides", 0, false);
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
    if (j.contains("radius"))
        m_Radius = j["radius"].get<float>();
    DeserializeCommon(j);
}
