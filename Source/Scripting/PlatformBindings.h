#pragma once

// 플랫폼 정보 → C# (Unity 의 Input.touchCount · GetTouch, Screen.safeArea · orientation, Application.platform).
//  C# (NovaScriptCore 의 Services.cs) 는 DllImport("NovaCore") 로 PlatformBindings.cpp 의 내보낸 함수를 부른다 (안드로이드는 PINVOKE_OVERRIDE 로 libnova.so).
//  플랫폼 코드 (안드로이드의 창 · 회전) 가 아래 함수로 값을 넣는다
namespace PlatformBindings
{
	// 화면 가장자리에서 가려지는 픽셀 (노치 · 둥근 모서리 — 안드로이드 DisplayCutout). Windows 는 0
	void SetSafeInsets(int left, int top, int right, int bottom);
	// Screen.orientation = 값을 바꾸려 할 때 부르는 함수 (안드로이드: Activity.setRequestedOrientation). 없으면 무시
	void SetOrientationHandler(void (*handler)(int unityOrientation));
}
