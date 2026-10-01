#include "pch.h"
#include "HubApp.h"
#include "CliInstaller.h"
#include "EngineInfo.h"
#include "EditorGUIManager.h"
#include "PathManager.h"
#include "GraphicsSettings.h"
#include "IGraphicsBackend.h"
#include "IconsFontAwesome/IconsFontAwesome6.h"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <shobjidl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <windowsx.h>

namespace fs = std::filesystem;

namespace
{
	// ---- Hub 색상 (Unity Hub 다크 테마 기준) ----
	const ImU32 kColBg       = IM_COL32(24, 24, 24, 255);
	const ImU32 kColPanel    = IM_COL32(30, 30, 30, 255);
	const ImU32 kColSidebar  = IM_COL32(32, 32, 32, 255);
	const ImU32 kColBorder   = IM_COL32(50, 50, 50, 255);
	const ImU32 kColSelected = IM_COL32(52, 52, 52, 255);
	const ImU32 kColHover    = IM_COL32(40, 40, 40, 255);
	const ImU32 kColText     = IM_COL32(232, 232, 232, 255);
	const ImU32 kColSubText  = IM_COL32(150, 150, 150, 255);
	const ImU32 kColBlue     = IM_COL32(28, 110, 225, 255);
	const ImU32 kColBlueHov  = IM_COL32(48, 132, 242, 255);
	const ImU32 kColField    = IM_COL32(40, 40, 40, 255);
	const ImU32 kColStar     = IM_COL32(242, 201, 76, 255);
	const ImU32 kColBadge    = IM_COL32(48, 48, 48, 255);
	const ImU32 kColError    = IM_COL32(240, 96, 96, 255);
	const ImU32 kColTitleBar = IM_COL32(17, 17, 17, 255);

	// Hub 글꼴 (EditorGUIManager::Init(hubMode)): 0 본문, 1 큰 제목, 2 강조, 3 작은 글자, 4 앱 바 아이콘, 5 앱 이름
	ImFont* HubFont(int index)
	{
		ImGuiIO& io = ImGui::GetIO();
		return index < io.Fonts->Fonts.Size ? io.Fonts->Fonts[index] : ImGui::GetFont();
	}

	// 앱 바 아이콘 버튼 (둥근 호버 배경 + 알림 점)
	bool AppBarButton(ImDrawList* dl, const char* id, const char* icon, ImVec2 pos, float size, const char* tip, bool dot)
	{
		ImGui::SetCursorScreenPos(pos);
		const bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
		const bool hovered = ImGui::IsItemHovered();
		if (hovered || ImGui::IsItemActive())
			dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), ImGui::IsItemActive() ? IM_COL32(60, 60, 60, 255) : IM_COL32(46, 46, 46, 255), size * 0.2f);
		ImFont* f = HubFont(4);
		const ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, icon);
		dl->AddText(f, f->FontSize, ImVec2(pos.x + (size - ts.x) * 0.5f, pos.y + (size - ts.y) * 0.5f), hovered ? IM_COL32(245, 245, 245, 255) : IM_COL32(196, 196, 196, 255), icon);
		if (dot)
			dl->AddCircleFilled(ImVec2(pos.x + size * 0.70f, pos.y + size * 0.27f), size * 0.10f, IM_COL32(232, 72, 60, 255), 16);
		if (hovered && tip && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
			ImGui::SetTooltip("%s", tip);
		return clicked;
	}

	ImVec4 V4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

	std::string ToUtf8(const std::wstring& w)
	{
		if (w.empty()) return {};
		int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string s(n, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
		return s;
	}

	std::wstring FromUtf8(const std::string& s)
	{
		if (s.empty()) return {};
		int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring w(n, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
		return w;
	}

	std::string ToLower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return s;
	}

	// "3초 전" 형태의 상대 시간
	std::string RelativeTime(long long epoch)
	{
		if (epoch <= 0) return "—";
		long long diff = (long long)std::time(nullptr) - epoch;
		if (diff < 0) diff = 0;
		char buf[64];
		if (diff < 60)                 snprintf(buf, sizeof(buf), "%lld초 전", diff < 1 ? 1LL : diff);
		else if (diff < 3600)          snprintf(buf, sizeof(buf), "%lld분 전", diff / 60);
		else if (diff < 86400)         snprintf(buf, sizeof(buf), "%lld시간 전", diff / 3600);
		else if (diff < 86400 * 7)     snprintf(buf, sizeof(buf), "%lld일 전", diff / 86400);
		else if (diff < 86400 * 30)    snprintf(buf, sizeof(buf), "%lld주 전", diff / (86400 * 7));
		else if (diff < 86400 * 365)   snprintf(buf, sizeof(buf), "%lld개월 전", diff / (86400 * 30));
		else                           snprintf(buf, sizeof(buf), "%lld년 전", diff / (86400 * 365));
		return buf;
	}

	// 폴더 선택 대화상자
	bool PickFolder(HWND owner, const wchar_t* title, std::wstring& outPath)
	{
		IFileOpenDialog* dialog = nullptr;
		if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
			return false;

		DWORD options = 0;
		dialog->GetOptions(&options);
		dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
		dialog->SetTitle(title);

		bool ok = false;
		if (SUCCEEDED(dialog->Show(owner)))
		{
			IShellItem* item = nullptr;
			if (SUCCEEDED(dialog->GetResult(&item)))
			{
				PWSTR raw = nullptr;
				if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) && raw)
				{
					outPath = raw;
					ok = true;
				}
				::CoTaskMemFree(raw);
				item->Release();
			}
		}
		dialog->Release();
		return ok;
	}

	bool BlueButton(const char* label, ImVec2 size)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, V4(kColBlue));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V4(kColBlueHov));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, V4(kColBlue));
		ImGui::PushStyleColor(ImGuiCol_Text, V4(IM_COL32(255, 255, 255, 255)));
		bool pressed = ImGui::Button(label, size);
		ImGui::PopStyleColor(4);
		return pressed;
	}

	bool GrayButton(const char* label, ImVec2 size)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, V4(IM_COL32(44, 44, 44, 255)));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V4(IM_COL32(58, 58, 58, 255)));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, V4(IM_COL32(48, 48, 48, 255)));
		ImGui::PushStyleColor(ImGuiCol_Text, V4(kColText));
		bool pressed = ImGui::Button(label, size);
		ImGui::PopStyleColor(4);
		return pressed;
	}
}

HubApp::HubApp(HINSTANCE hInstance)
	: App(hInstance)
{
	_mainWindowCaption = ENGINE_NAME_W L" Hub";
	_logFileName = "hub_log.txt";
	_clientWidth = 1200;
	_clientHeight = 720;
	_centerWindow = true;
}

HubApp::~HubApp()
{
}

