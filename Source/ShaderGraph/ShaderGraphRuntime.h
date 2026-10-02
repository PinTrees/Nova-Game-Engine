#pragma once
#include "ShaderGraph.h"
#include "NovaApi.h"

// Shader Graph 를 재질 셰이더로 (CustomShaders Provider).
//  - 셰이더 이름 = Unity 와 같이 "Shader Graphs/<파일 이름>" (재질의 "Shader")
//  - 처음 그 이름을 찾을 때: .shadergraph 읽기 → Generate → <프로젝트>/Library/ShaderGraph/<이름>.fx → LoadEffect → 등록
//  - 그리기: 정적 메시 (MeshBatcher → DrawInstanced, GraphBatchTech) · 스킨 메시 (DrawSkinned, GraphSkinnedTech)
//  - 재질 값 = .mat 의 "Properties" { "<Reference>": 값 } (없으면 그래프의 기본값), 재질 Inspector 는 그래프 속성 목록
namespace ShaderGraph
{
	constexpr const char* kShaderPrefix = "Shader Graphs/";
	constexpr const char* kExtension = ".shadergraph";

	NOVA_API void InitRuntime();   // Provider 등록 (에디터 · 게임 빌드 모두)

	// Assets\Shaders\Water.shadergraph → "Shader Graphs/Water"
	NOVA_API std::string ShaderNameOf(const std::string& assetPath);
	// "Shader Graphs/Water" → 그 .shadergraph (프로젝트 상대 경로, 없으면 "")
	NOVA_API std::string FindGraphAsset(const std::string& shaderName);
	// 프로젝트의 모든 .shadergraph (Assets\...)
	NOVA_API std::vector<std::string> GraphAssets(bool refresh = false);

	// 저장한 그래프를 다시 만든다 (그 셰이더를 쓰는 재질은 다음 프레임부터 새 셰이더). 실패하면 false + error (HLSL 오류 포함)
	NOVA_API bool Reload(const std::string& assetPath, std::string& error);
	// 마지막으로 만들 때 생긴 오류 ("" = 성공 또는 아직 안 만듦)
	NOVA_API std::string LastError(const std::string& shaderName);
	// 만든 .fx (디스크 전체 경로)
	NOVA_API std::wstring GeneratedPath(const std::string& shaderName);

	// 그래프 속성의 기본값 → 재질 "Properties"
	NOVA_API nlohmann::json DefaultProperties(const Graph& g);
	// 이 그래프를 쓰는 새 재질 (.mat) — matPath 가 비면 그래프 옆 "<이름>.mat". 만든 재질의 프로젝트 경로
	NOVA_API std::string MakeMaterial(const std::string& graphAsset, const std::string& matPath, std::string& error);

	// 프로젝트 상대 경로 ↔ 전체 경로
	NOVA_API std::wstring FullPath(const std::string& assetPath);
}
