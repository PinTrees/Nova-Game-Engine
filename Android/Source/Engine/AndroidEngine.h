#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 안드로이드 플랫폼 → 엔진: 앱 파일 폴더, 입력 상태 (Win32 입력 함수 GetAsyncKeyState · GetCursorPos 가 이 상태를 읽는다)
namespace NovaAndroid
{
	void SetFilesDir(const std::string& dir);
	const std::string& FilesDir();

	void SetKey(int vk, bool down);                  // 가상 키 (VK_*)
	void SetPointer(float x, float y, bool down);    // 첫 손가락 = 커서 + VK_LBUTTON
	// 손가락마다 (Input.GetTouch): action 0 = 누름, 1 = 움직임, 2 = 뗌, 3 = 취소
	void TouchEvent(int id, float x, float y, int action);
	// 프레임 시작 (App::Run, InputManager 보다 먼저): 한 프레임 안에 눌렀다 뗀 탭도 이번 프레임에 '눌림' 으로 보이게 하고,
	//  손가락 상태 → Touch (Began · Moved · Stationary · Ended) 를 InputManager 에
	void BeginInputFrame();
	struct PointerEvent { float X, Y; bool Down; };
	std::vector<PointerEvent> TakePointerEvents();   // 지난 프레임 뒤의 첫 손가락 이벤트 (ImGui 입력 큐 — UI 가 빠른 탭도 받는다)
	void SetFocus(bool focused);
	void RequestQuit();      // Application.Quit → 앱 끝내기 (AndroidMain 의 루프가 본다)
	bool QuitRequested();
	void SetAudioPaused(bool paused);
	void RequestTimeReset();   // 앱이 앞으로 돌아왔다: 다음 프레임의 deltaTime 이 뒤에 있던 시간만큼 튀지 않게 (AppAndroid 가 타이머를 새로)
	bool TakeTimeReset();                        // 앱이 뒤로 가면 소리를 멈춘다 (XAudio2Android.cpp)
	bool AudioStats(uint64_t& framesRendered, float& peak);   // AAudio 로 낸 프레임 수 · 마지막으로 물은 뒤의 최대 레벨 (검사)
	void SetGameView(int width, int height, bool focused);   // 게임 화면 크기 (UI · 스크립트 입력의 좌표) — EditorStubs.cpp
}
