#pragma once
#include <nlohmann/json.hpp>

// Rendering Path = Deferred (URP 와 같은 생각 — 렌더링 현대화 6 단계, docs/DEFERRED_RENDERING.md).
//  MeshBatcher 의 엔진 Lit 재질 묶음은 표면만 G-버퍼 (4 장) 에 쓰고, 전체 화면 한 번 (32. InstancedBasic.fx 의 DeferredLightTech) 이
//  픽셀마다 포워드와 같은 ShadeLit (그림자 · Forward+ 클러스터 · APV · 반사 프로브 · SSR · 날씨) + FinishLit (안개) 으로 비춘다.
//  나머지 (Skinned · 지형 · 나무 · Shader Graph · 패키지 셰이더 · 테셀레이션 · LOD 크로스페이드 · 투명) 는 그 뒤에 포워드로 (URP 의 Forward Only)
namespace DeferredRenderer
{
	// 이 뷰를 디퍼드로 그릴 수 있나 (기법이 있고 깊이 SRV 가 있다)
	bool Available();
	// G-버퍼 타깃을 묶는다 (뷰마다 — 0 Game, 1 Scene). 깊이는 프리패스 그대로 (EQUAL), 표시 칸 (G1) 을 0 으로 지운다
	bool BeginGBuffer(GfxContext* ctx, UINT width, UINT height, GfxDepthStencilView* dsv, const D3D11_VIEWPORT& viewport, int view);
	// 전체 화면 조명 → sceneTarget (빛 · 그림자 · 프로브 값은 이미 InstancedBasicFX 에 묶여 있어야 한다 — 포워드 본 패스와 같이)
	void Light(GfxContext* ctx, GfxRenderTargetView* sceneTarget, GfxShaderResourceView* depthSrv, const Matrix& viewProj, const D3D11_VIEWPORT& viewport, int view);

	GfxShaderResourceView* GBufferSRV(int index, int view);   // Rendering Debugger · 검사
	nlohmann::json Info();
}
