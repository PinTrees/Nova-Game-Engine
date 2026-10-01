#pragma once

// Unity 의 File > Build Settings... 창.
//  - Scenes In Build: 체크(포함) + 이름 + 빌드 인덱스, 끌어서 순서 바꾸기, Delete/우클릭 제거,
//    [Add Open Scenes], Project 창의 .scene 을 끌어 놓아 추가
//  - Platform: Windows (선택됨) / 나머지는 회색
//  - Development Build, [Player Settings...], [Build], [Build And Run]
namespace BuildSettingsWindow
{
	void Open();
	void Draw();
	void Close();
	bool IsOpen();
	// File > Build And Run (Ctrl+B): 마지막 빌드 폴더로 바로 (없으면 폴더 선택)
	void BuildAndRun();
}
