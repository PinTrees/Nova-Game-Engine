#include "pch.h"
#include "HubApp.h"
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

	EditorGUIManager::GetI()->Init(true);

	HubProjectRegistry::Load();
	std::string defaultLocation = ToUtf8(HubProjectRegistry::DefaultLocation());
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

	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);

	ImGui::PushStyleColor(ImGuiCol_WindowBg, V4(kColBg));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

	if (ImGui::Begin("##HubRoot", nullptr, flags))
	{
		const float W = viewport->Size.x;
		const float H = viewport->Size.y;
		const float topH = 46.0f * S;
		const float margin = 12.0f * S;
		const float sideW = 208.0f * S;

		DrawTopBar(S, W);

		DrawSidebar(S, ImVec2(margin, topH), ImVec2(sideW, H - topH - margin));

		ImVec2 mainPos(margin * 2 + sideW, topH);
		ImVec2 mainSize(W - mainPos.x - margin, H - topH - margin);
		if (m_Tab == Tab::Projects)
			DrawProjectsPanel(S, mainPos, mainSize);
		else
			DrawInstallsPanel(S, mainPos, mainSize);
	}
	ImGui::End();

	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor();

	DrawNewProjectPopup(S);
}

void HubApp::DrawTopBar(float S, float width)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 origin = ImGui::GetWindowPos();

	ImFont* bold = ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : ImGui::GetFont();
	ImVec2 pos(origin.x + 16.0f * S, origin.y + 8.0f * S);
	dl->AddText(bold, bold->FontSize, pos, kColText, ICON_FA_CUBES "  NOVA Hub");

	std::string version = std::string("v") + ENGINE_VERSION_A;
	ImVec2 vs = ImGui::CalcTextSize(version.c_str());
	dl->AddText(ImVec2(origin.x + width - vs.x - 20.0f * S, origin.y + 14.0f * S), kColSubText, version.c_str());
}

void HubApp::DrawSidebar(float S, ImVec2 pos, ImVec2 size)
{
	ImGui::SetCursorPos(pos);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, V4(kColSidebar));
	ImGui::PushStyleColor(ImGuiCol_Border, V4(kColBorder));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * S);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * S, 12.0f * S));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 4.0f * S));

	if (ImGui::BeginChild("##sidebar", size, ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar))
	{
		struct Item { const char* label; Tab tab; };
		const Item items[] =
		{
			{ ICON_FA_FOLDER "    프로젝트", Tab::Projects },
			{ ICON_FA_DOWNLOAD "    설치", Tab::Installs },
		};

		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * S);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f * S, 0.0f));
		for (const Item& item : items)
		{
			bool selected = (m_Tab == item.tab);
			ImGui::PushStyleColor(ImGuiCol_Button, selected ? V4(kColSelected) : ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? V4(kColSelected) : V4(kColHover));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, V4(kColSelected));
			ImGui::PushStyleColor(ImGuiCol_Text, V4(kColText));
			if (ImGui::Button(item.label, ImVec2(-1, 38.0f * S)))
				m_Tab = item.tab;
			ImGui::PopStyleColor(4);
		}
		ImGui::PopStyleVar(3);
	}
	ImGui::EndChild();

	ImGui::PopStyleVar(4);
	ImGui::PopStyleColor(2);
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
		const float ctrlH = 34.0f * S;
		const float searchW = 250.0f * S;
		const float addW = 84.0f * S;
		const float newW = 130.0f * S;
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
		const float headerH = 36.0f * S;
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
			const float rowH = 58.0f * S;
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
				dl->AddText(ImVec2(colName, rp.y + 9.0f * S), nameCol, p.Name.c_str());
				dl->AddText(font, fs * 0.86f, ImVec2(colName, rp.y + 9.0f * S + fs + 3.0f * S), kColSubText, pathUtf8.c_str());

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
		ImVec2 p1(p0.x + w, p0.y + 96.0f * S);
		dl->AddRectFilled(p0, p1, IM_COL32(37, 37, 37, 255), 8.0f * S);
		dl->AddRect(p0, p1, kColBorder, 8.0f * S);

		const float fs = ImGui::GetFontSize();
		dl->AddText(ImVec2(p0.x + 20.0f * S, p0.y + 18.0f * S), kColText, ICON_FA_CUBES);
		dl->AddText(ImVec2(p0.x + 56.0f * S, p0.y + 14.0f * S), kColText, ENGINE_VERSION_LABEL_A);
		dl->AddText(ImVec2(p0.x + 56.0f * S, p0.y + 14.0f * S + fs + 4.0f * S), kColSubText,
			(ToUtf8(PathManager::GetI()->GetEnginePathW()) + "Binaries").c_str());

		// 지원 렌더링 API 배지
		float bx = p0.x + 56.0f * S;
		const float by = p0.y + 14.0f * S + (fs + 4.0f * S) * 2;
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
		ImGui::Dummy(ImVec2(w, 110.0f * S));
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
