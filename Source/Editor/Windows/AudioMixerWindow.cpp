#include "pch.h"
#include "AudioMixerWindow.h"
#include "AudioMixer.h"
#include "UnityGUI.h"

AudioMixerWindow* AudioMixerWindow::s_Instance = nullptr;

namespace
{
	const ImU32 kPanel = IM_COL32(40, 40, 40, 255);
	const ImU32 kHeader = IM_COL32(53, 53, 53, 255);
	const ImU32 kBorder = IM_COL32(25, 25, 25, 255);
	const ImU32 kText = IM_COL32(210, 210, 210, 255);
	const ImU32 kDim = IM_COL32(140, 140, 140, 255);
	const ImU32 kSelected = IM_COL32(44, 93, 135, 255);

	void SectionHeader(const char* text, float width)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		dl->AddRectFilled(p, ImVec2(p.x + width, p.y + 20), kHeader);
		dl->AddText(UnityGUI::BoldFont(), ImGui::GetFontSize(), ImVec2(p.x + 6, p.y + 3), kText, text);
		ImGui::Dummy(ImVec2(width, 20));
	}

	// 오른쪽 위 작은 + / - 버튼 (방금 그린 헤더 줄 안)
	bool HeaderButton(const char* id, const char* label, float right)
	{
		const ImVec2 back = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(ImVec2(right, back.y - 19));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 0));
		const bool pressed = ImGui::SmallButton((std::string(label) + "##" + id).c_str());
		ImGui::PopStyleVar();
		ImGui::SetCursorScreenPos(back);
		return pressed;
	}
}

AudioMixerWindow::AudioMixerWindow()
	: EditorWindow("Audio Mixer", ICON_FA_SLIDERS)
{
	s_Instance = this;
	SetIsOpened(false);
}

void AudioMixerWindow::Open(const std::string& mixerPath)
{
	if (s_Instance == nullptr)
		return;
	if (!mixerPath.empty())
		s_Instance->m_Path = mixerPath;
	s_Instance->SetIsOpened(true);
	s_Instance->m_FocusNext = true;
}

void AudioMixerWindow::Close()
{
	if (s_Instance)
		s_Instance->SetIsOpened(false);
}

bool AudioMixerWindow::IsOpen() { return s_Instance && s_Instance->GetIsOpened(); }

void AudioMixerWindow::BeforeBegin()
{
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowSize(ImVec2(960.0f, 520.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	if (m_FocusNext)
	{
		ImGui::SetNextWindowFocus();
		m_FocusNext = false;
	}
}

void AudioMixerWindow::PushStyle() { ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); }
void AudioMixerWindow::PopStyle() { ImGui::PopStyleVar(); }

std::shared_ptr<AudioMixer> AudioMixerWindow::Current()
{
	if (m_Path.empty())
	{
		const auto all = AudioMixer::FindAll();
		if (!all.empty())
			m_Path = all[0];
	}
	return m_Path.empty() ? nullptr : AudioMixer::Load(m_Path);
}

void AudioMixerWindow::OnRender()
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 avail = ImGui::GetContentRegionAvail();

	// ---- 위 막대: 믹서 · 새 믹서
	dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + 26), kHeader);
	ImGui::SetCursorScreenPos(ImVec2(origin.x + 6, origin.y + 3));
	std::shared_ptr<AudioMixer> mixer = Current();
	ImGui::SetNextItemWidth(260);
	if (ImGui::BeginCombo("##mixer", mixer ? mixer->Path.c_str() : "(no Audio Mixer)"))
	{
		for (const std::string& p : AudioMixer::FindAll())
			if (ImGui::Selectable(p.c_str(), p == m_Path))
			{
				m_Path = p;
				m_Group = 0;
			}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("+ New Mixer"))
	{
		std::string path = "Assets\\NewAudioMixer.mixer";
		for (int n = 1; std::filesystem::exists(PathManager::GetI()->GetMovePathW(string_to_wstring(path))); ++n)
			path = "Assets\\NewAudioMixer " + std::to_string(n) + ".mixer";
		AudioMixer::Create(path);
		m_Path = path;
		m_Group = 0;
	}
	if (Application::IsPlaying())
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.3f, 1.0f), "Play: hearing the start snapshot + transitions / SetFloat (edits still apply)");
	}
	ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + 27));
	if (mixer == nullptr)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12);
		ImGui::TextDisabled("Create an Audio Mixer (+ New Mixer, or Project > Create > Audio Mixer) to route Audio Sources into groups.");
		return;
	}
	m_Group = std::clamp(m_Group, 0, (int)mixer->Groups.size() - 1);

	const float bodyH = avail.y - 27;
	const float leftW = 230.0f;
	ImGui::BeginChild("##mixleft", ImVec2(leftW, bodyH), false);
	DrawLeft(*mixer, bodyH);
	ImGui::EndChild();
	ImGui::SameLine(0, 0);
	const ImVec2 rp = ImGui::GetCursorScreenPos();
	dl->AddLine(ImVec2(rp.x, rp.y), ImVec2(rp.x, rp.y + bodyH), kBorder, 1.0f);
	ImGui::BeginChild("##mixright", ImVec2(avail.x - leftW, bodyH), false);
	const float stripsH = (std::min)(330.0f, bodyH * 0.62f);
	DrawStrips(*mixer, stripsH);
	DrawDetails(*mixer);
	ImGui::EndChild();
}

