#pragma once

// Unity 6 다크 테마 기준 색상/크기 상수 (에디터 UI 1:1 대조용).
// 값은 Unity 에디터 스크린샷(1920x1080, 100% DPI)에서 읽은 근사치이며 대조하면서 조정한다.
namespace EditorTheme
{
	constexpr int   FontSize          = 13;      // 본문 폰트 (Segoe UI 13px ≈ Unity 12px)
	constexpr int   FontSizeHeader    = 14;

	constexpr float MenuBarPaddingX   = 10.0f;
	constexpr float MenuBarPaddingY   = 4.0f;
	constexpr float ToolbarHeight     = 34.0f;

	inline ImVec4 Rgb(int r, int g, int b, int a = 255)
	{
		return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
	}

	inline ImVec4 Chrome()        { return Rgb(40, 40, 40); }     // 메뉴바/툴바/탭 스트립  #282828
	inline ImVec4 Panel()         { return Rgb(56, 56, 56); }     // 패널 본문            #383838
	inline ImVec4 PanelDark()     { return Rgb(48, 48, 48); }     // 리스트/입력 배경      #303030
	inline ImVec4 Separator()     { return Rgb(25, 25, 25); }     // 분리선               #191919
	inline ImVec4 Text()          { return Rgb(196, 196, 196); }  // 기본 글자            #C4C4C4
	inline ImVec4 TextDim()       { return Rgb(150, 150, 150); }
	inline ImVec4 Button()        { return Rgb(88, 88, 88); }     // 버튼                 #585858
	inline ImVec4 ButtonHover()   { return Rgb(103, 103, 103); }  //                      #676767
	inline ImVec4 Selection()     { return Rgb(44, 93, 135); }    // 선택(포커스) 행       #2C5D87
	inline ImVec4 SelectionIdle() { return Rgb(77, 77, 77); }     // 선택(비포커스) 행     #4D4D4D
	inline ImVec4 Accent()        { return Rgb(45, 127, 214); }   // 재생 중 버튼 등 강조
}
