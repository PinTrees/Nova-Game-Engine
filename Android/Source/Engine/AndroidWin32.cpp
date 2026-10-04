#include "pch.h"
#include "AndroidEngine.h"

// Win32 입력 · 문자열 변환 함수의 안드로이드 판 (엔진의 InputManager · 단축키 · Utils 가 그대로 쓴다)
namespace
{
	std::atomic<bool> s_Keys[256];
	std::atomic<int> s_X{ 0 }, s_Y{ 0 };
	std::atomic<bool> s_Focus{ true };
	std::string s_FilesDir;
	int s_Window = 1;   // GetFocus 가 돌려주는 "창" (null 이 아니면 된다)
}

namespace NovaAndroid
{
	void SetFilesDir(const std::string& dir) { s_FilesDir = dir; }
	const std::string& FilesDir() { return s_FilesDir; }
	void SetKey(int vk, bool down) { s_Keys[vk & 255] = down; }
	void SetPointer(float x, float y, bool down)
	{
		s_X = (int)x;
		s_Y = (int)y;
		s_Keys[VK_LBUTTON] = down;
	}
	void SetFocus(bool focused) { s_Focus = focused; }
	static std::atomic<bool> s_Quit{ false };
	void RequestQuit() { s_Quit = true; }
	bool QuitRequested() { return s_Quit; }
}

SHORT GetAsyncKeyState(int vk)
{
	if (vk == VK_SHIFT) return (s_Keys[VK_LSHIFT] || s_Keys[VK_RSHIFT] || s_Keys[VK_SHIFT]) ? (SHORT)0x8000 : 0;
	if (vk == VK_CONTROL) return (s_Keys[VK_LCONTROL] || s_Keys[VK_RCONTROL] || s_Keys[VK_CONTROL]) ? (SHORT)0x8000 : 0;
	if (vk == VK_MENU) return (s_Keys[VK_LMENU] || s_Keys[VK_RMENU] || s_Keys[VK_MENU]) ? (SHORT)0x8000 : 0;
	return s_Keys[vk & 255] ? (SHORT)0x8000 : 0;
}
SHORT GetKeyState(int vk) { return GetAsyncKeyState(vk); }
BOOL GetCursorPos(POINT* p) { p->x = s_X; p->y = s_Y; return TRUE; }
BOOL ScreenToClient(HWND, POINT*) { return TRUE; }   // 전체 화면 = 창
HWND GetFocus() { return s_Focus ? &s_Window : nullptr; }
HWND GetForegroundWindow() { return GetFocus(); }

// UTF-8 ↔ wchar_t (안드로이드 wchar_t = UTF-32)
int MultiByteToWideChar(UINT, DWORD, const char* src, int srcLen, wchar_t* dst, int dstLen)
{
	if (srcLen < 0) srcLen = (int)strlen(src) + 1;
	int n = 0;
	for (int i = 0; i < srcLen;)
	{
		const unsigned char c = (unsigned char)src[i];
		uint32_t cp;
		int len;
		if (c < 0x80) { cp = c; len = 1; }
		else if ((c >> 5) == 6) { cp = c & 0x1F; len = 2; }
		else if ((c >> 4) == 14) { cp = c & 0x0F; len = 3; }
		else if ((c >> 3) == 30) { cp = c & 0x07; len = 4; }
		else { cp = 0xFFFD; len = 1; }
		if (i + len > srcLen) { cp = 0xFFFD; len = srcLen - i; }
		else for (int k = 1; k < len; ++k) cp = (cp << 6) | ((unsigned char)src[i + k] & 0x3F);
		if (dst && dstLen) { if (n >= dstLen) return 0; dst[n] = (wchar_t)cp; }
		++n;
		i += len;
	}
	return n;
}

int WideCharToMultiByte(UINT, DWORD, const wchar_t* src, int srcLen, char* dst, int dstLen, const char*, BOOL*)
{
	if (srcLen < 0) srcLen = (int)wcslen(src) + 1;
	int n = 0;
	auto put = [&](char c) { if (dst && dstLen) { if (n < dstLen) dst[n] = c; } ++n; };
	for (int i = 0; i < srcLen; ++i)
	{
		const uint32_t cp = (uint32_t)src[i];
		if (cp < 0x80) put((char)cp);
		else if (cp < 0x800) { put((char)(0xC0 | (cp >> 6))); put((char)(0x80 | (cp & 0x3F))); }
		else if (cp < 0x10000) { put((char)(0xE0 | (cp >> 12))); put((char)(0x80 | ((cp >> 6) & 0x3F))); put((char)(0x80 | (cp & 0x3F))); }
		else { put((char)(0xF0 | (cp >> 18))); put((char)(0x80 | ((cp >> 12) & 0x3F))); put((char)(0x80 | ((cp >> 6) & 0x3F))); put((char)(0x80 | (cp & 0x3F))); }
	}
	if (dst && dstLen && n > dstLen) return 0;
	return n;
}