// ---------------------------------------------------------------------- 왼쪽: 스냅숏 · 그룹 · 노출 파라미터
void AudioMixerWindow::DrawLeft(AudioMixer& m, float)
{
	const float w = ImGui::GetContentRegionAvail().x;
	auto renameField = [&](int kind, int index, const std::string& current) -> bool {
		if (m_Renaming != kind || m_RenameIndex != index)
			return false;
		ImGui::SetNextItemWidth(w - 30);
		if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive())
			ImGui::SetKeyboardFocusHere();
		const bool done = ImGui::InputText("##rename", m_RenameBuf, sizeof(m_RenameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		if (done || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			const std::string name = m_RenameBuf;
			if (done && !name.empty() && name != current)
			{
				if (kind == 0) m.RenameGroup(index, name);
				else if (kind == 1 && m.FindSnapshot(name) < 0) m.Snapshots[index].Name = name;
				else if (kind == 2 && m.FindExposed(name) < 0) m.ExposedParams[index].Name = name;
				m.Save();
			}
			m_Renaming = -1;
		}
		return true;
	};
	auto startRename = [&](int kind, int index, const std::string& name) {
		m_Renaming = kind;
		m_RenameIndex = index;
		strncpy_s(m_RenameBuf, name.c_str(), _TRUNCATE);
	};

	// Snapshots
	SectionHeader("Snapshots", w);
	if (HeaderButton("snapadd", "+", ImGui::GetCursorScreenPos().x + w - 26))
	{
		m.EditSnapshot = m.AddSnapshot("Snapshot");
		m.Save();
	}
	for (int i = 0; i < (int)m.Snapshots.size(); ++i)
	{
		ImGui::PushID(i);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);
		if (!renameField(1, i, m.Snapshots[i].Name))
		{
			const std::string label = std::string(i == 0 ? ICON_FA_STAR "  " : "     ") + m.Snapshots[i].Name;
			if (ImGui::Selectable(label.c_str(), m.EditSnapshot == i, ImGuiSelectableFlags_AllowDoubleClick))
			{
				m.EditSnapshot = i;
				if (ImGui::IsMouseDoubleClicked(0))
					startRename(1, i, m.Snapshots[i].Name);
			}
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Set as start Snapshot", nullptr, false, i > 0))
				{
					std::swap(m.Snapshots[0], m.Snapshots[i]);
					m.EditSnapshot = 0;
					m.Save();
				}
				if (ImGui::MenuItem("Rename"))
					startRename(1, i, m.Snapshots[i].Name);
				if (ImGui::MenuItem("Delete", nullptr, false, m.Snapshots.size() > 1))
				{
					m.RemoveSnapshot(i);
					m.Save();
				}
				ImGui::EndPopup();
			}
		}
		ImGui::PopID();
	}
	ImGui::Dummy(ImVec2(0, 6));

	// Groups
	SectionHeader("Groups", w);
	const float headerRight = ImGui::GetCursorScreenPos().x + w;
	if (HeaderButton("grpadd", "+", headerRight - 50))
	{
		m_Group = m.AddGroup(m_Group, m_Group == 0 ? "Group" : m.Groups[m_Group].Name + " Child");
		m.Save();
	}
	if (HeaderButton("grpdel", "-", headerRight - 26) && m_Group > 0)
	{
		m.RemoveGroup(m_Group);
		m_Group = 0;
		m.Save();
	}
	for (int i = 0; i < (int)m.Groups.size(); ++i)
	{
		int depth = 0;
		for (int p = m.Groups[i].Parent; p >= 0; p = m.Groups[p].Parent)
			++depth;
		ImGui::PushID(1000 + i);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8 + depth * 14.0f);
		if (!renameField(0, i, m.Groups[i].Name))
		{
			if (ImGui::Selectable(m.Groups[i].Name.c_str(), m_Group == i, ImGuiSelectableFlags_AllowDoubleClick))
			{
				m_Group = i;
				if (ImGui::IsMouseDoubleClicked(0) && i > 0)
					startRename(0, i, m.Groups[i].Name);
			}
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Add child group"))
				{
					m_Group = m.AddGroup(i, "Group");
					m.Save();
				}
				if (ImGui::MenuItem("Rename", nullptr, false, i > 0))
					startRename(0, i, m.Groups[i].Name);
				if (ImGui::MenuItem("Remove", nullptr, false, i > 0))
				{
					m.RemoveGroup(i);
					m_Group = 0;
					m.Save();
				}
				ImGui::EndPopup();
			}
		}
		ImGui::PopID();
	}
	ImGui::Dummy(ImVec2(0, 6));

	// Exposed Parameters
	char title[64];
	sprintf_s(title, "Exposed Parameters (%d)", (int)m.ExposedParams.size());
	SectionHeader(title, w);
	if (m.ExposedParams.empty())
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kDim));
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - 16);
		ImGui::TextUnformatted("Right-click a volume or effect value below and choose Expose to change it from scripts (AudioMixer.SetFloat).");
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
	}
	for (int i = 0; i < (int)m.ExposedParams.size(); ++i)
	{
		ImGui::PushID(2000 + i);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);
		if (!renameField(2, i, m.ExposedParams[i].Name))
		{
			ImGui::Selectable(m.ExposedParams[i].Name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
				startRename(2, i, m.ExposedParams[i].Name);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", m.KeyLabel(m.ExposedParams[i].Key).c_str());
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Rename"))
					startRename(2, i, m.ExposedParams[i].Name);
				if (ImGui::MenuItem("Unexpose"))
				{
					m.Unexpose(i);
					m.Save();
				}
				ImGui::EndPopup();
			}
		}
		ImGui::PopID();
	}
}

