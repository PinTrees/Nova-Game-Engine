#include "pch.h"
#include "UIButton.h"
#include "ScriptEngine.h"

Button::Button()
{
	m_InspectorTitleName = "Button";
}

void Button::OnClick()
{
	if (!m_Interactable || m_pGameObject == nullptr)
		return;
	EditorLog::Write("UI", "Button '%s' clicked (%d persistent calls)", m_pGameObject->GetName().c_str(), (int)m_OnClick.Calls.size());
	m_OnClick.Invoke(m_pGameObject, "On Click");
	ScriptEngine::InvokeUIEvent(m_pGameObject->GetFileID(), 0);   // C# button.onClick.AddListener(...)
}

void Button::OnInspectorGUI()
{
	DrawSelectableInspector();
	m_OnClick.Draw("On Click ()", "");
}

GENERATE_COMPONENT_FUNC_TOJSON(Button)
{
	json j;
	SERIALIZE_TYPE(j, Button);
	SelectableToJson(j);
	j["onClick"] = m_OnClick.ToJson();
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Button)
{
	SelectableFromJson(j);
	m_OnClick.FromJson(j.contains("onClick") ? j["onClick"] : json::array());
}
