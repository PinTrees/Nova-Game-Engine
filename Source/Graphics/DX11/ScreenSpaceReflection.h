#pragma once

class VolumeStack;
class InstancedBasicEffect;

// Screen Space Reflection (HDRP 의 Volume 효과 — URP 에는 없다): 화면에 보이는 것을 반사한다 (물웅덩이 · 매끈한 바닥 · 금속).
//  - 포워드 렌더러라 G 버퍼가 없다 → 본 패스의 ShadeLit 안에서 계산한다: 반사 방향으로 깊이 프리패스 (뷰 노멀 · 깊이) 를 걸어
//    맞으면 그 자리의 **지난 프레임 장면 색** (지난 ViewProj 로 되돌려 찾음 — 카메라가 움직여도 맞다) 을 프로브 · 하늘 반사 대신
//  - 재질의 매끈함 · 금속성 · 프레넬은 그대로 (ShadeLit 의 반사 항을 바꿀 뿐). 거친 면일수록 장면 색의 밉을 내려간다
//  - Minimum Smoothness 아래는 끄고 Smoothness Fade Start 까지 서서히, 화면 가장자리 (Screen Edge Fade Distance) 에서 흐려짐
//  - Game · Scene 뷰마다 지난 프레임 색을 따로 둔다. 프로브 · GI 찍기에는 없다
namespace ScreenSpaceReflection
{
	// 뷰의 본 패스 전 (깊이 프리패스 다음): 이 뷰의 Volume 값 · 깊이 프리패스 · 지난 프레임 색을 정한다
	void Prepare(const VolumeStack& stack, bool editor, bool capture, GfxShaderResourceView* normalDepth, CXMMATRIX view, CXMMATRIX proj);
	// 32 를 쓰는 이펙트마다 (InstancedBasicFX · CustomShaders): Prepare 한 값을 넣는다
	void Bind(InstancedBasicEffect* effect);
	// 장면 (불투명 · 하늘 · 투명 · 물 · 입자) 을 다 그린 뒤, 후처리 전: 다음 프레임이 쓸 장면 색으로 복사 (밉까지)
	void StoreHistory(GfxContext* dc, GfxRenderTargetView* sceneTarget, bool editor);
	// 마지막 Prepare 가 켰는가 (CLI · 검사용)
	bool Active(bool editor);
}
