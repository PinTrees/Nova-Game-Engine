#include "pch.h"
#include "Collider.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"

Collider::Collider()
	: m_Center(Vec3::Zero)
{
}

Collider::~Collider()
{
}

Vec3 Collider::GetWorldCenter()
{
	Transform* transform = m_pGameObject ? m_pGameObject->GetTransform() : nullptr;
	if (transform == nullptr)
		return m_Center;
	return Vec3::Transform(m_Center, transform->GetWorldMatrix());
}

void Collider::SerializeCommon(json& j) const
{
	j["enabled"] = m_Enabled;
	j["isTrigger"] = m_IsTrigger;
	j["providesContacts"] = m_ProvidesContacts;
	j["center"] = { m_Center.x, m_Center.y, m_Center.z };
}

void Collider::DeserializeCommon(const json& j)
{
	m_Enabled = j.value("enabled", true);
	m_IsTrigger = j.value("isTrigger", false);
	m_ProvidesContacts = j.value("providesContacts", false);
	if (j.contains("center") && j.at("center").is_array() && j.at("center").size() == 3)
	{
		m_Center.x = j.at("center")[0].get<float>();
		m_Center.y = j.at("center")[1].get<float>();
		m_Center.z = j.at("center")[2].get<float>();
	}
}

void Collider::DrawCommonInspector(bool withCenter)
{
	UnityGUI::IconButtonRow("Edit Collider", "edit_collider");
	UnityGUI::Toggle("Is Trigger", &m_IsTrigger);
	UnityGUI::Toggle("Provides Contacts", &m_ProvidesContacts);
	UnityGUI::ObjectField("Material", "None (Physics Material)");
	if (withCenter)
		UnityGUI::Vector3("Center", &m_Center.x);
}

bool Collider::ShouldDrawGizmo() const
{
	return m_Enabled && m_pGameObject != nullptr && SceneViewOverlay::IsActive() &&
		SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT &&
		SelectionManager::GetSelectedGameObject() == m_pGameObject;
}

ImU32 Collider::GizmoColor()
{
	return IM_COL32(145, 244, 139, 210);   // Unity 콜라이더 기즈모 색
}

void Collider::GizmoCircle(const Vec3& c, const Vec3& u, const Vec3& v, float r, float a0, float a1, int segments)
{
	Vec3 prev = c + (u * cosf(a0) + v * sinf(a0)) * r;
	for (int i = 1; i <= segments; ++i)
	{
		float t = a0 + (a1 - a0) * i / segments;
		Vec3 p = c + (u * cosf(t) + v * sinf(t)) * r;
		SceneViewOverlay::DrawLine(XMFLOAT3(prev.x, prev.y, prev.z), XMFLOAT3(p.x, p.y, p.z), GizmoColor(), 1.0f);
		prev = p;
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(Collider)
{
	json j;
	j["type"] = "Collider";
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Collider)
{

}
