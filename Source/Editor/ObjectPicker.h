#pragma once
#include <string>
#include <vector>
#include <functional>

// Unity 의 Object Picker ("Select AudioClip" 창): 오브젝트 필드의 ⊙ 를 누르면 뜨는 별도 창.
//  - 검색, Assets / Scene 탭, 아이콘 크기 슬라이더(맨 왼쪽 = 목록, 오른쪽 = 격자), 패키지 항목 숨기기(눈 + 숨긴 개수)
//  - None + 항목 목록, 아래쪽 미리보기(선택 이름·경로, 선택적으로 추가 정보)
//  - 한 번 클릭 = 바로 할당, 더블클릭 / Enter = 할당하고 닫기, Esc = 닫기
// 사용: 필드에서 ⊙ 가 눌리면 Open(...), 매 프레임 Poll(key, path) 로 바뀐 선택을 받아 적용한다.
namespace ObjectPicker
{
	struct Options
	{
		std::string TypeName;                 // 창 제목 "Select <TypeName>"
		const char* Icon = nullptr;           // 항목 아이콘 (ProjectSetting/icons/svg/png)
		std::vector<std::string> Items;       // 상대 경로 (Assets\... / Resources\Packages\...)
		std::string Current;                  // 현재 값 (빈 문자열 = None)
		std::function<std::string(const std::string&)> Describe;   // 미리보기에 덧붙일 정보 (선택)
		std::function<void(const std::string&)> Preview;           // 미리보기 영역의 ▶ 버튼 (선택, 예: 오디오 재생)
	};

	// key = 요청한 필드를 구분하는 값 (예: "clip:" + 컴포넌트 주소)
	void Open(const std::string& key, Options options);
	// key 의 선택이 바뀌었으면 true 와 새 경로(빈 문자열 = None)
	bool Poll(const std::string& key, std::string& outPath);
	bool IsOpenFor(const std::string& key);
	void Draw();   // 매 프레임 (EditorGUIManager)
}
