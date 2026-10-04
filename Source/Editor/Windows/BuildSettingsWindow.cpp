#include "pch.h"
#include "BuildSettingsWindow.h"
#include "BuildSettings.h"
#include "BuildPipeline.h"
#include "AndroidBuild.h"
#include "ProjectSettingsWindow.h"
#include "UnityGUI.h"
#include "Debug.h"
#include "ScriptEngine.h"
#include <filesystem>
#include <shobjidl.h>

namespace fs = std::filesystem;

namespace
{
	bool s_Open = false;
	bool s_FocusNext = false;
	int s_Selected = -1;
	std::string s_LastError;

	// 폴더 선택 (Unity: Build 를 누르면 출력 폴더를 묻는다)
	bool PickFolder(const std::wstring& start, std::wstring& out)
	{
		IFileOpenDialog* dialog = nullptr;
		if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
			return false;
		DWORD options = 0;
		dialog->GetOptions(&options);
		dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
		dialog->SetTitle(L"Build: choose a folder for the game");
		IShellItem* folder = nullptr;
		std::error_code ec;
		if (!start.empty() && fs::exists(start, ec) && SUCCEEDED(::SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&folder))))
		{
			dialog->SetFolder(folder);
			folder->Release();
		}
		bool ok = false;
		if (SUCCEEDED(dialog->Show(Application::GetI()->GetMainHwnd())))
		{
			IShellItem* item = nullptr;
			if (SUCCEEDED(dialog->GetResult(&item)))
			{
				PWSTR raw = nullptr;
				if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) && raw)
				{
					out = raw;
					ok = true;
				}
				::CoTaskMemFree(raw);
				item->Release();
			}
		}
		dialog->Release();
		return ok;
	}

	void StartBuild(const std::wstring& folder, bool run, bool reveal = true)
	{
		BuildPipeline::Options o;
		o.Reveal = reveal;
		o.OutputFolder = folder;
		o.Run = run;
		o.Development = BuildSettings::DevelopmentBuild();
		std::string error;
		if (!BuildPipeline::Start(o, error))
		{
			s_LastError = error;
			Debug::LogError("Build failed: " + error);
			return;
		}
		s_LastError.clear();
		BuildSettings::LastBuildFolder() = wstring_to_string(folder);
		BuildSettings::SaveEditorBuild();
	}

	void Build(bool run, bool askFolder)
	{
		std::wstring folder = string_to_wstring(BuildSettings::LastBuildFolder());
		if (askFolder || folder.empty())
		{
			std::wstring start = folder.empty() ? PathManager::GetI()->GetContentPathW() : folder;
			if (!PickFolder(start, folder))
				return;
		}
		StartBuild(folder, run);
	}

	// Android: <폴더>/<제품>.apk (Unity 는 .apk 이름을 묻는다 — 여기서는 폴더를 묻고 제품 이름으로)
	std::vector<std::string> s_Devices;
	bool s_DevicesScanned = false;

	void BuildAndroid(bool run, bool askFolder)
	{
		std::wstring apk = string_to_wstring(BuildSettings::LastAndroidApk());
		std::wstring folder = apk.empty() ? std::wstring() : fs::path(apk).parent_path().wstring();
		if (askFolder || folder.empty())
		{
			std::wstring start = folder.empty() ? PathManager::GetI()->GetContentPathW() : folder;
			if (!PickFolder(start, folder))
				return;
		}
		AndroidBuild::Options o;
		o.OutputApk = (fs::path(folder) / (string_to_wstring(BuildSettings::ProductName()) + L".apk")).wstring();
		o.Run = run;
		o.Device = BuildSettings::AndroidRunDevice();
		std::string error;
		if (!AndroidBuild::Start(o, error))
		{
			s_LastError = error;
			Debug::LogError("Android build failed: " + error);
			return;
		}
		s_LastError.clear();
	}

	void DrawAndroidSettings()
	{
		using namespace UnityGUI;
		auto& player = BuildSettings::GetPlayer();
		static const char* kTc[] = { "ASTC", "ETC2", "DXT (BC)", "Don't override (RGBA32)" };
		if (Dropdown("Texture Compression", &player.AndroidTextureCompression, kTc, 4))
			BuildSettings::SavePlayer();
		// Run Device (Unity 의 Run Device): 기본 = 첫 장치. Refresh = adb devices (켜진 MuMu 플레이어에는 adb connect 먼저)
		if (!s_DevicesScanned)
		{
			s_DevicesScanned = true;
			s_Devices = AndroidBuild::Devices(false);
		}
		std::vector<std::string> names = { "Default device" };
		int index = 0;
		for (const std::string& d : s_Devices)
		{
			if (d == BuildSettings::AndroidRunDevice()) index = (int)names.size();
			names.push_back(d);
		}
		if (index == 0 && !BuildSettings::AndroidRunDevice().empty())   // 지금 연결되지 않은 장치도 이름은 보인다
		{
			index = (int)names.size();
			names.push_back(BuildSettings::AndroidRunDevice() + " (not connected)");
		}
		std::vector<const char*> items;
		for (const std::string& n : names) items.push_back(n.c_str());
		if (Dropdown("Run Device", &index, items.data(), (int)items.size()))
		{
			if (index == 0)
				BuildSettings::AndroidRunDevice().clear();
			else if ((size_t)(index - 1) < s_Devices.size())
				BuildSettings::AndroidRunDevice() = s_Devices[(size_t)(index - 1)];
			BuildSettings::SaveEditorBuild();
		}
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 80.0f);
		if (ImGui::Button("Refresh", ImVec2(80, 0)))
			s_Devices = AndroidBuild::Devices(true);
		if (Toggle("Development Build", &BuildSettings::DevelopmentBuild()))
			BuildSettings::SaveEditorBuild();
		ValueLabel("Package Name", AndroidBuild::PackageName().c_str());
		const std::wstring sdk = AndroidBuild::FindSdk(), java = AndroidBuild::FindJava(), lib = AndroidBuild::PlayerLibrary("x86_64");
		ValueLabel("Android SDK", sdk.empty() ? "Not found" : wstring_to_string(sdk).c_str());
		ValueLabel("JDK", java.empty() ? "Not found" : wstring_to_string(java).c_str());
		if (sdk.empty() || java.empty())
			HelpBox("Install \"Android Build Support\" from NOVA Hub (Installs > engine > Add modules).", true);
		else if (lib.empty())
			HelpBox("This engine has no Android player library (Android/Player/x86_64/libnova.so).", true);
		const std::string last = BuildSettings::LastAndroidApk();
		ValueLabel("Last Build", last.empty() ? "(none)" : last.c_str());
	}

	void AddScene(const std::string& path)
	{
		auto& scenes = BuildSettings::Scenes();
		for (const auto& s : scenes)
			if (_stricmp(s.Path.c_str(), path.c_str()) == 0)
				return;
		scenes.push_back({ path, true });
		BuildSettings::SaveScenes();
	}

	void DrawScenesInBuild()
	{
		auto& scenes = BuildSettings::Scenes();
		ImGui::TextUnformatted("Scenes In Build");
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const float h = 190.0f;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(42, 42, 42, 255));
		dl->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(26, 26, 26, 255));
		ImGui::BeginChild("##scenesInBuild", ImVec2(w, h), false);
		int buildIndex = 0, moveFrom = -1, moveTo = -1, remove = -1;
		for (int i = 0; i < (int)scenes.size(); ++i)
		{
			auto& s = scenes[i];
			ImGui::PushID(i);
			const ImVec2 rp = ImGui::GetCursorScreenPos();
			const bool selected = s_Selected == i;
			if (selected)
				ImGui::GetWindowDrawList()->AddRectFilled(rp, ImVec2(rp.x + w, rp.y + 20), IM_COL32(44, 93, 135, 255));
			ImGui::SetCursorScreenPos(ImVec2(rp.x + 6, rp.y + 1));
			bool enabled = s.Enabled;
			if (ImGui::Checkbox("##on", &enabled)) { s.Enabled = enabled; BuildSettings::SaveScenes(); }
			ImGui::SameLine();
			// 이름 (Assets\ 떼고 / 로)
			std::string name = s.Path;
			if (name.rfind("Assets\\", 0) == 0 || name.rfind("Assets/", 0) == 0) name = name.substr(7);
			std::replace(name.begin(), name.end(), '\\', '/');
			if (name.size() > 6 && name.substr(name.size() - 6) == ".scene") name = name.substr(0, name.size() - 6);
			const bool missing = !fs::exists(PathManager::GetI()->GetMovePathW(string_to_wstring(s.Path)));
			ImGui::PushStyleColor(ImGuiCol_Text, missing ? ImVec4(0.9f, 0.4f, 0.4f, 1) : (s.Enabled ? ImVec4(0.86f, 0.86f, 0.86f, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1)));
			ImGui::Selectable((name + (missing ? "  (Deleted)" : "")).c_str(), false, ImGuiSelectableFlags_AllowOverlap, ImVec2(w - 70.0f, 18));
			ImGui::PopStyleColor();
			if (ImGui::IsItemClicked())
				s_Selected = i;
			// 끌어서 순서 바꾸기
			if (ImGui::BeginDragDropSource())
			{
				ImGui::SetDragDropPayload("BUILD_SCENE", &i, sizeof(int));
				ImGui::TextUnformatted(name.c_str());
				ImGui::EndDragDropSource();
			}
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("BUILD_SCENE"))
				{
					moveFrom = *(const int*)pl->Data;
					moveTo = i;
				}
				ImGui::EndDragDropTarget();
			}
			if (ImGui::BeginPopupContextItem("##sceneCtx"))
			{
				if (ImGui::MenuItem("Remove Selection")) remove = i;
				ImGui::EndPopup();
			}
			if (s.Enabled)
			{
				const std::string idx = std::to_string(buildIndex++);
				ImGui::GetWindowDrawList()->AddText(ImVec2(rp.x + w - 24.0f - ImGui::CalcTextSize(idx.c_str()).x, rp.y + 2), IM_COL32(170, 170, 170, 255), idx.c_str());
			}
			ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + 20));
			ImGui::Dummy(ImVec2(w, 0));
			ImGui::PopID();
		}
		ImGui::Dummy(ImVec2(w, (std::max)(0.0f, h - 20.0f * scenes.size() - 8.0f)));
		// Project 창의 .scene 을 끌어 놓기
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET_FILE"))
			{
				const std::string dropped(static_cast<const char*>(pl->Data));
				if (fs::path(dropped).extension() == ".scene")
				{
					std::string rel = dropped;
					const std::string root = wstring_to_string(PathManager::GetI()->GetContentPathW());
					if (_strnicmp(rel.c_str(), root.c_str(), root.size()) == 0) rel = rel.substr(root.size());
					AddScene(rel);
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete) && s_Selected >= 0)
			remove = s_Selected;
		ImGui::EndChild();

		if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo)
		{
			auto item = scenes[moveFrom];
			scenes.erase(scenes.begin() + moveFrom);
			scenes.insert(scenes.begin() + moveTo, item);
			s_Selected = moveTo;
			BuildSettings::SaveScenes();
		}
		if (remove >= 0 && remove < (int)scenes.size())
		{
			scenes.erase(scenes.begin() + remove);
			s_Selected = -1;
			BuildSettings::SaveScenes();
		}

		// 오른쪽 아래 [Add Open Scenes]
		const float bw = 130.0f;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + w - bw);
		if (ImGui::Button("Add Open Scenes", ImVec2(bw, 0)))
		{
			Scene* scene = SceneManager::GetI()->GetCurrentScene();
			if (scene && !scene->GetScenePath().empty())
				AddScene(wstring_to_string(scene->GetScenePath()));
			else
				Debug::LogWarning("Save the scene first to add it to the build.");
		}
	}
}

