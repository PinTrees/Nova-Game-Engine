#include "pch.h"
#include "UIEventList.h"
#include "UnityGUI.h"
#include "ScriptEngine.h"
#include "CSharpScript.h"
#include "Debug.h"

namespace
{
	GameObject* FindObject(uint64 id)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		return (scene && id) ? scene->FindByFileID(id) : nullptr;
	}

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

	// Unity 표시 이름: float → Single, bool → Boolean, string → String
	const char* TypeLabel(const std::string& t)
	{
		if (t == "float") return "Single";
		if (t == "bool") return "Boolean";
		if (t == "string") return "String";
		if (t == "int") return "Int32";
		return "";
	}

	std::map<const void*, int> s_Selected;   // 목록별로 - 버튼이 지울 행
}

void UIEventList::Invoke(GameObject* owner, const char* eventName, const std::string& dynamicValue) const
{
	for (const UIPersistentCall& call : Calls)
	{
		if (call.CallState == 0 || call.Method.empty())
			continue;
		GameObject* target = FindObject(call.Target);
		if (target == nullptr)
			continue;
		const std::string arg = call.Dynamic ? dynamicValue : call.Argument;
		if (call.Method == "GameObject.SetActive")
		{
			target->SetActive(arg == "true" || arg == "1");
			continue;
		}
		const size_t dot = call.Method.rfind('.');
		if (dot == std::string::npos)
			continue;
		if (!ScriptEngine::InvokeMethod(target->GetFileID(), call.Method.substr(0, dot), call.Method.substr(dot + 1), arg))
			Debug::LogWarning(std::string(eventName) + " of '" + (owner ? owner->GetName() : std::string("?")) + "': could not call " + call.Method + " on '" + target->GetName() + "'");
	}
}