bool HubApp::Init()
{
	std::ofstream log(_logFileName, std::ios::trunc);
	log << "HubApp::Init" << std::endl;

	if (!InitPlatform())
		return false;

	// 엔진 리소스(폰트/아이콘) 경로 확보. Hub 는 프로젝트를 열지 않는다.
	PathManager::GetI()->Init();

	::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	// 테두리 없는 창: Windows 제목 표시줄 대신 앱 바를 그린다 (WM_NCCALCSIZE / WM_NCHITTEST = MsgProc). 1px 프레임으로 DWM 그림자 유지
	{
		const MARGINS margins = { 1, 1, 1, 1 };
		::DwmExtendFrameIntoClientArea(_hMainWnd, &margins);
		::SetWindowPos(_hMainWnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	EditorGUIManager::GetI()->Init(true);
	LoadSettings();
	{
		wchar_t name[256] = {};
		DWORD len = 256;
		if (::GetUserNameW(name, &len))
			m_UserName = ToUtf8(name);
		len = 256;
		wchar_t machine[256] = {};
		if (::GetComputerNameW(machine, &len))
			m_MachineName = ToUtf8(machine);
		if (m_UserName.empty())
			m_UserName = "User";
		// 이니셜: 단어 첫 글자 두 개, 한 단어면 앞 두 글자
		std::string initials;
		bool start = true;
		for (char c : m_UserName)
		{
			if (c == ' ' || c == '.' || c == '_' || c == '-') { start = true; continue; }
			if (start && initials.size() < 2 && (unsigned char)c < 128) initials += (char)toupper((unsigned char)c);
			start = false;
		}
		std::string caps;
		for (char c : m_UserName)
			if (c >= 'A' && c <= 'Z' && caps.size() < 2)
				caps += c;
		if (initials.size() < 2 && caps.size() == 2)
			initials = caps;
		if (initials.size() < 2 && m_UserName.size() >= 2 && (unsigned char)m_UserName[1] < 128)
			initials = std::string(1, (char)toupper((unsigned char)m_UserName[0])) + (char)toupper((unsigned char)m_UserName[1]);
		m_Initials = initials.empty() ? "U" : initials;
	}

	m_Logo = Utils::LoadTexture(_device, PathManager::GetI()->GetEnginePathW() + L"ProjectSetting\\logo\\nova-logo-128.png");

	HubProjectRegistry::Load();
	std::string defaultLocation = ToUtf8(m_DefaultLocation.empty() ? HubProjectRegistry::DefaultLocation() : m_DefaultLocation);
	strncpy_s(m_NewLocation, defaultLocation.c_str(), _TRUNCATE);

	log << "HubApp::Init finished" << std::endl;
	return true;
}

int32 HubApp::Run()
{
	MSG msg = { 0 };
	_timer.Reset();

	while (msg.message != WM_QUIT)
	{
		if (::PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			continue;
		}

		if (_minimized)
		{
			::Sleep(50);
			continue;
		}

		_timer.Tick();

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		DrawUI();

		ImGui::Render();

		const float clear[4] = { 24 / 255.0f, 24 / 255.0f, 24 / 255.0f, 1.0f };
		_deviceContext->OMSetRenderTargets(1, _renderTargetView.GetAddressOf(), _depthStencilView.Get());
		_deviceContext->ClearRenderTargetView(_renderTargetView.Get(), clear);
		_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		_swapChain->Present(1, 0);
	}

	EditorGUIManager::GetI()->Destroy();
	EditorGUIManager::GetI()->Dispose();
	::CoUninitialize();

	return (int)msg.wParam;
}

void HubApp::SetStatus(const std::string& message, bool isError)
{
	m_Status = message;
	m_StatusIsError = isError;
	m_StatusUntil = ImGui::GetTime() + 6.0;
	m_Notices.push_back({ message, isError, (long long)std::time(nullptr) });
	if (m_Notices.size() > 50)
		m_Notices.erase(m_Notices.begin());
	++m_Unread;
}

void HubApp::OpenProject(size_t index)
{
	auto& projects = HubProjectRegistry::Projects();
	if (index >= projects.size())
		return;

	const HubProject& p = projects[index];
	if (!HubProjectRegistry::Exists(p))
	{
		SetStatus("프로젝트를 찾을 수 없습니다: " + ToUtf8(p.Path), true);
		return;
	}
	if (HubLauncher::IsEditorRunning(p.Path))
	{
		SetStatus("이미 에디터에서 열려 있는 프로젝트입니다.", false);
		return;
	}

	std::string error;
	if (HubLauncher::LaunchEditor(p.Path, error))
	{
		std::string name = p.Name;
		HubProjectRegistry::MarkOpened(index);
		SetStatus("'" + name + "' 프로젝트를 여는 중... (처음 실행 시 셰이더/텍스처 캐시 생성으로 최대 15초 걸릴 수 있습니다)");
		if (m_CloseOnLaunch)
			::PostMessageW(_hMainWnd, WM_CLOSE, 0, 0);   // 설정: 에디터를 열면 Hub 닫기
	}
	else
	{
		SetStatus(error, true);
	}
}

void HubApp::AddProjectFromDisk()
{
	std::wstring folder;
	if (!PickFolder(_hMainWnd, L"추가할 프로젝트 폴더 선택", folder))
		return;

	std::string error;
	if (HubProjectRegistry::AddExisting(folder, error))
		SetStatus("프로젝트를 목록에 추가했습니다.");
	else
		SetStatus(error, true);
}

void HubApp::DrawUI()
{
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float S = (float)::GetDpiForWindow(_hMainWnd) / 96.0f;
	RefreshCli();

	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);

	ImGui::PushStyleColor(ImGuiCol_WindowBg, V4(kColBg));
	ImGui::PushStyleColor(ImGuiCol_PopupBg, V4(IM_COL32(36, 36, 36, 255)));
	ImGui::PushStyleColor(ImGuiCol_Border, V4(IM_COL32(60, 60, 60, 255)));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f * S);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

	if (ImGui::Begin("##HubRoot", nullptr, flags))
	{
		const float W = viewport->Size.x;
		const float H = viewport->Size.y;
		m_TitleBarH = 52.0f * S;
		const float margin = 12.0f * S;
		const float sideW = (m_SidebarCollapsed ? 64.0f : 220.0f) * S;
		const float top = m_TitleBarH + margin;

		DrawTopBar(S, W);

		DrawSidebar(S, ImVec2(margin, top), ImVec2(sideW, H - top - margin));

		ImVec2 mainPos(margin * 2 + sideW, top);
		ImVec2 mainSize(W - mainPos.x - margin, H - top - margin);
		if (m_Tab == Tab::Projects)
			DrawProjectsPanel(S, mainPos, mainSize);
		else
			DrawInstallsPanel(S, mainPos, mainSize);
	}
	ImGui::End();

	ImGui::PopStyleVar(5);
	ImGui::PopStyleColor(3);

	DrawNewProjectPopup(S);
}

