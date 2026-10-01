#pragma once

// 모든 Particle System 을 한 카메라로 그린다 (씬의 불투명 물체 다음, 후처리 전).
//  - 입자 하나 = 인스턴스 하나 (43. Particle.fx), 시스템마다 그리기 한 번
//  - 시스템은 카메라에서 먼 것부터, Alpha Blended + Sort Mode 면 시스템 안의 입자도 정렬
namespace ParticleRenderer
{
	void Render(const Matrix& view, const Matrix& proj, GfxRenderTargetView* rtv, GfxDepthStencilView* dsv);
	int LastDrawCalls();
	int LastParticleCount();
}
