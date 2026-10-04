#pragma once
#include <string>
#include <vector>
#include "GraphicsAPI.h"

// Unity 의 Build Settings(Scenes In Build) 와 Player Settings.
//  - ProjectSettings/EditorBuildSettings.json : 빌드에 넣을 씬 목록(순서 = 빌드 인덱스), Development Build, 마지막 빌드 폴더
//  - ProjectSettings/PlayerSettings.json      : 회사/제품 이름, 버전, 창 모드와 해상도, Run In Background
namespace BuildSettings
{
	struct SceneEntry
	{
		std::string Path;      // 프로젝트 기준 (Assets\Scenes\SampleScene.scene)
		bool Enabled = true;
	};

	enum class FullscreenMode { FullscreenWindow = 0, MaximizedWindow = 1, Windowed = 2 };

	struct Player
	{
		std::string CompanyName = "DefaultCompany";
		std::string ProductName;             // 빈 문자열 = 프로젝트 폴더 이름
		std::string Version = "0.1.0";
		FullscreenMode Mode = FullscreenMode::FullscreenWindow;
		int Width = 1920, Height = 1080;     // Windowed 일 때 창 크기
		bool Resizable = true;
		bool RunInBackground = true;
		// Unity 의 Other Settings > Rendering: Auto Graphics API for Windows / Graphics APIs (위가 우선)
		bool AutoGraphicsAPI = true;
		std::vector<GraphicsAPI> GraphicsAPIs = { GraphicsAPI::DirectX11 };
		// Android: Texture Compression (Unity 의 Build Settings > Android) — 가져오기 설정의 Android Format 이 Automatic 인 텍스처의 형식.
		//  0 ASTC (6x6, High Quality 는 4x4) · 1 ETC2 · 2 DXT (BC, 에뮬레이터 · 일부 GPU) · 3 압축 없음 (RGBA32)
		int AndroidTextureCompression = 0;
		// Android: Package Name (Unity 의 Other Settings > Identification) — 비면 com.<회사>.<제품>
		std::string AndroidPackageName;
	};
	// 빌드된 게임이 시도할 순서 (Auto 면 엔진 기본 순서)
	std::vector<GraphicsAPI> PlayerGraphicsAPIs();

	std::vector<SceneEntry>& Scenes();
	void SaveScenes();
	std::vector<std::string> EnabledScenes();   // 빌드에 들어가는 씬 (순서대로)

	Player& GetPlayer();
	void SavePlayer();
	std::string ProductName();                  // 비어 있으면 프로젝트 이름

	bool& DevelopmentBuild();
	std::string& LastBuildFolder();
	// Build Settings 의 플랫폼 (Unity 의 Switch Platform): 0 Windows, 1 Android
	int& ActivePlatform();
	std::string& AndroidRunDevice();     // Build And Run 장치 (adb 시리얼, 비면 첫 장치)
	std::string& LastAndroidApk();       // 마지막 .apk 경로
	void SaveEditorBuild();

	// 실행 중에 쓰는 씬 목록: 에디터 = Build Settings 의 켜진 씬, 플레이어 = 빌드할 때 넣은 목록
	std::vector<std::string> RuntimeScenes();
	void SetRuntimeScenes(const std::vector<std::string>& scenes);   // 플레이어 시작 시
}
