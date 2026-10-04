#pragma once
#include <string>

// 안드로이드 플랫폼 → 엔진: 앱 파일 폴더, 입력 상태 (Win32 입력 함수 GetAsyncKeyState · GetCursorPos 가 이 상태를 읽는다)
namespace NovaAndroid
{
	void SetFilesDir(const std::string& dir);
	const std::string& FilesDir();

	void SetKey(int vk, bool down);                  // 가상 키 (VK_*)
	void SetPointer(float x, float y, bool down);    // 첫 손가락 = 커서 + VK_LBUTTON
	void SetFocus(bool focused);
	void RequestQuit();      // Application.Quit → 앱 끝내기 (AndroidMain 의 루프가 본다)
	bool QuitRequested();
	void SetGameView(int width, int height, bool focused);   // 게임 화면 크기 (UI · 스크립트 입력의 좌표) — EditorStubs.cpp
}
