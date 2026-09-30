#include "pch.h"
#include "UIButton.h"
#include "UIGraphic.h"
#include "UnityGUI.h"
#include "ScriptEngine.h"
#include "CSharpScript.h"
#include "Debug.h"

std::vector<Button*> Button::s_All;

namespace
{
	GameObject* FindObject(uint64 id)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		return (scene && id) ? scene->FindByFileID(id) : nullptr;
	}

	// 어두운 Unity 콤보 스타일
	void PushComboStyle()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.318f, 0.318f, 0.318f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.37f, 0.37f, 0.37f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.176f, 0.176f, 0.176f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.19f, 0.19f, 0.19f, 1.0f));
	}
	void PopComboStyle() { ImGui::PopStyleColor(4); ImGui::PopStyleVar(3); }
}

Button::Button()
{
	m_InspectorTitleName = "Button";
	s_All.push_back(this);
}

Button::~Button()
{
	s_All.erase(std::remove(s_All.begin(), s_All.end(), this), s_All.end());
}

UIGraphic* Button::GetTargetGraphic()
{
	GameObject* go = m_TargetGraphic ? FindObject(m_TargetGraphic) : m_pGameObject;
	return go ? go->GetComponent<UIGraphic>() : nullptr;
}

void Button::SetPointer(bool hovered, bool pressed)
{
	m_Hovered = hovered;
	m_Down = pressed;
}

void Button::Click()
{
	if (!m_Interactable || m_pGameObject == nullptr)
		return;
	EditorLog::Write("UI", "Button '%s' clicked (%d persistent calls)", m_pGameObject->GetName().c_str(), (int)m_OnClick.size());
	for (const PersistentCall& call : m_OnClick)
	{
		if (call.CallState == 0 || call.Method.empty())
			continue;
		GameObject* target = FindObject(call.Target);
		if (target == nullptr)
			continue;
		if (call.Method == "GameObject.SetActive")
		{
			target->SetActive(call.Argument == "true" || call.Argument == "1");
			continue;
		}
		const size_t dot = call.Method.rfind('.');
		if (dot == std::string::npos)
			continue;
		if (!ScriptEngine::InvokeMethod(target->GetFileID(), call.Method.substr(0, dot), call.Method.substr(dot + 1), call.Argument))
			Debug::LogWarning("Button '" + m_pGameObject->GetName() + "': could not call " + call.Method + " on '" + target->GetName() + "'");
	}
	// C# 의 button.onClick.AddListener(...)
	ScriptEngine::InvokeUIEvent(m_pGameObject->GetFileID(), 0);
}

void Button::UpdateVisual(float dt, bool playing)
{
	const float* target = m_Normal;
	if (!m_Interactable) target = m_Disabled;
	else if (playing && m_Down && m_Hovered) target = m_Pressed;
	else if (playing && m_Hovered) target = m_Highlighted;
	if (!playing)
	{
		m_Hovered = m_Down = false;
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
			for (int i = 0; i < 4; ++i)
				tint[i] = (std::min)(1.0f, m_Current[i] * m_ColorMultiplier);
			tint[3] = m_Current[3];
			g->SetTint(tint);
		}
		else
			g->ResetTint();
	}
}

// ------------------------------------------------------------------ Inspector
void Button::OnInspectorGUI()
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
		UnityGUI::Color("Selected Color", m_Selected, 1);
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
	DrawOnClickList();
}

