#include "pch.h"
#include "BoxCollider.h"
#include "UnityGUI.h"

BoxCollider::BoxCollider()
    : m_Size(Vec3::One)
{
    m_InspectorTitleName = "Box Collider";
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
    // Center 만큼 이동한 위치에 크기 m_Size 상자를 그린다
    XMMATRIX world = m_pGameObject->GetComponent<Transform>()->GetWorldMatrix();
    XMMATRIX centered = XMMatrixTranslation(m_Center.x, m_Center.y, m_Center.z) * world;
    Gizmo::DrawCube(centered, m_Size);
}

void BoxCollider::OnInspectorGUI()
{
    DrawCommonInspector();
    UnityGUI::Vector3("Size", &m_Size.x);
    UnityGUI::FoldoutPlain("Layer Overrides", 0, false);
}

GENERATE_COMPONENT_FUNC_TOJSON(BoxCollider)
{
    json j;
    j["type"] = "BoxCollider";
    j["size"] = { m_Size.x, m_Size.y, m_Size.z };
    SerializeCommon(j);
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
    DeserializeCommon(j);

    // 이전 버전의 "offset" 은 Center 로 이전
    if (!j.contains("center") && j.contains("offset"))
    {
        m_Center.x = j.at("offset")[0];
        m_Center.y = j.at("offset")[1];
        m_Center.z = j.at("offset")[2];
    }
}