// ---- 앱 바: [로고 NOVA Hub ▣]  (끌기 영역)  [학습 CLI 알림 설정 (계정)] [— ▢ ✕]
void HubApp::DrawTopBar(float S, float width)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 o = ImGui::GetWindowPos();
	const float H = m_TitleBarH;
	dl->AddRectFilled(o, ImVec2(o.x + width, o.y + H), kColTitleBar);
	dl->AddLine(ImVec2(o.x, o.y + H - 1.0f), ImVec2(o.x + width, o.y + H - 1.0f), IM_COL32(40, 40, 40, 255));

	// 왼쪽: 로고 + 이름 + 사이드바 접기
	float x = o.x + 16.0f * S;
	const float logo = 26.0f * S;
	if (m_Logo)
		dl->AddImage((ImTextureID)m_Logo.Get(), ImVec2(x, o.y + (H - logo) * 0.5f), ImVec2(x + logo, o.y + (H + logo) * 0.5f));
	x += logo + 10.0f * S;
	ImFont* title = HubFont(5);
	const ImVec2 ts = title->CalcTextSizeA(title->FontSize, FLT_MAX, 0.0f, "NOVA Hub");
	dl->AddText(title, title->FontSize, ImVec2(x, o.y + (H - ts.y) * 0.5f), kColText, "NOVA Hub");
	x += ts.x + 16.0f * S;
	const float ib = 36.0f * S;
	if (AppBarButton(dl, "##sidebar", ICON_FA_TABLE_COLUMNS, ImVec2(x, o.y + (H - ib) * 0.5f), ib, m_SidebarCollapsed ? "사이드바 펼치기" : "사이드바 접기", false))
	{
		m_SidebarCollapsed = !m_SidebarCollapsed;
		SaveSettings();
	}
	m_DragMinX = x + ib + 6.0f * S - o.x;

	// 오른쪽: 창 버튼
	const float winW = 46.0f * S * 3;
	DrawWindowButtons(S, o.x + width, H);
	float rx = o.x + width - winW - 14.0f * S;

	// 계정 (이니셜 원)
	const float av = 30.0f * S;
	rx -= av;
	{
		const ImVec2 c(rx + av * 0.5f, o.y + H * 0.5f);
		ImGui::SetCursorScreenPos(ImVec2(rx, c.y - av * 0.5f));
		const bool clicked = ImGui::InvisibleButton("##account", ImVec2(av, av));
		const bool hovered = ImGui::IsItemHovered();
		dl->AddCircleFilled(c, av * 0.5f, hovered ? IM_COL32(226, 104, 20, 255) : IM_COL32(204, 85, 0, 255), 32);
		ImFont* f = HubFont(3);
		const ImVec2 is = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, m_Initials.c_str());
		dl->AddText(f, f->FontSize, ImVec2(c.x - is.x * 0.5f, c.y - is.y * 0.5f), IM_COL32(255, 255, 255, 255), m_Initials.c_str());
		if (hovered)
			ImGui::SetTooltip("%s", m_UserName.c_str());
		if (clicked)
			ImGui::OpenPopup("##accountPopup");
		if (ImGui::IsPopupOpen("##accountPopup"))
			ImGui::SetNextWindowPos(ImVec2(rx + av, o.y + H + 4.0f * S), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
		DrawAccountPopup(S);
	}
	rx -= 12.0f * S;

	// 아이콘 버튼 (오른쪽부터): 설정, 알림, NOVA CLI, 학습
	struct Item { const char* id; const char* icon; const char* tip; const char* popup; };
	const Item items[] = {
		{ "##settings", ICON_FA_GEAR, "설정", "##settingsPopup" },
		{ "##notice", ICON_FA_BELL, "알림", "##noticePopup" },
		{ "##cli", ICON_FA_TERMINAL, "NOVA CLI · 실행 중인 에디터", "##cliPopup" },
		{ "##learn", ICON_FA_GRADUATION_CAP, "학습 · 문서", "##learnPopup" },
	};
	for (const Item& it : items)
	{
		rx -= ib;
		const ImVec2 p(rx, o.y + (H - ib) * 0.5f);
		const bool dot = std::string(it.id) == "##notice" && m_Unread > 0;
		if (AppBarButton(dl, it.id, it.icon, p, ib, it.tip, dot))
		{
			ImGui::OpenPopup(it.popup);
			if (dot)
				m_Unread = 0;
			if (std::string(it.id) == "##cli")
				RefreshCli(true);
		}
		if (ImGui::IsPopupOpen(it.popup))
			ImGui::SetNextWindowPos(ImVec2(rx + ib, o.y + H + 4.0f * S), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
		if (std::string(it.id) == "##settings") DrawSettingsPopup(S);
		else if (std::string(it.id) == "##notice") DrawNoticePopup(S);
		else if (std::string(it.id) == "##cli") DrawCliPopup(S);
		else DrawLearnPopup(S);
		rx -= 4.0f * S;
	}
	m_DragMaxX = rx - 6.0f * S - o.x;
}

// 최소화 / 최대화(복원) / 닫기 — Windows 11 처럼 선으로 그린다
void HubApp::DrawWindowButtons(float S, float right, float height)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float bw = 46.0f * S;
	const float top = ImGui::GetWindowPos().y;
	const bool zoomed = ::IsZoomed(_hMainWnd) != 0;
	const ImU32 line = IM_COL32(220, 220, 220, 255);
	const float t = (std::max)(1.0f, S);
	for (int i = 0; i < 3; ++i)
	{
		const ImVec2 a(right - bw * (3 - i), top), b(a.x + bw, top + height - 1.0f);
		ImGui::SetCursorScreenPos(a);
		ImGui::PushID(i);
		const bool clicked = ImGui::InvisibleButton("##win", ImVec2(bw, height - 1.0f));
		const bool hovered = ImGui::IsItemHovered();
		const bool held = ImGui::IsItemActive();
		ImGui::PopID();
		if (hovered)
			dl->AddRectFilled(a, b, i == 2 ? (held ? IM_COL32(200, 30, 45, 255) : IM_COL32(232, 17, 35, 255)) : (held ? IM_COL32(60, 60, 60, 255) : IM_COL32(48, 48, 48, 255)));
		const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
		const float h = 5.0f * S;
		if (i == 0)
			dl->AddLine(ImVec2(c.x - h, c.y), ImVec2(c.x + h, c.y), line, t);
		else if (i == 1)
		{
			if (zoomed)
			{
				dl->AddRect(ImVec2(c.x - h, c.y - h + 2.0f * S), ImVec2(c.x + h - 2.0f * S, c.y + h), line, 1.5f * S, 0, t);
				dl->AddLine(ImVec2(c.x - h + 2.0f * S, c.y - h), ImVec2(c.x + h, c.y - h), line, t);
				dl->AddLine(ImVec2(c.x + h, c.y - h), ImVec2(c.x + h, c.y + h - 2.0f * S), line, t);
			}
			else
				dl->AddRect(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), line, 1.5f * S, 0, t);
		}
		else
		{
			const ImU32 col = hovered ? IM_COL32(255, 255, 255, 255) : line;
			dl->AddLine(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), col, t);
			dl->AddLine(ImVec2(c.x - h, c.y + h), ImVec2(c.x + h, c.y - h), col, t);
		}
		if (clicked)
		{
			if (i == 0) ::ShowWindow(_hMainWnd, SW_MINIMIZE);
			else if (i == 1) ::ShowWindow(_hMainWnd, zoomed ? SW_RESTORE : SW_MAXIMIZE);
			else ::PostMessageW(_hMainWnd, WM_CLOSE, 0, 0);
		}
	}
}

void HubApp::DrawSidebar(float S, ImVec2 pos, ImVec2 size)
{
	ImGui::SetCursorPos(pos);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, V4(kColSidebar));
	ImGui::PushStyleColor(ImGuiCol_Border, V4(kColBorder));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * S);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2((m_SidebarCollapsed ? 8.0f : 10.0f) * S, 12.0f * S));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 6.0f * S));

	if (ImGui::BeginChild("##sidebar", size, ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar))
	{
		struct Item { const char* icon; const char* label; Tab tab; };
		const Item items[] =
		{
			{ ICON_FA_FOLDER, "프로젝트", Tab::Projects },
			{ ICON_FA_DOWNLOAD, "설치", Tab::Installs },
		};
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float w = ImGui::GetContentRegionAvail().x;
		const float h = 44.0f * S;
		for (const Item& item : items)
		{
			const bool selected = m_Tab == item.tab;
			const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + w, a.y + h);
			ImGui::PushID(item.label);
			if (ImGui::InvisibleButton("##tab", ImVec2(w, h)))
				m_Tab = item.tab;
			const bool hovered = ImGui::IsItemHovered();
			if (hovered && m_SidebarCollapsed)
				ImGui::SetTooltip("%s", item.label);
			ImGui::PopID();
			if (selected || hovered)
				dl->AddRectFilled(a, b, selected ? kColSelected : kColHover, 7.0f * S);
			if (selected)
				dl->AddRectFilled(ImVec2(a.x, a.y + 10.0f * S), ImVec2(a.x + 3.0f * S, b.y - 10.0f * S), kColBlue, 2.0f * S);
			const float fs = ImGui::GetFontSize();
			const ImU32 col = selected ? kColText : IM_COL32(200, 200, 200, 255);
			if (m_SidebarCollapsed)
			{
				const ImVec2 is = ImGui::CalcTextSize(item.icon);
				dl->AddText(ImVec2(a.x + (w - is.x) * 0.5f, a.y + (h - fs) * 0.5f), col, item.icon);
			}
			else
			{
				dl->AddText(ImVec2(a.x + 16.0f * S, a.y + (h - fs) * 0.5f), col, item.icon);
				dl->AddText(selected ? HubFont(2) : ImGui::GetFont(), fs, ImVec2(a.x + 48.0f * S, a.y + (h - fs) * 0.5f), col, item.label);
			}
		}

		// 아래: 엔진 버전
		if (!m_SidebarCollapsed)
		{
			ImFont* smallFont = HubFont(3);
			const std::string ver = std::string("NOVA ") + ENGINE_VERSION_A;
			const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
			dl->AddText(smallFont, smallFont->FontSize, ImVec2(wp.x + 18.0f * S, wp.y + ws.y - smallFont->FontSize - 14.0f * S), kColSubText, ver.c_str());
		}
	}
	ImGui::EndChild();

	ImGui::PopStyleVar(4);
	ImGui::PopStyleColor(2);
}