void Button::DrawOnClickList()
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 p = ImGui::GetCursorScreenPos();
	const float x0 = p.x + UnityGUI::kBaseIndent - 4.0f;
	const float w = ImGui::GetContentRegionAvail().x - UnityGUI::kBaseIndent - 8.0f;
	const float headerH = 22.0f, rowH = 44.0f;
	const float bodyH = m_OnClick.empty() ? 24.0f : rowH * m_OnClick.size() + 6.0f;

	// 머리글 "On Click ()" + 본문 상자
	dl->AddRectFilled(ImVec2(x0, p.y), ImVec2(x0 + w, p.y + headerH), IM_COL32(53, 53, 53, 255), 3.0f, ImDrawFlags_RoundCornersTop);
	dl->AddRectFilled(ImVec2(x0, p.y + headerH), ImVec2(x0 + w, p.y + headerH + bodyH), IM_COL32(65, 65, 65, 255), 3.0f, ImDrawFlags_RoundCornersBottom);
	dl->AddRect(ImVec2(x0, p.y), ImVec2(x0 + w, p.y + headerH + bodyH), IM_COL32(36, 36, 36, 255), 3.0f);
	dl->AddText(ImVec2(x0 + 6.0f, p.y + 4.0f), IM_COL32(210, 210, 210, 255), "On Click ()");
	if (m_OnClick.empty())
		dl->AddText(ImVec2(x0 + 8.0f, p.y + headerH + 5.0f), IM_COL32(150, 150, 150, 255), "List is Empty");

	int removeIndex = -1;
	static int s_Selected = -1;
	PushComboStyle();
	for (int i = 0; i < (int)m_OnClick.size(); ++i)
	{
		PersistentCall& call = m_OnClick[i];
		ImGui::PushID(i);
		const float y = p.y + headerH + 4.0f + i * rowH;
		const float half = (w - 24.0f) * 0.42f;
		const float lx = x0 + 8.0f, rx = lx + half + 8.0f, rw = x0 + w - 8.0f - rx;

		// 1 줄: [Runtime Only ▾]  [함수 ▾]
		static const char* kStates[] = { "Off", "Editor And Runtime", "Runtime Only" };
		ImGui::SetCursorScreenPos(ImVec2(lx, y));
		ImGui::SetNextItemWidth(half);
		ImGui::Combo("##state", &call.CallState, kStates, 3);

		GameObject* target = FindObject(call.Target);
		std::string label = call.Method.empty() ? "No Function" : call.Method + (call.ParamType.empty() ? "" : " (" + call.ParamType + ")");
		ImGui::SetCursorScreenPos(ImVec2(rx, y));
		ImGui::SetNextItemWidth(rw);
		if (target == nullptr) ImGui::BeginDisabled();
		if (ImGui::BeginCombo("##func", label.c_str()))
		{
			if (ImGui::Selectable("No Function", call.Method.empty()))
				call.Method.clear(), call.ParamType.clear();
			ImGui::Separator();
			if (target)
			{
				if (ImGui::BeginMenu("GameObject"))
				{
					if (ImGui::MenuItem("SetActive (bool)"))
						call.Method = "GameObject.SetActive", call.ParamType = "bool", call.Argument = "false";
					ImGui::EndMenu();
				}
				// 대상에 붙은 C# 스크립트의 public 메서드 (인자 없음 / int, float, string, bool 하나)
				for (auto& comp : target->GetComponents())
				{
					auto* script = dynamic_cast<CSharpScript*>(comp.get());
					if (script == nullptr)
						continue;
					const ScriptEngine::ClassInfo* info = ScriptEngine::FindClass(script->GetClassName());
					if (info == nullptr)
						continue;
					if (ImGui::BeginMenu(info->Name.c_str()))
					{
						if (info->Methods.empty())
							ImGui::TextDisabled("(no public methods)");
						for (const ScriptEngine::MethodInfo& m : info->Methods)
						{
							const std::string item = m.Name + " (" + (m.ParamType.empty() ? "" : m.ParamType) + ")";
							if (ImGui::MenuItem(item.c_str()))
							{
								call.Method = info->Name + "." + m.Name;
								call.ParamType = m.ParamType;
								call.Argument = m.ParamType == "bool" ? "false" : (m.ParamType.empty() || m.ParamType == "string" ? "" : "0");
							}
						}
						ImGui::EndMenu();
					}
				}
			}
			ImGui::EndCombo();
		}
		if (target == nullptr) ImGui::EndDisabled();

		// 2 줄: [대상 GameObject]  [인자]
		const float y2 = y + 20.0f;
		const std::string targetText = target ? target->GetName() : "None (Object)";
		ImGui::SetCursorScreenPos(ImVec2(lx, y2));
		ImGui::InvisibleButton("##target", ImVec2(half, 18.0f));
		const bool hov = ImGui::IsItemHovered();
		dl->AddRectFilled(ImVec2(lx, y2), ImVec2(lx + half, y2 + 18.0f), hov ? IM_COL32(50, 50, 50, 255) : IM_COL32(42, 42, 42, 255), 3.0f);
		dl->AddRect(ImVec2(lx, y2), ImVec2(lx + half, y2 + 18.0f), IM_COL32(26, 26, 26, 255), 3.0f);
		UnityGUI::DrawIcon(dl, "gameobject", ImVec2(lx + 3.0f, y2 + 2.0f), 14.0f);
		dl->PushClipRect(ImVec2(lx, y2), ImVec2(lx + half - 4.0f, y2 + 18.0f), true);
		dl->AddText(ImVec2(lx + 20.0f, y2 + 2.0f), IM_COL32(220, 220, 220, 255), targetText.c_str());
		dl->PopClipRect();
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAME_OBJECT"))
				if (GameObject* go = *(GameObject**)payload->Data)
				{
					if (go->GetFileID() != call.Target)
						call.Method.clear(), call.ParamType.clear();
					call.Target = go->GetFileID();
				}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::IsItemClicked())
			s_Selected = i;
		if (hov)
			ImGui::SetTooltip("Drag a GameObject from the Hierarchy here");

		if (!call.ParamType.empty())
		{
			ImGui::SetCursorScreenPos(ImVec2(rx, y2));
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.165f, 0.165f, 0.165f, 1.0f));
			if (call.ParamType == "bool")
			{
				bool b = call.Argument == "true" || call.Argument == "1";
				if (ImGui::Checkbox("##argb", &b))
					call.Argument = b ? "true" : "false";
			}
			else
			{
				char buf[256];
				strncpy_s(buf, call.Argument.c_str(), _TRUNCATE);
				ImGui::SetNextItemWidth(rw);
				const ImGuiInputTextFlags flags = call.ParamType == "string" ? 0 : ImGuiInputTextFlags_CharsScientific;
				if (ImGui::InputText("##arg", buf, sizeof(buf), flags))
					call.Argument = buf;
			}
			ImGui::PopStyleColor();
		}
		// 행 선택 표시 (- 로 지울 행)
		if (s_Selected == i)
			dl->AddRect(ImVec2(x0 + 2.0f, y - 2.0f), ImVec2(x0 + w - 2.0f, y + rowH - 4.0f), IM_COL32(62, 125, 190, 255), 2.0f);
		ImGui::PopID();
	}
	PopComboStyle();

	// + / -
	const float by = p.y + headerH + bodyH;
	ImGui::SetCursorScreenPos(ImVec2(x0 + w - 56.0f, by));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
	if (ImGui::Button("+##addCall", ImVec2(26.0f, 16.0f)))
	{
		PersistentCall call;
		// Unity: 새 항목은 이전 항목의 대상을 이어받는다
		if (!m_OnClick.empty()) call.Target = m_OnClick.back().Target;
		m_OnClick.push_back(call);
	}
	ImGui::SameLine(0, 2);
	if (ImGui::Button("-##removeCall", ImVec2(26.0f, 16.0f)) && !m_OnClick.empty())
		removeIndex = (s_Selected >= 0 && s_Selected < (int)m_OnClick.size()) ? s_Selected : (int)m_OnClick.size() - 1;
	ImGui::PopStyleVar();
	if (removeIndex >= 0)
	{
		m_OnClick.erase(m_OnClick.begin() + removeIndex);
		s_Selected = -1;
	}
	ImGui::SetCursorScreenPos(ImVec2(p.x, by + 22.0f));
	ImGui::Dummy(ImVec2(w, 0));
}

