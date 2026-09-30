#include "pch.h"
#include "UISelectable.h"
#include "UIGraphic.h"
#include "UnityGUI.h"

std::vector<UISelectable*> UISelectable::s_All;

namespace
{
	json ColorJson(const float c[4]) { return json{ c[0], c[1], c[2], c[3] }; }
	void ReadColor(const json& j, const char* key, float out[4])
	{
		if (j.contains(key) && j[key].is_array() && j[key].size() == 4)
			for (int i = 0; i < 4; ++i)
				out[i] = j[key][i].get<float>();
	}
}

UISelectable::UISelectable()
{
	s_All.push_back(this);
}

UISelectable::~UISelectable()
{
	s_All.erase(std::remove(s_All.begin(), s_All.end(), this), s_All.end());
}

GameObject* UISelectable::FindObject(uint64 id)
{
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	return (scene && id) ? scene->FindByFileID(id) : nullptr;
}

UIGraphic* UISelectable::GetTargetGraphic()
{
	GameObject* go = m_TargetGraphic ? FindObject(m_TargetGraphic) : m_pGameObject;
	return go ? go->GetComponent<UIGraphic>() : nullptr;
}

void UISelectable::UpdateVisual(float dt, bool playing)
{
	const float* target = m_Normal;
	if (!m_Interactable) target = m_Disabled;
	else if (playing && m_Down && m_Hovered) target = m_Pressed;
	else if (playing && m_Hovered) target = m_Highlighted;
	else if (playing && m_Selected) target = m_SelectedColor;
	if (!playing)
	{
		m_Hovered = m_Down = m_Selected = false;
		memcpy(m_Current, target, sizeof(m_Current));
	}
	else
	{
		// Fade Duration 동안 선형으로 (Unity CrossFadeColor)
		const float t = m_FadeDuration <= 0.0001f ? 1.0f : std::clamp(dt / m_FadeDuration, 0.0f, 1.0f);
		for (int i = 0; i < 4; ++i)
			m_Current[i] += (target[i] - m_Current[i]) * t;
	}
	if (UIGraphic* g = GetTargetGraphic())
	{
		if (m_Transition == Transition::ColorTint && IsEnabled())
		{
			float tint[4];
			for (int i = 0; i < 3; ++i)
				tint[i] = (std::min)(1.0f, m_Current[i] * m_ColorMultiplier);
			tint[3] = m_Current[3];
			g->SetTint(tint);
		}
		else
			g->ResetTint();
	}
}

void UISelectable::DrawSelectableInspector()
{
	UnityGUI::Toggle("Interactable", &m_Interactable);
	static const char* kTransitions[] = { "None", "Color Tint", "Sprite Swap", "Animation" };
	int tr = (int)m_Transition;
	if (UnityGUI::Dropdown("Transition", &tr, kTransitions, 4))
		m_Transition = (Transition)tr;
	if (m_Transition == Transition::ColorTint)
	{
		// Target Graphic [ 이름 (Image) ⊙ ] — Hierarchy 에서 끌어 놓기
		GameObject* tg = m_TargetGraphic ? FindObject(m_TargetGraphic) : m_pGameObject;
		UIGraphic* graphic = tg ? tg->GetComponent<UIGraphic>() : nullptr;
		const std::string text = graphic ? tg->GetName() + " (" + graphic->InspectorTitle() + ")" : "None (Graphic)";
		ImVec2 fmin, fmax;
		const int pressed = UnityGUI::ObjectFieldButtons("Target Graphic", text.c_str(), "ui_image", nullptr, 0, &fmin, &fmax, 1);
		const ImVec2 after = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(fmin);
		ImGui::InvisibleButton("##targetGraphic", ImVec2((std::max)(1.0f, fmax.x - fmin.x - 22.0f), fmax.y - fmin.y));
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAME_OBJECT"))
				if (GameObject* go = *(GameObject**)payload->Data)
					m_TargetGraphic = go == m_pGameObject ? 0 : go->GetFileID();
			ImGui::EndDragDropTarget();
		}
		if (pressed == -1)
			m_TargetGraphic = 0;
		ImGui::SetCursorScreenPos(after);
		UnityGUI::Color("Normal Color", m_Normal, 1);
		UnityGUI::Color("Highlighted Color", m_Highlighted, 1);
		UnityGUI::Color("Pressed Color", m_Pressed, 1);
		UnityGUI::Color("Selected Color", m_SelectedColor, 1);
		UnityGUI::Color("Disabled Color", m_Disabled, 1);
		UnityGUI::Slider("Color Multiplier", &m_ColorMultiplier, 1.0f, 5.0f, 1);
		if (UnityGUI::Float("Fade Duration", &m_FadeDuration, 1))
			m_FadeDuration = (std::max)(0.0f, m_FadeDuration);
	}
	else if (m_Transition != Transition::None)
		UnityGUI::HelpBox("Sprite Swap and Animation transitions are not supported yet.", true, 1);
	static const char* kNav[] = { "None", "Automatic", "Horizontal", "Vertical", "Explicit" };
	UnityGUI::Dropdown("Navigation", &m_Navigation, kNav, 5);
	UnityGUI::Spacing(6.0f);
}

void UISelectable::SelectableToJson(json& j) const
{
	j["enabled"] = m_Enabled;
	j["interactable"] = m_Interactable;
	j["transition"] = (int)m_Transition;
	j["targetGraphic"] = m_TargetGraphic;
	j["normalColor"] = ColorJson(m_Normal);
	j["highlightedColor"] = ColorJson(m_Highlighted);
	j["pressedColor"] = ColorJson(m_Pressed);
	j["selectedColor"] = ColorJson(m_SelectedColor);
	j["disabledColor"] = ColorJson(m_Disabled);
	j["colorMultiplier"] = m_ColorMultiplier;
	j["fadeDuration"] = m_FadeDuration;
	j["navigation"] = m_Navigation;
}

void UISelectable::SelectableFromJson(const json& j)
{
	m_Enabled = j.value("enabled", true);
	m_Interactable = j.value("interactable", true);
	m_Transition = (Transition)j.value("transition", 1);
	m_TargetGraphic = j.value("targetGraphic", (uint64)0);
	ReadColor(j, "normalColor", m_Normal);
	ReadColor(j, "highlightedColor", m_Highlighted);
	ReadColor(j, "pressedColor", m_Pressed);
	ReadColor(j, "selectedColor", m_SelectedColor);
	ReadColor(j, "disabledColor", m_Disabled);
	m_ColorMultiplier = j.value("colorMultiplier", 1.0f);
	m_FadeDuration = j.value("fadeDuration", 0.1f);
	m_Navigation = j.value("navigation", 1);
}
