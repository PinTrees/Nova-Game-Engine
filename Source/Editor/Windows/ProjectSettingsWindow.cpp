#include "pch.h"
#include "ProjectSettingsWindow.h"
#include "UnityGUI.h"
#include "VolumeEditor.h"
#include "VolumeProfile.h"
#include "RenderPipelineSettings.h"
#include "BuildSettings.h"
#include "GraphicsSettings.h"

namespace
{
	bool s_Open = false;
	bool s_FocusNext = false;
	std::string s_Category = "Graphics";

	const char* kCategories[] = { "Graphics", "Player" };

	// Unity 의 Graphics APIs for Windows 목록: 위가 우선. 선택한 줄을 위/아래로, + 로 추가, - 로 제거 (하나는 남긴다)
	bool DrawGraphicsApiList(std::vector<GraphicsAPI>& list)
	{
		static int s_Selected = 0;
		bool changed = false;
		s_Selected = std::clamp(s_Selected, 0, (int)list.size() - 1);
		UnityGUI::Label("Graphics APIs for Windows", 1);
		ImGui::Indent(18.0f);
		const float w = (std::min)(460.0f, ImGui::GetContentRegionAvail().x - 8.0f);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.17f, 0.17f, 0.17f, 1.0f));
		ImGui::BeginChild("##graphicsApis", ImVec2(w, list.size() * 24.0f + 10.0f), true);
		bool anyUnsupported = false;
		for (int i = 0; i < (int)list.size(); ++i)
		{
			std::string reason;
			const bool ok = GraphicsSettings::IsSupported(list[i], &reason);
			anyUnsupported |= !ok;
			std::string label = std::string(GraphicsAPIToString(list[i])) + (ok ? (GraphicsSettings::IsExperimental(list[i]) ? "   (experimental)" : "") : "   (not available: " + reason + ")") + "##api" + std::to_string(i);
			if (!ok)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
			if (ImGui::Selectable(label.c_str(), s_Selected == i))
				s_Selected = i;
			if (!ok)
				ImGui::PopStyleColor();
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
		// 버튼: ▲ ▼  -  +
		ImGui::BeginDisabled(s_Selected <= 0);
		if (ImGui::Button(ICON_FA_ARROW_UP "##apiUp", ImVec2(28, 0)))
		{
			std::swap(list[s_Selected], list[s_Selected - 1]);
			--s_Selected;
			changed = true;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(s_Selected >= (int)list.size() - 1);
		if (ImGui::Button(ICON_FA_ARROW_DOWN "##apiDown", ImVec2(28, 0)))
		{
			std::swap(list[s_Selected], list[s_Selected + 1]);
			++s_Selected;
			changed = true;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(list.size() <= 1);
		if (ImGui::Button(ICON_FA_MINUS "##apiRemove", ImVec2(28, 0)))
		{
			list.erase(list.begin() + s_Selected);
			s_Selected = (std::max)(0, s_Selected - 1);
			changed = true;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		std::vector<GraphicsAPI> missing;
		for (GraphicsAPI api : GraphicsSettings::AllAPIs())
			if (std::find(list.begin(), list.end(), api) == list.end())
				missing.push_back(api);
		ImGui::BeginDisabled(missing.empty());
		if (ImGui::Button(ICON_FA_PLUS "##apiAdd", ImVec2(28, 0)))
			ImGui::OpenPopup("##addApi");
		ImGui::EndDisabled();
		if (ImGui::BeginPopup("##addApi"))
		{
			for (GraphicsAPI api : missing)
				if (ImGui::MenuItem(GraphicsAPIToString(api)))
				{
					list.push_back(api);
					s_Selected = (int)list.size() - 1;
					changed = true;
				}
			ImGui::EndPopup();
		}
		ImGui::Unindent(18.0f);
		if (anyUnsupported)
			UnityGUI::HelpBox("APIs that are not available on the player PC (or not finished in this engine version) are skipped: the game starts with the next API in the list.", false);
		return changed;
	}

	// Unity 의 Project Settings > Player (Windows 탭의 Resolution and Presentation 까지)
	void DrawPlayer()
	{
		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::SetWindowFontScale(1.35f);
		ImGui::TextUnformatted("Player");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopFont();
		ImGui::Spacing();
		BuildSettings::Player& p = BuildSettings::GetPlayer();
		bool changed = false;
		changed |= UnityGUI::TextField("Company Name", &p.CompanyName);
		std::string product = p.ProductName.empty() ? BuildSettings::ProductName() : p.ProductName;
		if (UnityGUI::TextField("Product Name", &product)) { p.ProductName = product; changed = true; }
		changed |= UnityGUI::TextField("Version", &p.Version);
		UnityGUI::ValueLabel("Default Icon", "NOVA logo (exe icon)");
		UnityGUI::Spacing(8.0f);
		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::TextUnformatted("Resolution and Presentation");
		ImGui::PopFont();
		static const char* kModes[] = { "Fullscreen Window", "Maximized Window", "Windowed" };
		int mode = (int)p.Mode;
		if (UnityGUI::Dropdown("Fullscreen Mode", &mode, kModes, 3)) { p.Mode = (BuildSettings::FullscreenMode)mode; changed = true; }
		if (p.Mode == BuildSettings::FullscreenMode::Windowed)
		{
			if (UnityGUI::Int("Default Screen Width", &p.Width, 1)) { p.Width = (std::max)(320, p.Width); changed = true; }
			if (UnityGUI::Int("Default Screen Height", &p.Height, 1)) { p.Height = (std::max)(240, p.Height); changed = true; }
		}
		if (p.Mode != BuildSettings::FullscreenMode::FullscreenWindow)
			changed |= UnityGUI::Toggle("Resizable Window", &p.Resizable);
		changed |= UnityGUI::Toggle("Run In Background", &p.RunInBackground);
		UnityGUI::Spacing(8.0f);
		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::TextUnformatted("Other Settings");
		ImGui::PopFont();
		UnityGUI::Label("Rendering", 0, true);
		changed |= UnityGUI::Toggle("Auto Graphics API for Windows", &p.AutoGraphicsAPI, 1);
		if (p.AutoGraphicsAPI)
			UnityGUI::HelpBox("Auto: the game uses DirectX 11 (like Unity). Turn Auto off and add OpenGL to the list to ship it too - the build then also includes the shader converter (about 23 MB).", false, 1);
		else
			changed |= DrawGraphicsApiList(p.GraphicsAPIs);
		if (changed)
			BuildSettings::SavePlayer();
		UnityGUI::Spacing(6.0f);
		UnityGUI::HelpBox("The built game needs the .NET 8 (or newer) runtime on the target PC when the project uses C# scripts.", false);
	}

	void SectionTitle(const char* text)
	{
		ImFont* bold = UnityGUI::BoldFont();
		ImGui::PushFont(bold);
		ImGui::TextUnformatted(text);
		ImGui::PopFont();
	}

	void DrawGraphics()
	{
		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::SetWindowFontScale(1.35f);
		ImGui::TextUnformatted("Graphics");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopFont();
		ImGui::Spacing();

		SectionTitle("Pipeline Specific Settings");
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.72f, 0.72f, 1.0f));
		ImGui::TextWrapped("Set the default values of all the scenes within the project. These settings are used by the Universal Render Pipeline of this engine.");
		ImGui::PopStyleColor();
		ImGui::Spacing();

		SectionTitle("Volume");
		// Unity 처럼 기본 프로파일은 항상 있다: 없으면 Assets/Settings/DefaultVolumeProfile 을 만든다
		auto profile = RenderPipelineSettings::EnsureDefaultVolumeProfile();
		std::string path = RenderPipelineSettings::DefaultVolumeProfilePath();
		if (VolumeEditor::ProfileField("Default Profile", path, "DefaultVolumeProfile", false))
		{
			RenderPipelineSettings::SetDefaultVolumeProfilePath(path);
			profile = RenderPipelineSettings::DefaultVolumeProfile();
		}
		UnityGUI::HelpBox("The values in the Default Volume can be overridden by Volumes inside scenes.", false);
		if (profile == nullptr)
			return;

		// 기본 프로파일에는 모든 효과가 있어야 한다 (빠진 것은 기본값으로 채움)
		bool added = false;
		for (const std::string& type : VolumeComponent::Types())
			if (!profile->Has(type))
			{
				profile->Add(type);
				added = true;
			}
		if (added)
			profile->Save();

		UnityGUI::Spacing(6.0f);
		SectionTitle("Post-processing");
		VolumeEditor::DrawProfile(profile, true);
	}
}

