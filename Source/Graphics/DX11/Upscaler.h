#pragma once
#include <nlohmann/json.hpp>

// URP 의 Render Scale · Upscaling Filter (RenderPipelineSettings): Game 카메라를 배율 해상도 타깃에 그린 뒤 출력 크기로 늘린다.
//  필터: Bilinear · Nearest-Neighbor · FidelityFX Super Resolution 1.0 (EASU + RCAS) — 셰이더 68. Upscale.fx.
//  Automatic = 정수 배율이면 Nearest, 아니면 Bilinear (URP 와 같음). FSR 은 늘릴 때만 (배율 1 이상이면 Bilinear)
namespace Upscaler
{
	// 배율 해상도 LDR 타깃 (R8G8B8A8, 크기가 바뀌면 다시 만든다). 실패 = nullptr
	GfxRenderTargetView* Target(UINT width, UINT height);
	// 방금 그린 배율 타깃 → output (outW × outH)
	void Resolve(GfxRenderTargetView* output, UINT outW, UINT outH);
	// 배율 없이 그린 프레임 (CLI info 가 지금 상태를 알게)
	void NoteNative(UINT width, UINT height);
	// 마지막 Game 카메라 프레임을 배율 해상도로 그렸나
	bool Active();
	// 마지막 Game 카메라 프레임: 입력 · 출력 크기, 쓴 필터 (nova renderscale)
	nlohmann::json Info();
}
