#pragma once
#include "App.h"
#include "HubProject.h"
#include "CliInstaller.h"
#include <string>
#include <wrl/client.h>
#include <d3d11.h>
#include <vector>

// NOVA Hub: 프로그램을 인자 없이 실행했을 때 가장 먼저 뜨는 프로젝트 관리 창.
// 프로젝트를 만들거나 선택하면 같은 exe 를 --project 인자로 실행해 에디터를 연다. (Unity Hub 방식)
//  창 테두리 없이 위쪽 앱 바를 직접 그린다 (로고·사이드바 접기 / 학습·NOVA CLI·알림·설정·계정 / 최소화·최대화·닫기).
//  끌기·두 번 눌러 최대화·스냅·가장자리 크기 조절은 WM_NCHITTEST 로 Windows 에 맡긴다.
class HubApp : public App
{
public:
	HubApp(HINSTANCE hInstance);
	~HubApp();

	bool Init() override;
	int32 Run() override;
	void UpdateScene(float dt) override {}
	LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
	enum class Tab { Projects, Installs };

	void DrawUI();
	void DrawTopBar(float S, float width);
	void DrawWindowButtons(float S, float right, float height);
	void DrawSidebar(float S, ImVec2 pos, ImVec2 size);
	void DrawProjectsPanel(float S, ImVec2 pos, ImVec2 size);
	void DrawInstallsPanel(float S, ImVec2 pos, ImVec2 size);
	void DrawNewProjectPopup(float S);
	void DrawLearnPopup(float S);
	void DrawCliPopup(float S);
	void DrawNoticePopup(float S);
	void DrawSettingsPopup(float S);
	void DrawAccountPopup(float S);

	void OpenProject(size_t index);
	void AddProjectFromDisk();
	void SetStatus(const std::string& message, bool isError = false);
	void LoadSettings();
	void SaveSettings();
	void RefreshCli(bool force = false);

private:
	Tab m_Tab = Tab::Projects;
	char m_Search[128] = {};

	bool m_OpenNewProjectPopup = false;
	char m_NewName[128] = "New NOVA Project";
	char m_NewLocation[512] = {};
	int  m_NewTemplate = 0;
	std::string m_NewError;

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_Logo;   // NOVA 로고

	std::string m_Status;
	bool   m_StatusIsError = false;
	double m_StatusUntil = 0.0;

	// ---- 앱 바 (창 끌기 영역은 MsgProc 의 WM_NCHITTEST 가 쓴다, 클라이언트 픽셀)
	float m_TitleBarH = 52.0f;
	float m_DragMinX = 0.0f, m_DragMaxX = 0.0f;
	bool m_SidebarCollapsed = false;

	// ---- 알림 (상태 메시지 기록)
	struct Notice
	{
		std::string Text;
		bool Error = false;
		long long Time = 0;
	};
	std::vector<Notice> m_Notices;
	int m_Unread = 0;

	// ---- 설정 (%LOCALAPPDATA%\NOVA\Hub\settings.json)
	std::wstring m_DefaultLocation;   // 비면 문서\NOVA Projects
	bool m_CloseOnLaunch = false;     // 에디터를 열면 Hub 닫기

	// ---- NOVA CLI 상태 / 실행 중인 에디터 (2 초마다)
	CliInstaller::Status m_Cli;
	struct RunningEditor
	{
		unsigned Pid = 0;
		std::string Project, ProjectName;
	};
	std::vector<RunningEditor> m_Running;
	double m_CliNextQuery = 0.0;

	std::string m_UserName, m_Initials, m_MachineName;
};
