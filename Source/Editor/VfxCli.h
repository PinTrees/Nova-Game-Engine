#pragma once
#include <functional>
#include <string>
#include <nlohmann/json.hpp>

// nova vfx — .vfx 에셋 편집 · 장면의 Visual Effect 제어 (VfxCli.cpp)
namespace VfxCli
{
	// 창 연산 (그래프 창 · VFX Assistant): window, assistant, assistant.send, assistant.status, assistant.stop — 처리했으면 true
	using WindowOps = std::function<bool(const std::string& op, const nlohmann::json& args, nlohmann::json& result, std::string& error)>;
	void Register(WindowOps windowOps);
	bool RunOp(const std::string& op, const nlohmann::json& args, nlohmann::json& result, std::string& error);
}
