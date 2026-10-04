#pragma once
#include "ShaderGraph.h"
#include "NovaApi.h"

// Shader Graph 를 재질 셰이더로 (CustomShaders Provider).
//  - 셰이더 이름 = Unity 와 같이 "<경로>/<파일 이름>" (경로 기본 "Shader Graphs" — Blackboard 에서 바꾼다), 재질의 "Shader"
//  - 처음 그 이름을 찾을 때: .shadergraph 읽기 → Generate → <프로젝트>/Library/ShaderGraph/<이름>_<경로 해시>.fx →
//    작업 스레드에서 컴파일 (셰이더 캐시만 채움) → 다음 프레임들 중 끝나면 LoadEffect (캐시 적중) → 등록. 그동안 재질은 Fallback,
//    다시 만들 때 (저장) 는 예전 셰이더로 그린다 (Unity 처럼 에디터가 멈추지 않는다)
//  - 그리기: 정적 메시 (MeshBatcher → DrawInstanced: 본 · 투명 · 깊이 · 그림자 패스) · 스킨 메시 (DrawSkinned)
//  - 재질 값 = .mat 의 "Properties" { "<Reference>": 값 } (없으면 그래프의 기본값), 재질 Inspector 는 그래프 속성 목록
namespace ShaderGraph
{
	constexpr const char* kDefaultPath = "Shader Graphs";
	constexpr const char* kExtension = ".shadergraph";
	constexpr const char* kSubExtension = ".shadersubgraph";

	NOVA_API void InitRuntime();     // Provider 등록 (에디터 · 게임 빌드 모두)
	NOVA_API void UpdateRuntime();   // 매 프레임: 끝난 백그라운드 컴파일을 이펙트로 (App 루프)

	// Assets\Shaders\Water.shadergraph → "Shader Graphs/Water" (그래프의 경로 설정을 읽는다)
	NOVA_API std::string ShaderNameOf(const std::string& assetPath);
	// 셰이더 이름 → 그 .shadergraph (프로젝트 상대 경로, 없거나 같은 이름이 둘이면 "" — LastError 에 이유)
	NOVA_API std::string FindGraphAsset(const std::string& shaderName);
	NOVA_API bool IsGraphShader(const std::string& shaderName);
	// 지금 쓰는 그래프 셰이더에서 그 이름 (Reference) 의 속성이 GPU 인스턴싱 속성인가 (InstanceSlotOf). 아니거나 같은 칸의 속성이 둘이면 None.
	//  isColor = 속성이 Color 인지 (MaterialPropertyBlock 값의 종류와 맞아야 한다) — MaterialBlock::Instanced
	NOVA_API InstanceSlot InstanceSlotFor(const std::string& shaderName, const std::string& name, bool& isColor);
	// 프로젝트의 모든 .shadergraph (Assets\...)
	NOVA_API std::vector<std::string> GraphAssets(bool refresh = false);

	// 저장한 그래프를 다시 만든다. wait = 컴파일이 끝날 때까지 기다림 (CLI — 오류를 바로 돌려준다), 아니면 백그라운드 (창)
	// 실패하면 false + error (HLSL 오류 포함). 기다리지 않으면 true = 시작함
	NOVA_API bool Reload(const std::string& assetPath, std::string& error, bool wait = true);
	NOVA_API bool IsCompiling(const std::string& shaderName);
	// Sub Graph 를 저장한 뒤: 그것을 쓰는 그래프들을 다시 만든다 (기다림 — CLI). 실패한 그래프의 오류를 error 에
	NOVA_API bool RebuildUsers(const std::string& subGraphAsset, std::string& error);
	// 프로젝트의 모든 .shadersubgraph (Create Node 메뉴)
	NOVA_API std::vector<std::string> SubGraphAssets();
	// 마지막으로 만들 때 생긴 오류 ("" = 성공 또는 아직 안 만듦)
	NOVA_API std::string LastError(const std::string& shaderName);
	// 만든 .fx (디스크 전체 경로)
	NOVA_API std::wstring GeneratedPath(const std::string& shaderName, const std::string& assetPath);

	// 백그라운드 컴파일 (셰이더 캐시만 채운다 — 다음 LoadEffect 가 바로 끝난다): 0 = 진행 중, 1 = 끝, -1 = 실패 (error = 컴파일러 메시지)
	//  파일 내용이 진행 중에 바뀌었으면 다시 컴파일한다
	int CompileInBackground(const std::wstring& fxPath, std::string& error);
	// 쓸 내용이 같으면 쓰지 않는다 (셰이더 캐시가 그대로 맞게). 실패하면 false
	bool WriteIfChanged(const std::wstring& path, const std::string& text);

	// 그래프 속성의 기본값 → 재질 "Properties"
	NOVA_API nlohmann::json DefaultProperties(const Graph& g);
	// 이 그래프를 쓰는 새 재질 (.mat) — matPath 가 비면 그래프 옆 "<이름>.mat". 만든 재질의 프로젝트 경로
	NOVA_API std::string MakeMaterial(const std::string& graphAsset, const std::string& matPath, std::string& error);

	// 프로젝트 상대 경로 ↔ 전체 경로
	NOVA_API std::wstring FullPath(const std::string& assetPath);
}
