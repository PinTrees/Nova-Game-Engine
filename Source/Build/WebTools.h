#pragma once
#include <nlohmann/json.hpp>
#include <string>

// 웹 빌드 도구 (CLI nova web …) — WebGPU + WebAssembly
//  - shaders --out 폴더: 모든 .fx 를 WGSL 로 (DXC → SPIR-V → Tint) <이름>.wgsl.json — 브라우저에는 변환기가 없다
namespace WebTools
{
	constexpr int kWgslShaderVersion = 1;   // Web/Source 의 WebGPU 효과와 같게
	void RegisterEditor();

	bool ExportShaders(const nlohmann::json& args, nlohmann::json& result, std::string& error);
}
