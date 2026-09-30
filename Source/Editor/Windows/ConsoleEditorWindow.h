#pragma once
#include "EditorWindow.h"

// Unity 의 Console 창.
//  - 툴바: Clear · Collapse · Clear on Play · Error Pause · Open Editor Log … 검색 · ⓘ/⚠/⛔ 개수(누르면 그 종류 숨기기/보이기)
//  - 목록: 종류 아이콘 + [시간] + 메시지 첫 줄 (Collapse 면 같은 메시지를 묶고 개수)
//  - 아래: 선택한 항목의 전체 메시지와 스택 (위아래 경계를 끌어 높이 조절)
//  - 더블클릭 = 코드 편집기에서 그 파일:줄 열기 (C# 로그/예외, 컴파일 오류)
class ConsoleEditorWindow
	: public EditorWindow
{
public:
	ConsoleEditorWindow();
	~ConsoleEditorWindow();

public:
	virtual void PushStyle() override;
	virtual void PopStyle() override;
	virtual void OnRender() override;

private:
	bool m_ShowLog = true, m_ShowWarning = true, m_ShowError = true;
	char m_Search[128] = {};
	int m_Selected = -1;          // 표시 목록에서의 순번
	uint64_t m_SeenVersion = 0;
	bool m_AutoScroll = true;
	float m_DetailHeight = 110.0f;
};
