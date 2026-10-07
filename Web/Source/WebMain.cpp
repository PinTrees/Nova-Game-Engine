#include "pch.h"
#include "EditorApp.h"
#include "AndroidEngine.h"
#include "PlayerRuntime.h"
#include "GfxWgpu.h"
#include "GfxWgpuInternal.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <typeinfo>

// 웹 플레이어 진입점: 게임 데이터 = 메모리 파일 시스템의 /game (페이지가 game.data 를 먼저 불러 둔다 — Web/Shell),
//  캔버스 #canvas (CSS 크기 × 기기 픽셀 비율), 입력 = 브라우저 이벤트 → 안드로이드와 같은 입력 상태 (GetAsyncKeyState · 커서 · 터치),
//  프레임 = requestAnimationFrame (emscripten_set_main_loop) → App::Run 한 번 + Present
namespace
{
	EditorApp* s_App = nullptr;
	int s_Width = 0, s_Height = 0;
	double s_Ratio = 1.0;
	uint64_t s_Frames = 0;
	bool s_Failed = false;

	void Resize()
	{
		double cssW = 0, cssH = 0;
		emscripten_get_element_css_size("#canvas", &cssW, &cssH);
		s_Ratio = emscripten_get_device_pixel_ratio();
		const int w = (std::max)(1, (int)(cssW * s_Ratio + 0.5)), h = (std::max)(1, (int)(cssH * s_Ratio + 0.5));
		if (w == s_Width && h == s_Height) return;
		s_Width = w;
		s_Height = h;
		emscripten_set_canvas_element_size("#canvas", w, h);
		if (s_App)
		{
			s_App->SetScreenSize((UINT)w, (UINT)h);
			s_App->OnResize();
		}
	}

	// 엔진 안에서 던진 C++ 예외: 브라우저에는 "[object WebAssembly.Exception]" 만 보이므로 종류 · 내용을 로그에 남기고 멈춘다
	void Fail(const char* where, const char* type, const char* what)
	{
		EditorLog::Write("Web", "exception in %s: %s: %s", where, type, what);
		s_Failed = true;
	}

	void FrameBody()
	{
		Resize();
		s_App->Run();
		GfxWgpu::Present(Gfx::Device(), s_App->BackBufferTexture(), s_Width, s_Height);
	}

	void Frame()
	{
		if (!s_App || s_Failed) return;
		try { FrameBody(); }
		catch (const std::exception& e) { Fail("frame", typeid(e).name(), e.what()); return; }
		catch (...) { Fail("frame", "unknown", ""); return; }
		if (++s_Frames == 1)
			EditorLog::Write("Web", "first frame %d x %d", s_Width, s_Height);
		if (NovaAndroid::QuitRequested())
		{
			emscripten_cancel_main_loop();
			EM_ASM({ if (window.novaState) window.novaState.phase = 'quit'; });   // Application.Quit (페이지 제목은 게임 것 — 건드리지 않는다)
		}
	}

	// DOM keyCode (대부분 Windows 가상 키와 같다 — 글자 · 숫자 · 화살표 · F1 … · Space · Enter · Shift · Ctrl · Alt)
	EM_BOOL OnKey(int type, const EmscriptenKeyboardEvent* e, void*)
	{
		const int vk = (int)e->keyCode;
		if (vk > 0 && vk < 256)
			NovaAndroid::SetKey(vk, type == EMSCRIPTEN_EVENT_KEYDOWN);
		// 브라우저 단축키 (F5 · Ctrl+R · F12) 는 그대로, 게임 키 (화살표 · Space · Tab) 는 페이지 스크롤을 막는다
		return vk == 32 || (vk >= 37 && vk <= 40) || vk == 9;
	}

	EM_BOOL OnMouse(int type, const EmscriptenMouseEvent* e, void*)
	{
		const float x = (float)(e->targetX * s_Ratio), y = (float)(e->targetY * s_Ratio);
		static bool down = false;
		if (type == EMSCRIPTEN_EVENT_MOUSEDOWN && e->button == 0) down = true;
		if (type == EMSCRIPTEN_EVENT_MOUSEUP && e->button == 0) down = false;
		NovaAndroid::SetPointer(x, y, down);
		if (e->button == 2) NovaAndroid::SetKey(VK_RBUTTON, type == EMSCRIPTEN_EVENT_MOUSEDOWN);
		if (e->button == 1) NovaAndroid::SetKey(VK_MBUTTON, type == EMSCRIPTEN_EVENT_MOUSEDOWN);
		return type != EMSCRIPTEN_EVENT_MOUSEMOVE;
	}

