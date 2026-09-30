#pragma once
#include "EditorWindow.h"
#include <string>
#include <vector>

struct ProjectInfo
{
	std::string Name;
	std::wstring Path;
	std::string LastModified;
	std::string EngineVersion;
};

class ProjectHubWindow : public EditorWindow
{
public:
	ProjectHubWindow();
	virtual ~ProjectHubWindow();

protected:
	virtual void OnRender() override;

private:
	void LoadProjectList();
	void SaveProjectList();
	void RenderProjectsTab();
	void RenderNewProjectModal();
	void CreateProject(const std::string& name, const std::wstring& targetDir);
	void BuildProjectWithCMake(const std::wstring& projectPath);

private:
	std::vector<ProjectInfo> m_Projects;
	int m_SelectedProjectIndex = -1;

	// New Project Dialog State
	bool m_ShowNewProjectModal = false;
	char m_NewProjectName[128] = "My Project";
	wchar_t m_NewProjectLocation[260] = L"";

	// Hub navigation sidebar
	int m_SelectedSidebarTab = 0; // 0: Projects, 1: Installs, 2: Learn
	std::string m_StatusMessage = "";
};
