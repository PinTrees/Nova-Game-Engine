#include "pch.h"
#include "PreferencesWindow.h"
#include "UnityGUI.h"
#include "EditorPrefs.h"
#include "EditorUtility.h"
#include "ExternalScriptEditor.h"
#include "ScriptEngine.h"
#include "GraphicsSettings.h"
#include <filesystem>

namespace
{
	bool s_Open = false;
	bool s_FocusNext = false;
	std::string s_Category = "External Tools";
	char s_Args[512] = {};
	bool s_ArgsLoaded = false;

	const char* kCategories[] = { "External Tools", "Graphics", "NOVA Code" };
	constexpr float kLabelW = 230.0f;

	void Title(const char* text)
	{
		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::SetWindowFontScale(1.35f);
		ImGui::TextUnformatted(text);
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopFont();
		ImGui::Spacing();
	}

	// Unity 식 한 줄: 왼쪽 이름, 오른쪽 값
	void Label(const char* text, const char* tooltip = nullptr)
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(text);
		if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("%s", tooltip);
		ImGui::SameLine(kLabelW);
		ImGui::SetNextItemWidth((std::min)(420.0f, ImGui::GetContentRegionAvail().x));
	}

	// 에디터가 쓸 그래픽 API (다음 실행부터). 빌드된 게임의 순서는 Project Settings > Player
	void DrawGraphics()
	{
		Title("Graphics");
		const GraphicsAPI current = GraphicsSettings::GetEditorAPI();
		Label("Editor Graphics API", "Applied the next time the editor starts");
		if (ImGui::BeginCombo("##editorApi", GraphicsAPIToString(current)))
		{
			for (GraphicsAPI api : GraphicsSettings::AllAPIs())
			{
				std::string reason;
				const bool ok = GraphicsSettings::IsSupported(api, &reason);
				const std::string label = std::string(GraphicsAPIToString(api)) + (ok ? "" : "  (not available: " + reason + ")");
				if (ImGui::Selectable(label.c_str(), api == current))
				{
					GraphicsSettings::SetEditorAPI(api);
					EditorLog::Write("Graphics", "editor graphics API set to %s (next start)", GraphicsAPIToKey(api));
				}
			}
			ImGui::EndCombo();
		}
		Label("Running With");
		ImGui::TextUnformatted(GraphicsAPIToString(GraphicsSettings::GetActiveAPI()));
		ImGui::TextDisabled("%s", GraphicsSettings::SelectionLog().c_str());
		ImGui::Spacing();
		std::string reason;
		if (GraphicsSettings::GetEditorAPI() != GraphicsSettings::GetActiveAPI())
			UnityGUI::HelpBox(GraphicsSettings::IsSupported(GraphicsSettings::GetEditorAPI(), &reason)
				? "Restart the editor to switch the graphics API."
				: ("The selected API is not available yet (" + reason + "), so the editor keeps running with DirectX 11.").c_str(), true);
		UnityGUI::HelpBox("To override once, start the editor with -force-d3d11 or -force-opengl. The order used by built games is set in Project Settings > Player > Other Settings.", false);
	}

	void DrawExternalTools()
	{
		Title("External Tools");
		using namespace ExternalScriptEditor;
		const Editor current = Current();

		Label("External Script Editor", "The code editor that opens C# scripts (double-click in the Project window, Console, Inspector).");
		const std::string preview = current.Type == Kind::Custom ? current.Name + " (" + wstring_to_string(current.Path) + ")" : current.Name;
		if (ImGui::BeginCombo("##scriptEditor", preview.c_str()))
		{
			for (const Editor& e : Installed())
			{
				const bool selected = e.Type == current.Type && (e.Path.empty() || e.Path == current.Path);
				std::string label = e.Name;
				if (!e.Path.empty() && e.Type == Kind::VisualStudio)
					label += "##" + wstring_to_string(e.Path);
				if (ImGui::Selectable(label.c_str(), selected))
					SetCurrent(e);
			}
			if (current.Type == Kind::Custom)
				ImGui::Selectable(preview.c_str(), true);
			ImGui::Separator();
			if (ImGui::Selectable("Browse..."))
			{
				wchar_t pf[MAX_PATH] = {};
				::GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH);
				const std::wstring exe = EditorUtility::OpenFileDialog(std::wstring(pf) + L"\\", std::wstring(L"Select External Script Editor"), std::vector<std::wstring>{ L"exe" });
				if (!exe.empty())
				{
					Editor e;
					e.Type = Kind::Custom;
					e.Path = exe;
					e.Name = wstring_to_string(std::filesystem::path(exe).stem().wstring());
					SetCurrent(e);
				}
			}
			ImGui::EndCombo();
		}

		ImGui::Spacing();
		switch (current.Type)
		{
		case Kind::BuiltIn:
			UnityGUI::HelpBox("NOVA Code is the code editor built into this editor: C# syntax highlighting, completion for the engine API, "
				"find/replace and compile errors shown in the code. Saving a script (Ctrl+S) recompiles it right away.", false);
			break;
		case Kind::Custom:
		{
			if (!s_ArgsLoaded)
			{
				strncpy_s(s_Args, CustomArgs().c_str(), _TRUNCATE);
				s_ArgsLoaded = true;
			}
			Label("External Script Editor Args", "$(File) = script path, $(Line) = line number, $(ProjectPath) = project folder");
			if (ImGui::InputText("##args", s_Args, sizeof(s_Args)))
				SetCustomArgs(s_Args);
			ImGui::SetCursorPosX(kLabelW);
			if (ImGui::Button("Reset argument"))
			{
				strncpy_s(s_Args, DefaultCustomArgs(), _TRUNCATE);
				SetCustomArgs(s_Args);
			}
			ImGui::SameLine();
			ImGui::TextDisabled("$(File)  $(Line)  $(ProjectPath)");
			break;
		}
		case Kind::SystemDefault:
			UnityGUI::HelpBox("Scripts open with the program Windows associates with .cs files.", false);
			break;
		default:
			UnityGUI::HelpBox("The project folder is opened with Assembly-CSharp.csproj, so IntelliSense knows the NOVA engine API.", false);
			break;
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TextUnformatted("Generate .csproj files for:");
		ImGui::Indent(16.0f);
		bool assets = true;
		ImGui::BeginDisabled();
		ImGui::Checkbox("Assets (Assembly-CSharp)", &assets);
		ImGui::EndDisabled();
		ImGui::Unindent(16.0f);
		ImGui::Spacing();
		if (ImGui::Button("Regenerate project files"))
			ScriptEngine::RegenerateProjectFiles();
		ImGui::SameLine();
		if (ImGui::Button("Refresh editor list"))
			Installed(true);
	}

	void DrawNovaCode()
	{
		Title("NOVA Code");
		float fontSize = EditorPrefs::GetFloat("NovaCode.FontSize", 16.0f);
		Label("Font Size");
		if (ImGui::SliderFloat("##fontSize", &fontSize, 10.0f, 32.0f, "%.0f"))
			EditorPrefs::SetFloat("NovaCode.FontSize", floorf(fontSize + 0.5f));

		int tab = EditorPrefs::GetInt("NovaCode.TabSize", 4);
		Label("Tab Size");
		const char* tabs[] = { "2", "4", "8" };
		int tabIndex = tab == 2 ? 0 : (tab == 8 ? 2 : 1);
		if (ImGui::Combo("##tabSize", &tabIndex, tabs, 3))
			EditorPrefs::SetInt("NovaCode.TabSize", tabIndex == 0 ? 2 : (tabIndex == 2 ? 8 : 4));

		bool complete = EditorPrefs::GetBool("NovaCode.AutoComplete", true);
		Label("Auto Complete", "Show completions while typing (Ctrl+Space always works)");
		if (ImGui::Checkbox("##autoComplete", &complete))
			EditorPrefs::SetBool("NovaCode.AutoComplete", complete);

		bool brackets = EditorPrefs::GetBool("NovaCode.AutoCloseBrackets", true);
		Label("Auto Close Brackets", "Typing ( [ { \" inserts the closing one");
		if (ImGui::Checkbox("##autoClose", &brackets))
			EditorPrefs::SetBool("NovaCode.AutoCloseBrackets", brackets);
	}
}

