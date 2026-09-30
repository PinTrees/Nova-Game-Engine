#pragma once
#include <string>
#include <vector>

// Unity 의 Preferences > External Tools > External Script Editor.
// 스크립트를 열 편집기를 고르고(EditorPrefs 에 저장), 그 편집기로 파일:줄을 연다.
//  - NOVA Code (built-in): 기본값. 에디터 안의 코드 편집기 창
//  - Visual Studio Code / Visual Studio (vswhere 로 찾음) / Rider: 설치되어 있으면 목록에 나온다
//  - Open by file extension: .cs 에 연결된 프로그램
//  - Browse...: 직접 고른 exe + 인자 템플릿 ($(File), $(Line), $(ProjectPath))
namespace ExternalScriptEditor
{
	enum class Kind { BuiltIn, VSCode, VisualStudio, Rider, SystemDefault, Custom };

	struct Editor
	{
		Kind Type = Kind::BuiltIn;
		std::string Name;      // 목록에 보일 이름 (예: "Visual Studio Community 2022 [17.9]")
		std::wstring Path;     // 실행 파일 (BuiltIn / SystemDefault 는 비어 있음)
	};

	// 설치된 편집기 목록 (처음 부를 때 찾고 저장해 둔다. refresh = 다시 찾기)
	const std::vector<Editor>& Installed(bool refresh = false);

	Editor Current();
	void SetCurrent(const Editor& editor);
	bool IsBuiltIn();

	// 직접 고른 편집기의 인자 템플릿 (Unity 의 External Script Editor Args)
	std::string CustomArgs();
	void SetCustomArgs(const std::string& args);
	const char* DefaultCustomArgs();

	// 고른 편집기로 연다 (line 은 1부터, 0 = 줄 지정 없음). 실패하면 기본 프로그램으로
	void Open(const std::wstring& file, int line);
}
