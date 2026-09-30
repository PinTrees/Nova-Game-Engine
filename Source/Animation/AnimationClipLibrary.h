#pragma once
#include <string>
#include <vector>

// 엔진 패키지(Resources/Packages)와 프로젝트 Assets 의 FBX 안에 있는 애니메이션 클립 목록.
// 클립 선택 팝업에서 쓴다 (처음 열 때 한 번 훑고, Refresh 로 다시 훑는다).
namespace AnimationClipLibrary
{
	struct Entry
	{
		std::string Path;    // FBX 경로 ("Resources\\Packages\\..." 또는 "Assets\\...")
		int Index = 0;       // FBX 안의 클립 번호
		std::string Name;    // 클립 이름
	};

	const std::vector<Entry>& Entries();
	void Refresh();

	// ImGui 팝업(BeginPopup 은 호출자가 OpenPopup 으로 연다). 클립을 고르면 true.
	// allowNone 이면 "None" 항목(경로를 비움)을 보여 준다.
	bool DrawPickerPopup(const char* popupId, std::string& path, int& index, bool allowNone = true);
}