// ---------------------------------------------------------------- 앱 바 팝업
namespace
{
	void PopupHeader(const char* text)
	{
		ImGui::PushFont(HubFont(2));
		ImGui::TextUnformatted(text);
		ImGui::PopFont();
		ImGui::Separator();
	}

	void OpenUrl(const wchar_t* url) { ::ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL); }
}

void HubApp::DrawLearnPopup(float S)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * S, 12.0f * S));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * S, 8.0f * S));
	if (ImGui::BeginPopup("##learnPopup"))
	{
		PopupHeader("학습");
		if (ImGui::MenuItem(ICON_FA_BOOK "   NOVA 문서 (README)"))
			OpenUrl(L"https://github.com/PinTrees/Nova-Game-Engine#readme");
		if (ImGui::MenuItem(ICON_FA_CODE "   C# 스크립트"))
			OpenUrl(L"https://github.com/PinTrees/Nova-Game-Engine#c-%EC%8A%A4%ED%81%AC%EB%A6%BD%ED%8A%B8");
		if (ImGui::MenuItem(ICON_FA_TERMINAL "   NOVA CLI 가이드"))
		{
			const fs::path local = fs::path(PathManager::GetI()->GetEnginePathW()) / L"docs" / L"NOVA_CLI.md";
			std::error_code ec;
			if (fs::exists(local, ec))
				::ShellExecuteW(nullptr, L"open", local.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			else
				OpenUrl(L"https://github.com/PinTrees/Nova-Game-Engine/blob/main/docs/NOVA_CLI.md");
		}
		if (ImGui::MenuItem(ICON_FA_KEYBOARD "   단축키"))
			OpenUrl(L"https://github.com/PinTrees/Nova-Game-Engine#%EB%8B%A8%EC%B6%95%ED%82%A4-unity-%EC%99%80-%EA%B0%99%EC%9D%8C");
		ImGui::Separator();
		if (ImGui::MenuItem(ICON_FA_CODE_BRANCH "   GitHub 저장소"))
			OpenUrl(L"https://github.com/PinTrees/Nova-Game-Engine");
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(2);
}

void HubApp::DrawCliPopup(float S)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * S, 14.0f * S));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * S, 8.0f * S));
	ImGui::SetNextWindowSizeConstraints(ImVec2(380.0f * S, 0), ImVec2(560.0f * S, 600.0f * S));
	if (ImGui::BeginPopup("##cliPopup"))
	{
		PopupHeader("NOVA CLI");
		const char* state = !m_Cli.Installed ? "설치되지 않음" : (!m_Cli.OnPath ? "설치됨 (PATH 미등록)" : (m_Cli.UpToDate ? "설치됨 · 최신" : "설치됨 · 업데이트 있음"));
		const ImU32 col = m_Cli.Installed && m_Cli.OnPath && m_Cli.UpToDate ? IM_COL32(120, 200, 120, 255) : IM_COL32(230, 190, 90, 255);
		ImGui::PushStyleColor(ImGuiCol_Text, V4(col));
		ImGui::Text(ICON_FA_CIRCLE "  %s", state);
		ImGui::PopStyleColor();
		ImGui::Spacing();
		ImGui::PushFont(HubFont(2));
		ImGui::Text("실행 중인 에디터 (%d)", (int)m_Running.size());
		ImGui::PopFont();
		if (m_Running.empty())
			ImGui::TextColored(V4(kColSubText), "없음 — 프로젝트를 열면 nova 로 다룰 수 있습니다");
		for (const RunningEditor& r : m_Running)
		{
			ImGui::Text(ICON_FA_CUBE "  %s", r.ProjectName.c_str());
			ImGui::SameLine();
			ImGui::TextColored(V4(kColSubText), "pid %u", r.Pid);
		}
		ImGui::Spacing();
		if (GrayButton(m_Cli.Installed ? "설치 탭 열기" : "설치하러 가기", ImVec2(150.0f * S, 32.0f * S)))
		{
			m_Tab = Tab::Installs;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (GrayButton("AI 안내 복사", ImVec2(150.0f * S, 32.0f * S)))
		{
			ImGui::SetClipboardText("This project uses the NOVA game engine. Control the running editor with the `nova` command line tool "
				"(run `nova ai-guide` first, then `nova info` and `nova hierarchy --components`).");
			SetStatus("AI 에이전트에게 붙여 넣을 안내를 복사했습니다.");
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(2);
}

void HubApp::DrawNoticePopup(float S)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * S, 14.0f * S));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * S, 10.0f * S));
	ImGui::SetNextWindowSizeConstraints(ImVec2(400.0f * S, 0), ImVec2(400.0f * S, 520.0f * S));
	if (ImGui::BeginPopup("##noticePopup"))
	{
		PopupHeader("알림");
		if (m_Notices.empty())
			ImGui::TextColored(V4(kColSubText), "새 알림이 없습니다.");
		for (auto it = m_Notices.rbegin(); it != m_Notices.rend(); ++it)
		{
			char when[32] = "";
			std::tm tm = {};
			const time_t t = (time_t)it->Time;
			if (localtime_s(&tm, &t) == 0)
				strftime(when, sizeof(when), "%H:%M", &tm);
			ImGui::PushStyleColor(ImGuiCol_Text, V4(it->Error ? kColError : kColText));
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 330.0f * S);
			ImGui::TextWrapped("%s  %s", it->Error ? ICON_FA_TRIANGLE_EXCLAMATION : ICON_FA_CIRCLE_INFO, it->Text.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
			ImGui::PushFont(HubFont(3));
			ImGui::TextColored(V4(kColSubText), "%s", when);
			ImGui::PopFont();
		}
		if (!m_Notices.empty())
		{
			ImGui::Separator();
			if (GrayButton("모두 지우기", ImVec2(120.0f * S, 30.0f * S)))
				m_Notices.clear();
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(2);
}

void HubApp::DrawSettingsPopup(float S)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * S, 14.0f * S));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * S, 10.0f * S));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * S, 5.0f * S));
	ImGui::SetNextWindowSizeConstraints(ImVec2(440.0f * S, 0), ImVec2(560.0f * S, 600.0f * S));
	if (ImGui::BeginPopup("##settingsPopup"))
	{
		PopupHeader("설정");
		ImGui::TextUnformatted("새 프로젝트 기본 위치");
		const std::wstring loc = m_DefaultLocation.empty() ? HubProjectRegistry::DefaultLocation() : m_DefaultLocation;
		ImGui::PushFont(HubFont(3));
		ImGui::TextColored(V4(kColSubText), "%s", ToUtf8(loc).c_str());
		ImGui::PopFont();
		if (GrayButton(ICON_FA_FOLDER_OPEN "  변경...", ImVec2(120.0f * S, 30.0f * S)))
		{
			std::wstring folder;
			if (PickFolder(_hMainWnd, L"새 프로젝트 기본 위치", folder))
			{
				m_DefaultLocation = folder;
				strncpy_s(m_NewLocation, ToUtf8(folder).c_str(), _TRUNCATE);
				SaveSettings();
			}
		}
		if (!m_DefaultLocation.empty())
		{
			ImGui::SameLine();
			if (GrayButton("기본값으로", ImVec2(110.0f * S, 30.0f * S)))
			{
				m_DefaultLocation.clear();
				strncpy_s(m_NewLocation, ToUtf8(HubProjectRegistry::DefaultLocation()).c_str(), _TRUNCATE);
				SaveSettings();
			}
		}
		ImGui::Separator();
		ImGui::PushStyleColor(ImGuiCol_FrameBg, V4(IM_COL32(52, 52, 52, 255)));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, V4(IM_COL32(64, 64, 64, 255)));
		ImGui::PushStyleColor(ImGuiCol_CheckMark, V4(kColBlueHov));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * S);
		if (ImGui::Checkbox("에디터를 열면 Hub 닫기", &m_CloseOnLaunch))
			SaveSettings();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(3);
		ImGui::Separator();
		if (GrayButton(ICON_FA_FOLDER "  엔진 폴더 열기", ImVec2(170.0f * S, 30.0f * S)))
			::ShellExecuteW(nullptr, L"open", PathManager::GetI()->GetEnginePathW().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		ImGui::SameLine();
		if (GrayButton(ICON_FA_FILE_LINES "  로그 폴더 열기", ImVec2(170.0f * S, 30.0f * S)))
		{
			const fs::path logs = fs::path(PathManager::GetI()->GetEnginePathW()) / L"Binaries" / L"Logs";
			std::error_code ec;
			fs::create_directories(logs, ec);
			::ShellExecuteW(nullptr, L"open", logs.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(3);
}

void HubApp::DrawAccountPopup(float S)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * S, 16.0f * S));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * S, 6.0f * S));
	ImGui::SetNextWindowSizeConstraints(ImVec2(300.0f * S, 0), ImVec2(420.0f * S, 400.0f * S));
	if (ImGui::BeginPopup("##accountPopup"))
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float r = 24.0f * S;
		dl->AddCircleFilled(ImVec2(p.x + r, p.y + r), r, IM_COL32(204, 85, 0, 255), 40);
		ImFont* big = HubFont(2);
		const ImVec2 is = big->CalcTextSizeA(big->FontSize, FLT_MAX, 0.0f, m_Initials.c_str());
		dl->AddText(big, big->FontSize, ImVec2(p.x + r - is.x * 0.5f, p.y + r - is.y * 0.5f), IM_COL32(255, 255, 255, 255), m_Initials.c_str());
		ImGui::SetCursorScreenPos(ImVec2(p.x + r * 2 + 14.0f * S, p.y + 2.0f * S));
		ImGui::BeginGroup();
		ImGui::PushFont(HubFont(2));
		ImGui::TextUnformatted(m_UserName.c_str());
		ImGui::PopFont();
		ImGui::PushFont(HubFont(3));
		ImGui::TextColored(V4(kColSubText), "로컬 사용자 · %s", m_MachineName.c_str());
		ImGui::PopFont();
		ImGui::EndGroup();
		ImGui::Dummy(ImVec2(1, 10.0f * S));
		ImGui::Separator();
		ImGui::PushFont(HubFont(3));
		ImGui::TextColored(V4(kColSubText), "NOVA Hub %s  ·  계정 로그인은 아직 없습니다", ENGINE_VERSION_A);
		ImGui::PopFont();
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(2);
}

