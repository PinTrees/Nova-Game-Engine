#pragma once
#include "EditorWindow.h"

// Unity 의 Rendering Debugger (Window > Analysis > Rendering Debugger): Scene · Game 뷰를 깊이 · 노멀 · SSAO · 모션 벡터 · APV 로 바꿔 본다.
//  값은 RenderingDebug (Source/Graphics/DX11/RenderingDebug.h) — Scene 뷰 툴바 Debug 드롭다운 · CLI nova debugview 와 같은 것
class RenderingDebuggerWindow
	: public EditorWindow
{
public:
	RenderingDebuggerWindow();
	static void Toggle();   // Window > Analysis > Rendering Debugger
	static bool IsOpen();

protected:
	void BeforeBegin() override;
	void OnRender() override;

private:
	bool m_FocusNext = false;
};
