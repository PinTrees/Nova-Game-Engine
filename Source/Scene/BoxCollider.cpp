#include "pch.h"
#include "BoxCollider.h"

BoxCollider::BoxCollider()
    : m_Size(Vec3::One),
    m_Offset(Vec3::Zero)
{
    m_InspectorTitleName = "BoxCollider";
}

BoxCollider::~BoxCollider()
{

}

Vec3 BoxCollider::GetSize()
{
    return m_Size; 
}

void BoxCollider::OnDrawGizmos()
{
    Gizmo::DrawCube(m_pGameObject->GetComponent<Transform>()->GetWorldMatrix(), m_Size); 
}

void BoxCollider::OnInspectorGUI()
{
    if (ImGui::DragFloat3("Size", &m_Size.x))
    {
    }

    if (ImGui::DragFloat3("Offset", &m_Offset.x))
    {
    }
}


GENERATE_COMPONENT_FUNC_TOJSON(BoxCollider)
{
    json j;
    j["type"] = "BoxCollider";
    j["size"] = { m_Size.x, m_Size.y, m_Size.z };
    j["offset"] = { m_Offset.x, m_Offset.y, m_Offset.z };
    return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(BoxCollider)
{
    if (j.contains("size"))
    {
        m_Size.x = j.at("size")[0];
        m_Size.y = j.at("size")[1];
        m_Size.z = j.at("size")[2];
    }

    if (j.contains("offset"))
    {
        m_Offset.x = j.at("offset")[0];
        m_Offset.y = j.at("offset")[1];
        m_Offset.z = j.at("offset")[2];
    }
}