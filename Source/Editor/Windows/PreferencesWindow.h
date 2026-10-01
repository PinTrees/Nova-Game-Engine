#pragma once

// Unity 의 Edit > Preferences... 창 (사용자별 에디터 설정, EditorPrefs 에 저장).
//  - External Tools: External Script Editor (기본 NOVA Code), 인자, Regenerate project files
//  - NOVA Code: 내장 코드 편집기의 글꼴 크기, 탭 크기, 자동 완성, 괄호 자동 닫기
namespace PreferencesWindow
{
	void Open(const char* category = nullptr);
	void Draw();
	void Close();
	bool IsOpen();
}
