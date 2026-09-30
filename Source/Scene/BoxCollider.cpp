#include "pch.h"
#include "BoxCollider.h"
#include "SceneViewOverlay.h"
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
    if (!ShouldDrawGizmo())
        return;
    // Center 를 중심으로 하는 크기 m_Size 상자의 12 모서리 (오브젝트의 월드 행렬 적용)
    Matrix world = m_pGameObject->GetTransform()->GetWorldMatrix();
    Vec3 h = m_Size * 0.5f;
    Vec3 c[8];
    for (int i = 0; i < 8; ++i)
    {
        Vec3 local(m_Center.x + ((i & 1) ? h.x : -h.x), m_Center.y + ((i & 2) ? h.y : -h.y), m_Center.z + ((i & 4) ? h.z : -h.z));
        c[i] = Vec3::Transform(local, world);
    }
    static const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
    for (const auto& e : edges)
        SceneViewOverlay::DrawLine(XMFLOAT3(c[e[0]].x, c[e[0]].y, c[e[0]].z), XMFLOAT3(c[e[1]].x, c[e[1]].y, c[e[1]].z), GizmoColor(), 1.0f);
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
