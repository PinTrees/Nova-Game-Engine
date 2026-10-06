#include "pch.h"
#include "PlatformBindings.h"
#include "InputManager.h"
#include "GameViewEditorWindow.h"

// C# 의 DllImport("NovaCore") 가 부르는 함수 (이름 그대로 내보낸다). 좌표는 Unity 와 같이 게임 화면 픽셀, 왼쪽 아래 (0,0)
namespace
{
	int s_Insets[4] = {};   // 왼 · 위 · 오른 · 아래
	void (*s_Orientation)(int) = nullptr;

	void GameSize(float& w, float& h)
	{
		int gw = 0, gh = 0;
		GameViewEditorWindow::GameSize(gw, gh);
		w = (float)(std::max)(gw, 1);
		h = (float)(std::max)(gh, 1);
	}
}

namespace PlatformBindings
{
	void SetSafeInsets(int left, int top, int right, int bottom)
	{
		s_Insets[0] = left; s_Insets[1] = top; s_Insets[2] = right; s_Insets[3] = bottom;
	}

	void SetOrientationHandler(void (*handler)(int)) { s_Orientation = handler; }
}

// Unity 의 RuntimePlatform 값: WindowsPlayer 2, WindowsEditor 7, Android 11, WebGLPlayer 17 (Unity 는 WebGPU 도 이 값)
NOVA_PACKAGE_EXPORT int NovaApp_Platform()
{
#if defined(NOVA_WEB)
	return 17;
#elif defined(NOVA_ANDROID)
	return 11;
#else
	return Application::IsPlayer() ? 2 : 7;
#endif
}

NOVA_PACKAGE_EXPORT int NovaInput_TouchCount()
{
	return (int)InputManager::GetI()->GetTouches().size();
}

// out[7] = fingerId, x, y, deltaX, deltaY, phase (Unity 의 TouchPhase 순서), tapCount. 없으면 0
NOVA_PACKAGE_EXPORT int NovaInput_GetTouch(int index, float* out)
{
	const auto& touches = InputManager::GetI()->GetTouches();
	if (index < 0 || index >= (int)touches.size() || out == nullptr)
		return 0;
	const Touch& t = touches[(size_t)index];
	float w, h;
	GameSize(w, h);
	out[0] = (float)t.fingerId;
	out[1] = t.position.x;
	out[2] = h - t.position.y;          // 엔진 (왼쪽 위) → Unity (왼쪽 아래)
	out[3] = t.deltaPosition.x;
	out[4] = -t.deltaPosition.y;
	out[5] = (float)(int)t.phase;
	out[6] = (float)t.tapCount;
	return 1;
}

// Screen.safeArea (x, y, width, height — 왼쪽 아래 기준)
NOVA_PACKAGE_EXPORT void NovaScreen_SafeArea(float* out)
{
	float w, h;
	GameSize(w, h);
	out[0] = (float)s_Insets[0];
	out[1] = (float)s_Insets[3];
	out[2] = (std::max)(0.0f, w - s_Insets[0] - s_Insets[2]);
	out[3] = (std::max)(0.0f, h - s_Insets[1] - s_Insets[3]);
}

// Screen.orientation (Unity 의 ScreenOrientation: Portrait 1, PortraitUpsideDown 2, LandscapeLeft 3, LandscapeRight 4) — 지금 화면 비율로
NOVA_PACKAGE_EXPORT int NovaScreen_GetOrientation()
{
	float w, h;
	GameSize(w, h);
	return w >= h ? 3 : 1;
}

NOVA_PACKAGE_EXPORT void NovaScreen_SetOrientation(int orientation)
{
	if (s_Orientation)
		s_Orientation(orientation);
}