// ---------------------------------------------------------------- 창 (테두리 없음)
LRESULT HubApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_NCCALCSIZE:
		// 클라이언트 = 창 전체 (Windows 제목 표시줄 없음). 최대화되면 화면 밖으로 나가는 테두리 두께만큼 줄인다
		if (wParam == TRUE)
		{
			if (::IsZoomed(hwnd))
			{
				auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
				const UINT dpi = ::GetDpiForWindow(hwnd);
				const int pad = ::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
				const int fx = ::GetSystemMetricsForDpi(SM_CXFRAME, dpi) + pad;
				const int fy = ::GetSystemMetricsForDpi(SM_CYFRAME, dpi) + pad;
				p->rgrc[0].left += fx;
				p->rgrc[0].right -= fx;
				p->rgrc[0].top += fy;
				p->rgrc[0].bottom -= fy;
			}
			return 0;
		}
		break;
	case WM_NCHITTEST:
	{
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		::ScreenToClient(hwnd, &pt);
		RECT rc;
		::GetClientRect(hwnd, &rc);
		const float S = (float)::GetDpiForWindow(hwnd) / 96.0f;
		if (!::IsZoomed(hwnd))
		{
			const int b = (int)(6.0f * S);
			const bool l = pt.x < b, r = pt.x >= rc.right - b, t = pt.y < b, bt = pt.y >= rc.bottom - b;
			if (t && l) return HTTOPLEFT;
			if (t && r) return HTTOPRIGHT;
			if (bt && l) return HTBOTTOMLEFT;
			if (bt && r) return HTBOTTOMRIGHT;
			if (l) return HTLEFT;
			if (r) return HTRIGHT;
			if (t) return HTTOP;
			if (bt) return HTBOTTOM;
		}
		// 앱 바의 빈 곳 = 제목 표시줄 (끌기, 두 번 눌러 최대화, 스냅). 팝업이 떠 있으면 ImGui 가 받는다
		if (pt.y >= 0 && pt.y < (LONG)m_TitleBarH && pt.x >= (LONG)m_DragMinX && pt.x < (LONG)m_DragMaxX && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
			return HTCAPTION;
		return HTCLIENT;
	}
	case WM_GETMINMAXINFO:
	{
		auto* mm = reinterpret_cast<MINMAXINFO*>(lParam);
		const float S = _hMainWnd ? (float)::GetDpiForWindow(_hMainWnd) / 96.0f : 1.0f;
		mm->ptMinTrackSize.x = (LONG)(940.0f * S);
		mm->ptMinTrackSize.y = (LONG)(580.0f * S);
		return 0;
	}
	default:
		break;
	}
	return App::MsgProc(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------- 설정 / CLI 상태
namespace
{
	fs::path HubSettingsFile()
	{
		wchar_t buf[MAX_PATH] = {};
		::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
		return fs::path(buf) / L"NOVA" / L"Hub" / L"settings.json";
	}
}

void HubApp::LoadSettings()
{
	std::ifstream is(HubSettingsFile());
	const nlohmann::json j = nlohmann::json::parse(is, nullptr, false);
	if (!j.is_object())
		return;
	m_DefaultLocation = FromUtf8(j.value("defaultLocation", std::string()));
	m_CloseOnLaunch = j.value("closeOnLaunch", false);
	m_SidebarCollapsed = j.value("sidebarCollapsed", false);
}

void HubApp::SaveSettings()
{
	const fs::path file = HubSettingsFile();
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	const nlohmann::json j = { { "defaultLocation", ToUtf8(m_DefaultLocation) }, { "closeOnLaunch", m_CloseOnLaunch }, { "sidebarCollapsed", m_SidebarCollapsed } };
	std::ofstream(file, std::ios::trunc) << j.dump(2);
}

void HubApp::RefreshCli(bool force)
{
	if (!force && ImGui::GetTime() < m_CliNextQuery)
		return;
	m_CliNextQuery = ImGui::GetTime() + 2.0;
	m_Cli = CliInstaller::Query();
	m_Running.clear();
	wchar_t buf[MAX_PATH] = {};
	::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
	std::error_code ec;
	for (const auto& e : fs::directory_iterator(fs::path(buf) / L"NOVA" / L"Instances", ec))
	{
		if (e.path().extension() != L".json")
			continue;
		std::ifstream is(e.path());
		const nlohmann::json j = nlohmann::json::parse(is, nullptr, false);
		if (!j.is_object())
			continue;
		RunningEditor r;
		r.Pid = j.value("pid", 0u);
		HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, r.Pid);
		DWORD code = 0;
		const bool alive = h && ::GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
		if (h)
			::CloseHandle(h);
		if (!alive)
			continue;
		r.Project = j.value("project", std::string());
		r.ProjectName = j.value("projectName", std::string());
		m_Running.push_back(r);
	}
}

void HubApp::DrawProjectsPanel(float S, ImVec2 pos, ImVec2 size)

