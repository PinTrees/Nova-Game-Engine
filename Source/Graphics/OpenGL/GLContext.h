#pragma once
#include <string>
#include <windows.h>

// OpenGL 4.5 core 컨텍스트 만들기 (WGL). 만든 스레드에서 현재로 두고, 함수 로더·디버그 출력·좌표 규칙·D3D 기본 상태까지 맞춘다.
//  window = nullptr 이면 숨은 창을 만든다 (검사·도구용), 아니면 그 창에 (에디터 본 창)
namespace GLContext
{
	struct Handle
	{
		HWND Wnd = nullptr;
		bool OwnWindow = false;
		HDC Dc = nullptr;
		HGLRC Rc = nullptr;
	};

	bool Create(HWND window, Handle& out, std::string& error);
	void Destroy(Handle& h);
	bool MakeCurrent(const Handle& h);
	void SetSwapInterval(int interval);   // 0 = 수직 동기 없음, 1 = 있음 (wglSwapIntervalEXT)
}
