#pragma once
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <string>
#include <vector>

struct android_app;
struct ANativeWindow;
struct AInputEvent;
class EditorApp;

// 안드로이드 플랫폼 층: EGL (OpenGL ES 3.2 컨텍스트 하나, 창 표면은 생겼다 없어졌다), 생명 주기, 터치.
//  컨텍스트는 앱이 살아 있는 동안 유지한다 — 창이 없을 때는 작은 pbuffer 에 묶어 두어 GPU 자원이 사라지지 않는다
namespace AndroidPlatform
{
	struct Egl
	{
		EGLDisplay Display = EGL_NO_DISPLAY;
		EGLConfig Config = nullptr;
		EGLContext Context = EGL_NO_CONTEXT;
		EGLSurface Pbuffer = EGL_NO_SURFACE;
		EGLSurface Window = EGL_NO_SURFACE;

		bool Init(std::string& error);                          // 컨텍스트 + pbuffer 를 현재로
		bool AttachWindow(ANativeWindow* window, std::string& error);
		void DetachWindow();                                   // 창 표면만 지운다 (컨텍스트 유지)
		bool Swap();
		void WindowSize(int& width, int& height) const;
		~Egl();
	};

	// 터치 한 점 (창 화소 좌표, 왼쪽 위 = 0,0)
	struct Pointer
	{
		int32_t Id = 0;
		float X = 0, Y = 0;
	};

	// 플레이어 셸: 엔진 런타임이 올라가기 전의 바탕. 창이 있고 앱이 앞에 있을 때만 그린다
	//  지금 그리는 것: 배경, 누르고 있는 손가락마다 흰 네모, 마지막으로 뗀 자리에 주황 네모, 아래쪽에 프레임마다 움직이는 막대
	class Shell
	{
	public:
		Shell(android_app* app, const std::string& filesDir, bool engine);   // engine = 게임 데이터의 첫 씬을 엔진으로 (EditorApp — PC 플레이어와 같은 렌더 경로)
		~Shell();
		bool Running() const { return m_EglReady && m_HasWindow && m_Resumed; }
		void Frame();

	private:
		static void OnCommand(android_app* app, int32_t cmd);
		static int32_t OnInput(android_app* app, AInputEvent* event);
		void Command(int32_t cmd);
		int32_t Input(AInputEvent* event);
		void Event(const char* name, const char* fmt = nullptr, ...);
		void QuerySafeInsets();   // DisplayCutout → PlatformBindings (Screen.safeArea)
		bool CreateOverlay();
		void Rect(float x, float y, float w, float h, const float color[4]);

		android_app* m_App = nullptr;
		std::string m_FilesDir;
		Egl m_Egl;
		bool m_EglReady = false;
		bool m_HasWindow = false;
		bool m_Resumed = false;
		int m_Width = 0, m_Height = 0;
		uint64_t m_Frames = 0;
		std::vector<Pointer> m_Pointers;
		float m_TapX = -1, m_TapY = -1;
		int m_Taps = 0;
		GLuint m_Program = 0, m_Vao = 0, m_Vbo = 0;
		GLint m_ColorLoc = -1, m_ScreenLoc = -1;
		bool m_Engine = false;
		::EditorApp* m_GameApp = nullptr;   // 첫 창이 생길 때 만든다 (앱이 끝날 때까지)
	};
}