{
	ImGui::SetCursorPos(pos);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, V4(kColPanel));
	ImGui::PushStyleColor(ImGuiCol_Border, V4(kColBorder));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * S);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * S, 18.0f * S));

	if (ImGui::BeginChild("##projects", size, ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
	{
		ImFont* bold = ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : ImGui::GetFont();
		const float contentW = ImGui::GetContentRegionAvail().x;
		const float rowStartY = ImGui::GetCursorPosY();

		// ---- 제목 ----
		ImGui::PushFont(bold);
		ImGui::SetCursorPosY(rowStartY + 2.0f * S);
		ImGui::TextUnformatted("프로젝트");
		ImGui::PopFont();

		// ---- 우측 컨트롤: 검색 / 추가 / 새 프로젝트 ----
		const float ctrlH = 38.0f * S;
		const float searchW = 280.0f * S;
		const float addW = 96.0f * S;
		const float newW = 150.0f * S;
		const float gap = 10.0f * S;
		float x = ImGui::GetCursorPosX() + contentW - (searchW + addW + newW + gap * 2);
		float y = rowStartY;

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f * S);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f * S, (ctrlH - ImGui::GetFontSize()) * 0.5f));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, V4(kColField));
		ImGui::PushStyleColor(ImGuiCol_Border, V4(IM_COL32(64, 64, 64, 255)));

		ImGui::SetCursorPos(ImVec2(x, y));
		ImGui::SetNextItemWidth(searchW);
		ImGui::InputTextWithHint("##search", ICON_FA_MAGNIFYING_GLASS "  검색", m_Search, sizeof(m_Search));

		ImGui::SetCursorPos(ImVec2(x + searchW + gap, y));
		if (GrayButton("추가  " ICON_FA_CHEVRON_DOWN, ImVec2(addW, ctrlH)))
			ImGui::OpenPopup("##addmenu");
		if (ImGui::BeginPopup("##addmenu"))
		{
			if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "   디스크에서 프로젝트 추가..."))
				AddProjectFromDisk();
			ImGui::EndPopup();
		}

		ImGui::SetCursorPos(ImVec2(x + searchW + addW + gap * 2, y));
		if (BlueButton(ICON_FA_PLUS "  새 프로젝트", ImVec2(newW, ctrlH)))
		{
			m_OpenNewProjectPopup = true;
			m_NewError.clear();
		}
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(3);

		// ---- 목록 ----
		ImGui::SetCursorPosY(rowStartY + ctrlH + 22.0f * S);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImFont* font = ImGui::GetFont();
		const float fs = ImGui::GetFontSize();

		const float listX = ImGui::GetCursorScreenPos().x - 24.0f * S;    // 패널 좌측 끝
		const float listW = contentW + 48.0f * S;
		const bool wide = listW > 860.0f * S;

		// 헤더 행
		ImVec2 hp = ImGui::GetCursorScreenPos();
		const float headerH = 40.0f * S;
		dl->AddRectFilled(ImVec2(listX, hp.y), ImVec2(listX + listW, hp.y + headerH), IM_COL32(37, 37, 37, 255));
		dl->AddLine(ImVec2(listX, hp.y), ImVec2(listX + listW, hp.y), kColBorder);
		dl->AddLine(ImVec2(listX, hp.y + headerH), ImVec2(listX + listW, hp.y + headerH), kColBorder);

		const float colStar = listX + 22.0f * S;
		const float colName = listX + 96.0f * S;
		const float colMenu = listX + listW - 52.0f * S;
		const float colMod = colMenu - 150.0f * S;
		const float colVer = colMod - 170.0f * S;
		const float colPlat = colVer - 200.0f * S;

		const float hy = hp.y + (headerH - fs) * 0.5f;
		dl->AddText(ImVec2(colName, hy), kColText, "이름");
		if (wide)
		{
			dl->AddText(ImVec2(colPlat, hy), kColText, "플랫폼");
			dl->AddText(ImVec2(colVer, hy), kColText, "에디터 버전");
		}
		dl->AddText(ImVec2(colMod, hy), kColText, "수정됨  " ICON_FA_ARROW_DOWN);
		ImGui::Dummy(ImVec2(1, headerH));

		// 스크롤 영역
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::SetCursorPosX(0);
		ImVec2 listAvail = ImGui::GetContentRegionAvail();
		listAvail.x = listW;
		if (ImGui::BeginChild("##list", ImVec2(listW, listAvail.y + 18.0f * S), ImGuiChildFlags_None))
		{
			auto& projects = HubProjectRegistry::Projects();

			// 최근 수정 순 정렬 (레지스트리 인덱스 유지)
			std::vector<size_t> order(projects.size());
			for (size_t i = 0; i < order.size(); ++i) order[i] = i;
			std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return projects[a].LastOpened > projects[b].LastOpened; });

			const std::string query = ToLower(m_Search);
			const float rowH = 66.0f * S;
			size_t shown = 0;
			int pendingRemove = -1;

			for (size_t idx : order)
			{
				HubProject& p = projects[idx];
				std::string pathUtf8 = ToUtf8(p.Path);
				if (!query.empty() && ToLower(p.Name).find(query) == std::string::npos && ToLower(pathUtf8).find(query) == std::string::npos)
					continue;
				++shown;

				ImGui::PushID((int)idx);
				const bool exists = HubProjectRegistry::Exists(p);
				ImVec2 rp = ImGui::GetCursorScreenPos();
				rp.x = listX;
				ImVec2 rmax(rp.x + listW, rp.y + rowH);

				// 행 전체 클릭 영역 (별/메뉴 버튼이 위에 겹칠 수 있음)
				ImGui::SetCursorScreenPos(rp);
				ImGui::SetNextItemAllowOverlap();
				bool rowClicked = ImGui::InvisibleButton("##row", ImVec2(listW, rowH));
				bool rowHovered = ImGui::IsItemHovered();
				if (rowHovered)
					dl->AddRectFilled(rp, rmax, kColHover);
				dl->AddLine(ImVec2(rp.x, rmax.y), ImVec2(rmax.x, rmax.y), IM_COL32(40, 40, 40, 255));

				const float cy = rp.y + rowH * 0.5f;

				// 즐겨찾기 별
				ImGui::SetCursorScreenPos(ImVec2(colStar - 12.0f * S, cy - 14.0f * S));
				ImGui::SetNextItemAllowOverlap();
				if (ImGui::InvisibleButton("##star", ImVec2(28.0f * S, 28.0f * S)))
				{
					p.Favorite = !p.Favorite;
					HubProjectRegistry::Save();
				}
				dl->AddText(ImVec2(colStar - 7.0f * S, cy - fs * 0.5f), p.Favorite ? kColStar : IM_COL32(110, 110, 110, 255), ICON_FA_STAR);

				// 아이콘 + 이름 + 경로
				dl->AddText(ImVec2(colName - 34.0f * S, cy - fs * 0.5f), exists ? IM_COL32(200, 200, 200, 255) : IM_COL32(110, 110, 110, 255), ICON_FA_CUBE);
				const ImU32 nameCol = exists ? kColText : IM_COL32(170, 170, 170, 255);
				dl->AddText(HubFont(2), HubFont(2)->FontSize, ImVec2(colName, rp.y + 11.0f * S), nameCol, p.Name.c_str());
				dl->AddText(HubFont(3), HubFont(3)->FontSize, ImVec2(colName, rp.y + 15.0f * S + fs), kColSubText, pathUtf8.c_str());

				if (!exists)
				{
					ImVec2 ns = ImGui::CalcTextSize(p.Name.c_str());
					const char* badge = "프로젝트를 찾을 수 없음  " ICON_FA_CIRCLE_INFO;
					ImVec2 bs = ImGui::CalcTextSize(badge);
					ImVec2 b0(colName + ns.x + 12.0f * S, rp.y + 8.0f * S);
					ImVec2 b1(b0.x + bs.x + 16.0f * S, b0.y + fs + 6.0f * S);
					dl->AddRectFilled(b0, b1, kColBadge, 6.0f * S);
					dl->AddRect(b0, b1, IM_COL32(70, 70, 70, 255), 6.0f * S);
					dl->AddText(font, fs * 0.9f, ImVec2(b0.x + 8.0f * S, b0.y + 3.0f * S), kColText, badge);
				}

				if (wide)
				{
					dl->AddText(ImVec2(colPlat, cy - fs * 0.5f), kColText, "Windows 64-bit");
					dl->AddText(ImVec2(colVer, cy - fs * 0.5f), kColText, p.EngineVersion.c_str());
				}
				dl->AddText(ImVec2(colMod, cy - fs * 0.5f), kColText, RelativeTime(p.LastOpened).c_str());

				// ⋯ 메뉴
				ImGui::SetCursorScreenPos(ImVec2(colMenu - 4.0f * S, cy - 16.0f * S));
				ImGui::SetNextItemAllowOverlap();
				bool menuClicked = ImGui::InvisibleButton("##menu", ImVec2(34.0f * S, 32.0f * S));
				dl->AddText(ImVec2(colMenu + 4.0f * S, cy - fs * 0.5f), kColText, ICON_FA_ELLIPSIS);
				if (menuClicked)
					ImGui::OpenPopup("##rowmenu");
				if (ImGui::BeginPopup("##rowmenu"))
				{
					if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "   열기", nullptr, false, exists))
						OpenProject(idx);
					if (ImGui::MenuItem(ICON_FA_FOLDER "   탐색기에서 보기", nullptr, false, exists))
						::ShellExecuteW(nullptr, L"open", p.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					ImGui::Separator();
					if (ImGui::MenuItem(ICON_FA_TRASH_CAN "   목록에서 제거"))
						pendingRemove = (int)idx;
					ImGui::EndPopup();
				}

				if (rowClicked && !ImGui::IsPopupOpen("##rowmenu"))
					OpenProject(idx);

				ImGui::SetCursorScreenPos(ImVec2(rp.x, rmax.y));
				ImGui::Dummy(ImVec2(1, 0));
				ImGui::PopID();
			}

			if (pendingRemove >= 0)
				HubProjectRegistry::Remove((size_t)pendingRemove);

			if (shown == 0)
			{
				const char* msg = projects.empty()
					? "프로젝트가 없습니다. '새 프로젝트'를 만들거나 '추가'로 기존 프로젝트를 불러오세요."
					: "검색 결과가 없습니다.";
				ImVec2 ts = ImGui::CalcTextSize(msg);
				ImVec2 c = ImGui::GetCursorScreenPos();
				dl->AddText(ImVec2(listX + (listW - ts.x) * 0.5f, c.y + 60.0f * S), kColSubText, msg);
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		// ---- 상태 메시지 (패널 하단) ----
		if (!m_Status.empty() && ImGui::GetTime() < m_StatusUntil)
		{
			ImVec2 wp = ImGui::GetWindowPos();
			ImVec2 ws = ImGui::GetWindowSize();
			ImVec2 ts = ImGui::CalcTextSize(m_Status.c_str());
			ImVec2 b0(wp.x + 24.0f * S, wp.y + ws.y - ts.y - 34.0f * S);
			ImVec2 b1(b0.x + ts.x + 28.0f * S, b0.y + ts.y + 16.0f * S);
			ImDrawList* fg = ImGui::GetForegroundDrawList();
			fg->AddRectFilled(b0, b1, IM_COL32(45, 45, 45, 245), 8.0f * S);
			fg->AddRect(b0, b1, m_StatusIsError ? kColError : IM_COL32(80, 80, 80, 255), 8.0f * S);
			fg->AddText(ImVec2(b0.x + 14.0f * S, b0.y + 8.0f * S), m_StatusIsError ? kColError : kColText, m_Status.c_str());
		}
	}
	ImGui::EndChild();

	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(2);
}

