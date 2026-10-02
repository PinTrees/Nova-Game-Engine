#include "pch.h"
#include "AnimatorInspector.h"
#include "AnimatorController.h"
#include "AnimationClipLibrary.h"
#include "SkinnedData.h"
#include "UnityGUI.h"
#include "UndoSystem.h"

using namespace AnimatorTypes;

namespace
{
	const ImU32 kText = IM_COL32(210, 210, 210, 255);
	const ImU32 kTextDim = IM_COL32(150, 150, 150, 255);
	const ImU32 kBoxBg = IM_COL32(65, 65, 65, 255);
	const ImU32 kBoxHeader = IM_COL32(53, 53, 53, 255);
	const ImU32 kBoxBorder = IM_COL32(36, 36, 36, 255);
	const ImU32 kRowSelected = IM_COL32(44, 93, 135, 255);

	// 편집은 Revision 만 바로 올리고, 입력이 끝나면 (활성 위젯이 없을 때) 파일로 저장한다
	std::weak_ptr<AnimatorController> s_PendingSave;

	void Changed(const std::shared_ptr<AnimatorController>& c)
	{
		c->MarkChanged();
		s_PendingSave = c;
	}

	void FlushSave()
	{
		if (ImGui::IsAnyItemActive())
			return;
		if (auto c = s_PendingSave.lock())
			c->Save();
		s_PendingSave.reset();
	}

	std::string DisplayName(const std::string& node)
	{
		if (node == kAnyState) return "Any State";
		if (node == kEntry) return "Entry";
		if (node == kExit) return "Exit";
		return node;
	}

