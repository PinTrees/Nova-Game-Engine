#pragma once

// 화면 공간 데칼 (Decal Projector). EditorApp 의 Game · Scene 뷰가 불투명 패스 다음에 부른다.
//  - 깊이 프리패스의 노멀 · 깊이 (Ssao::NormalDepthSRV — 뷰 노멀 + 뷰 깊이) 에서 픽셀마다 표면의 위치 · 노멀을 되살려
//    데칼 상자 안이면 재질을 칠한다 (상자 뒷면을 그려 카메라가 상자 안이어도 된다, 깊이 버퍼는 묶지 않음)
//  - 엔진 데칼 = Shaders/52. Decal.fx (재질의 Lit / Unlit 값), Shader Graph 의 Decal 그래프 = CustomShaders::Shader::DrawDecal
//  - 빛 · 그림자 · 하늘 · 안개는 ShadeLit (사용자 이펙트로 불러 프레임마다 빛 값을 받는다), 섞기는 RGB 만 (DecalBS)
namespace DecalRenderer
{
	void Render(GfxContext* dc, GfxRenderTargetView* target, const D3D11_VIEWPORT& viewport, CXMMATRIX view, CXMMATRIX proj,
		GfxShaderResourceView* normalDepth, bool editor);
	int LastCount(bool editor);   // 마지막으로 그린 데칼 수 (검사 · 통계)
}
