#include "pch.h"
#include "PlayerRuntime.h"
#include "BuildSettings.h"
#include "UISystem.h"
#include "ScriptEngine.h"
#include "GameViewEditorWindow.h"
#include "App.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
	bool s_Active = false;
	json s_Config;
	std::wstring s_DataDir;
	int s_LastW = 0, s_LastH = 0;
	Camera* s_LastCamera = nullptr;
}

namespace PlayerRuntime
{
	bool Detect()
	{
		wchar_t buf[MAX_PATH] = {};
		::GetModuleFileNameW(nullptr, buf, MAX_PATH);
		const fs::path exe(buf);
		const fs::path data = exe.parent_path() / (exe.stem().wstring() + L"_Data");
		std::error_code ec;
		if (!fs::exists(data / L"player.json", ec))
			return false;
		std::ifstream in(data / L"player.json");
		s_Config = json::parse(in, nullptr, false);
		if (s_Config.is_discarded() || !s_Config.is_object())
			return false;
		s_Active = true;
		s_DataDir = data.wstring() + L"\\";
		Application::isPlayer = true;
		// 엔진 리소스와 프로젝트 에셋 모두 _Data 안에 있다. 작업 폴더 = _Data/Binaries ("../Shaders" 가 맞도록)
		PathManager::SetEngineOverride(s_DataDir);
		PathManager::SetProjectOverride(s_DataDir);
		fs::create_directories(data / L"Binaries", ec);
		::SetCurrentDirectoryW((data / L"Binaries").c_str());
		std::vector<std::string> scenes;
		if (s_Config.contains("scenes") && s_Config["scenes"].is_array())
			for (const json& s : s_Config["scenes"])
				scenes.push_back(s.get<std::string>());
		BuildSettings::SetRuntimeScenes(scenes);
		return true;
	}

	bool IsActive() { return s_Active; }

	std::wstring ProductName() { return string_to_wstring(s_Config.value("productName", std::string("Game"))); }
	int FullscreenMode() { return s_Config.value("fullscreenMode", 0); }
	void WindowSize(int& width, int& height)
	{
		width = (std::max)(320, s_Config.value("width", 1280));
		height = (std::max)(240, s_Config.value("height", 720));
	}
	bool Resizable() { return s_Config.value("resizable", true); }
	bool RunInBackground() { return s_Config.value("runInBackground", true); }
	std::vector<GraphicsAPI> GraphicsAPIs()
	{
		std::vector<GraphicsAPI> apis;
		if (s_Config.contains("graphicsAPIs") && s_Config["graphicsAPIs"].is_array())
			for (const json& k : s_Config["graphicsAPIs"])
				if (k.is_string())
					apis.push_back(GraphicsAPIFromKey(k.get<std::string>()));
		return apis;
	}

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

	void Render(ID3D11RenderTargetView* backBuffer, ID3D11DepthStencilView* depth, int width, int height, bool focused)
	{
		if (backBuffer == nullptr || width <= 0 || height <= 0)
			return;
		auto ctx = Application::GetI()->GetDeviceContext();
		GameViewEditorWindow::SetPlayerView(width, height, focused);
		std::shared_ptr<Camera> camera = DisplayManager::GetI()->GetCameraForDisplay(0);
		ID3D11RenderTargetView* rtvs[1] = { backBuffer };
		ctx->OMSetRenderTargets(1, rtvs, depth);
		if (camera == nullptr)
		{
			const float black[4] = { 0, 0, 0, 1 };
			ctx->ClearRenderTargetView(backBuffer, black);
		}
		else
		{
			// Game 뷰와 같은 순서: 크기가 바뀌면 뷰포트/SSAO, 카메라 비율 → 씬 → UI
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
		UISystem::RenderGameView(backBuffer, (UINT)width, (UINT)height, 0);
		ctx->OMSetRenderTargets(1, rtvs, depth);
	}

	void Quit()
	{
		EditorLog::Write("Player", "Application.Quit");
		::PostQuitMessage(0);
	}
}
