#pragma once
#include <string>
#include <vector>
#include "GraphicsAPI.h"

// 빌드된 게임(Unity 의 Player)으로 실행하기.
// 빌드 결과:  <폴더>/<제품>.exe  +  <폴더>/<제품>_Data/ (player.json, Assets, Shaders, Resources ..., Binaries/ShaderCache·Scripting)
// exe 옆에 <exe 이름>_Data/player.json 이 있으면 플레이어로 실행한다:
//  - 엔진 루트 = 프로젝트 루트 = <제품>_Data, 작업 폴더 = <제품>_Data/Binaries (로그 = Logs/Editor.log)
//  - 에디터 창 없이 창 전체에 첫 씬의 카메라 + UI 를 그린다, 시작하자마자 Play
//  - 창: Fullscreen Window(테두리 없는 전체 화면) / Maximized Window / Windowed(크기, 크기 조절 여부)
namespace PlayerRuntime
{
	// WinMain 맨 처음: 플레이어로 실행해야 하면 경로를 설정하고 true
	bool Detect();
	bool IsActive();

	std::wstring ProductName();
	int FullscreenMode();          // BuildSettings::FullscreenMode
	void WindowSize(int& width, int& height);
	bool Resizable();
	bool RunInBackground();
	std::vector<GraphicsAPI> GraphicsAPIs();   // player.json 의 순서 (없으면 빈 목록)
	std::wstring FirstScene();     // 빌드 씬 목록의 0번

	// 초기화가 끝난 뒤: 첫 씬 Play
	void Start();
	// 매 프레임 (App::Run 에서 에디터 창 대신): 카메라 → 백버퍼, UI
	void Render(GfxRenderTargetView* backBuffer, GfxDepthStencilView* depth, int width, int height, bool focused);
	// Application.Quit
	void Quit();
}