void HubApp::DrawInstallsPanel(float S, ImVec2 pos, ImVec2 size)
{
	ImGui::SetCursorPos(pos);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, V4(kColPanel));
	ImGui::PushStyleColor(ImGuiCol_Border, V4(kColBorder));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * S);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * S, 18.0f * S));

	if (ImGui::BeginChild("##installs", size, ImGuiChildFlags_Border))
	{
		ImFont* bold = ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : ImGui::GetFont();
		ImGui::PushFont(bold);
		ImGui::TextUnformatted("설치");
		ImGui::PopFont();
		ImGui::Dummy(ImVec2(0, 14.0f * S));

		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 p0 = ImGui::GetCursorScreenPos();
		float w = ImGui::GetContentRegionAvail().x;
		ImVec2 p1(p0.x + w, p0.y + 104.0f * S);
		dl->AddRectFilled(p0, p1, IM_COL32(37, 37, 37, 255), 8.0f * S);
		dl->AddRect(p0, p1, kColBorder, 8.0f * S);

		const float fs = ImGui::GetFontSize();
		if (m_Logo)
			dl->AddImage((ImTextureID)m_Logo.Get(), ImVec2(p0.x + 16.0f * S, p0.y + 16.0f * S), ImVec2(p0.x + 48.0f * S, p0.y + 48.0f * S));
		dl->AddText(HubFont(2), HubFont(2)->FontSize, ImVec2(p0.x + 64.0f * S, p0.y + 14.0f * S), kColText, ENGINE_VERSION_LABEL_A);
		dl->AddText(ImVec2(p0.x + 64.0f * S, p0.y + 14.0f * S + fs + 6.0f * S), kColSubText,
			(ToUtf8(PathManager::GetI()->GetEnginePathW()) + "Binaries").c_str());

		// 지원 렌더링 API 배지
		float bx = p0.x + 64.0f * S;
		const float by = p0.y + 14.0f * S + (fs + 6.0f * S) * 2 + 2.0f * S;
		for (int i = 0; i < (int)GraphicsAPI::Count; ++i)
		{
			GraphicsAPI api = (GraphicsAPI)i;
			bool supported = (api == GraphicsAPI::DirectX11);
			std::string label = std::string(GraphicsAPIToString(api)) + (supported ? "" : " (미구현)");
			ImVec2 ts = ImGui::CalcTextSize(label.c_str());
			ImVec2 b0(bx, by), b1(bx + ts.x + 18.0f * S, by + fs + 6.0f * S);
			dl->AddRectFilled(b0, b1, kColBadge, 6.0f * S);
			dl->AddText(ImVec2(b0.x + 9.0f * S, b0.y + 3.0f * S), supported ? kColText : kColSubText, label.c_str());
			bx = b1.x + 8.0f * S;
		}
		ImGui::Dummy(ImVec2(w, 118.0f * S));

		// ---- NOVA CLI: 터미널·AI 에이전트가 실행 중인 에디터를 다룬다 (Unity CLI 처럼)
		{
			static CliInstaller::Status s_Cli;
			static double s_NextQuery = 0.0;
			if (ImGui::GetTime() >= s_NextQuery)
			{
				s_Cli = CliInstaller::Query();
				s_NextQuery = ImGui::GetTime() + 2.0;
			}
			const ImVec2 c0 = ImGui::GetCursorScreenPos();
			const ImVec2 c1(c0.x + w, c0.y + 142.0f * S);
			dl->AddRectFilled(c0, c1, IM_COL32(37, 37, 37, 255), 8.0f * S);
			dl->AddRect(c0, c1, kColBorder, 8.0f * S);
			ImFont* iconFont = HubFont(4);
			dl->AddText(iconFont, iconFont->FontSize * 1.3f, ImVec2(c0.x + 18.0f * S, c0.y + 18.0f * S), kColText, ICON_FA_TERMINAL);
			const float tx = c0.x + 64.0f * S;
			ImFont* semi = HubFont(2);
			ImFont* smallF = HubFont(3);
			float ty = c0.y + 16.0f * S;
			dl->AddText(semi, semi->FontSize, ImVec2(tx, ty), kColText, "NOVA CLI");
			ty += semi->FontSize + 6.0f * S;
			dl->AddText(ImVec2(tx, ty), kColSubText, "터미널이나 AI 에이전트가 실행 중인 에디터를 명령으로 다룹니다 (창 포커스·마우스 조작 없이)");
			ty += fs + 8.0f * S;
			std::string state;
			ImU32 stateCol = kColSubText;
			if (!s_Cli.SourceFound)
				state = "엔진에 nova.exe 가 없습니다 (엔진을 다시 빌드하세요)";
			else if (!s_Cli.Installed)
				state = "설치되지 않음";
			else
			{
				state = s_Cli.UpToDate ? "설치됨 (최신)" : "설치됨 - 엔진에 새 버전이 있습니다";
				state += s_Cli.OnPath ? "  ·  PATH 등록됨" : "  ·  PATH 미등록";
				stateCol = s_Cli.UpToDate && s_Cli.OnPath ? IM_COL32(120, 200, 120, 255) : IM_COL32(230, 190, 90, 255);
			}
			dl->AddText(semi, smallF->FontSize, ImVec2(tx, ty), stateCol, state.c_str());
			dl->AddText(smallF, smallF->FontSize, ImVec2(tx + semi->CalcTextSizeA(smallF->FontSize, FLT_MAX, 0.0f, state.c_str()).x + 12.0f * S, ty), kColSubText, ToUtf8(s_Cli.Dir).c_str());
			ty += smallF->FontSize + 8.0f * S;
			dl->AddText(smallF, smallF->FontSize, ImVec2(tx, ty), kColSubText,
				"예: nova status  ·  nova hierarchy --components  ·  nova screenshot shot.png  ·  AI 에게는 nova ai-guide");

			// 오른쪽 버튼
			const float bw = 120.0f * S, bh = 32.0f * S;
			float bx2 = c1.x - 16.0f * S - bw;
			ImGui::SetCursorScreenPos(ImVec2(bx2, c0.y + 16.0f * S));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * S);
			ImGui::BeginDisabled(!s_Cli.SourceFound);
			const char* installLabel = !s_Cli.Installed ? "설치" : (s_Cli.UpToDate && s_Cli.OnPath ? "다시 설치" : "업데이트");
			const bool needed = !s_Cli.Installed || !s_Cli.UpToDate || !s_Cli.OnPath;
			if (needed ? BlueButton(installLabel, ImVec2(bw, bh)) : GrayButton(installLabel, ImVec2(bw, bh)))
			{
				std::string error;
				if (CliInstaller::Install(error))
					SetStatus("NOVA CLI 를 설치했습니다. 새로 연 터미널에서 nova 를 쓸 수 있습니다.");
				else
					SetStatus(error, true);
				s_NextQuery = 0.0;
			}
			ImGui::EndDisabled();
			if (s_Cli.Installed)
			{
				ImGui::SetCursorScreenPos(ImVec2(bx2, c0.y + 16.0f * S + (bh + 6.0f * S) * 1));
				if (GrayButton("제거", ImVec2(bw, bh)))
				{
					std::string error;
					if (CliInstaller::Uninstall(error))
						SetStatus("NOVA CLI 를 제거했습니다.");
					else
						SetStatus(error, true);
					s_NextQuery = 0.0;
				}
			}
			ImGui::SetCursorScreenPos(ImVec2(bx2, c0.y + 16.0f * S + (bh + 6.0f * S) * 2));
			if (GrayButton("AI 안내 복사", ImVec2(bw, bh)))
			{
				ImGui::SetClipboardText("This project uses the NOVA game engine. Control the running editor with the `nova` command line tool "
					"(run `nova ai-guide` first, then `nova info` and `nova hierarchy --components`).");
				SetStatus("AI 에이전트에게 붙여 넣을 안내를 복사했습니다.");
			}
			ImGui::PopStyleVar();
			ImGui::SetCursorScreenPos(ImVec2(c0.x, c1.y + 10.0f * S));
			ImGui::Dummy(ImVec2(w, 1.0f));
		}
	}
	ImGui::EndChild();

	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(2);
}

