#include "pch.h"
#include "Collider.h"
#include "UnityGUI.h"

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

void Collider::DrawCommonInspector()
{
	UnityGUI::IconButtonRow("Edit Collider", "edit_collider");
	UnityGUI::Toggle("Is Trigger", &m_IsTrigger);
	UnityGUI::Toggle("Provides Contacts", &m_ProvidesContacts);
	UnityGUI::ObjectField("Material", "None (Physics Material)");
	UnityGUI::Vector3("Center", &m_Center.x);
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