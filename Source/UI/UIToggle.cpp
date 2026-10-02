#include "pch.h"
#include "UIToggle.h"
#include "UIGraphic.h"
#include "UnityGUI.h"
#include "ScriptEngine.h"

Toggle::Toggle()
{
	m_InspectorTitleName = "Toggle";
}

void Toggle::SetIsOn(bool on, bool notify)
{
	if (m_IsOn == on)
		return;
	m_IsOn = on;
	if (notify && m_pGameObject && Application::IsPlaying())
	{
		m_OnValueChanged.Invoke(m_pGameObject, "On Value Changed", on ? "true" : "false");
		ScriptEngine::InvokeUIEvent(m_pGameObject->GetFileID(), 2, on ? 1.0f : 0.0f);
	}
}

void Toggle::OnClick()
{
	if (m_Interactable)
		SetIsOn(!m_IsOn);
}

void Toggle::UpdateBeforeLayout(float dt, bool playing)
{
	// 체크 표시: 켜지면 보이고 꺼지면 투명 (Fade 면 0.1초 동안)
	const float target = m_IsOn ? 1.0f : 0.0f;
	if (!playing || m_ToggleTransition == 0)
		m_Alpha = target;
	else
		m_Alpha += (target - m_Alpha) * std::clamp(dt / 0.1f, 0.0f, 1.0f);
	if (GameObject* go = FindObject(m_Graphic))
		if (UIGraphic* g = go->GetComponent<UIGraphic>())
		{
			const float tint[4] = { 1.0f, 1.0f, 1.0f, m_Alpha };
			g->SetTint(tint);
		}
}

void Toggle::OnInspectorGUI()
{
	DrawSelectableInspector();
	bool on = m_IsOn;
	if (UnityGUI::Toggle("Is On", &on))
		m_IsOn = on;
	static const char* kTransitions[] = { "None", "Fade" };
	UnityGUI::Dropdown("Toggle Transition", &m_ToggleTransition, kTransitions, 2);

	// Graphic [ 체크 표시 ⊙ ]
	GameObject* g = FindObject(m_Graphic);
	const std::string text = g ? g->GetName() + " (Image)" : "None (Graphic)";
	ImVec2 fmin, fmax;
	const int pressed = UnityGUI::ObjectFieldButtons("Graphic", text.c_str(), "ui_image", nullptr, 0, &fmin, &fmax);
	const ImVec2 after = ImGui::GetCursorScreenPos();
	ImGui::SetCursorScreenPos(fmin);
	ImGui::InvisibleButton("##toggleGraphic", ImVec2((std::max)(1.0f, fmax.x - fmin.x - 22.0f), fmax.y - fmin.y));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAME_OBJECT"))
			if (GameObject* go = *(GameObject**)payload->Data)
				m_Graphic = go->GetFileID();
		ImGui::EndDragDropTarget();
	}
	if (pressed == -1)
		m_Graphic = 0;
	ImGui::SetCursorScreenPos(after);
	UnityGUI::ValueLabel("Group", "None (Toggle Group)");
	UnityGUI::Spacing(6.0f);
	m_OnValueChanged.Draw("On Value Changed (Boolean)", "bool");
}

GENERATE_COMPONENT_FUNC_TOJSON(Toggle)
{
	json j;
	SERIALIZE_TYPE(j, Toggle);
	SelectableToJson(j);
	j["isOn"] = m_IsOn;
	j["toggleTransition"] = m_ToggleTransition;
	j["graphic"] = m_Graphic;
	j["group"] = m_Group;
	j["onValueChanged"] = m_OnValueChanged.ToJson();
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Toggle)
{
	SelectableFromJson(j);
	m_IsOn = j.value("isOn", true);
	m_ToggleTransition = j.value("toggleTransition", 1);
	m_Graphic = j.value("graphic", (uint64)0);
	m_Group = j.value("group", (uint64)0);
	m_OnValueChanged.FromJson(j.contains("onValueChanged") ? j["onValueChanged"] : json::array());
	m_Alpha = m_IsOn ? 1.0f : 0.0f;
}

void Toggle::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	UISelectable::RemapFileIDs(map);
	if (auto it = map.find(m_Graphic); it != map.end()) m_Graphic = it->second;
	if (auto it = map.find(m_Group); it != map.end()) m_Group = it->second;
}