// ---------------------------------------------------------------------- 오른쪽: 채널 스트립
void AudioMixerWindow::DrawStrips(AudioMixer& m, float height)
{
	ImGui::BeginChild("##strips", ImVec2(0, height), false, ImGuiWindowFlags_HorizontalScrollbar);
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float stripW = 112.0f, faderH = 150.0f;
	ImVec2 cursor = ImGui::GetCursorScreenPos();
	cursor.x += 8;
	cursor.y += 6;
	for (int i = 0; i < (int)m.Groups.size(); ++i)
	{
		AudioMixer::Group& g = m.Groups[i];
		ImGui::PushID(i);
		const ImVec2 p = ImVec2(cursor.x + i * (stripW + 6), cursor.y);
		const ImVec2 q = ImVec2(p.x + stripW, p.y + height - 18);
		dl->AddRectFilled(p, q, kPanel, 3.0f);
		dl->AddRect(p, q, m_Group == i ? kSelected : kBorder, 3.0f, 0, m_Group == i ? 2.0f : 1.0f);
		// 이름 (누르면 고름)
		dl->AddRectFilled(p, ImVec2(q.x, p.y + 20), m_Group == i ? kSelected : kHeader, 3.0f, ImDrawFlags_RoundCornersTop);
		const ImVec2 ts = ImGui::CalcTextSize(g.Name.c_str());
		dl->AddText(ImVec2(p.x + (stripW - ts.x) * 0.5f, p.y + 3), kText, g.Name.c_str());
		ImGui::SetCursorScreenPos(p);
		if (ImGui::InvisibleButton("##sel", ImVec2(stripW, 20)))
			m_Group = i;

		// 레벨 미터 + 페이더
		const ImVec2 meterP = ImVec2(p.x + 16, p.y + 30);
		const float level = m.GroupLevelDb(i);
		const float fill = std::clamp((level + 80.0f) / 80.0f, 0.0f, 1.0f);
		dl->AddRectFilled(meterP, ImVec2(meterP.x + 10, meterP.y + faderH), IM_COL32(20, 20, 20, 255));
		const ImU32 meterCol = level > -3.0f ? IM_COL32(230, 70, 60, 255) : level > -12.0f ? IM_COL32(230, 200, 60, 255) : IM_COL32(80, 200, 90, 255);
		dl->AddRectFilled(ImVec2(meterP.x, meterP.y + faderH * (1.0f - fill)), ImVec2(meterP.x + 10, meterP.y + faderH), meterCol);
		const std::string key = AudioMixer::VolumeKey(g);
		// Play 중엔 지금 들리는 값 (전환·SetFloat 포함), 편집 중엔 고른 스냅숏 값
		float db = Application::IsPlaying() ? m.LiveValue(key) : m.GetValue(m.EditSnapshot, key);
		ImGui::SetCursorScreenPos(ImVec2(p.x + 40, p.y + 30));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
		if (ImGui::VSliderFloat("##vol", ImVec2(26, faderH), &db, -80.0f, 20.0f, ""))
			m.SetValue(m.EditSnapshot, key, db);
		if (ImGui::IsItemDeactivatedAfterEdit())
			m.Save();
		ImGui::PopStyleColor();
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Expose Volume to script"))
			{
				m.Expose(key);
				m.Save();
			}
			if (ImGui::MenuItem("Reset to 0 dB"))
			{
				m.SetValue(m.EditSnapshot, key, 0.0f);
				m.Save();
			}
			ImGui::EndPopup();
		}
		char dbText[32];
		sprintf_s(dbText, db <= -79.9f ? "-inf dB" : "%.1f dB", db);
		dl->AddText(ImVec2(p.x + 72, p.y + 30 + faderH * 0.5f - 7), kText, dbText);
		char lvText[32];
		sprintf_s(lvText, level <= -79.9f ? "" : "%.0f", level);
		dl->AddText(ImVec2(p.x + 8, p.y + 32 + faderH), kDim, lvText);

		// Mute / Solo / Bypass
		const float by = p.y + 50 + faderH;
		auto toggle = [&](const char* label, bool& v, ImVec4 on, float x) {
			ImGui::SetCursorScreenPos(ImVec2(x, by));
			ImGui::PushStyleColor(ImGuiCol_Button, v ? on : ImVec4(0.24f, 0.24f, 0.24f, 1.0f));
			if (ImGui::Button(label, ImVec2(28, 20)))
			{
				v = !v;
				m.Save();
			}
			ImGui::PopStyleColor();
		};
		toggle("M", g.Mute, ImVec4(0.75f, 0.25f, 0.25f, 1.0f), p.x + 10);
		toggle("S", g.Solo, ImVec4(0.8f, 0.7f, 0.2f, 1.0f), p.x + 42);
		toggle("B", g.BypassEffects, ImVec4(0.25f, 0.45f, 0.8f, 1.0f), p.x + 74);

		// 이펙트 목록 + Add Effect
		float ey = by + 26;
		int removeAt = -1;
		for (int e = 0; e < (int)g.Effects.size(); ++e)
		{
			AudioMixer::Effect& fx = g.Effects[e];
			ImGui::PushID(e);
			ImGui::SetCursorScreenPos(ImVec2(p.x + 6, ey));
			ImGui::PushStyleColor(ImGuiCol_Button, fx.Bypass || g.BypassEffects ? ImVec4(0.2f, 0.2f, 0.2f, 1.0f) : ImVec4(0.28f, 0.33f, 0.4f, 1.0f));
			if (ImGui::Button(AudioMixer::EffectName(fx.Type), ImVec2(stripW - 12, 18)))
				m_Group = i;
			ImGui::PopStyleColor();
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Bypass", nullptr, fx.Bypass))
				{
					fx.Bypass = !fx.Bypass;
					m.Save();
				}
				if (ImGui::MenuItem("Remove"))
					removeAt = e;
				ImGui::EndPopup();
			}
			ImGui::PopID();
			ey += 21;
		}
		if (removeAt >= 0)
		{
			m.RemoveEffect(i, removeAt);
			m.Save();
		}
		ImGui::SetCursorScreenPos(ImVec2(p.x + 6, ey));
		if (ImGui::Button("Add Effect", ImVec2(stripW - 12, 18)))
			ImGui::OpenPopup("##addfx");
		if (ImGui::BeginPopup("##addfx"))
		{
			for (int t = 0; t < AudioMixer::EffectTypeCount; ++t)
				if (ImGui::MenuItem(AudioMixer::EffectName(t)))
				{
					m.AddEffect(i, t);
					m_Group = i;
					m.Save();
				}
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y));
	ImGui::Dummy(ImVec2(m.Groups.size() * (stripW + 6) + 8, height - 24));
	ImGui::EndChild();
}

