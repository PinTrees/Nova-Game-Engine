#pragma once
#include "Anim2DDocument.h"

// 2D 애니메이터 연산 표: 창과 CLI (nova anim2d <op> …) 가 같은 연산을 쓴다. 고치는 연산은 하나씩 Undo
namespace Anim2D
{
	struct OpInfo { std::string Name, Help; bool Mutates = false; };
	const std::vector<OpInfo>& Ops();
	bool RunOp(const std::string& name, const nlohmann::json& args, nlohmann::json& result, std::string& error);
	bool RunOpNoUndo(const std::string& name, const nlohmann::json& args, nlohmann::json& result, std::string& error);
}
