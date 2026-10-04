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
}
