#pragma once
#include "ShaderCross.h"
#include <nlohmann/json.hpp>

// ShaderCross 결과 ↔ JSON (PC 의 변환 캐시 · 안드로이드 셰이더 묶음이 같이 쓴다). DXC · SPIRV-Cross 없이 컴파일된다
namespace ShaderCross::Json
{
	using json = nlohmann::json;
	json FxToJson(const FxParser::Effect& e);
	void FxFromJson(const json& fx, FxParser::Effect& e);
	json BlocksToJson(const std::map<std::string, UniformBlock>& blocks);
	void BlocksFromJson(const json& j, std::map<std::string, UniformBlock>& blocks);
	json ToJson(const EffectGlsl& e, int version);
	bool FromJson(const json& j, EffectGlsl& e, int version);   // version 이 다르면 false
	// WebGPU 효과 (웹 빌드 게임 데이터 Shaders/<이름>.wgsl.json · PC 캐시 ShaderCache/WGSL): SPIR-V 코드는 넣지 않는다
	json WgslToJson(const EffectSpirv& e, int version);
	bool WgslFromJson(const json& j, EffectSpirv& e, int version);   // version 이 다르면 false
}
