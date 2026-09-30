#pragma once

// Unity 6 다크 테마 기준 색상/크기 상수 (에디터 UI 1:1 대조용).
// 값은 Unity 에디터 스크린샷(1920x1080, 100% DPI)에서 읽은 근사치이며 대조하면서 조정한다.
namespace EditorTheme
{
	constexpr int   FontSize          = 14;      // 본문 폰트 크기(px). 행 높이 18px 안에서 정수 패딩이 나오도록 14 사용
	constexpr int   FontSizeHeader    = 15;

	constexpr float MenuBarPaddingX   = 10.0f;
	constexpr float MenuBarPaddingY   = 4.0f;
	constexpr float ToolbarHeight     = 34.0f;

	// 폰트 파일: ProjectSetting/fonts 에 Pretendard(Regular / SemiBold 또는 Bold)가 있으면 사용하고,
	// 없으면 Windows 의 Segoe UI 로 대체한다. (Pretendard 는 한글도 포함하므로 별도 병합이 필요 없음)
	inline std::string FontFile(bool bold, bool& isPretendard)
	{
		std::string dir = PathManager::GetI()->GetEnginePathS() + "ProjectSetting\\fonts\\";
		const char* boldNames[] = { "Pretendard-SemiBold.ttf", "Pretendard-Bold.ttf", "Pretendard-SemiBold.otf", "Pretendard-Bold.otf" };
		const char* regNames[] = { "Pretendard-Regular.ttf", "Pretendard-Regular.otf" };
		std::error_code ec;
		if (bold)
		{
			for (const char* n : boldNames)
				if (std::filesystem::exists(dir + n, ec)) { isPretendard = true; return dir + n; }
		}
		else
		{
			for (const char* n : regNames)
				if (std::filesystem::exists(dir + n, ec)) { isPretendard = true; return dir + n; }
		}
		isPretendard = false;
		return bold ? "C:\\Windows\\Fonts\\segoeuib.ttf" : "C:\\Windows\\Fonts\\segoeui.ttf";
	}

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
