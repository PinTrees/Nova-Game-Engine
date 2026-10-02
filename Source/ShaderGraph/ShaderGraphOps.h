#pragma once
#include "ShaderGraph.h"

// 열린 그래프 문서 + 연산 (Shader Graph 창과 CLI "nova shadergraph <op>" 가 같은 연산을 쓴다)
namespace ShaderGraph
{
	struct Document
	{
		Graph G;
		std::string Asset;        // Assets\...\X.shadergraph ("" = 아직 저장 안 함)
		bool Dirty = false;
		uint64 Revision = 1;      // 바뀔 때마다 (창이 노드 위치를 다시 넣는다)
		uint64 Edits = 1;         // 값까지 바뀔 때마다 (미리보기를 다시 만든다 — 위치만 옮긴 것은 아님)
		std::vector<json> UndoStack, RedoStack;

		void Snapshot();          // 바꾸기 전에 (Undo)
		bool Undo();
		bool Redo();
		void Changed() { Dirty = true; ++Revision; ++Edits; }
		void Touch() { Dirty = true; ++Edits; }   // 값만 (노드 위치는 다시 넣지 않는다)
		json Summary() const;
	};
	Document& Doc();

	struct OpInfo { std::string Name, Help; };
	const std::vector<OpInfo>& Ops();
	// 연산 하나 (args = {op 인자들}). 성공하면 result, 실패하면 error
	bool RunOp(const std::string& op, const json& args, json& result, std::string& error);

	// 문서 저장 + 셰이더 다시 만들기 (오류는 error, 저장은 됐어도 셰이더가 실패하면 false)
	//  wait = 셰이더 컴파일을 기다린다 (CLI), 아니면 백그라운드 (창 — IsCompiling 으로 상태)
	bool SaveDoc(const std::string& assetPath, std::string& error, bool wait = true);
	bool OpenDoc(const std::string& assetPath, std::string& error);
	// 새 그래프 (Unity 의 새 Shader Graph 처럼 노드 없이 Master 만 — 비어 있는 입력은 기본값)
	Graph NewGraph(const std::string& material);
	// 노드 하나의 JSON (id · type · pos · 입력 (이은 것 / 값) · 출력 폭)
	json NodeJson(const Graph& g, const Node& n);
}
