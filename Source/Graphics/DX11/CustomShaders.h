#pragma once
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "NovaApi.h"

class UMaterial;
class InstancedBasicEffect;
class GfxContext;

// 패키지가 붙이는 재질 셰이더 (예: com.nova.toon 의 lilToon).
//  - 재질 파일: "Shader": "<이름>", "Properties": { 셰이더 값 }, "Fallback": "Unlit" | "Lit" (패키지가 없을 때 엔진이 대신 그리는 방법)
//  - 패키지 셰이더(.fx) 는 엔진의 "32. InstancedBasic.fx" 를 #include 해 빛 · 그림자 · 하늘 함수와 변수를 그대로 쓴다.
//    LoadEffect 로 만든 이펙트에는 엔진이 프레임마다 빛 · 그림자 · 하늘 · SSAO · 환경광을 기본 이펙트와 똑같이 넣는다
//  - 스킨 메시: 렌더러가 그 재질의 서브메시를 DrawSkinned 에 맡기고, 본 패스 뒤 DrawSkinnedOutline (외곽선 등 두 번째 패스) 을 부른다.
//    두 번째 패스가 바꾼 래스터 · 깊이 상태는 렌더러가 원래대로 되돌린다
//  - 정적 메시 (Mesh Renderer) 는 아직 Fallback 으로 그린다
namespace CustomShaders
{
	struct SkinnedDraw
	{
		GfxContext* Context = nullptr;
		UMaterial* Material = nullptr;
		XMMATRIX World = XMMatrixIdentity();
		XMMATRIX ViewProj = XMMatrixIdentity();
		const XMFLOAT4X4* Bones = nullptr;
		int BoneCount = 0;
		bool Editor = false;          // Scene 뷰 카메라
		std::function<void()> Draw;   // 이 서브메시의 정점 · 인덱스를 묶고 그린다 (입력 배치 = 엔진 스킨 정점)
	};

	struct Shader
	{
		std::string Name;             // 재질의 "Shader" 값
		std::string Owner;            // 패키지 이름 (내릴 때 한꺼번에 뺀다)
		std::function<void(SkinnedDraw&)> DrawSkinned;
		std::function<void(SkinnedDraw&)> DrawSkinnedOutline;   // 없으면 두 번째 패스 없음
		std::function<bool(const UMaterial&)> HasOutline;      // 이 재질에 두 번째 패스가 있나
		std::function<bool(UMaterial&)> Inspector;             // Inspector 본문 (값을 바꾸면 true → 저장)
		std::function<nlohmann::json()> DefaultProperties;     // 새로 이 셰이더로 바꿀 때
	};

	NOVA_API void Register(const Shader& shader);
	NOVA_API void UnregisterOwner(const std::string& owner);
	NOVA_API const Shader* Find(const std::string& name);
	NOVA_API std::vector<std::string> Names();

	// 패키지 셰이더 이펙트를 엔진이 만든다 (프레임 상수를 받는 목록에 들어간다). 실패하면 nullptr + error
	NOVA_API InstancedBasicEffect* LoadEffect(const std::string& owner, const std::wstring& fxPath, std::string& error);
	// (엔진) 프레임 상수를 넣을 패키지 이펙트들
	void ForEachEffect(const std::function<void(InstancedBasicEffect*)>& fn);
}
