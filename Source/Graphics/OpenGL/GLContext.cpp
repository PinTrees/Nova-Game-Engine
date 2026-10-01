#include "pch.h"
#include "GLContext.h"
#include "GLLoader.h"
#include "GLState.h"

namespace GLContext
{
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
		const int attribs[] = { WGL_CONTEXT_MAJOR_VERSION_ARB, 4, WGL_CONTEXT_MINOR_VERSION_ARB, 5, WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
			WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB, 0 };
		out.Rc = createAttribs(out.Dc, nullptr, attribs);
		::wglMakeCurrent(nullptr, nullptr);
		::wglDeleteContext(temp);
		if (!out.Rc || !::wglMakeCurrent(out.Dc, out.Rc)) { error = "OpenGL 4.5 core context not available"; Destroy(out); return false; }
		std::string missing;
		if (!GLLoader::Load(missing)) { error = "OpenGL functions missing: " + missing; Destroy(out); return false; }
		GLState::InstallDebugOutput();
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

	void SetSwapInterval(int interval)
	{
		typedef BOOL(WINAPI * SwapInterval)(int);
		static auto fn = reinterpret_cast<SwapInterval>(::wglGetProcAddress("wglSwapIntervalEXT"));
		if (fn) fn(interval);
	}
}
