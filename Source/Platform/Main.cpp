#include "pch.h"
#include "App.h"
#include "EditorApp.h"
#include "HubApp.h"
#include "HubProject.h"
#include "PlayerRuntime.h"
#include "CliInstaller.h"
#include "AutoSave.h"

#include <filesystem>
#include <fstream>
#include <shellapi.h>

// 실행 파일(Source/Launcher)의 WinMain 이 부르는 엔진 진입점
extern "C" NOVA_API int NovaMain(HINSTANCE hInstance, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	// Ensure working directory is always the executable's directory (Binaries)
	wchar_t exePathBuf[MAX_PATH] = { 0 };
	GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);
	std::filesystem::path exeDir = std::filesystem::path(exePathBuf).parent_path();
	SetCurrentDirectoryW(exeDir.c_str());

	// 빌드된 게임: exe 옆에 <exe 이름>_Data/player.json 이 있으면 Hub/에디터 없이 게임으로 실행 (작업 폴더도 바뀜)
	const bool player = PlayerRuntime::Detect();

	// 실행 모드
	//   (인자 없음)          : NOVA Hub (프로젝트 선택/생성)
	//   --project <폴더>     : 해당 프로젝트를 에디터로 연다
	//   --editor             : 프로젝트 없이 엔진 폴더의 Assets 로 에디터를 연다 (엔진 개발용)
	std::wstring projectPath;
	bool editorOnly = false;
	std::wstring createLocation, createName;   // --create-project <위치> <이름> (자동화/테스트용)
	int cliInstall = 0;                        // --install-cli / --uninstall-cli (Hub 의 NOVA CLI 설치와 같은 일, 자동화용)
	{
		int argc = 0;
		LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
		for (int i = 1; argv && i < argc; ++i)
		{
			if (wcscmp(argv[i], L"--project") == 0 && i + 1 < argc)
				projectPath = argv[++i];
			else if (wcscmp(argv[i], L"--editor") == 0)
				editorOnly = true;
			else if (wcscmp(argv[i], L"--install-cli") == 0)
				cliInstall = 1;
			else if (wcscmp(argv[i], L"--uninstall-cli") == 0)
				cliInstall = -1;
			else if (wcscmp(argv[i], L"--no-activate") == 0)
				Application::noActivate = true;   // NOVA CLI: 백그라운드로 열기
			else if (wcscmp(argv[i], L"--create-project") == 0 && i + 2 < argc)
			{
				createLocation = argv[++i];
				createName = argv[++i];
			}
		}
		::LocalFree(argv);
	}

	// NOVA CLI 설치/제거만 하고 종료 (결과는 hub_log.txt 에 기록)
	if (cliInstall != 0)
	{
		std::string error;
		const bool ok = cliInstall > 0 ? CliInstaller::Install(error) : CliInstaller::Uninstall(error);
		const CliInstaller::Status s = CliInstaller::Query();
		std::ofstream log("hub_log.txt", std::ios::trunc);
		log << (ok ? (cliInstall > 0 ? "CLI_INSTALL_OK" : "CLI_UNINSTALL_OK") : "CLI_FAILED: " + error)
			<< " installed=" << s.Installed << " upToDate=" << s.UpToDate << " onPath=" << s.OnPath << std::endl;
		return ok ? 0 : 1;
	}

	// 프로젝트만 생성하고 종료 (결과는 hub_log.txt 에 기록)
	if (!createName.empty())
	{
		std::string error;
		std::string name(createName.begin(), createName.end());
		HubProjectRegistry::Load();
		bool ok = HubProjectRegistry::Create(name, createLocation, "3D", error);
		std::ofstream log("hub_log.txt", std::ios::trunc);
		log << (ok ? "CREATE_OK" : "CREATE_FAILED: " + error) << std::endl;
		return ok ? 0 : 1;
	}

	if (projectPath.empty() && !editorOnly && !player)
	{
		try
		{
			HubApp hub(hInstance);
			if (!hub.Init())
			{
				::MessageBoxW(NULL, L"NOVA Hub 초기화에 실패했습니다.", L"Init Error", MB_OK | MB_ICONERROR);
				return 1;
			}
			return hub.Run();
		}
		catch (const std::exception& e)
		{
			::MessageBoxA(NULL, e.what(), "Exception", MB_OK | MB_ICONERROR);
			return 1;
		}
	}

	if (!projectPath.empty() && !player)
		PathManager::SetProjectOverride(projectPath);

	try
	{
		std::ofstream log("run_log.txt");
		log << "Starting EditorApp..." << std::endl;
		log.flush();

		EditorLog::Init();
		// PNG/JPG 디코딩(WIC)에 COM 이 필요하다. 지금까지는 Hub 가 만든 텍스처 캐시(DDS)에 기대어 우연히 동작했다.
		const HRESULT comHr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		EditorLog::Write("App", "CoInitializeEx hr=0x%08X", (unsigned)comHr);
		EditorLog::Write("App", "editor start, project=%s", wstring_to_string(projectPath).c_str());
		EditorApp theApp(hInstance);
		
		log << "Calling theApp.Init()..." << std::endl;
		log.flush();

		if (!theApp.Init())
		{
			log << "Init() returned false!" << std::endl;
			::MessageBoxW(NULL, player ? L"The game failed to start. See Logs\\Editor.log in the _Data\\Binaries folder." : L"EditorApp::Init() failed!", L"Init Error", MB_OK | MB_ICONERROR);
			return 1;
		}

		log << "Init() succeeded! Calling theApp.Run()..." << std::endl;
		log.flush();

		int ret = theApp.Run();
		AutoSave::Shutdown();   // 정상 종료: 이번 세션의 autosave 를 지운다 (남아 있으면 다음 시작 때 충돌로 본다)
		log << "theApp.Run() exited with code: " << ret << std::endl;
		EditorLog::Shutdown();
		return ret;
	}
	catch (const std::exception& e)
	{
		std::ofstream log("run_log.txt", std::ios::app);
		log << "std::exception: " << e.what() << std::endl;
		::MessageBoxA(NULL, e.what(), "Exception", MB_OK | MB_ICONERROR);
		return 1;
	}
	catch (...)
	{
		std::ofstream log("run_log.txt", std::ios::app);
		log << "Unknown exception occurred!" << std::endl;
		::MessageBoxW(NULL, L"Unknown Exception occurred!", L"Fatal Error", MB_OK | MB_ICONERROR);
		return 1;
	}
}