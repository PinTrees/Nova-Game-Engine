#pragma once
#include <nlohmann/json.hpp>
#include <string>

// 안드로이드 빌드 도구 (CLI nova android …)
//  - shaders --out 폴더: 모든 .fx 를 OpenGL ES 3.20 으로 변환해 <이름>.json 으로 (APK 의 assets/Shaders — 휴대폰에는 변환기가 없다)
//  - export --out 폴더: 게임 데이터 (assets/game) — 텍스처 굽기 · 메시 캐시
//  - build --out x.apk [--run]: APK (AndroidBuild)
namespace AndroidTools
{
	constexpr int kGlesShaderVersion = 1;   // Android/Source/GLESRhi.cpp 와 같게
	void RegisterEditor();

	// AndroidBuild 도 쓰는 단계 (CLI 와 같은 인자 · 결과 JSON)
	bool ExportShaders(const nlohmann::json& args, nlohmann::json& result, std::string& error);
	bool ExportGame(const nlohmann::json& args, nlohmann::json& result, std::string& error);
}
