#include "pch.h"
#include "ProjectHubWindow.h"
#include "EditorGUI.h"
#include "PathManager.h"
#include "SceneManager.h"
#include "EditorSettingManager.h"
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

ProjectHubWindow::ProjectHubWindow()
	: EditorWindow("Unity Hub", ICON_FA_FOLDER_OPEN)
{
	LoadProjectList();
	std::wstring contentPath = PathManager::GetI()->GetContentPathW();
	wcscpy_s(m_NewProjectLocation, contentPath.c_str());
}

ProjectHubWindow::~ProjectHubWindow()
{
	SaveProjectList();
}

void ProjectHubWindow::LoadProjectList()
{
	m_Projects.clear();

	std::wstring configPath = PathManager::GetI()->GetMovePathW(L"ProjectSetting\\projects.json");
	std::ifstream inFile(wstring_to_string(configPath));
	if (inFile.is_open())
	{
		try
		{
			json j;
			inFile >> j;
			if (j.is_array())
			{
				for (const auto& item : j)
				{
					ProjectInfo info;
					info.Name = item.value("name", "Unnamed Project");
					info.Path = string_to_wstring(item.value("path", ""));
					info.LastModified = item.value("modified", "2026-09-30");
					info.EngineVersion = item.value("version", "DX11 2026.1 (C++20)");
					m_Projects.push_back(info);
				}
			}
		}
		catch (...) {}
		inFile.close();
	}

	// Always ensure current project is in list
	std::wstring currentRoot = PathManager::GetI()->GetContentPathW();
	bool foundCurrent = false;
	for (const auto& p : m_Projects)
	{
		if (p.Path == currentRoot)
		{
			foundCurrent = true;
			break;
		}
	}

	if (!foundCurrent)
	{
		ProjectInfo current;
		current.Name = "DX11 Engine Core";
		current.Path = currentRoot;
		current.LastModified = "2026-09-30";
		current.EngineVersion = "DX11 2026.1 (C++20)";
		m_Projects.insert(m_Projects.begin(), current);
		SaveProjectList();
	}
}

void ProjectHubWindow::SaveProjectList()
{
	std::wstring configPath = PathManager::GetI()->GetMovePathW(L"ProjectSetting\\projects.json");
	std::filesystem::create_directories(std::filesystem::path(configPath).parent_path());

	json j = json::array();
	for (const auto& p : m_Projects)
	{
		json item;
		item["name"] = p.Name;
		item["path"] = wstring_to_string(p.Path);
		item["modified"] = p.LastModified;
		item["version"] = p.EngineVersion;
		j.push_back(item);
	}

	std::ofstream outFile(wstring_to_string(configPath));
	if (outFile.is_open())
	{
		outFile << j.dump(4);
		outFile.close();
	}
}

void ProjectHubWindow::OnRender()
{
	ImVec2 avail = ImGui::GetContentRegionAvail();

	// Sidebar (Unity Hub style)
	float sidebarWidth = 180.0f;
	ImGui::BeginChild("##HubSidebar", ImVec2(sidebarWidth, avail.y), true);
	
	ImGui::SetCursorPosY(16);
	ImGui::SetCursorPosX(16);
	ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "%s Unity Hub", ICON_FA_CUBE);
	ImGui::Separator();
	ImGui::Spacing();

	auto SidebarTab = [&](int index, const char* label, const char* icon) {
		bool isSelected = (m_SelectedSidebarTab == index);
		if (isSelected)
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.7f, 1.0f));
		else
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 0.0f));

		std::string text = std::string(icon) + "  " + label;
		if (ImGui::Button(text.c_str(), ImVec2(sidebarWidth - 20, 36)))
		{
			m_SelectedSidebarTab = index;
		}
		ImGui::PopStyleColor();
	};

	SidebarTab(0, "Projects", ICON_FA_FOLDER);
	SidebarTab(1, "Installs", ICON_FA_GEAR);
	SidebarTab(2, "Learn", ICON_FA_BOOK);

	ImGui::EndChild();

	ImGui::SameLine();

	// Main Content Area
	ImGui::BeginChild("##HubMainContent", ImVec2(avail.x - sidebarWidth - 10, avail.y), false);

	if (m_SelectedSidebarTab == 0)
	{
		RenderProjectsTab();
	}
	else if (m_SelectedSidebarTab == 1)
	{
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Installed Editor Versions");
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::Text("  %s DX11 Game Engine 2026.1 (C++20 DirectX 11.0)", ICON_FA_CHECK);
		ImGui::Text("  Location: %s", PathManager::GetI()->GetContentPathS().c_str());
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Build Engine: CMake 3.20+ / MSVC 19.44");
	}
	else if (m_SelectedSidebarTab == 2)
	{
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Learning & Documentation");
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::BulletText("DirectX 11 Shaders: Basic, Lighting, SSAO, ShadowMap, SkinnedMesh");
		ImGui::BulletText("Component Architecture: Transform, MeshRenderer, Camera, Light, Collider");
		ImGui::BulletText("CMake build system: run 'build.bat' or use CMake directly");
	}

	ImGui::EndChild();

	RenderNewProjectModal();
}

