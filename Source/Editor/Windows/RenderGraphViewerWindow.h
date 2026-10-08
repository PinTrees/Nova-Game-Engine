#pragma once
#include "EditorWindow.h"

// Unity 의 Render Graph Viewer (Window > Analysis > Render Graph Viewer): 뷰마다 마지막으로 실행한 Render Graph —
//  패스 (열) x 자원 (행) 표에 읽기 R · 쓰기 W, 빠진 패스 (아무도 결과를 읽지 않음) 는 흐리게, 패스마다 CPU 시간, 임시 텍스처 풀
class RenderGraphViewerWindow
	: public EditorWindow
{
public:
	RenderGraphViewerWindow();
	static void Toggle();
	static bool IsOpen();

protected:
	void BeforeBegin() override;
	void OnRender() override;

private:
	bool m_FocusNext = false;
	int m_Graph = 0;
};
