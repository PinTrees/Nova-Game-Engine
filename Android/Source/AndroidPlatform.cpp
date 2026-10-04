#include "pch.h"
#include "AndroidPlatform.h"
#include "AndroidEngine.h"
#include <android_native_app_glue.h>
#include <android/log.h>
#include <cstdarg>

namespace AndroidPlatform
{
	// ---- EGL
	bool Egl::Init(std::string& error)
	{
		Display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
		EGLint major = 0, minor = 0;
		if (!eglInitialize(Display, &major, &minor)) { error = "eglInitialize failed"; return false; }
		const EGLint cfgAttr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT | EGL_WINDOW_BIT,
			EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE };
		EGLint n = 0;
		if (!eglChooseConfig(Display, cfgAttr, &Config, 1, &n) || n == 0) { error = "no ES3 window/pbuffer config"; return false; }
		const EGLint ctxAttr[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE };
		Context = eglCreateContext(Display, Config, EGL_NO_CONTEXT, ctxAttr);
		if (Context == EGL_NO_CONTEXT) { error = "OpenGL ES 3.2 context failed"; return false; }
		const EGLint pb[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
		Pbuffer = eglCreatePbufferSurface(Display, Config, pb);
		if (!eglMakeCurrent(Display, Pbuffer, Pbuffer, Context)) { error = "eglMakeCurrent failed"; return false; }
		return true;
	}

	bool Egl::AttachWindow(ANativeWindow* window, std::string& error)
	{
		DetachWindow();
		EGLint format = 0;
		eglGetConfigAttrib(Display, Config, EGL_NATIVE_VISUAL_ID, &format);
		ANativeWindow_setBuffersGeometry(window, 0, 0, format);
		Window = eglCreateWindowSurface(Display, Config, window, nullptr);
		if (Window == EGL_NO_SURFACE) { error = "eglCreateWindowSurface failed"; return false; }
		if (!eglMakeCurrent(Display, Window, Window, Context)) { error = "eglMakeCurrent(window) failed"; DetachWindow(); return false; }
		eglSwapInterval(Display, 1);
		return true;
	}

	void Egl::DetachWindow()
	{
		if (Window == EGL_NO_SURFACE) return;
		eglMakeCurrent(Display, Pbuffer, Pbuffer, Context);
		eglDestroySurface(Display, Window);
		Window = EGL_NO_SURFACE;
	}

	bool Egl::Swap()
	{
		return Window != EGL_NO_SURFACE && eglSwapBuffers(Display, Window);
	}

	void Egl::WindowSize(int& width, int& height) const
	{
		EGLint w = 0, h = 0;
		if (Window != EGL_NO_SURFACE)
		{
			eglQuerySurface(Display, Window, EGL_WIDTH, &w);
			eglQuerySurface(Display, Window, EGL_HEIGHT, &h);
		}
		width = w;
		height = h;
	}