// ---------------------------------------------------------------------- 아래: 고른 그룹의 값
void AudioMixerWindow::ParamSlider(AudioMixer& m, const std::string& key, const char* label, float min, float max, const char* format)
{
	float v = Application::IsPlaying() ? m.LiveValue(key) : m.GetValue(m.EditSnapshot, key);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
	ImGui::TextUnformatted(label);
	ImGui::SameLine(200);
	ImGui::SetNextItemWidth((std::min)(360.0f, ImGui::GetContentRegionAvail().x - 20));
	if (ImGui::SliderFloat(("##" + key).c_str(), &v, min, max, format))
		m.SetValue(m.EditSnapshot, key, v);
	if (ImGui::IsItemDeactivatedAfterEdit())
		m.Save();
	const bool exposed = [&] { for (const auto& e : m.ExposedParams) if (e.Key == key) return true; return false; }();
	if (exposed)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("(exposed)");
	}
	if (ImGui::BeginPopupContextItem(("##ctx" + key).c_str()))
	{
		if (ImGui::MenuItem(exposed ? "Already exposed" : "Expose to script", nullptr, false, !exposed))
		{
			m.Expose(key);
			m.Save();
		}
		ImGui::EndPopup();
	}
}

void AudioMixerWindow::DrawDetails(AudioMixer& m)
{
	if (m_Group < 0 || m_Group >= (int)m.Groups.size())
		return;
	AudioMixer::Group& g = m.Groups[m_Group];
	const float w = ImGui::GetContentRegionAvail().x;
	char title[160];
	sprintf_s(title, "%s  —  Snapshot: %s", g.Name.c_str(), m.Snapshots[m.EditSnapshot].Name.c_str());
	SectionHeader(title, w);
	ImGui::Dummy(ImVec2(0, 2));
	ParamSlider(m, AudioMixer::VolumeKey(g), "Volume", -80.0f, 20.0f, "%.1f dB");
	for (const AudioMixer::Effect& fx : g.Effects)
	{
		ImGui::Dummy(ImVec2(0, 2));
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
		ImGui::TextColored(fx.Bypass ? ImVec4(0.5f, 0.5f, 0.5f, 1.0f) : ImVec4(0.75f, 0.85f, 1.0f, 1.0f), "%s%s", AudioMixer::EffectName(fx.Type), fx.Bypass ? "  (bypassed)" : "");
		for (int p = 0; p < AudioMixer::EffectParamCount(fx.Type); ++p)
		{
			const AudioMixer::ParamInfo& info = AudioMixer::EffectParam(fx.Type, p);
			const std::string key = AudioMixer::EffectKey(fx, p);
			if (fx.Type == AudioMixer::Reverb && p == 0)
			{
				// 방 종류 (I3DL2 프리셋)
				int preset = std::clamp((int)lroundf(m.GetValue(m.EditSnapshot, key)), 0, AudioMixer::ReverbPresetCount() - 1);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
				ImGui::TextUnformatted("Room");
				ImGui::SameLine(200);
				ImGui::SetNextItemWidth((std::min)(360.0f, ImGui::GetContentRegionAvail().x - 20));
				if (ImGui::BeginCombo(("##" + key).c_str(), AudioMixer::ReverbPresetName(preset)))
				{
					for (int r = 0; r < AudioMixer::ReverbPresetCount(); ++r)
						if (ImGui::Selectable(AudioMixer::ReverbPresetName(r), r == preset))
						{
							m.SetValue(m.EditSnapshot, key, (float)r);
							m.Save();
						}
					ImGui::EndCombo();
				}
				continue;
			}
			char format[32];
			sprintf_s(format, info.Max > 100.0f ? "%%.0f %s" : "%%.2f %s", info.Unit);
			ParamSlider(m, key, info.Name, info.Min, info.Max, format);
		}
	}
	if (g.Effects.empty())
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
		ImGui::TextDisabled("No effects. Use Add Effect on the strip (Lowpass, Highpass, Echo, Reverb).");
	}
}