void HubApp::DrawNewProjectPopup(float S)
{
	if (m_OpenNewProjectPopup)
	{
		ImGui::OpenPopup("새 프로젝트");
		m_OpenNewProjectPopup = false;
	}

	ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(720.0f * S, 440.0f * S));

	ImGui::PushStyleColor(ImGuiCol_PopupBg, V4(IM_COL32(34, 34, 34, 255)));
	ImGui::PushStyleColor(ImGuiCol_Border, V4(kColBorder));
	ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.55f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * S);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * S, 20.0f * S));

	bool open = true;
	if (ImGui::BeginPopupModal("새 프로젝트", &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar))
	{
		const auto& templates = HubProjectRegistry::Templates();
		const float leftW = 220.0f * S;

		// 좌측: 템플릿 목록
		ImGui::PushStyleColor(ImGuiCol_ChildBg, V4(IM_COL32(28, 28, 28, 255)));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f * S);
		if (ImGui::BeginChild("##templates", ImVec2(leftW, -52.0f * S), ImGuiChildFlags_Border))
		{
			ImGui::PushStyleColor(ImGuiCol_Header, V4(kColSelected));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, V4(kColHover));
			for (int i = 0; i < (int)templates.size(); ++i)
			{
				std::string label = std::string(ICON_FA_CUBE "   ") + templates[i].Title;
				if (ImGui::Selectable(label.c_str(), m_NewTemplate == i, 0, ImVec2(0, 34.0f * S)))
					m_NewTemplate = i;
			}
			ImGui::PopStyleColor(2);
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		// 우측: 프로젝트 설정
		ImGui::SameLine(0, 20.0f * S);
		ImGui::BeginGroup();
		ImGui::TextColored(V4(kColSubText), "템플릿 설명");
		ImGui::TextWrapped("%s", templates[m_NewTemplate].Description);
		ImGui::Dummy(ImVec2(0, 16.0f * S));

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * S);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * S, 8.0f * S));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, V4(kColField));
		ImGui::PushStyleColor(ImGuiCol_Border, V4(IM_COL32(64, 64, 64, 255)));

		const float fieldW = ImGui::GetContentRegionAvail().x;

		ImGui::TextColored(V4(kColSubText), "프로젝트 이름");
		ImGui::SetNextItemWidth(fieldW);
		ImGui::InputText("##newname", m_NewName, sizeof(m_NewName));
		ImGui::Dummy(ImVec2(0, 10.0f * S));

		ImGui::TextColored(V4(kColSubText), "위치");
		ImGui::SetNextItemWidth(fieldW - 46.0f * S);
		ImGui::InputText("##newloc", m_NewLocation, sizeof(m_NewLocation));
		ImGui::SameLine(0, 6.0f * S);
		if (GrayButton(ICON_FA_FOLDER_OPEN "##browse", ImVec2(40.0f * S, 0)))
		{
			std::wstring folder;
			if (PickFolder(_hMainWnd, L"프로젝트를 만들 위치 선택", folder))
				strncpy_s(m_NewLocation, ToUtf8(folder).c_str(), _TRUNCATE);
		}
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(3);

		std::wstring target = (fs::path(FromUtf8(m_NewLocation)) / FromUtf8(m_NewName)).wstring();
		ImGui::Dummy(ImVec2(0, 8.0f * S));
		ImGui::PushStyleColor(ImGuiCol_Text, V4(kColSubText));
		ImGui::TextWrapped("생성 경로: %s", ToUtf8(target).c_str());
		ImGui::PopStyleColor();

		if (!m_NewError.empty())
		{
			ImGui::Dummy(ImVec2(0, 6.0f * S));
			ImGui::PushStyleColor(ImGuiCol_Text, V4(kColError));
			ImGui::TextWrapped("%s", m_NewError.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::EndGroup();

		// 하단 버튼
		ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 52.0f * S);
		const float btnW = 130.0f * S;
		const float btnH = 36.0f * S;
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 24.0f * S - btnW * 2 - 10.0f * S);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f * S);
		if (GrayButton("취소", ImVec2(btnW, btnH)))
			ImGui::CloseCurrentPopup();
		ImGui::SameLine(0, 10.0f * S);
		if (BlueButton("프로젝트 만들기", ImVec2(btnW, btnH)))
		{
			std::string error;
			if (HubProjectRegistry::Create(m_NewName, FromUtf8(m_NewLocation), templates[m_NewTemplate].Id, error))
			{
				ImGui::CloseCurrentPopup();
				// 만든 프로젝트는 목록 맨 앞(가장 최근)에 있으므로 바로 에디터로 연다.
				auto& projects = HubProjectRegistry::Projects();
				if (!projects.empty())
					OpenProject(0);
			}
			else
			{
				m_NewError = error;
			}
		}
		ImGui::PopStyleVar();

		ImGui::EndPopup();
	}

	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(3);
}
