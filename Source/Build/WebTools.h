#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>

// 웹 빌드 도구 (CLI nova web …) — WebGPU + WebAssembly
//  - shaders --out 폴더: 모든 .fx 를 WGSL 로 (DXC → SPIR-V → Tint) <이름>.wgsl.json — 브라우저에는 변환기가 없다
//  - export --out 폴더: 웹 게임 (index.html · nova.js · nova.wasm · game.json · game.data) — 안드로이드 게임 데이터 (BC 텍스처) + WGSL 을 한 덩어리로
namespace WebTools
{
	constexpr int kWgslShaderVersion = 2;   // Web/Source 의 WebGPU 효과와 같게
	void RegisterEditor();

	bool ExportShaders(const nlohmann::json& args, nlohmann::json& result, std::string& error);
	bool ExportGame(const nlohmann::json& args, nlohmann::json& result, std::string& error);
	// 엔진의 웹 플레이어 (nova.js · nova.wasm) 폴더: Binaries/Web → Web/build/Release (없으면 빈 경로)
	std::filesystem::path PlayerDir();
}
