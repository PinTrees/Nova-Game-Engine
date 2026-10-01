#pragma once
#include "EditorWindow.h"

// Unity 의 Package Manager 창 (Window > Package Manager).
//  - 위 막대: 보기 (In Project / NOVA Registry), 검색, 새로 고침
//  - 왼쪽: 패키지 목록 (표시 이름 + 버전, 프로젝트에 들어간 것은 ✓)
//  - 오른쪽: 고른 패키지 정보 (이름 · 버전 · 패키지 이름 · 만든 이 · 설명 · 컴포넌트 · 상태) + Install / Remove
//  - 아래: 다시 시작해야 빠지는 패키지가 있으면 안내
class PackageManagerWindow
	: public EditorWindow
{
public:
	PackageManagerWindow();

	static void Open(const char* select = nullptr);
	static void Close();
	static bool IsOpen();

protected:
	void BeforeBegin() override;
	void PushStyle() override;
	void PopStyle() override;
	void OnRender() override;

private:
	static PackageManagerWindow* s_Instance;
	bool m_FocusNext = false;
	int m_View = 1;              // 0 In Project, 1 NOVA Registry (처음엔 고를 수 있는 전체 목록)
	char m_Search[128] = {};
	std::string m_Selected;      // 패키지 이름
	std::string m_LastError;     // 마지막 Install/Remove 실패
};