	// 큰 아이콘 + 이름 헤더
	bool Header(const char* icon, std::string* name, const char* subtitle, bool editable)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		bool changed = false;
		UnityGUI::DrawIcon(dl, icon, ImVec2(p.x + 10, p.y + 8), 30.0f);
		if (editable)
		{
			ImGui::SetCursorScreenPos(ImVec2(p.x + 52, p.y + 8));
			ImGui::SetNextItemWidth(w - 62);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.165f, 0.165f, 0.165f, 1.0f));
			char buf[128];
			strncpy_s(buf, name->c_str(), _TRUNCATE);
			if (ImGui::InputText("##hdrname", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) && buf[0])
			{
				*name = buf;
				changed = true;
			}
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(2);
		}
		else
			dl->AddText(UnityGUI::BoldFont(), ImGui::GetFontSize(), ImVec2(p.x + 52, p.y + 10), kText, name->c_str());
		if (subtitle)
			dl->AddText(ImVec2(p.x + 52, p.y + 30), kTextDim, subtitle);
		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, 52));
		dl->AddLine(ImVec2(p.x, p.y + 50), ImVec2(p.x + w, p.y + 50), kBoxBorder);
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 54));
		return changed;
	}

	// Unity 의 "Transitions | Solo | Mute" 목록. 행을 누르면 그 전이를 선택한다.
	void TransitionList(const std::shared_ptr<AnimatorController>& c, int layerIndex, const std::vector<int>& indices, int selected)
	{
		AnimatorLayer& layer = c->Layers[layerIndex];
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const float x0 = p.x + 14, x1 = p.x + w - 8;
		const float headerH = 20.0f, rowH = 20.0f;
		const float bodyH = indices.empty() ? rowH : rowH * indices.size();
		dl->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, p.y + headerH), kBoxHeader, 3.0f, ImDrawFlags_RoundCornersTop);
		dl->AddRectFilled(ImVec2(x0, p.y + headerH), ImVec2(x1, p.y + headerH + bodyH + 4), kBoxBg, 3.0f, ImDrawFlags_RoundCornersBottom);
		dl->AddRect(ImVec2(x0, p.y), ImVec2(x1, p.y + headerH + bodyH + 4), kBoxBorder, 3.0f);
		dl->AddText(ImVec2(x0 + 6, p.y + 3), kText, "Transitions");
		dl->AddText(ImVec2(x1 - 74, p.y + 3), kText, "Solo");
		dl->AddText(ImVec2(x1 - 36, p.y + 3), kText, "Mute");
		if (indices.empty())
			dl->AddText(ImVec2(x0 + 6, p.y + headerH + 3), kTextDim, "List is Empty");

		float y = p.y + headerH + 2;
		for (int k = 0; k < (int)indices.size(); ++k)
		{
			AnimatorTransition& t = layer.Transitions[indices[k]];
			ImGui::PushID(indices[k]);
			if (indices[k] == selected)
				dl->AddRectFilled(ImVec2(x0 + 1, y), ImVec2(x1 - 1, y + rowH), kRowSelected);
			ImGui::SetCursorScreenPos(ImVec2(x0, y));
			if (ImGui::InvisibleButton("##tr", ImVec2(x1 - x0 - 84, rowH)))
				AnimatorInspector::Select(c, layerIndex, -1, indices[k]);
			const std::string label = DisplayName(t.From) + " -> " + DisplayName(t.To);
			dl->AddText(ImVec2(x0 + 8, y + 3), kText, label.c_str());
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1, 1));
			ImGui::SetCursorScreenPos(ImVec2(x1 - 68, y + 2));
			if (ImGui::Checkbox("##solo", &t.Solo)) { if (t.Solo) t.Mute = false; Changed(c); }
			ImGui::SetCursorScreenPos(ImVec2(x1 - 30, y + 2));
			if (ImGui::Checkbox("##mute", &t.Mute)) { if (t.Mute) t.Solo = false; Changed(c); }
			ImGui::PopStyleVar();
			ImGui::PopID();
			y += rowH;
		}
		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, headerH + bodyH + 10));
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + headerH + bodyH + 10));
	}

	bool SmallCombo(const char* id, float x, float y, float width, const char* preview, const std::vector<std::string>& items, int* current)
	{
		bool changed = false;
		ImGui::SetCursorScreenPos(ImVec2(x, y));
		ImGui::SetNextItemWidth(width);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 1));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.32f, 0.32f, 0.32f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.19f, 0.19f, 0.19f, 1.0f));
		if (ImGui::BeginCombo(id, preview))
		{
			for (int i = 0; i < (int)items.size(); ++i)
				if (ImGui::Selectable(items[i].c_str(), i == *current))
				{
					*current = i;
					changed = true;
				}
			ImGui::EndCombo();
		}
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(3);
		return changed;
	}

	// 파라미터 종류별 비교 방식 (Unity 와 같다)
	std::vector<ConditionMode> ModesFor(ParamType type)
	{
		switch (type)
		{
		case ParamType::Float: return { ConditionMode::Greater, ConditionMode::Less };
		case ParamType::Int: return { ConditionMode::Greater, ConditionMode::Less, ConditionMode::Equals, ConditionMode::NotEqual };
		case ParamType::Bool: return { ConditionMode::If, ConditionMode::IfNot };
		default: return { ConditionMode::If };
		}
	}

	const char* ModeLabel(ParamType type, ConditionMode m)
	{
		if (type == ParamType::Bool)
			return m == ConditionMode::IfNot ? "false" : "true";
		switch (m)
		{
		case ConditionMode::Greater: return "Greater";
		case ConditionMode::Less: return "Less";
		case ConditionMode::Equals: return "Equals";
		case ConditionMode::NotEqual: return "NotEqual";
		default: return "";
		}
	}

	void ConditionList(const std::shared_ptr<AnimatorController>& c, AnimatorTransition& t)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const float x0 = p.x + 14, x1 = p.x + w - 8;
		const float headerH = 20.0f, rowH = 22.0f;
		const int n = (int)t.Conditions.size();
		const float bodyH = n == 0 ? rowH : rowH * n;
		dl->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, p.y + headerH), kBoxHeader, 3.0f, ImDrawFlags_RoundCornersTop);
		dl->AddRectFilled(ImVec2(x0, p.y + headerH), ImVec2(x1, p.y + headerH + bodyH + 4), kBoxBg);
		dl->AddRect(ImVec2(x0, p.y), ImVec2(x1, p.y + headerH + bodyH + 4), kBoxBorder, 3.0f, ImDrawFlags_RoundCornersTop);
		dl->AddText(ImVec2(x0 + 6, p.y + 3), kText, "Conditions");
		if (n == 0)
			dl->AddText(ImVec2(x0 + 6, p.y + headerH + 4), kTextDim, "List is Empty");

		std::vector<std::string> paramNames;
		for (const auto& prm : c->Parameters)
			paramNames.push_back(prm.Name);

		float y = p.y + headerH + 3;
		const float inner = x1 - x0 - 16;
		for (int i = 0; i < n; ++i)
		{
			AnimatorCondition& cond = t.Conditions[i];
			ImGui::PushID(i);
			const int pi = c->FindParameter(cond.Parameter);
			const ParamType type = pi >= 0 ? c->Parameters[pi].Type : ParamType::Trigger;

			// [파라미터 ▾] [비교 ▾] [값]
			int sel = pi;
			const float w0 = inner * 0.4f, w1 = inner * 0.3f, w2 = inner * 0.3f - 8;
			if (SmallCombo("##param", x0 + 8, y, w0 - 4, pi >= 0 ? cond.Parameter.c_str() : "(none)", paramNames, &sel) && sel >= 0)
			{
				cond.Parameter = paramNames[sel];
				const auto modes = ModesFor(c->Parameters[sel].Type);
				if (std::find(modes.begin(), modes.end(), cond.Mode) == modes.end())
					cond.Mode = modes[0];
				Changed(c);
			}
			if (pi >= 0 && type != ParamType::Trigger)
			{
				const auto modes = ModesFor(type);
				std::vector<std::string> labels;
				int cur = 0;
				for (int m = 0; m < (int)modes.size(); ++m)
				{
					labels.push_back(ModeLabel(type, modes[m]));
					if (modes[m] == cond.Mode) cur = m;
				}
				if (SmallCombo("##mode", x0 + 8 + w0, y, w1 - 4, ModeLabel(type, modes[cur]), labels, &cur))
				{
					cond.Mode = modes[cur];
					Changed(c);
				}
				if (type == ParamType::Float || type == ParamType::Int)
				{
					ImGui::SetCursorScreenPos(ImVec2(x0 + 8 + w0 + w1, y));
					ImGui::SetNextItemWidth(w2);
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 1));
					ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.165f, 0.165f, 0.165f, 1.0f));
					bool edited;
					if (type == ParamType::Int)
					{
						int v = (int)cond.Threshold;
						edited = ImGui::InputInt("##th", &v, 0, 0);
						if (edited) cond.Threshold = (float)v;
					}
					else
						edited = ImGui::InputFloat("##th", &cond.Threshold, 0, 0, "%g");
					if (edited) Changed(c);
					ImGui::PopStyleColor();
					ImGui::PopStyleVar(2);
				}
			}
			ImGui::PopID();
			y += rowH;
		}

		// 아래쪽 + / -  (Unity 리스트 발)
		const float footY = p.y + headerH + bodyH + 4;
		const ImVec2 fa(x1 - 56, footY), fb(x1, footY + 18);
		dl->AddRectFilled(fa, fb, kBoxHeader, 3.0f, ImDrawFlags_RoundCornersBottom);
		dl->AddRect(fa, fb, kBoxBorder, 3.0f, ImDrawFlags_RoundCornersBottom);
		ImGui::SetCursorScreenPos(ImVec2(fa.x + 4, footY));
		if (ImGui::InvisibleButton("##condplus", ImVec2(22, 18)))
		{
			AnimatorCondition cond;
			if (!c->Parameters.empty())
			{
				cond.Parameter = c->Parameters[0].Name;
				cond.Mode = ModesFor(c->Parameters[0].Type)[0];
			}
			t.Conditions.push_back(cond);
			Changed(c);
		}
		UnityGUI::DrawIcon(dl, "plus", ImVec2(fa.x + 8, footY + 2), 14.0f);
		ImGui::SetCursorScreenPos(ImVec2(fa.x + 30, footY));
		if (ImGui::InvisibleButton("##condminus", ImVec2(22, 18)) && !t.Conditions.empty())
		{
			t.Conditions.pop_back();
			Changed(c);
		}
		UnityGUI::DrawIcon(dl, "minus", ImVec2(fa.x + 34, footY + 2), 14.0f);

		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, headerH + bodyH + 28));
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + headerH + bodyH + 28));
	}

	// Blend Tree: 종류, 파라미터, 자식 클립 목록 (클립 · 문턱값 또는 위치 · Time Scale)
	void DrawBlendTree(const std::shared_ptr<AnimatorController>& c, AnimatorState& s)
	{
		using namespace UnityGUI;
		BlendTree& t = s.Tree;
		static const char* kTypes[] = { "1D", "2D Simple Directional", "2D Freeform Directional", "2D Freeform Cartesian" };
		if (Dropdown("Blend Type", &t.BlendType, kTypes, 4)) Changed(c);
		std::vector<std::string> floats;
		for (const auto& p : c->Parameters)
			if (p.Type == ParamType::Float)
				floats.push_back(p.Name);
		std::vector<const char*> items;
		for (const auto& f : floats)
			items.push_back(f.c_str());
		auto paramField = [&](const char* label, std::string& value) {
			int cur = -1;
			for (int i = 0; i < (int)floats.size(); ++i)
				if (floats[i] == value) cur = i;
			if (items.empty())
				ValueLabel(label, "(no Float parameter)");
			else if (Dropdown(label, &cur, items.data(), (int)items.size()) && cur >= 0)
			{
				value = floats[cur];
				Changed(c);
			}
		};
		paramField(t.Is2D() ? "Parameter X" : "Parameter", t.ParameterX);
		if (t.Is2D())
			paramField("Parameter Y", t.ParameterY);

		// 자식 목록: [클립 ▾] [문턱값 | X Y] [Time Scale] [x]
		Spacing(4);
		Label("Motions", 0, true);
		const float w = ImGui::GetContentRegionAvail().x;
		int removeAt = -1;
		for (int i = 0; i < (int)t.Children.size(); ++i)
		{
			BlendTreeChild& ch = t.Children[i];
			ImGui::PushID(i);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14);
			if (ch.Clip == nullptr && !ch.ClipPath.empty())
				ch.LoadClip();
			const std::string label = (ch.Clip ? ch.DisplayName() : std::string("None (Motion)")) + "##clip";
			const float numW = t.Is2D() ? 52.0f : 64.0f;
			const float clipW = (std::max)(80.0f, w - 14 - (t.Is2D() ? numW * 2 : numW) - 52 - 26 - 16);
			if (ImGui::Button(label.c_str(), ImVec2(clipW, 0)))
				ImGui::OpenPopup("##childclip");
			if (AnimationClipLibrary::DrawPickerPopup("##childclip", ch.ClipPath, ch.ClipIndex, true))
			{
				ch.LoadClip();
				Changed(c);
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(numW);
			if (!t.Is2D())
			{
				if (ImGui::InputFloat("##th", &ch.Threshold, 0, 0, "%g")) Changed(c);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Threshold");
			}
			else
			{
				if (ImGui::InputFloat("##px", &ch.PosX, 0, 0, "%g")) Changed(c);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pos X");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(numW);
				if (ImGui::InputFloat("##py", &ch.PosY, 0, 0, "%g")) Changed(c);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pos Y");
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(46);
			if (ImGui::InputFloat("##ts", &ch.TimeScale, 0, 0, "%g")) { ch.TimeScale = (std::max)(0.01f, ch.TimeScale); Changed(c); }
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Time Scale");
			ImGui::SameLine();
			if (ImGui::Button("x", ImVec2(22, 0)))
				removeAt = i;
			ImGui::PopID();
		}
		if (removeAt >= 0)
		{
			t.Children.erase(t.Children.begin() + removeAt);
			Changed(c);
		}
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14);
		if (ImGui::Button("+ Add Motion"))
		{
			BlendTreeChild ch;
			// 다음 문턱값 = 마지막 + 1 (Unity 의 Automate Thresholds 처럼 고르게)
			if (!t.Children.empty())
			{
				ch.Threshold = t.Children.back().Threshold + 1.0f;
				ch.PosX = t.Children.back().PosX + 1.0f;
			}
			t.Children.push_back(ch);
			Changed(c);
		}
		if (t.Children.empty())
			HelpBox("Add motions (clips) and set where each one plays: the threshold of the parameter (1D) or a point (2D).", false);
	}

	void DrawState(const std::shared_ptr<AnimatorController>& c, int layerIndex, int stateIndex)
	{
		using namespace UnityGUI;
		AnimatorLayer& layer = c->Layers[layerIndex];
		AnimatorState& s = layer.States[stateIndex];

		std::string name = s.Name;
		if (Header("animator", &name, nullptr, true))
			if (c->RenameState(layerIndex, stateIndex, name))
				Changed(c);

		static const char* kMotionKinds[] = { "Clip", "Blend Tree" };
		int kind = s.IsBlendTree ? 1 : 0;
		if (Dropdown("Motion Type", &kind, kMotionKinds, 2))
		{
			if (kind == 1)
				c->MakeBlendTree(layerIndex, stateIndex);
			else
				s.IsBlendTree = false;
			Changed(c);
		}
		if (!s.IsBlendTree)
		{
			if (s.Clip == nullptr && !s.ClipPath.empty())
				s.LoadClip();
			const std::string motion = s.Clip ? s.Clip->Name : "None (Motion)";
			if (ObjectField("Motion", motion.c_str(), 0, s.Clip ? "animation_clip" : nullptr))
				ImGui::OpenPopup("##statemotion");
			if (AnimationClipLibrary::DrawPickerPopup("##statemotion", s.ClipPath, s.ClipIndex, true))
			{
				s.LoadClip();
				Changed(c);
			}
		}
		else
			DrawBlendTree(c, s);
		if (Float("Speed", &s.Speed)) Changed(c);
		if (Toggle("Loop Time", &s.Loop)) Changed(c);
		if (Float("Cycle Offset", &s.CycleOffset)) Changed(c);
		if (Toggle("Foot IK", &s.FootIK)) Changed(c);
		if (Toggle("Write Defaults", &s.WriteDefaults)) Changed(c);
		if (s.Clip && !s.IsBlendTree)
		{
			char info[128];
			sprintf_s(info, "%.2f s", s.Clip->GetClipEndTime());
			ValueLabel("Length", info);
		}
		Spacing(6);

		std::vector<int> outgoing;
		for (int i = 0; i < (int)layer.Transitions.size(); ++i)
			if (layer.Transitions[i].From == s.Name)
				outgoing.push_back(i);
		TransitionList(c, layerIndex, outgoing, -1);

		if (layer.DefaultState != s.Name)
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14);
			if (ImGui::Button("Set as Layer Default State"))
			{
				layer.DefaultState = s.Name;
				Changed(c);
			}
		}
	}

	void DrawTransition(const std::shared_ptr<AnimatorController>& c, int layerIndex, int index)
	{
		using namespace UnityGUI;
		AnimatorLayer& layer = c->Layers[layerIndex];
		AnimatorTransition& t = layer.Transitions[index];

		std::string title = DisplayName(t.From) + " -> " + DisplayName(t.To);
		Header("animator", &title, t.From == kEntry ? "Entry Transition" : "Animator Transition", false);

		std::vector<int> group;
		for (int i = 0; i < (int)layer.Transitions.size(); ++i)
			if (layer.Transitions[i].From == t.From && layer.Transitions[i].To == t.To)
				group.push_back(i);
		TransitionList(c, layerIndex, group, index);

		// Entry 전이는 조건만 (Unity 와 같다)
		if (t.From != kEntry)
		{
			if (Toggle("Has Exit Time", &t.HasExitTime)) Changed(c);
			if (FoldoutPlain("Settings", 0, true))
			{
				if (Float("Exit Time", &t.ExitTime, 1)) Changed(c);
				if (Toggle("Fixed Duration", &t.FixedDuration, 1)) Changed(c);
				if (Float(t.FixedDuration ? "Transition Duration (s)" : "Transition Duration (%)", &t.Duration, 1))
				{
					t.Duration = (std::max)(0.0f, t.Duration);
					Changed(c);
				}
				if (Float("Transition Offset", &t.Offset, 1)) Changed(c);
				if (t.From == kAnyState && Toggle("Can Transition To Self", &t.CanTransitionToSelf, 1)) Changed(c);
			}
			if (!t.HasExitTime && t.Conditions.empty())
				HelpBox("Transition needs at least one condition or an Exit Time to be valid, otherwise it will be ignored.", true);
			Spacing(6);
		}
		ConditionList(c, t);
	}
}

