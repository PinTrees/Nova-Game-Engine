#include "pch.h"
#include "Application.h"
#include "App.h"
#include "AndroidEngine.h"

// Source/Platform/Application.cpp 의 안드로이드 판 (그 폴더의 Windows pch.h 를 피해서). 안드로이드는 늘 플레이어
SINGLE_BODY(Application)

bool Application::isPlaying = false;
bool Application::isPaused = false;
bool Application::stepRequested = false;

Application::Application() : m_pCurrApp(nullptr) {}
Application::~Application() {}

HWND Application::GetMainHwnd() { return m_pCurrApp ? m_pCurrApp->MainWnd() : nullptr; }
HINSTANCE Application::GetInstance() { return m_pCurrApp ? m_pCurrApp->AppInst() : nullptr; }
GfxDevice* Application::GetDevice() { return m_pCurrApp ? m_pCurrApp->GetDevice() : Gfx::Device(); }
GfxContext* Application::GetDeviceContext() { return m_pCurrApp ? m_pCurrApp->GetDeviceContext() : Gfx::Context(); }

// 에셋을 푼 폴더의 Assets (앱 파일 폴더 아래)
wstring Application::GetDataPath() { return string_to_wstring(NovaAndroid::FilesDir() + "/game/Assets/"); }
