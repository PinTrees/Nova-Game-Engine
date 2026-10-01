#pragma once

// Unity 의 Edit > Project Settings... 창 (떠 있는 창, 왼쪽 분류 목록 | 오른쪽 설정).
// 현재 분류: Graphics (URP: Default Volume Profile).
namespace ProjectSettingsWindow
{
	void Open(const char* category = "Graphics");
	void Draw();   // 매 프레임 (열려 있을 때만 그림)
	void Close();
	bool IsOpen();
}
