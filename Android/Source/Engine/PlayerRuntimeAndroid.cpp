#include "pch.h"
#include "PlayerRuntime.h"
#include "BuildSettings.h"
#include "UISystem.h"
#include "GameViewEditorWindow.h"
#include "App.h"
#include "AndroidEngine.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>

// Source/Build/PlayerRuntime.cpp 의 안드로이드 판: 게임 데이터 = 앱 파일 폴더의 game/ (APK 의 assets/game 을 풀어 놓은 것 — nova android export).
//  Detect 만 다르고 (exe 경로 대신 game/player.json) 나머지는 Windows 판과 같다
namespace fs = std::filesystem;

namespace
{
	bool s_Active = false;
	json s_Config;
	int s_LastW = 0, s_LastH = 0;
	Camera* s_LastCamera = nullptr;
}

namespace PlayerRuntime
{
	bool Detect()
	{
		const fs::path data = fs::path(NovaAndroid::FilesDir()) / "game";
		std::error_code ec;
		if (!fs::exists(data / "player.json", ec))
			return false;
		std::ifstream in(data / "player.json");
		s_Config = json::parse(in, nullptr, false);
		if (s_Config.is_discarded() || !s_Config.is_object())
			return false;
		s_Active = true;
		Application::isPlayer = true;
		const std::wstring root = string_to_wstring(data.string() + "/");
		PathManager::SetEngineOverride(root);
		PathManager::SetProjectOverride(root);
		PathManager::GetI()->Init();
		// 작업 폴더 = game/Binaries ("../Shaders" · "../Resources" 가 맞도록 — Windows 플레이어와 같음)
		fs::create_directories(data / "Binaries", ec);
		chdir((data / "Binaries").c_str());
		std::vector<std::string> scenes;
		if (s_Config.contains("scenes") && s_Config["scenes"].is_array())
			for (const json& s : s_Config["scenes"])
				scenes.push_back(s.get<std::string>());
		BuildSettings::SetRuntimeScenes(scenes);
		return true;
	}

	bool IsActive() { return s_Active; }
	std::wstring ProductName() { return string_to_wstring(s_Config.value("productName", std::string("Game"))); }
	int FullscreenMode() { return 0; }
	void WindowSize(int& width, int& height) { width = 0; height = 0; }   // 안드로이드 = 화면 전체
	bool Resizable() { return true; }
	bool RunInBackground() { return false; }
	std::vector<GraphicsAPI> GraphicsAPIs() { return { GraphicsAPI::OpenGL }; }

	std::wstring FirstScene()
	{
		const std::vector<std::string> scenes = BuildSettings::RuntimeScenes();
		return scenes.empty() ? std::wstring() : string_to_wstring(scenes[0]);
	}

	void Start()
	{
		EditorLog::Write("Player", "start: %s, first scene %s", wstring_to_string(ProductName()).c_str(), wstring_to_string(FirstScene()).c_str());
		Application::SetPaused(false);
		Application::SetPlaying(true);
		SceneManager::GetI()->HandlePlay();
	}

	// Windows 판과 같은 순서: 크기가 바뀌면 뷰포트/SSAO, 카메라 비율 → 씬 → UI
	void Render(GfxRenderTargetView* backBuffer, GfxDepthStencilView* depth, int width, int height, bool focused)
	{
		if (backBuffer == nullptr || width <= 0 || height <= 0)
			return;
		auto ctx = Application::GetI()->GetDeviceContext();
		GameViewEditorWindow::SetPlayerView(width, height, focused);
		std::shared_ptr<Camera> camera = DisplayManager::GetI()->GetCameraForDisplay(0);
		GfxRenderTargetView* rtvs[1] = { backBuffer };
		ctx->OMSetRenderTargets(1, rtvs, depth);
		if (camera == nullptr)
		{
			const float black[4] = { 0, 0, 0, 1 };
			ctx->ClearRenderTargetView(backBuffer, black);
		}
		else
		{
			if (width != s_LastW || height != s_LastH || camera.get() != s_LastCamera)
			{
				s_LastW = width;
				s_LastH = height;
				s_LastCamera = camera.get();
				RenderManager::GetI()->SetViewport(width, height);
				PostProcessingManager::GetI()->SetSSAO(width, height, camera.get());
			}
			camera->SetAspect((float)width / (float)height);
			camera->LateUpdate();
			RenderManager::GetI()->CameraViewProjectionMatrix = camera->View() * camera->Proj();
			Application::GetI()->GetApp()->OnSceneRender(backBuffer, camera.get());
		}
		UISystem::RenderGameView(backBuffer, (UINT)width, (UINT)height, 0, camera.get(), camera ? Application::GetI()->GetApp()->SceneDepth((UINT)width, (UINT)height) : nullptr);
		ctx->OMSetRenderTargets(1, rtvs, depth);
	}

	void Quit()
	{
		EditorLog::Write("Player", "Application.Quit");
		NovaAndroid::RequestQuit();
	}
}
