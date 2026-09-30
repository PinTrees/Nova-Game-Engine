#pragma once
#include "App.h"
#include "HubProject.h"
#include <string>
#include <vector>

// NOVA Hub: 프로그램을 인자 없이 실행했을 때 가장 먼저 뜨는 프로젝트 관리 창.
// 프로젝트를 만들거나 선택하면 같은 exe 를 --project 인자로 실행해 에디터를 연다. (Unity Hub 방식)
class HubApp : public App
{
public:
	HubApp(HINSTANCE hInstance);
	~HubApp();

	bool Init() override;
	int32 Run() override;
	void UpdateScene(float dt) override {}

private:
	enum class Tab { Projects, Installs };

	void DrawUI();
	void DrawTopBar(float S, float width);
	void DrawSidebar(float S, ImVec2 pos, ImVec2 size);
	void DrawProjectsPanel(float S, ImVec2 pos, ImVec2 size);
	void DrawInstallsPanel(float S, ImVec2 pos, ImVec2 size);
	void DrawNewProjectPopup(float S);

	void OpenProject(size_t index);
	void AddProjectFromDisk();
	void SetStatus(const std::string& message, bool isError = false);

private:
	Tab m_Tab = Tab::Projects;
	char m_Search[128] = {};

	bool m_OpenNewProjectPopup = false;
	char m_NewName[128] = "New NOVA Project";
	char m_NewLocation[512] = {};
	int  m_NewTemplate = 0;
	std::string m_NewError;

	std::string m_Status;
	bool   m_StatusIsError = false;
	double m_StatusUntil = 0.0;
};