	EM_BOOL OnTouch(int type, const EmscriptenTouchEvent* e, void*)
	{
		const int action = type == EMSCRIPTEN_EVENT_TOUCHSTART ? 0 : type == EMSCRIPTEN_EVENT_TOUCHMOVE ? 1 : type == EMSCRIPTEN_EVENT_TOUCHEND ? 2 : 3;
		for (int i = 0; i < e->numTouches; ++i)
		{
			const EmscriptenTouchPoint& t = e->touches[i];
			if (!t.isChanged) continue;
			NovaAndroid::TouchEvent((int)t.identifier, (float)(t.targetX * s_Ratio), (float)(t.targetY * s_Ratio), action);
		}
		return EM_TRUE;
	}

	EM_BOOL OnFocus(int type, const EmscriptenFocusEvent*, void*)
	{
		NovaAndroid::SetFocus(type == EMSCRIPTEN_EVENT_FOCUS);
		return EM_FALSE;
	}
}

// 검사 (Tools/web/headless.mjs · 웹 스위트): 지난 프레임 수 · 캔버스 크기 · 실패
extern "C" EMSCRIPTEN_KEEPALIVE int nova_web_frames() { return (int)s_Frames; }
extern "C" EMSCRIPTEN_KEEPALIVE int nova_web_failed() { return s_Failed ? 1 : 0; }
// 지난 프레임의 그래픽 수 (JSON — 원인 모를 빈 화면 진단)
extern "C" EMSCRIPTEN_KEEPALIVE const char* nova_web_stats()
{
	static std::string s;
	const GfxWgpuImpl::Stats& st = GfxWgpuImpl::LastStats;
	char buf[512];
	snprintf(buf, sizeof(buf), "{\"draws\":%u,\"dispatches\":%u,\"passes\":%u,\"clears\":%u,\"skipNoProgram\":%u,\"skipNoTarget\":%u,\"skipNoPipeline\":%u,\"skipNoIndex\":%u,\"newPipelines\":%u,\"newGroups\":%u,\"calls\":%u,\"skipNoVs\":%u,\"skipDynIndex\":%u}",
		st.Draws, st.Dispatches, st.Passes, st.Clears, st.SkipNoProgram, st.SkipNoTarget, st.SkipNoPipeline, st.SkipNoIndex, st.NewPipelines, st.NewGroups, st.Calls, st.SkipNoVs, st.SkipDynIndex);
	s = buf;
	return s.c_str();
}

// 엔진 시작: 게임 데이터 확인 → 입력 연결 → 엔진 초기화 → 브라우저 프레임 루프 (돌아온 뒤에도 계속 — requestAnimationFrame). 1 = 시작함
//  C# 이 있는 빌드 (Web/Host, NOVA_WEB_DOTNET) 는 .NET 런타임이 먼저 뜨고 C# 의 Main 이 이것을 부른다 (DllImport "NovaWeb")
extern "C" EMSCRIPTEN_KEEPALIVE int nova_web_start()
{
	NovaAndroid::SetFilesDir("/");   // 게임 데이터 = /game (파일 폴더 = 루트 — 빈 문자열이면 game/ 이 상대 경로가 된다)
	EditorLog::Init();
	PathManager::GetI()->Init();
	if (!PlayerRuntime::Detect())
	{
		EditorLog::Write("Web", "no game data (/game/player.json) - nothing to run");
		s_Failed = true;
		return 0;
	}
	emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, OnKey);
	emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, OnKey);
	emscripten_set_mousedown_callback("#canvas", nullptr, true, OnMouse);
	emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, OnMouse);
	emscripten_set_mousemove_callback("#canvas", nullptr, true, OnMouse);
	emscripten_set_touchstart_callback("#canvas", nullptr, true, OnTouch);
	emscripten_set_touchmove_callback("#canvas", nullptr, true, OnTouch);
	emscripten_set_touchend_callback("#canvas", nullptr, true, OnTouch);
	emscripten_set_touchcancel_callback("#canvas", nullptr, true, OnTouch);
	emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, OnFocus);
	emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, OnFocus);
	NovaAndroid::SetFocus(true);

	Resize();
	s_App = new EditorApp(nullptr);
	s_App->SetScreenSize((UINT)s_Width, (UINT)s_Height);
	bool ok = false;
	try { ok = s_App->Init(); }
	catch (const std::exception& e) { Fail("init", typeid(e).name(), e.what()); return 0; }
	catch (...) { Fail("init", "unknown", ""); return 0; }
	if (!ok)
	{
		EditorLog::Write("Web", "engine init failed");
		s_Failed = true;
		return 0;
	}
	emscripten_set_main_loop(Frame, 0, false);
	return 1;
}

#ifndef NOVA_WEB_DOTNET
// C# 없는 빌드 (엔진만 — Web/build.sh 의 nova)
int main() { nova_web_start(); return 0; }
#endif