void ProjectHubWindow::RenderProjectsTab()
{
	// Top Header
	ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Projects");
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - 220.0f);

	if (ImGui::Button("Open", ImVec2(80, 30)))
	{
		std::wstring dir = PathManager::GetI()->GetContentPathW();
		ProjectInfo p;
		p.Name = "Imported Project";
		p.Path = dir;
		p.LastModified = "Just now";
		p.EngineVersion = "DX11 2026.1 (C++20)";
		m_Projects.push_back(p);
		SaveProjectList();
	}

	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.45f, 0.85f, 1.0f));
	if (ImGui::Button("New project", ImVec2(120, 30)))
	{
		m_ShowNewProjectModal = true;
	}
	ImGui::PopStyleColor();

	ImGui::Separator();
	ImGui::Spacing();

	if (!m_StatusMessage.empty())
	{
		ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", m_StatusMessage.c_str());
		ImGui::Spacing();
	}

	// Projects Table
	if (ImGui::BeginTable("##ProjectsTable", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable))
	{
		ImGui::TableSetupColumn("Project Name", ImGuiTableColumnFlags_WidthStretch, 0.3f);
		ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 0.4f);
		ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 100.0f);
		ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 140.0f);
		ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 180.0f);
		ImGui::TableHeadersRow();

		for (int i = 0; i < (int)m_Projects.size(); ++i)
		{
			auto& p = m_Projects[i];
			ImGui::TableNextRow(0, 36.0f);

			// Name
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::Text("%s %s", ICON_FA_FOLDER, p.Name.c_str());

			// Path
			ImGui::TableSetColumnIndex(1);
			std::string pathStr = wstring_to_string(p.Path);
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", pathStr.c_str());

			// Modified
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%s", p.LastModified.c_str());

			// Version
			ImGui::TableSetColumnIndex(3);
			ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%s", p.EngineVersion.c_str());

			// Actions
			ImGui::TableSetColumnIndex(4);
			ImGui::PushID(i);
			if (ImGui::Button("Open"))
			{
				m_StatusMessage = "Loaded project: " + p.Name;
				SceneManager::GetI()->LoadScene(L"Assets\\TestScene.scene");
			}
			ImGui::SameLine();
			if (ImGui::Button("CMake Build"))
			{
				BuildProjectWithCMake(p.Path);
			}
			ImGui::PopID();
		}

		ImGui::EndTable();
	}
}

void ProjectHubWindow::RenderNewProjectModal()
{
	if (!m_ShowNewProjectModal)
		return;

	ImGui::OpenPopup("New Project");

	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(520, 280));

	if (ImGui::BeginPopupModal("New Project", &m_ShowNewProjectModal, ImGuiWindowFlags_NoResize))
	{
		ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "%s Create a new DirectX 11 Project", ICON_FA_PLUS);
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::Text("Project Name:");
		ImGui::InputText("##ProjName", m_NewProjectName, sizeof(m_NewProjectName));

		ImGui::Spacing();
		ImGui::Text("Template:");
		static int selectedTemplate = 0;
		const char* templates[] = { "3D Core (Standard DX11)", "Empty Scene", "Custom Gameplay" };
		ImGui::Combo("##Template", &selectedTemplate, templates, IM_ARRAYSIZE(templates));

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::Button("Create Project", ImVec2(130, 32)))
		{
			CreateProject(m_NewProjectName, m_NewProjectLocation);
			m_ShowNewProjectModal = false;
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(90, 32)))
		{
			m_ShowNewProjectModal = false;
		}

		ImGui::EndPopup();
	}
}

void ProjectHubWindow::CreateProject(const std::string& name, const std::wstring& targetDir)
{
	ProjectInfo newProj;
	newProj.Name = name;
	newProj.Path = targetDir;
	newProj.LastModified = "Just now";
	newProj.EngineVersion = "DX11 2026.1 (C++20)";
	m_Projects.insert(m_Projects.begin(), newProj);
	SaveProjectList();

	m_StatusMessage = "Created and registered new project: " + name;
}

void ProjectHubWindow::BuildProjectWithCMake(const std::wstring& projectPath)
{
	std::wstring cmd = L"cmd.exe /k \"cd /d \"" + projectPath + L"\" && cmake -B build -G \"Visual Studio 17 2022\" -A x64 && cmake --build build --config Debug\"";
	
	STARTUPINFO si = { sizeof(si) };
	PROCESS_INFORMATION pi;
	
	if (CreateProcessW(NULL, const_cast<wchar_t*>(cmd.c_str()), NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, projectPath.c_str(), &si, &pi))
	{
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		m_StatusMessage = "Started CMake build in external console!";
	}
	else
	{
		m_StatusMessage = "Failed to launch CMake build!";
	}
}
