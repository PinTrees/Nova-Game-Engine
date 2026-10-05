#pragma once
#include "ParticleRenderer.h"

// Visual Effect 의 GPU 일 (58. VFX.fx): 시뮬레이션 (Spawn · Update · GPU Event) 과 그리기.
//  - 시뮬레이션은 프레임마다 한 번, 그 프레임의 첫 Render 에서 (렌더 단계라 모든 백엔드에서 명령 버퍼가 열려 있다)
//  - 파티클 버퍼를 그대로 인스턴스 정점 버퍼로 그린다 (CPU 로 읽어 오지 않는다)
//  - GfxContext::SupportsGpuDriven 이 false 인 장치에서는 그리지 않는다
namespace VfxRuntime
{
	// 다음 Render 에서 시뮬레이션한다 (VisualEffect::UpdateAll 이 프레임마다)
	void MarkFrame();
	void Render(const Matrix& view, const Matrix& proj, GfxRenderTargetView* rtv, GfxDepthStencilView* dsv, const ParticleRenderer::Environment* env = nullptr);
	int LastDrawCalls();
	int LastSystemCount();
	bool Supported();   // compute 를 쓸 수 있고 58. VFX.fx 를 읽었다
	const std::string& LastError();
	// 검사용 요약 (안드로이드 scene 검사의 NOVA_TEST · nova vfx stats 와 같은 모양): {gpu, error, drawCalls, effects:[{object, asset, alive, systems:[{name, alive}]}]}
	std::string InfoJson();
}
