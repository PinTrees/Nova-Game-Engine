#include "pch.h"
#include "SphereCollider.h"

SphereCollider::SphereCollider()
    : m_Radius(0)
{
	m_InspectorTitleName = "SphereCollider";
}

SphereCollider::~SphereCollider()
{
}

float SphereCollider::GetRadius()
{
    //auto scale = GetGameObject()->GetTransform()->GetScale(); 
    return m_Radius;
}

void SphereCollider::OnDrawGizmos()
{
	Transform* transform = m_pGameObject->GetTransform();
    Gizmo::DrawSphere(transform->GetWorldMatrix(), m_Radius);
}

void SphereCollider::OnInspectorGUI()
{
	ImGui::DragFloat("Radius", &m_Radius);
}

GENERATE_COMPONENT_FUNC_TOJSON(SphereCollider)
{
    json j;
    j["type"] = "SphereCollider";
    j["radius"] = m_Radius;  // Add radius to JSON
    return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(SphereCollider)
{
    if (j.contains("radius"))  // Check if the JSON contains radius
    {
        m_Radius = j["radius"].get<float>();  // Set radius from JSON
    }
}