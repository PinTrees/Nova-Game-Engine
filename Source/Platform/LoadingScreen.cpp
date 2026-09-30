#include "pch.h"
#include "LoadingScreen.h"
#include "resource.h"
#include <thread>
#include <mutex>
#include <atomic>

namespace
{
	struct State
	{
		std::mutex Lock;
		std::wstring Subtitle;
		std::wstring Status = L"Starting...";
		float Target = 0.0f;       // 목표 진행률
		float Shown = 0.0f;        // 화면에 보이는 진행률 (부드럽게 따라간다)
		float PhaseFrom = 0.0f, PhaseTo = 0.0f;
		int PhaseExpected = 0, PhaseDone = 0;
		DWORD LastChange = 0;      // 마지막으로 상태가 바뀐 시각 (오래 걸리는 작업 표시용)
	};
	State s;
	std::thread s_Thread;
	std::atomic<bool> s_Active = false;
	std::atomic<HWND> s_Window = nullptr;

	constexpr int kWidth = 600, kHeight = 330;
	const COLORREF kBg = RGB(32, 32, 32), kPanel = RGB(40, 40, 40), kTrack = RGB(58, 58, 58), kFill = RGB(58, 121, 187);

	void Paint(HWND hwnd, HDC target)
	{
		RECT rc;
		::GetClientRect(hwnd, &rc);
		HDC dc = ::CreateCompatibleDC(target);
		HBITMAP bmp = ::CreateCompatibleBitmap(target, rc.right, rc.bottom);
		HGDIOBJ oldBmp = ::SelectObject(dc, bmp);

		std::wstring subtitle, status;
		float shown;
		DWORD since;
		{
			std::lock_guard<std::mutex> g(s.Lock);
			subtitle = s.Subtitle;
			status = s.Status;
			shown = s.Shown;
			since = ::GetTickCount() - s.LastChange;
		}

		auto fill = [&](int l, int t, int r, int b, COLORREF c) {
			HBRUSH br = ::CreateSolidBrush(c);
			RECT x = { l, t, r, b };
			::FillRect(dc, &x, br);
			::DeleteObject(br);
		};
		fill(0, 0, rc.right, rc.bottom, kBg);
		fill(0, 0, rc.right, 190, kPanel);
		// 테두리
		HBRUSH border = ::CreateSolidBrush(RGB(20, 20, 20));
		::FrameRect(dc, &rc, border);
		::DeleteObject(border);

		// 아이콘 + 제목
		static HICON icon = (HICON)::LoadImage(::GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_MAIN_ICON), IMAGE_ICON, 72, 72, 0);
		if (icon)
			::DrawIconEx(dc, 40, 58, icon, 72, 72, 0, nullptr, DI_NORMAL);
		::SetBkMode(dc, TRANSPARENT);
		auto font = [](int size, int weight) {
			return ::CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
		};
		HFONT title = font(30, FW_SEMIBOLD), sub = font(16, FW_NORMAL), smallFont = font(14, FW_NORMAL);
		HGDIOBJ oldFont = ::SelectObject(dc, title);
		::SetTextColor(dc, RGB(236, 236, 236));
		RECT t = { 132, 62, rc.right - 30, 102 };
		::DrawTextW(dc, L"NOVA Game Engine", -1, &t, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
		::SelectObject(dc, sub);
		::SetTextColor(dc, RGB(160, 160, 160));
		RECT st = { 134, 104, rc.right - 30, 128 };
		::DrawTextW(dc, subtitle.c_str(), -1, &st, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

		// 현재 작업
		::SelectObject(dc, smallFont);
		::SetTextColor(dc, RGB(205, 205, 205));
		RECT sr = { 40, 222, rc.right - 110, 244 };
		::DrawTextW(dc, status.c_str(), -1, &sr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
		wchar_t pct[16];
		swprintf_s(pct, L"%d%%", (int)(shown * 100.0f + 0.5f));
		RECT pr = { rc.right - 110, 222, rc.right - 40, 244 };
		::DrawTextW(dc, pct, -1, &pr, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

		// 진행 막대 (한 작업이 1초 넘게 걸리면 막대 위로 빛이 흐른다)
		const int bx0 = 40, bx1 = rc.right - 40, by0 = 252, by1 = 258;
		fill(bx0, by0, bx1, by1, kTrack);
		const int fx = bx0 + (int)((bx1 - bx0) * std::clamp(shown, 0.0f, 1.0f));
		fill(bx0, by0, fx, by1, kFill);
		if (since > 1000 && fx - bx0 > 20)
		{
			const int w = 70;
			const int pos = bx0 + (int)((::GetTickCount() / 4) % (DWORD)(fx - bx0 + w)) - w;
			for (int i = 0; i < w; ++i)
			{
				const int x = pos + i;
				if (x < bx0 || x >= fx) continue;
				const float k = 1.0f - fabsf(i - w * 0.5f) / (w * 0.5f);
				const int add = (int)(70 * k);
				fill(x, by0, x + 1, by1, RGB((std::min)(255, 58 + add), (std::min)(255, 121 + add), (std::min)(255, 187 + add)));
			}
		}

		::SetTextColor(dc, RGB(110, 110, 110));
		RECT fr = { 40, rc.bottom - 40, rc.right - 40, rc.bottom - 18 };
		::DrawTextW(dc, L"Loading editor...", -1, &fr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

		::SelectObject(dc, oldFont);
		::DeleteObject(title);
		::DeleteObject(sub);
		::DeleteObject(smallFont);
		::BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
		::SelectObject(dc, oldBmp);
		::DeleteObject(bmp);
		::DeleteDC(dc);
	}

	LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		switch (msg)
		{
		case WM_TIMER:
		{
			std::lock_guard<std::mutex> g(s.Lock);
			s.Shown += (s.Target - s.Shown) * 0.25f;   // 부드럽게
			if (fabsf(s.Target - s.Shown) < 0.001f) s.Shown = s.Target;
		}
			::InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC dc = ::BeginPaint(hwnd, &ps);
			Paint(hwnd, dc);
			::EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_ERASEBKGND:
			return 1;
		case WM_NCHITTEST:
			return HTCAPTION;   // 창을 끌어서 옮길 수 있게
		case WM_CLOSE:
			::DestroyWindow(hwnd);
			return 0;
		case WM_DESTROY:
			::PostQuitMessage(0);
			return 0;
		}
		return ::DefWindowProcW(hwnd, msg, wp, lp);
	}

	void ThreadMain()
	{
		HINSTANCE inst = ::GetModuleHandle(nullptr);
		WNDCLASSW wc = {};
		wc.lpfnWndProc = WndProc;
		wc.hInstance = inst;
		wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
		wc.hIcon = (HICON)::LoadImage(inst, MAKEINTRESOURCE(IDI_MAIN_ICON), IMAGE_ICON, 32, 32, 0);
		wc.lpszClassName = L"NovaLoadingScreen";
		::RegisterClassW(&wc);

		const int x = (::GetSystemMetrics(SM_CXSCREEN) - kWidth) / 2;
		const int y = (::GetSystemMetrics(SM_CYSCREEN) - kHeight) / 2;
		HWND hwnd = ::CreateWindowExW(WS_EX_APPWINDOW, L"NovaLoadingScreen", L"NOVA Game Engine", WS_POPUP, x, y, kWidth, kHeight, nullptr, nullptr, inst, nullptr);
		if (hwnd == nullptr)
			return;
		s_Window = hwnd;
		::ShowWindow(hwnd, SW_SHOW);
		::UpdateWindow(hwnd);
		::SetTimer(hwnd, 1, 30, nullptr);

		MSG msg;
		while (::GetMessageW(&msg, nullptr, 0, 0) > 0)
		{
			::TranslateMessage(&msg);
			::DispatchMessageW(&msg);
		}
		s_Window = nullptr;
	}
}

namespace LoadingScreen
{
	void Begin(const std::wstring& subtitle)
	{
		if (s_Active)
			return;
		{
			std::lock_guard<std::mutex> g(s.Lock);
			s.Subtitle = subtitle;
			s.Status = L"Starting...";
			s.Target = s.Shown = 0.0f;
			s.LastChange = ::GetTickCount();
		}
		s_Active = true;
		s_Thread = std::thread(ThreadMain);
	}

	void SetSubtitle(const std::wstring& subtitle)
	{
		std::lock_guard<std::mutex> g(s.Lock);
		s.Subtitle = subtitle;
	}

	void End()
	{
		if (!s_Active)
			return;
		SetProgress(1.0f, L"Ready");
		if (HWND hwnd = s_Window.load())
			::PostMessageW(hwnd, WM_CLOSE, 0, 0);
		if (s_Thread.joinable())
			s_Thread.join();
		s_Active = false;
	}

	bool IsActive() { return s_Active; }

	void SetProgress(float progress, const std::wstring& status)
	{
		EditorLog::Write("Startup", "%3d%% %s", (int)(progress * 100.0f + 0.5f), wstring_to_string(status).c_str());
		if (!s_Active)
			return;
		std::lock_guard<std::mutex> g(s.Lock);
		s.Target = (std::max)(s.Target, std::clamp(progress, 0.0f, 1.0f));
		s.Status = status;
		s.LastChange = ::GetTickCount();
	}

	void SetStatus(const std::wstring& status)
	{
		EditorLog::Write("Startup", "%s", wstring_to_string(status).c_str());
		if (!s_Active)
			return;
		std::lock_guard<std::mutex> g(s.Lock);
		s.Status = status;
		s.LastChange = ::GetTickCount();
	}

	void BeginShaderPhase(float from, float to, int expected)
	{
		std::lock_guard<std::mutex> g(s.Lock);
		s.PhaseFrom = from;
		s.PhaseTo = to;
		s.PhaseExpected = (std::max)(1, expected);
		s.PhaseDone = 0;
	}

	void OnShader(const std::wstring& fileName, bool fromCache)
	{
		if (!s_Active)
			return;
		std::lock_guard<std::mutex> g(s.Lock);
		// 이 셰이더를 시작할 때 부른다: 막대는 끝난 개수만큼
		const float progress = s.PhaseFrom + (s.PhaseTo - s.PhaseFrom) * (float)(std::min)(s.PhaseDone, s.PhaseExpected) / s.PhaseExpected;
		++s.PhaseDone;
		s.Target = (std::max)(s.Target, progress);
		wchar_t buf[256];
		swprintf_s(buf, L"%s shaders (%d/%d): %s", fromCache ? L"Loading" : L"Compiling", (std::min)(s.PhaseDone, s.PhaseExpected), s.PhaseExpected,
			std::filesystem::path(fileName).filename().c_str());
		s.Status = buf;
		s.LastChange = ::GetTickCount();
	}
}