namespace PreferencesWindow
{
	void Close() { s_Open = false; }
	bool IsOpen() { return s_Open; }

	void Open(const char* category)
	{
		s_Open = true;
		s_FocusNext = true;
		s_ArgsLoaded = false;
		if (category)
			s_Category = category;
		ExternalScriptEditor::Installed(true);   // 설치된 편집기를 다시 찾는다
		EditorLog::Write("App", "Preferences opened (%s)", s_Category.c_str());
	}

	void Draw()
	{
		if (!s_Open)
			return;
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowSize(ImVec2(860, 560), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
		if (s_FocusNext)
		{
			ImGui::SetNextWindowFocus();
			s_FocusNext = false;
		}
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
		if (!ImGui::Begin("Preferences", &s_Open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
		{
			ImGui::End();
			ImGui::PopStyleColor();
			return;
		}

		// 왼쪽: 분류 목록
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
		ImGui::BeginChild("##prefCategories", ImVec2(190, 0), true);
		for (const char* c : kCategories)
			if (ImGui::Selectable(c, s_Category == c))
				s_Category = c;
		ImGui::EndChild();
		ImGui::PopStyleColor();

		ImGui::SameLine();
		ImGui::BeginChild("##prefBody", ImVec2(0, 0), false);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 6));
		if (s_Category == "External Tools")
			DrawExternalTools();
		else if (s_Category == "Graphics")
			DrawGraphics();
		else if (s_Category == "NOVA Code")
			DrawNovaCode();
		ImGui::PopStyleVar();
		ImGui::EndChild();

		ImGui::End();
		ImGui::PopStyleColor();
	}
}
