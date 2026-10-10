#include "pch.h"
#include "VolumeEditor.h"
#include "VolumeProfile.h"
#include "UnityGUI.h"
#include "UndoSystem.h"
#include "ObjectPicker.h"
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace
{
	// 편집 중인 프로파일: 입력이 끝나는 순간(활성 위젯 없음) 저장한다
	std::set<std::string> s_Dirty;

	void MarkDirty(const VolumeProfile& profile) { s_Dirty.insert(profile.Path); }

	void SaveIfIdle(const std::shared_ptr<VolumeProfile>& profile)
	{
		if (s_Dirty.count(profile->Path) && !ImGui::IsAnyItemActive())
		{
			profile->Save();
			s_Dirty.erase(profile->Path);
		}
	}

	// 프로젝트의 모든 .volumeprofile (1초 캐시)
	const std::vector<std::string>& AllProfiles()
	{
		static std::vector<std::string> s_List;
		static double s_Time = -10.0;
		if (ImGui::GetTime() - s_Time < 1.0)
			return s_List;
		s_Time = ImGui::GetTime();
		s_List.clear();
		std::error_code ec;
		const fs::path root = PathManager::GetI()->GetMovePathW(L"Assets\\");
		for (const auto& e : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec))
		{
			if (e.is_regular_file(ec) && _wcsicmp(e.path().extension().c_str(), L".volumeprofile") == 0)
				s_List.push_back(wstring_to_string(PathManager::GetI()->GetCutSolutionPath(e.path().wstring())));
		}
		std::sort(s_List.begin(), s_List.end());
		return s_List;
	}

	// Shadows: 캐스케이드가 Max Distance 를 어떻게 나누는지 색 막대로 (Unity URP 의 Cascade 막대)
	void DrawCascadeBar(const VolumeComponent& c)
	{
		const int count = std::clamp(c.I("cascadeCount"), 1, 8);
		const float maxDist = (std::max)(c.F("maxDistance"), 0.0f);
		float ends[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
		const float splits[7] = { c.F("split1"), c.F("split2"), c.F("split3"), c.F("split4"), c.F("split5"), c.F("split6"), c.F("split7") };
		float prev = 0.0f;
		for (int i = 0; i < count - 1; ++i)
		{
			ends[i] = std::clamp(splits[i], prev + 0.001f, 1.0f);
			prev = ends[i];
		}
		static const ImU32 kColors[8] = { IM_COL32(94, 145, 94, 255), IM_COL32(126, 126, 186, 255), IM_COL32(161, 128, 77, 255), IM_COL32(158, 84, 84, 255),
			IM_COL32(84, 150, 158, 255), IM_COL32(150, 98, 160, 255), IM_COL32(170, 160, 80, 255), IM_COL32(120, 120, 120, 255) };

		UnityGUI::Spacing(4.0f);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x - 28.0f;
		const float x0 = p.x + 18.0f, h = 20.0f;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		float start = 0.0f;
		for (int i = 0; i < count; ++i)
		{
			const float a = x0 + w * start, b = x0 + w * ends[i];
			dl->AddRectFilled(ImVec2(a, p.y), ImVec2(b, p.y + h), kColors[i]);
			char text[48];
			snprintf(text, sizeof(text), "%d  %.1fm", i, maxDist * (ends[i] - start));
			const ImVec2 ts = ImGui::CalcTextSize(text);
			if (ts.x + 6.0f < b - a)
				dl->AddText(ImVec2(a + 4.0f, p.y + (h - ts.y) * 0.5f), IM_COL32(20, 20, 20, 255), text);
			start = ends[i];
		}
		dl->AddRect(ImVec2(x0, p.y), ImVec2(x0 + w, p.y + h), IM_COL32(26, 26, 26, 255));
		ImGui::Dummy(ImVec2(w, h + 2.0f));
	}

	bool IsVolumeProfilePath(const std::string& p)
	{
		return fs::path(p).extension() == ".volumeprofile";
	}

	void DrawParam(VolumeParameter& p, bool isDefault, bool& changed)
	{
		ImGui::PushID(p.Key.c_str());
		if (!isDefault)
			changed |= UnityGUI::LeadingCheckbox("##ov", &p.Override, 1);
		const bool editable = isDefault || p.Override;
		ImGui::BeginDisabled(!editable);
		const char* label = p.Label.c_str();
		switch (p.Kind)
		{
		case VolumeParamKind::Float:
			if (UnityGUI::Float(label, &p.Value[0], 1))
			{
				if (p.Min > -1.0e8f)
					p.Value[0] = (std::max)(p.Value[0], p.Min);
				changed = true;
			}
			break;
		case VolumeParamKind::Clamped:
			changed |= UnityGUI::Slider(label, &p.Value[0], p.Min, p.Max, 1);
			break;
		case VolumeParamKind::Int:
		{
			int v = p.I();
			if (UnityGUI::Int(label, &v, 1))
			{
				p.Value[0] = (float)std::clamp(v, (int)p.Min, (int)p.Max);
				changed = true;
			}
			break;
		}
		case VolumeParamKind::Bool:
		{
			bool v = p.B();
			if (UnityGUI::Toggle(label, &v, 1))
			{
				p.Value[0] = v ? 1.0f : 0.0f;
				changed = true;
			}
			break;
		}
		case VolumeParamKind::Enum:
		{
			std::vector<const char*> items;
			for (const std::string& s : p.Items)
				items.push_back(s.c_str());
			int v = p.I();
			if (UnityGUI::Dropdown(label, &v, items.data(), (int)items.size(), 1))
			{
				p.Value[0] = (float)v;
				changed = true;
			}
			break;
		}
		case VolumeParamKind::Color:
			p.Value[3] = 1.0f;
			changed |= UnityGUI::Color(label, p.Value, 1);
			break;
		case VolumeParamKind::Vector2:
			changed |= UnityGUI::Vector2Pair(label, "X", &p.Value[0], "Y", &p.Value[1], 1);
			break;
		}
		ImGui::EndDisabled();
		ImGui::PopID();
	}
}