namespace AnimatorInspector
{
	void Select(const std::shared_ptr<AnimatorController>& controller, int layer, int state, int transition)
	{
		auto sel = std::make_shared<AnimatorSelection>();
		sel->Controller = controller;
		sel->Layer = layer;
		sel->State = state;
		sel->Transition = transition;
		SelectionManager::SetCustomSelection("Animator", sel, [sel]() {
			if (sel->Controller)
			{
				std::weak_ptr<AnimatorController> weak = sel->Controller;
				Undo::WatchAsset("controller:" + sel->Controller->Path, sel->Controller->Name(),
					[weak]() { auto c = weak.lock(); return c ? c->ToJsonString() : std::string(); },
					[weak](const std::string& text) { if (auto c = weak.lock()) { c->ApplyJson(text); c->Commit(); } });
			}
			DrawSelection(*sel);
		});
	}

	const AnimatorSelection* Current()
	{
		const CustomSelection& c = SelectionManager::GetCustomSelection();
		if (SelectionManager::GetSelectedObjectType() != SelectionType::CUSTOM || c.Owner != "Animator" || c.Data == nullptr)
			return nullptr;
		return static_cast<const AnimatorSelection*>(c.Data.get());
	}

	void DrawSelection(const AnimatorSelection& sel)
	{
		const auto& c = sel.Controller;
		if (c == nullptr || sel.Layer < 0 || sel.Layer >= (int)c->Layers.size())
			return;
		const AnimatorLayer& layer = c->Layers[sel.Layer];
		if (sel.State >= 0 && sel.State < (int)layer.States.size())
			DrawState(c, sel.Layer, sel.State);
		else if (sel.Transition >= 0 && sel.Transition < (int)layer.Transitions.size())
			DrawTransition(c, sel.Layer, sel.Transition);
		FlushSave();
	}

	void DrawController(const std::shared_ptr<AnimatorController>& c)
	{
		if (c == nullptr)
			return;
		std::string name = c->Name();
		Header("animator_controller", &name, "Animator Controller", false);
		char buf[64];
		sprintf_s(buf, "%d", (int)c->Layers.size());
		UnityGUI::ValueLabel("Layers", buf);
		sprintf_s(buf, "%d", (int)c->Parameters.size());
		UnityGUI::ValueLabel("Parameters", buf);
		int states = 0, transitions = 0;
		for (const auto& l : c->Layers)
		{
			states += (int)l.States.size();
			transitions += (int)l.Transitions.size();
		}
		sprintf_s(buf, "%d", states);
		UnityGUI::ValueLabel("States", buf);
		sprintf_s(buf, "%d", transitions);
		UnityGUI::ValueLabel("Transitions", buf);
		UnityGUI::Spacing(6);
		UnityGUI::HelpBox("Double-click or open the Animator window to edit the state machine.", false);
	}
}