	Egl::~Egl()
	{
		if (Display == EGL_NO_DISPLAY) return;
		eglMakeCurrent(Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (Window != EGL_NO_SURFACE) eglDestroySurface(Display, Window);
		if (Pbuffer != EGL_NO_SURFACE) eglDestroySurface(Display, Pbuffer);
		if (Context != EGL_NO_CONTEXT) eglDestroyContext(Display, Context);
		eglTerminate(Display);
	}

	// ---- 플레이어 셸
	Shell::Shell(android_app* app, const std::string& filesDir) : m_App(app), m_FilesDir(filesDir)
	{
		app->userData = this;
		app->onAppCmd = OnCommand;
		app->onInputEvent = OnInput;
		std::string error;
		m_EglReady = m_Egl.Init(error) && CreateOverlay();
		if (m_EglReady)
			Event("start", "\"gl\":\"%s\"", (const char*)glGetString(GL_VERSION));
		else
		{
			Event("error", "\"error\":\"%s\"", error.c_str());
			ANativeActivity_finish(app->activity);
		}
	}

	Shell::~Shell()
	{
		if (m_EglReady)
		{
			glDeleteProgram(m_Program);
			glDeleteBuffers(1, &m_Vbo);
			glDeleteVertexArrays(1, &m_Vao);
		}
		m_App->userData = nullptr;
		m_App->onAppCmd = nullptr;
		m_App->onInputEvent = nullptr;
	}

	// logcat "NOVA_EVENT {"event":"이름", ...}" — 검사가 이 줄로 상태를 읽는다
	void Shell::Event(const char* name, const char* fmt, ...)
	{
		char extra[512] = "";
		if (fmt)
		{
			va_list ap;
			va_start(ap, fmt);
			vsnprintf(extra, sizeof(extra), fmt, ap);
			va_end(ap);
		}
		__android_log_print(ANDROID_LOG_INFO, "NOVA", "NOVA_EVENT {\"event\":\"%s\",\"frames\":%llu,\"width\":%d,\"height\":%d%s%s}",
			name, (unsigned long long)m_Frames, m_Width, m_Height, extra[0] ? "," : "", extra);
	}

	void Shell::OnCommand(android_app* app, int32_t cmd)
	{
		if (app->userData) static_cast<Shell*>(app->userData)->Command(cmd);
	}

	int32_t Shell::OnInput(android_app* app, AInputEvent* event)
	{
		return app->userData ? static_cast<Shell*>(app->userData)->Input(event) : 0;
	}

	void Shell::Command(int32_t cmd)
	{
		switch (cmd)
		{
		case APP_CMD_INIT_WINDOW:
			if (m_EglReady && m_App->window)
			{
				std::string error;
				m_HasWindow = m_Egl.AttachWindow(m_App->window, error);
				m_Egl.WindowSize(m_Width, m_Height);
				if (m_HasWindow) Event("window");
				else Event("error", "\"error\":\"%s\"", error.c_str());
			}
			break;
		case APP_CMD_TERM_WINDOW:
			m_Egl.DetachWindow();
			m_HasWindow = false;
			m_Pointers.clear();
			Event("window-lost");
			break;
		case APP_CMD_RESUME: m_Resumed = true; Event("resume"); break;
		case APP_CMD_PAUSE: m_Resumed = false; Event("pause"); break;
		case APP_CMD_GAINED_FOCUS: NovaAndroid::SetFocus(true); Event("focus"); break;
		case APP_CMD_LOST_FOCUS: m_Pointers.clear(); NovaAndroid::SetFocus(false); NovaAndroid::SetPointer(0, 0, false); Event("focus-lost"); break;
		case APP_CMD_CONFIG_CHANGED: Event("config"); break;
		case APP_CMD_DESTROY: Event("destroy"); break;
		default: break;
		}
	}

	int32_t Shell::Input(AInputEvent* event)
	{
		if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION)
			return 0;   // 키 (뒤로 가기 등) 는 시스템 기본 동작
		const int32_t action = AMotionEvent_getAction(event);
		const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
		const size_t index = (size_t)((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
		auto find = [&](int32_t id) { return std::find_if(m_Pointers.begin(), m_Pointers.end(), [id](const Pointer& p) { return p.Id == id; }); };
		switch (masked)
		{
		case AMOTION_EVENT_ACTION_DOWN:
		case AMOTION_EVENT_ACTION_POINTER_DOWN:
		{
			Pointer p{ AMotionEvent_getPointerId(event, index), AMotionEvent_getX(event, index), AMotionEvent_getY(event, index) };
			m_Pointers.push_back(p);
			if (m_Pointers.size() == 1) NovaAndroid::SetPointer(p.X, p.Y, true);   // 첫 손가락 = 마우스 왼쪽 (엔진 Input)
			Event("touch-down", "\"id\":%d,\"x\":%.0f,\"y\":%.0f,\"pointers\":%d", p.Id, p.X, p.Y, (int)m_Pointers.size());
			break;
		}
		case AMOTION_EVENT_ACTION_MOVE:
			for (size_t i = 0; i < AMotionEvent_getPointerCount(event); ++i)
			{
				auto it = find(AMotionEvent_getPointerId(event, i));
				if (it != m_Pointers.end()) { it->X = AMotionEvent_getX(event, i); it->Y = AMotionEvent_getY(event, i); }
			}
			if (!m_Pointers.empty()) NovaAndroid::SetPointer(m_Pointers[0].X, m_Pointers[0].Y, true);
			break;
		case AMOTION_EVENT_ACTION_UP:
		case AMOTION_EVENT_ACTION_POINTER_UP:
		{
			const int32_t id = AMotionEvent_getPointerId(event, index);
			m_TapX = AMotionEvent_getX(event, index);
			m_TapY = AMotionEvent_getY(event, index);
			++m_Taps;
			auto it = find(id);
			if (it != m_Pointers.end()) m_Pointers.erase(it);
			if (m_Pointers.empty()) NovaAndroid::SetPointer(m_TapX, m_TapY, false);
			else NovaAndroid::SetPointer(m_Pointers[0].X, m_Pointers[0].Y, true);
			Event("touch-up", "\"id\":%d,\"x\":%.0f,\"y\":%.0f,\"taps\":%d", id, m_TapX, m_TapY, m_Taps);
			break;
		}
		case AMOTION_EVENT_ACTION_CANCEL:
			m_Pointers.clear();
			NovaAndroid::SetPointer(0, 0, false);
			Event("touch-cancel");
			break;
		default: break;
		}
		return 1;
	}

	bool Shell::CreateOverlay()
	{
		const char* vs = "#version 300 es\nlayout(location = 0) in vec2 aPos;\nuniform vec2 uScreen;\n"
			"void main() { vec2 p = aPos / uScreen * 2.0 - 1.0; gl_Position = vec4(p.x, -p.y, 0.0, 1.0); }\n";
		const char* fs = "#version 300 es\nprecision mediump float;\nuniform vec4 uColor;\nout vec4 oColor;\nvoid main() { oColor = uColor; }\n";
		auto compile = [](GLenum type, const char* src) {
			GLuint s = glCreateShader(type);
			glShaderSource(s, 1, &src, nullptr);
			glCompileShader(s);
			GLint ok = 0;
			glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
			return ok ? s : 0u;
		};
		const GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
		if (!v || !f) return false;
		m_Program = glCreateProgram();
		glAttachShader(m_Program, v);
		glAttachShader(m_Program, f);
		glLinkProgram(m_Program);
		glDeleteShader(v);
		glDeleteShader(f);
		GLint linked = 0;
		glGetProgramiv(m_Program, GL_LINK_STATUS, &linked);
		if (!linked) return false;
		m_ColorLoc = glGetUniformLocation(m_Program, "uColor");
		m_ScreenLoc = glGetUniformLocation(m_Program, "uScreen");
		glGenVertexArrays(1, &m_Vao);
		glGenBuffers(1, &m_Vbo);
		glBindVertexArray(m_Vao);
		glBindBuffer(GL_ARRAY_BUFFER, m_Vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 12, nullptr, GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
		glBindVertexArray(0);
		return true;
	}

	void Shell::Rect(float x, float y, float w, float h, const float color[4])
	{
		const float v[12] = { x, y, x + w, y, x, y + h, x + w, y, x + w, y + h, x, y + h };
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
		glUniform4fv(m_ColorLoc, 1, color);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}

	void Shell::Frame()
	{
		if (!Running()) return;
		int w = 0, h = 0;
		m_Egl.WindowSize(w, h);
		if (w != m_Width || h != m_Height)
		{
			m_Width = w;
			m_Height = h;
			Event("resize");
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, m_Width, m_Height);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		glClearColor(0.08f, 0.12f, 0.22f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		glUseProgram(m_Program);
		glUniform2f(m_ScreenLoc, (float)m_Width, (float)m_Height);
		glBindVertexArray(m_Vao);
		glBindBuffer(GL_ARRAY_BUFFER, m_Vbo);
		const float orange[4] = { 1.0f, 0.5f, 0.0f, 1.0f }, white[4] = { 1, 1, 1, 1 }, green[4] = { 0.2f, 0.9f, 0.3f, 1.0f };
		if (m_Taps > 0) Rect(m_TapX - 40, m_TapY - 40, 80, 80, orange);
		for (const Pointer& p : m_Pointers) Rect(p.X - 60, p.Y - 60, 120, 120, white);
		// 루프가 도는지: 2 초에 한 번 왼쪽에서 오른쪽으로 지나가는 막대 (아래 40 px)
		const float t = (float)(m_Frames % 120) / 120.0f;
		Rect(t * (m_Width - 60), (float)m_Height - 40, 60, 40, green);
		glBindVertexArray(0);
		if (!m_Egl.Swap())
		{
			Event("error", "\"error\":\"eglSwapBuffers 0x%x\"", eglGetError());
			return;
		}
		if (++m_Frames % 300 == 0) Event("frame");
	}
}