namespace BuildSettingsWindow
{
	void Close() { s_Open = false; }
	bool IsOpen() { return s_Open; }

	void Open()
	{
		s_Open = true;
		s_FocusNext = true;
		EditorLog::Write("App", "Build Settings opened");
	}

	void BuildAndRun()
	{
		if (BuildSettings::ActivePlatform() == 1)
			BuildAndroid(true, false);
		else
			Build(true, false);
	}

	void Draw()
	{
		BuildPipeline::Update();
		BuildPipeline::DrawProgress();
		AndroidBuild::Update();
		AndroidBuild::DrawProgress();

		// (개발/검증용) NOVA_DEV_BUILD=<폴더>: 시작 후 한 번 그 폴더로 빌드 (대화상자 없이)
		static bool s_DevBuilt = false;
		char devDir[MAX_PATH] = {};
		// 시작할 때 스크립트를 다시 컴파일하는 중이면 끝날 때까지 기다린다
		if (!s_DevBuilt && ImGui::GetFrameCount() > 180 && !ScriptEngine::IsCompiling() && ::GetEnvironmentVariableA("NOVA_DEV_BUILD", devDir, sizeof(devDir)) > 0)
		{
			s_DevBuilt = true;
			StartBuild(string_to_wstring(devDir), false, false);
		}

		if (!s_Open)
			return;
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowSize(ImVec2(720, 640), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
		if (s_FocusNext)
		{
			ImGui::SetNextWindowFocus();
			s_FocusNext = false;
		}
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 6));
		if (!ImGui::Begin("Build Settings", &s_Open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
		{
			ImGui::End();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
			return;
		}

		DrawScenesInBuild();
		ImGui::Spacing();

		// Platform (왼쪽) | 설정 (오른쪽)
		const float leftW = 230.0f;
		ImGui::TextUnformatted("Platform");
		ImGui::BeginChild("##platforms", ImVec2(leftW, 200), true);
		int& platform = BuildSettings::ActivePlatform();
		if (ImGui::Selectable(ICON_FA_DESKTOP "  Windows", platform == 0)) { platform = 0; BuildSettings::SaveEditorBuild(); }
		if (ImGui::Selectable(ICON_FA_MOBILE_SCREEN "  Android", platform == 1)) { platform = 1; BuildSettings::SaveEditorBuild(); }
		ImGui::BeginDisabled();
		ImGui::Selectable(ICON_FA_LAPTOP "  macOS");
		ImGui::Selectable(ICON_FA_TERMINAL "  Linux");
		ImGui::Selectable(ICON_FA_GLOBE "  Web");
		ImGui::EndDisabled();
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginChild("##platformSettings", ImVec2(0, 200), false);
		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::TextUnformatted(platform == 1 ? ICON_FA_MOBILE_SCREEN "  Android" : ICON_FA_DESKTOP "  Windows");
		ImGui::PopFont();
		ImGui::Spacing();
		if (platform == 1)
			DrawAndroidSettings();
		else
		{
		UnityGUI::ValueLabel("Target Platform", "Windows");
		UnityGUI::ValueLabel("Architecture", "Intel 64-bit");
		if (UnityGUI::Toggle("Development Build", &BuildSettings::DevelopmentBuild()))
			BuildSettings::SaveEditorBuild();
		UnityGUI::ValueLabel("Product Name", BuildSettings::ProductName().c_str());
		const std::string last = BuildSettings::LastBuildFolder();
		UnityGUI::ValueLabel("Last Build Folder", last.empty() ? "(none)" : last.c_str());
		}
		ImGui::EndChild();
		if (!s_LastError.empty())
			UnityGUI::HelpBox(s_LastError.c_str(), true);

		// 아래: [Player Settings...]            [Build] [Build And Run] (창 아래에 붙인다)
		ImGui::SetCursorPosY((std::max)(ImGui::GetCursorPosY() + 4.0f, ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - 24.0f));
		if (ImGui::Button("Player Settings...", ImVec2(140, 24)))
			ProjectSettingsWindow::Open("Player");
		const float bw = 120.0f;
		ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - bw * 2 - 6.0f);
		ImGui::BeginDisabled(BuildPipeline::IsRunning() || AndroidBuild::IsRunning());
		if (ImGui::Button("Build", ImVec2(bw, 24)))
			platform == 1 ? BuildAndroid(false, true) : Build(false, true);
		ImGui::SameLine();
		if (ImGui::Button("Build And Run", ImVec2(bw, 24)))
			platform == 1 ? BuildAndroid(true, true) : Build(true, true);
		ImGui::EndDisabled();

		ImGui::End();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
	}
}
