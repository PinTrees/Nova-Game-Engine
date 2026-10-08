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
	// 이번 프레임에 아직 시뮬레이션하지 않았고 켜진 Visual Effect 가 있다
	bool NeedsSimulation();
	// 켜진 Visual Effect 가 장면 텍스처 (뷰 깊이 · 날씨 덮개) 를 읽는다 — 그러면 비동기 컴퓨트로 보내지 않는다 (그래픽 패스가 같은 텍스처를 쓰는 중)
	bool ReadsSceneTextures();
	// 시뮬레이션만 (Render Graph 의 VFX Simulation 패스 — 비동기 컴퓨트로 그림자 · 불투명과 겹쳐 돈다). 뷰 = 깊이 충돌 카메라
	void SimulateEffects(const Matrix& view, const Matrix& proj, const ParticleRenderer::Environment* env);
	void Render(const Matrix& view, const Matrix& proj, GfxRenderTargetView* rtv, GfxDepthStencilView* dsv, const ParticleRenderer::Environment* env = nullptr);
	int LastDrawCalls();
	int LastSystemCount();
	int LastCulledCount();   // 지난 뷰에서 화면 밖이라 건너뛴 Visual Effect
	bool Supported();   // compute 를 쓸 수 있고 58. VFX.fx 를 읽었다
	const std::string& LastError();
	// 검사용 요약 (안드로이드 scene 검사의 NOVA_TEST · nova vfx stats 와 같은 모양): {gpu, error, drawCalls, effects:[{object, asset, alive, systems:[{name, alive}]}]}
	std::string InfoJson();
}