namespace ProjectSettingsWindow
{
	void Close() { s_Open = false; }
	bool IsOpen() { return s_Open; }

	void Open(const char* category)
	{
		s_Open = true;
		s_FocusNext = true;
		if (category)
			s_Category = category;
		EditorLog::Write("App", "Project Settings opened (%s)", s_Category.c_str());
	}

	void Draw()
	{
		if (!s_Open)
			return;
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowSize(ImVec2(920, 720), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
		if (s_FocusNext)
		{
			ImGui::SetNextWindowFocus();
			s_FocusNext = false;
		}
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
		if (!ImGui::Begin("Project Settings", &s_Open, ImGuiWindowFlags_NoCollapse))
		{
			ImGui::End();
			ImGui::PopStyleColor();
			return;
		}

		// 왼쪽: 분류 목록
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
		ImGui::BeginChild("##psCategories", ImVec2(190, 0), true);
		for (const char* c : kCategories)
			if (ImGui::Selectable(c, s_Category == c))
				s_Category = c;
		ImGui::EndChild();
		ImGui::PopStyleColor();

		ImGui::SameLine();
		ImGui::BeginChild("##psBody", ImVec2(0, 0), false);
		if (s_Category == "Graphics")
			DrawGraphics();
		else if (s_Category == "Player")
			DrawPlayer();
		ImGui::EndChild();

		ImGui::End();
		ImGui::PopStyleColor();
	}
}
