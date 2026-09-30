#include "pch.h"
#include "App.h"
#include "EditorApp.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		EditorApp theApp(hInstance);
		
		if (!theApp.Init())
		{
			::MessageBoxW(NULL, L"EditorApp::Init() failed!", L"Init Error", MB_OK | MB_ICONERROR);
			return 1;
		}

		return theApp.Run();
	}
	catch (const std::exception& e)
	{
		::MessageBoxA(NULL, e.what(), "Exception", MB_OK | MB_ICONERROR);
		return 1;
	}
	catch (...)
	{
		::MessageBoxW(NULL, L"Unknown Exception occurred!", L"Fatal Error", MB_OK | MB_ICONERROR);
		return 1;
	}
}