// ------------------------------------------------------------------ 저장
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

GENERATE_COMPONENT_FUNC_TOJSON(Button)
{
	json j;
	SERIALIZE_TYPE(j, Button);
	j["enabled"] = m_Enabled;
	j["interactable"] = m_Interactable;
	j["transition"] = (int)m_Transition;
	j["targetGraphic"] = m_TargetGraphic;
	j["normalColor"] = ColorJson(m_Normal);
	j["highlightedColor"] = ColorJson(m_Highlighted);
	j["pressedColor"] = ColorJson(m_Pressed);
	j["selectedColor"] = ColorJson(m_Selected);
	j["disabledColor"] = ColorJson(m_Disabled);
	j["colorMultiplier"] = m_ColorMultiplier;
	j["fadeDuration"] = m_FadeDuration;
	j["navigation"] = m_Navigation;
	json calls = json::array();
	for (const PersistentCall& c : m_OnClick)
		calls.push_back({ { "target", c.Target }, { "method", c.Method }, { "paramType", c.ParamType }, { "argument", c.Argument }, { "callState", c.CallState } });
	j["onClick"] = calls;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Button)
{
	m_Enabled = j.value("enabled", true);
	m_Interactable = j.value("interactable", true);
	m_Transition = (Transition)j.value("transition", 1);
	m_TargetGraphic = j.value("targetGraphic", (uint64)0);
	ReadColor(j, "normalColor", m_Normal);
	ReadColor(j, "highlightedColor", m_Highlighted);
	ReadColor(j, "pressedColor", m_Pressed);
	ReadColor(j, "selectedColor", m_Selected);
	ReadColor(j, "disabledColor", m_Disabled);
	m_ColorMultiplier = j.value("colorMultiplier", 1.0f);
	m_FadeDuration = j.value("fadeDuration", 0.1f);
	m_Navigation = j.value("navigation", 1);
	m_OnClick.clear();
	if (j.contains("onClick") && j["onClick"].is_array())
		for (const json& c : j["onClick"])
		{
			PersistentCall call;
			call.Target = c.value("target", (uint64)0);
			call.Method = c.value("method", std::string());
			call.ParamType = c.value("paramType", std::string());
			call.Argument = c.value("argument", std::string());
			call.CallState = c.value("callState", 2);
			m_OnClick.push_back(call);
		}
}
