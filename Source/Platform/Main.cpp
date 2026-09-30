#include "pch.h"
#include "App.h"
#include "EditorApp.h"
#include "HubApp.h"
#include "HubProject.h"

#include <filesystem>
#include <fstream>
#include <shellapi.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	// Ensure working directory is always the executable's directory (Binaries)
	wchar_t exePathBuf[MAX_PATH] = { 0 };
	GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);
	std::filesystem::path exeDir = std::filesystem::path(exePathBuf).parent_path();
	SetCurrentDirectoryW(exeDir.c_str());

	// 실행 모드
	//   (인자 없음)          : NOVA Hub (프로젝트 선택/생성)
	//   --project <폴더>     : 해당 프로젝트를 에디터로 연다
	//   --editor             : 프로젝트 없이 엔진 폴더의 Assets 로 에디터를 연다 (엔진 개발용)
	std::wstring projectPath;
	bool editorOnly = false;
	std::wstring createLocation, createName;   // --create-project <위치> <이름> (자동화/테스트용)
	{
		int argc = 0;
		LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
		for (int i = 1; argv && i < argc; ++i)
		{
			if (wcscmp(argv[i], L"--project") == 0 && i + 1 < argc)
				projectPath = argv[++i];
			else if (wcscmp(argv[i], L"--editor") == 0)
				editorOnly = true;
			else if (wcscmp(argv[i], L"--create-project") == 0 && i + 2 < argc)
			{
				createLocation = argv[++i];
				createName = argv[++i];
			}
		}
		::LocalFree(argv);
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

	if (projectPath.empty() && !editorOnly)
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

	if (!projectPath.empty())
		PathManager::SetProjectOverride(projectPath);

	try
	{
		std::ofstream log("run_log.txt");
		log << "Starting EditorApp..." << std::endl;
		log.flush();

		EditorApp theApp(hInstance);
		
		log << "Calling theApp.Init()..." << std::endl;
		log.flush();

		if (!theApp.Init())
		{
			log << "Init() returned false!" << std::endl;
			::MessageBoxW(NULL, L"EditorApp::Init() failed!", L"Init Error", MB_OK | MB_ICONERROR);
			return 1;
		}

		log << "Init() succeeded! Calling theApp.Run()..." << std::endl;
		log.flush();

		int ret = theApp.Run();
		log << "theApp.Run() exited with code: " << ret << std::endl;
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