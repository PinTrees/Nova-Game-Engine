#pragma once
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "NovaApi.h"

class UMaterial;
class InstancedBasicEffect;
class GfxContext;
class FxEffect;

// 패키지가 붙이는 재질 셰이더 (예: com.nova.toon 의 lilToon).
//  - 재질 파일: "Shader": "<이름>", "Properties": { 셰이더 값 }, "Fallback": "Unlit" | "Lit" (패키지가 없을 때 엔진이 대신 그리는 방법)
//  - 패키지 셰이더(.fx) 는 엔진의 "32. InstancedBasic.fx" 를 #include 해 빛 · 그림자 · 하늘 함수와 변수를 그대로 쓴다.
//    LoadEffect 로 만든 이펙트에는 엔진이 프레임마다 빛 · 그림자 · 하늘 · SSAO · 환경광을 기본 이펙트와 똑같이 넣는다
//  - 스킨 메시: 렌더러가 그 재질의 서브메시를 DrawSkinned 에 맡기고, 본 패스 뒤 DrawSkinnedOutline (외곽선 등 두 번째 패스) 을 부른다.
//    두 번째 패스가 바꾼 래스터 · 깊이 상태는 렌더러가 원래대로 되돌린다
//  - 정적 메시 (Mesh Renderer): MeshBatcher 가 같은 메시 · 재질 묶음을 DrawInstanced 에 맡긴다 (인스턴스 = 월드 행렬, 입력 배치 = 엔진 InstancedBasic).
//    DrawInstanced 가 없으면 Fallback 으로 그린다
//  - Provider: 이름이 앞부분 (예: "Shader Graphs/") 과 맞으면 처음 찾을 때 만들어 등록한다 (Shader Graph 처럼 에셋에서 생기는 셰이더)
namespace CustomShaders
{
	// 어느 패스에서 부르나: Main (본 패스, EQUAL 깊이), NormalDepth (깊이 · 노멀 프리패스), Shadow (그림자 맵), Transparent (투명 패스 — 섞기 · 깊이 읽기만)
	//  NormalDepth · Shadow 는 CustomDepth 가 true 인 재질만 (그 밖의 재질은 엔진이 그린다), Transparent 는 Transparent 가 true 인 재질만
	enum class DrawPass { Main, NormalDepth, Shadow, Transparent };

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
		DrawPass Pass = DrawPass::Main;
		XMMATRIX View = XMMatrixIdentity();   // NormalDepth: 카메라 View (노멀 · 깊이는 뷰 공간)
	};

	// 정적 메시 묶음 (MeshBatcher): Draw = 인스턴스 버퍼 · 입력 배치를 묶고 인스턴싱으로 그린다
	struct InstancedDraw
	{
		GfxContext* Context = nullptr;
		UMaterial* Material = nullptr;
		XMMATRIX ViewProj = XMMatrixIdentity();
		bool Editor = false;
		uint32 LayerBit = 0xFFFFFFFFu;   // Light.cullingMask 용 물체 레이어
		std::function<void()> Draw;
		DrawPass Pass = DrawPass::Main;
		XMMATRIX View = XMMatrixIdentity();   // NormalDepth: 카메라 View. Shadow: ViewProj = 빛의 ViewProj (CurrentShadow 의 빛 · 바이어스와 같이)
		// 테셀레이션 (Shader Graph 의 Tessellation): 셰이더가 패치로 그리면 Topology 를 바꾼다 — Draw 가 이 값으로 묶는다.
		//  나눔은 늘 화면 카메라 기준 (그림자 패스도 — 패스마다 같은 삼각형)
		D3D11_PRIMITIVE_TOPOLOGY Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		XMMATRIX CameraViewProj = XMMatrixIdentity();
		XMFLOAT3 CameraPos = XMFLOAT3(0, 0, 0);
		float CameraProj22 = 1.732f;   // 투영 행렬 _22 (1 / tan(fovY / 2))
	};

	// 데칼 (Decal Projector 의 재질이 이 셰이더 — 예: Shader Graph 의 Material = Decal): SetDecalVars = 그 이펙트에 데칼 상자 · 화면 값을 넣는다
	struct DecalDraw
	{
		GfxContext* Context = nullptr;
		UMaterial* Material = nullptr;
		std::function<void(FxEffect*)> SetDecalVars;
		std::function<void()> Draw;   // 데칼 상자 (정점 · 인덱스 · 입력 배치) 를 묶고 그린다
	};

	// 지금 그리는 그림자 맵 조각의 빛 (ShadowRenderer 가 조각마다 넣는다): 사용자 셰이더의 그림자 VS 가 엔진과 같은 바이어스를 쓰게
	struct ShadowCaster
	{
		XMFLOAT4 Light = XMFLOAT4(0, 1, 0, 0);   // w 0 = 방향광 (xyz = 빛 쪽), 1 = 스포트 · 점광 (xyz = 위치)
		float Bias[2] = { 0, 0 };                // 깊이 · 노멀 바이어스
	};
	NOVA_API ShadowCaster& CurrentShadow();

	struct Shader
	{
		std::string Name;             // 재질의 "Shader" 값
		std::string Owner;            // 패키지 이름 (내릴 때 한꺼번에 뺀다)
		std::function<void(SkinnedDraw&)> DrawSkinned;
		std::function<void(SkinnedDraw&)> DrawSkinnedOutline;   // 없으면 두 번째 패스 없음
		std::function<void(InstancedDraw&)> DrawInstanced;     // 정적 메시 (없으면 Fallback)
		std::function<void(DecalDraw&)> DrawDecal;             // 데칼 (없으면 엔진 데칼이 재질의 Lit 값으로)
		std::function<bool(const UMaterial&)> CustomDepth;      // 프리패스 · 그림자도 이 셰이더가 (잘라내기 · 정점 이동 — Pass = NormalDepth / Shadow)
		std::function<bool(const UMaterial&)> Transparent;     // 투명: 투명 패스에서만 (본 패스 · 프리패스 · 그림자 제외, 뒤에서부터)
		std::function<bool(const UMaterial&)> HasOutline;      // 이 재질에 두 번째 패스가 있나
		std::function<bool(UMaterial&)> Inspector;             // Inspector 본문 (값을 바꾸면 true → 저장)
		std::function<nlohmann::json()> DefaultProperties;     // 새로 이 셰이더로 바꿀 때
	};

	NOVA_API void Register(const Shader& shader);
	NOVA_API void UnregisterOwner(const std::string& owner);
	NOVA_API const Shader* Find(const std::string& name);
	NOVA_API std::vector<std::string> Names();

	// 이름 앞부분 → 만들기 (true = 등록했다) · 목록 (재질 Inspector 의 Shader 목록)
	struct Provider
	{
		std::string Prefix;
		std::string Owner;
		std::function<bool(const std::string& name)> Create;
		std::function<std::vector<std::string>()> List;
		std::function<bool(const std::string& name)> Pending;   // 만드는 중 (백그라운드 컴파일) — 실패로 적지 않고 다음에 다시 묻는다
	};
	NOVA_API void RegisterProvider(const Provider& provider);
	NOVA_API void Forget(const std::string& name);   // 실패 기록을 지운다 (에셋을 고쳤을 때 다시 만들어 보게)

	// 패키지 셰이더 이펙트를 엔진이 만든다 (프레임 상수를 받는 목록에 들어간다). 실패하면 nullptr + error
	NOVA_API InstancedBasicEffect* LoadEffect(const std::string& owner, const std::wstring& fxPath, std::string& error);
	// (엔진) 프레임 상수를 넣을 패키지 이펙트들
	void ForEachEffect(const std::function<void(InstancedBasicEffect*)>& fn);
}
