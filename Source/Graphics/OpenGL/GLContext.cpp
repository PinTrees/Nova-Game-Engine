#include "pch.h"
#include "GLContext.h"
#include "GLLoader.h"
#include "GLState.h"

namespace GLContext
{
	// 디버그 컨텍스트(드라이버가 호출마다 검사 + 디버그 출력을 부른 자리에서 바로)는 D3D11 디버그 레이어처럼 Debug 빌드에서만.
	// Release 에서도 켜져 있어 GL 의 CPU 시간이 DX11 보다 크게 나왔다 (그리기 호출 하나 약 11 µs). NOVA_GL_DEBUG=1 이면 Release 도 켠다
	bool WantDebugContext()
	{
#if defined(_DEBUG) || defined(DEBUG)
		return true;
#else
		static const bool s_On = ::GetEnvironmentVariableA("NOVA_GL_DEBUG", nullptr, 0) > 0;
		return s_On;
#endif
	}

	bool Create(HWND window, Handle& out, std::string& error)
	{
		out = Handle();
		HINSTANCE inst = ::GetModuleHandleW(nullptr);
		if (!window)
		{
			static const wchar_t* cls = L"NovaGLHiddenWindow";
			static bool registered = false;
			if (!registered)
			{
				WNDCLASSW wc = {};
				wc.style = CS_OWNDC;
				wc.lpfnWndProc = ::DefWindowProcW;
				wc.hInstance = inst;
				wc.lpszClassName = cls;
				::RegisterClassW(&wc);
				registered = true;
			}
			window = ::CreateWindowExW(0, cls, L"NOVA OpenGL", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, inst, nullptr);
			if (!window) { error = "cannot create the OpenGL window"; return false; }
			out.OwnWindow = true;
		}
		out.Wnd = window;
		out.Dc = ::GetDC(window);
		PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1 };
		pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
		pfd.iPixelType = PFD_TYPE_RGBA;
		pfd.cColorBits = 32;
		pfd.cDepthBits = 24;
		pfd.cStencilBits = 8;
		const int pf = ::ChoosePixelFormat(out.Dc, &pfd);
		if (!pf || !::SetPixelFormat(out.Dc, pf, &pfd)) { error = "no OpenGL pixel format"; Destroy(out); return false; }
		HGLRC temp = ::wglCreateContext(out.Dc);
		if (!temp || !::wglMakeCurrent(out.Dc, temp)) { error = "wglCreateContext failed (no OpenGL driver?)"; if (temp) ::wglDeleteContext(temp); Destroy(out); return false; }
		typedef HGLRC(WINAPI * CreateAttribs)(HDC, HGLRC, const int*);
		auto createAttribs = reinterpret_cast<CreateAttribs>(::wglGetProcAddress("wglCreateContextAttribsARB"));
		if (!createAttribs)
		{
			::wglMakeCurrent(nullptr, nullptr);
			::wglDeleteContext(temp);
			error = "WGL_ARB_create_context not supported";
			Destroy(out);
			return false;
		}
		const bool debug = WantDebugContext();
		const int attribs[] = { WGL_CONTEXT_MAJOR_VERSION_ARB, 4, WGL_CONTEXT_MINOR_VERSION_ARB, 5, WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
			WGL_CONTEXT_FLAGS_ARB, debug ? WGL_CONTEXT_DEBUG_BIT_ARB : 0, 0 };
		out.Rc = createAttribs(out.Dc, nullptr, attribs);
		::wglMakeCurrent(nullptr, nullptr);
		::wglDeleteContext(temp);
		if (!out.Rc || !::wglMakeCurrent(out.Dc, out.Rc)) { error = "OpenGL 4.5 core context not available"; Destroy(out); return false; }
		std::string missing;
		if (!GLLoader::Load(missing)) { error = "OpenGL functions missing: " + missing; Destroy(out); return false; }
		GLState::InstallDebugOutput(debug);   // Release: 오류는 그대로 받되 비동기 (드라이버가 기다리지 않게)
		glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);   // + 셰이더의 -fvk-invert-y = D3D 와 같은 깊이 범위·텍스처 행 순서
		GLState::ApplyDefaults();
		return true;
	}

	void Destroy(Handle& h)
	{
		if (h.Rc)
		{
			if (::wglGetCurrentContext() == h.Rc)
				::wglMakeCurrent(nullptr, nullptr);
			::wglDeleteContext(h.Rc);
		}
		if (h.Dc && h.Wnd) ::ReleaseDC(h.Wnd, h.Dc);
		if (h.OwnWindow && h.Wnd) ::DestroyWindow(h.Wnd);
		h = Handle();
	}

	bool MakeCurrent(const Handle& h)
	{
		return h.Rc && ::wglMakeCurrent(h.Dc, h.Rc);
	}

	KeepCurrent::KeepCurrent() : Dc(::wglGetCurrentDC()), Rc(::wglGetCurrentContext()) {}
	KeepCurrent::~KeepCurrent()
	{
		if (Rc && ::wglGetCurrentContext() != Rc)
			::wglMakeCurrent(Dc, Rc);
	}

	void SetSwapInterval(int interval)
	{
		typedef BOOL(WINAPI * SwapInterval)(int);
		static auto fn = reinterpret_cast<SwapInterval>(::wglGetProcAddress("wglSwapIntervalEXT"));
		if (fn) fn(interval);
	}
}
