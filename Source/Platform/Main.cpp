#include "pch.h"
#include "App.h"
#include "EditorApp.h"

#include <filesystem>
#include <fstream>

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