void UIEventList::Draw(const char* title, const char* dynamicType)
{
	const std::string dyn = dynamicType ? dynamicType : "";
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 p = ImGui::GetCursorScreenPos();
	const float x0 = p.x + UnityGUI::kBaseIndent - 4.0f;
	const float w = ImGui::GetContentRegionAvail().x - UnityGUI::kBaseIndent - 8.0f;
	const float headerH = 22.0f, rowH = 44.0f;
	const float bodyH = Calls.empty() ? 24.0f : rowH * Calls.size() + 6.0f;

	dl->AddRectFilled(ImVec2(x0, p.y), ImVec2(x0 + w, p.y + headerH), IM_COL32(53, 53, 53, 255), 3.0f, ImDrawFlags_RoundCornersTop);
	dl->AddRectFilled(ImVec2(x0, p.y + headerH), ImVec2(x0 + w, p.y + headerH + bodyH), IM_COL32(65, 65, 65, 255), 3.0f, ImDrawFlags_RoundCornersBottom);
	dl->AddRect(ImVec2(x0, p.y), ImVec2(x0 + w, p.y + headerH + bodyH), IM_COL32(36, 36, 36, 255), 3.0f);
	dl->AddText(ImVec2(x0 + 6.0f, p.y + 4.0f), IM_COL32(210, 210, 210, 255), title);
	if (Calls.empty())
		dl->AddText(ImVec2(x0 + 8.0f, p.y + headerH + 5.0f), IM_COL32(150, 150, 150, 255), "List is Empty");

	ImGui::PushID(this);
	int& selected = s_Selected[this];
	int removeIndex = -1;
	PushComboStyle();
	for (int i = 0; i < (int)Calls.size(); ++i)
	{
		UIPersistentCall& call = Calls[i];
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
		std::string label = call.Method.empty() ? "No Function" : call.Method + (call.Dynamic || call.ParamType.empty() ? "" : " (" + call.ParamType + ")");
		ImGui::SetCursorScreenPos(ImVec2(rx, y));
		ImGui::SetNextItemWidth(rw);
		if (target == nullptr) ImGui::BeginDisabled();
		if (ImGui::BeginCombo("##func", label.c_str()))
		{
			if (ImGui::Selectable("No Function", call.Method.empty()))
				call.Method.clear(), call.ParamType.clear(), call.Dynamic = false;
			ImGui::Separator();
			if (target)
			{
				auto pick = [&](const std::string& method, const std::string& param, bool dynamic) {
					call.Method = method;
					call.ParamType = param;
					call.Dynamic = dynamic;
					if (!dynamic)
						call.Argument = param == "bool" ? "false" : (param.empty() || param == "string" ? "" : "0");
				};
				if (ImGui::BeginMenu("GameObject"))
				{
					if (dyn == "bool")
					{
						ImGui::TextDisabled("Dynamic bool");
						if (ImGui::MenuItem("SetActive##dyn")) pick("GameObject.SetActive", "bool", true);
						ImGui::TextDisabled("Static Parameters");
					}
					if (ImGui::MenuItem("SetActive (bool)")) pick("GameObject.SetActive", "bool", false);
					ImGui::EndMenu();
				}
				// 대상에 붙은 C# 스크립트의 public 메서드
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
						// Unity: 이벤트 값을 그대로 받는 메서드 (Dynamic) 를 위에
						if (!dyn.empty())
						{
							bool any = false;
							for (const ScriptEngine::MethodInfo& m : info->Methods)
								if (m.ParamType == dyn)
								{
									if (!any) { ImGui::TextDisabled("Dynamic %s", dyn.c_str()); any = true; }
									if (ImGui::MenuItem((m.Name + "##dyn").c_str()))
										pick(info->Name + "." + m.Name, m.ParamType, true);
								}
							if (any) ImGui::TextDisabled("Static Parameters");
						}
						for (const ScriptEngine::MethodInfo& m : info->Methods)
						{
							const std::string item = m.Name + " (" + m.ParamType + ")";
							if (ImGui::MenuItem(item.c_str()))
								pick(info->Name + "." + m.Name, m.ParamType, false);
						}
						ImGui::EndMenu();
					}
				}
			}
			ImGui::EndCombo();
		}
		if (target == nullptr) ImGui::EndDisabled();

		// 2 줄: [대상 GameObject]  [인자 (Static 일 때)]
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
						call.Method.clear(), call.ParamType.clear(), call.Dynamic = false;
					call.Target = go->GetFileID();
				}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::IsItemClicked())
			selected = i;
		if (hov)
			ImGui::SetTooltip("Drag a GameObject from the Hierarchy here");

		if (!call.ParamType.empty() && !call.Dynamic)
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
		else if (call.Dynamic)
			dl->AddText(ImVec2(rx + 4.0f, y2 + 2.0f), IM_COL32(150, 150, 150, 255), (std::string("(dynamic ") + TypeLabel(call.ParamType) + ")").c_str());
		if (selected == i)
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
		UIPersistentCall call;
		if (!Calls.empty()) call.Target = Calls.back().Target;   // Unity: 이전 항목의 대상을 이어받는다
		Calls.push_back(call);
	}
	ImGui::SameLine(0, 2);
	if (ImGui::Button("-##removeCall", ImVec2(26.0f, 16.0f)) && !Calls.empty())
		removeIndex = (selected >= 0 && selected < (int)Calls.size()) ? selected : (int)Calls.size() - 1;
	ImGui::PopStyleVar();
	if (removeIndex >= 0)
	{
		Calls.erase(Calls.begin() + removeIndex);
		selected = -1;
	}
	ImGui::PopID();
	ImGui::SetCursorScreenPos(ImVec2(p.x, by + 22.0f));
	ImGui::Dummy(ImVec2(w, 0));
}

nlohmann::json UIEventList::ToJson() const
{
	json calls = json::array();
	for (const UIPersistentCall& c : Calls)
		calls.push_back({ { "target", c.Target }, { "method", c.Method }, { "paramType", c.ParamType }, { "argument", c.Argument }, { "callState", c.CallState }, { "dynamic", c.Dynamic } });
	return calls;
}

void UIEventList::FromJson(const nlohmann::json& j)
{
	Calls.clear();
	if (!j.is_array())
		return;
	for (const json& c : j)
	{
		UIPersistentCall call;
		call.Target = c.value("target", (uint64)0);
		call.Method = c.value("method", std::string());
		call.ParamType = c.value("paramType", std::string());
		call.Argument = c.value("argument", std::string());
		call.CallState = c.value("callState", 2);
		call.Dynamic = c.value("dynamic", false);
		Calls.push_back(call);
	}
}
