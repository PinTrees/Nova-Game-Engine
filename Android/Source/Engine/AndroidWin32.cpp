#include "pch.h"
#include "AndroidEngine.h"

// Win32 입력 · 문자열 변환 함수의 안드로이드 판 (엔진의 InputManager · 단축키 · Utils 가 그대로 쓴다)
namespace
{
	std::atomic<bool> s_Keys[256];
	std::atomic<int> s_X{ 0 }, s_Y{ 0 };
	std::atomic<bool> s_Focus{ true };
	std::string s_FilesDir;
	// 탭 하나가 프레임 사이에 끝나도 잃지 않게: 누른 적이 있으면 다음 프레임 한 번은 눌림
	std::mutex s_InputLock;
	bool s_PressedSinceFrame = false, s_LatchedDown = false;
	std::vector<NovaAndroid::PointerEvent> s_PointerEvents;
	struct Finger { int Id; float X, Y, PrevX, PrevY; bool Down, Began, Ended, Canceled; };
	std::vector<Finger> s_Fingers;
	int s_Window = 1;   // GetFocus 가 돌려주는 "창" (null 이 아니면 된다)
}

namespace NovaAndroid
{
	void SetFilesDir(const std::string& dir) { s_FilesDir = dir; }
	const std::string& FilesDir() { return s_FilesDir; }
	void SetKey(int vk, bool down) { s_Keys[vk & 255] = down; }
	void SetPointer(float x, float y, bool down)
	{
		std::lock_guard<std::mutex> g(s_InputLock);
		if (down && !s_Keys[VK_LBUTTON]) s_PressedSinceFrame = true;
		s_X = (int)x;
		s_Y = (int)y;
		s_Keys[VK_LBUTTON] = down;
		if (s_PointerEvents.size() < 256) s_PointerEvents.push_back({ x, y, down });
	}

	void TouchEvent(int id, float x, float y, int action)
	{
		std::lock_guard<std::mutex> g(s_InputLock);
		auto it = std::find_if(s_Fingers.begin(), s_Fingers.end(), [id](const Finger& f) { return f.Id == id && f.Down; });
		if (action == 0)
			s_Fingers.push_back({ id, x, y, x, y, true, true, false, false });
		else if (action == 3)
			for (Finger& f : s_Fingers) { f.Down = false; f.Ended = true; f.Canceled = true; }
		else if (it != s_Fingers.end())
		{
			it->X = x;
			it->Y = y;
			if (action == 2) { it->Down = false; it->Ended = true; }
		}
	}

	void BeginInputFrame()
	{
		std::vector<Touch> touches;
		{
			std::lock_guard<std::mutex> g(s_InputLock);
			s_LatchedDown = s_Keys[VK_LBUTTON] || s_PressedSinceFrame;
			s_PressedSinceFrame = false;
			for (Finger& f : s_Fingers)
			{
				Touch t;
				t.fingerId = f.Id;
				t.position = Vec2(f.X, f.Y);
				t.deltaPosition = Vec2(f.X - f.PrevX, f.Y - f.PrevY);
				if (f.Began) { t.phase = TouchPhase::Began; f.Began = false; }   // 이번 프레임에 뗐어도 Began 먼저, Ended 는 다음 프레임
				else if (f.Ended) { t.phase = f.Canceled ? TouchPhase::Canceled : TouchPhase::Ended; f.Ended = false; f.Id = -1; }
				else t.phase = (f.X != f.PrevX || f.Y != f.PrevY) ? TouchPhase::Moved : TouchPhase::Stationary;
				f.PrevX = f.X;
				f.PrevY = f.Y;
				touches.push_back(t);
			}
			s_Fingers.erase(std::remove_if(s_Fingers.begin(), s_Fingers.end(), [](const Finger& f) { return f.Id == -1; }), s_Fingers.end());
		}
		InputManager::GetI()->SetTouches(std::move(touches));
	}

	std::vector<PointerEvent> TakePointerEvents()
	{
		std::lock_guard<std::mutex> g(s_InputLock);
		std::vector<PointerEvent> out;
		out.swap(s_PointerEvents);
		return out;
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
	if (vk == VK_LBUTTON) return s_LatchedDown ? (SHORT)0x8000 : 0;   // 프레임마다 정해진 값 (BeginInputFrame)
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
