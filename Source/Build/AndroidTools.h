#pragma once

// 안드로이드 빌드 도구 (CLI nova android …)
//  - shaders --out 폴더: 모든 .fx 를 OpenGL ES 3.20 으로 변환해 <이름>.json 으로 (APK 의 assets/Shaders — 휴대폰에는 변환기가 없다)
namespace AndroidTools
{
	constexpr int kGlesShaderVersion = 1;   // Android/Source/GLESRhi.cpp 와 같게
	void RegisterEditor();
}
