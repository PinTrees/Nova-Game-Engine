#include "pch.h"
#include "MonoBehaviour.h"
#include "GameObject.h"
#include "Transform.h"

MonoBehaviour::MonoBehaviour()
{
	m_InspectorTitleName = "Script (MonoBehaviour)";
	m_InspectorIconPath = L"\\ProjectSetting\\icons\\icon_gameobject.png";
}

MonoBehaviour::~MonoBehaviour()
{
}

void MonoBehaviour::SetEnabled(bool enabled)
{
	if (m_IsEnabled != enabled)
	{
		m_IsEnabled = enabled;
		if (m_IsEnabled)
			OnEnable();
		else
			OnDisable();
	}
}

Transform* MonoBehaviour::GetTransform()
{
	return m_pGameObject ? m_pGameObject->GetTransform() : nullptr;
}

Transform* MonoBehaviour::transform()
{
	return GetTransform();
}

GameObject* MonoBehaviour::gameObject()
{
	return GetGameObject();
}

void MonoBehaviour::RegisterField(IScriptField* field)
{
	if (field)
	{
		m_Fields.push_back(field);
	}
}

void MonoBehaviour::OnInspectorGUI()
{
	EditorGUI::BoolField("Enabled", m_IsEnabled);
	EditorGUI::ComponentDivider();

	if (m_Fields.empty())
	{
		EditorGUI::Label("No serialized fields in script.");
	}
	else
	{
		for (auto* field : m_Fields)
		{
			if (field)
			{
				field->OnInspectorGUI();
			}
		}
	}
}

json MonoBehaviour::toJson() const
{
	json j;
	j["type"] = GetType();
	j["scriptName"] = m_ScriptName;
	j["enabled"] = m_IsEnabled;

	json fieldsJson = json::object();
	for (const auto* field : m_Fields)
	{
		if (field)
		{
			field->Serialize(fieldsJson);
		}
	}
	j["fields"] = fieldsJson;

	return j;
}

void MonoBehaviour::fromJson(const json& j)
{
	if (j.contains("scriptName") && j["scriptName"].is_string())
	{
		m_ScriptName = j["scriptName"].get<std::string>();
		m_InspectorTitleName = m_ScriptName + " (Script)";
	}

	if (j.contains("enabled") && j["enabled"].is_boolean())
	{
		m_IsEnabled = j["enabled"].get<bool>();
	}

	if (j.contains("fields") && j["fields"].is_object())
	{
		const json& fieldsJson = j["fields"];
		for (auto* field : m_Fields)
		{
			if (field)
			{
				field->Deserialize(fieldsJson);
			}
		}
	}
}
