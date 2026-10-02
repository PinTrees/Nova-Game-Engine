#pragma once
#include "ModelDocument.h"

// 모델 편집 연산 목록 — 편집기 창과 CLI (nova model <op> …) 가 같은 이름 · 같은 JSON 인자로 부른다.
//  - 고치는 연산은 먼저 Undo 스냅숏을 남기고, 결과로 요약 (점 · 면 수, 경계 상자, 선택) 을 돌려준다 → AI 가 바로 확인
//  - 좌표 인자는 월드 좌표 (미터, Y 위, 캐릭터 앞 = +Z). 점 번호는 활성 오브젝트 메시의 번호 (연산 뒤 바뀔 수 있다 → get 으로 다시)
//  - 마지막 연산은 기억해 둔다 (편집기의 Last Operation 패널: 값을 바꾸면 되돌리고 다시 한다)
namespace Modeling
{
	struct OpInfo
	{
		std::string Name;
		std::string Help;     // 인자 설명 (CLI 도움말)
		bool Mutates = true;
	};

	const std::vector<OpInfo>& Ops();
	bool RunOp(const std::string& name, const nlohmann::json& args, nlohmann::json& result, std::string& error);
	// Undo 스냅숏 없이 (창의 끌기 연산: 시작할 때 한 번 남기고, 움직일 때마다 되돌린 뒤 다시)
	bool RunOpNoUndo(const std::string& name, const nlohmann::json& args, nlohmann::json& result, std::string& error);

	struct LastOp
	{
		std::string Name;
		nlohmann::json Args;
	};
	const LastOp& Last();
	void SetLast(const std::string& name, const nlohmann::json& args);   // 창의 끌기 연산 (G/R/S) 이 끝났을 때
	// 마지막 연산을 되돌리고 새 인자로 다시
	bool RerunLast(const nlohmann::json& args, nlohmann::json& result, std::string& error);
}