namespace VolumeEditor
{
	void DrawProfile(const std::shared_ptr<VolumeProfile>& profile, bool isDefault)
	{
		if (profile == nullptr)
			return;

		std::weak_ptr<VolumeProfile> weak = profile;
		Undo::WatchAsset("volumeprofile:" + profile->Path, profile->Name(),
			[weak]() { auto p = weak.lock(); return p ? p->ToJsonString() : std::string(); },
			[weak](const std::string& text) { if (auto p = weak.lock()) { p->ApplyJson(text); p->Save(); } });

		ImGui::PushID(profile->Path.c_str());
		UnityGUI::Spacing(4.0f);
		bool changed = false;
		std::string removeType;
		for (auto& comp : profile->Components)
		{
			ImGui::PushID(comp->Type.c_str());
			ImGuiStorage* st = ImGui::GetStateStorage();
			const ImGuiID openId = ImGui::GetID("##open");
			bool open = st->GetBool(openId, !isDefault);
			bool active = comp->Active;
			bool all = false, none = false;
			const UnityGUI::HeaderAction action = UnityGUI::VolumeEffectHeader(comp->Type.c_str(), comp->DisplayName.c_str(), &open, &active,
				isDefault ? nullptr : &all, isDefault ? nullptr : &none);
			st->SetBool(openId, open);
			if (active != comp->Active) { comp->Active = active; changed = true; }
			if (all || none)
			{
				for (VolumeParameter& p : comp->Params)
					p.Override = all;
				changed = true;
			}
			if (action == UnityGUI::HeaderAction::Reset)
			{
				comp->Params = VolumeComponent::Create(comp->Type)->Params;
				changed = true;
			}
			else if (action == UnityGUI::HeaderAction::Remove)
				removeType = comp->Type;

			if (open)
			{
				UnityGUI::Spacing(3.0f);
				for (VolumeParameter& p : comp->Params)
					if (p.ShowIfKey.empty() || comp->I(p.ShowIfKey) == p.ShowIfValue)   // 모드에 맞는 칸만 (Depth Of Field 등)
						DrawParam(p, isDefault, changed);
				if (comp->Type == "Shadows")
					DrawCascadeBar(*comp);
				UnityGUI::Spacing(4.0f);
			}
			ImGui::PopID();
		}
		if (!removeType.empty())
		{
			profile->Remove(removeType);
			changed = true;
		}

		// Add Override: 아직 없는 효과만 (기본 프로파일은 모든 효과가 늘 있으므로 없음)
		UnityGUI::Spacing(8.0f);
		if (!isDefault && UnityGUI::CenterButton("Add Override##addov"))
			ImGui::OpenPopup("##addOverride");
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.19f, 0.19f, 0.19f, 1.0f));
		if (ImGui::BeginPopup("##addOverride"))
		{
			ImGui::TextDisabled("Volume Overrides");
			ImGui::Separator();
			// 분류별 하위 메뉴 (Post-processing / Shadowing)
			std::vector<std::string> categories;
			for (const std::string& type : VolumeComponent::Types())
			{
				const std::string cat = VolumeComponent::Create(type)->Category;
				if (std::find(categories.begin(), categories.end(), cat) == categories.end())
					categories.push_back(cat);
			}
			for (const std::string& cat : categories)
			{
				if (!ImGui::BeginMenu(cat.c_str()))
					continue;
				for (const std::string& type : VolumeComponent::Types())
				{
					auto sample = VolumeComponent::Create(type);
					if (sample->Category != cat)
						continue;
					if (ImGui::MenuItem(sample->DisplayName.c_str(), nullptr, false, !profile->Has(type)))
					{
						profile->Add(type);
						changed = true;
					}
				}
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}
		ImGui::PopStyleColor();
		ImGui::PopID();

		if (changed)
			MarkDirty(*profile);
		SaveIfIdle(profile);
	}

	bool ProfileField(const char* label, std::string& path, const std::string& newBaseName, bool showNewClone)
	{
		auto profile = path.empty() ? nullptr : VolumeProfile::Load(path);
		const std::string text = profile ? profile->Name() + " (Volume Profile)" : std::string("None (Volume Profile)");
		static const char* kButtons[] = { "New", "Clone" };
		const int buttonCount = showNewClone ? (profile ? 2 : 1) : 0;
		ImVec2 fmin, fmax;
		const int pressed = UnityGUI::ObjectFieldButtons(label, text.c_str(), "volume_profile", kButtons, buttonCount, &fmin, &fmax);
		bool changed = false;

		ImGui::PushID(label);
		// ⊙ = Object Picker 창 (Select Volume Profile). 키는 이 필드의 ImGui ID
		const std::string pickerKey = "profile:" + std::to_string(ImGui::GetID("##picker"));
		if (pressed == -1)
		{
			ObjectPicker::Options opt;
			opt.TypeName = "Volume Profile";
			opt.Icon = "volume_profile";
			opt.Items = AllProfiles();
			opt.Current = path;
			opt.Describe = [](const std::string& p) {
				auto vp = VolumeProfile::Load(p);
				return vp ? std::to_string(vp->Components.size()) + " overrides" : std::string("(cannot load)");
			};
			ObjectPicker::Open(pickerKey, std::move(opt));
		}
		else if (pressed == 0)
		{
			path = VolumeProfile::CreateAsset("Assets\\Settings\\", newBaseName);
			changed = true;
		}
		else if (pressed == 1 && profile)
		{
			// Clone: 같은 내용의 새 파일
			const std::string clonePath = VolumeProfile::CreateAsset(fs::path(profile->Path).parent_path().string(), profile->Name());
			if (auto clone = VolumeProfile::Load(clonePath))
			{
				clone->ApplyJson(profile->ToJsonString());
				clone->Save();
				path = clonePath;
				changed = true;
			}
		}

		// Project 창에서 끌어 놓기
		const ImVec2 after = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(fmin);
		ImGui::InvisibleButton("##dropArea", ImVec2((std::max)(1.0f, fmax.x - fmin.x - 22.0f), fmax.y - fmin.y));
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
			{
				const std::string dropped(static_cast<const char*>(payload->Data));
				if (IsVolumeProfilePath(dropped))
				{
					path = dropped;
					changed = true;
				}
			}
			ImGui::EndDragDropTarget();
		}
		// 필드 클릭 = Project 창에서 그 에셋 선택 (Unity 의 ping)
		if (ImGui::IsItemClicked() && profile)
			SelectionManager::SetSelectedFile(PathManager::GetI()->GetMovePathW(string_to_wstring(profile->Path)));
		ImGui::SetCursorScreenPos(after);

		std::string picked;
		if (ObjectPicker::Poll(pickerKey, picked) && picked != path)
		{
			path = picked;
			changed = true;
		}
		ImGui::PopID();
		if (changed)
			EditorLog::Write("Volume", "profile field '%s' -> %s", label, path.empty() ? "(none)" : path.c_str());
		return changed;
	}
}
