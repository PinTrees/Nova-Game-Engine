#pragma once

// 모든 Particle System 을 한 카메라로 그린다 (씬의 불투명 물체 다음, 후처리 전).
//  - 입자 하나 = 인스턴스 하나 (43. Particle.fx), 시스템마다 그리기 한 번
//  - 시스템은 카메라에서 먼 것부터, Alpha Blended + Sort Mode 면 시스템 안의 입자도 정렬
namespace ParticleRenderer
{
	// 장면 정보 (Lit · Soft Particles 용). 없으면 둘 다 꺼진 것처럼 그린다
	struct Environment
	{
		GfxDepthStencilView* DepthReadOnly = nullptr;   // dsv 와 같은 깊이의 읽기 전용 뷰 (SRV 와 같이 묶을 수 있게)
		GfxShaderResourceView* DepthSRV = nullptr;
		GfxShaderResourceView* Sky = nullptr;           // 하늘 큐브맵 (환경광)
		XMFLOAT4 Indirect = { 1.0f, 1.0f, 1.0f, 1.0f }; // Volume 의 Indirect Lighting 배율
		bool HasSun = false;
		XMFLOAT3 SunDirection = { 0.0f, -1.0f, 0.0f };  // 방향광 0 이 비추는 방향
		XMFLOAT3 SunColor = { 1.0f, 1.0f, 1.0f };       // 방향광 0 의 Diffuse
	};
	void Render(const Matrix& view, const Matrix& proj, GfxRenderTargetView* rtv, GfxDepthStencilView* dsv, const Environment* env = nullptr);
	int LastDrawCalls();
	int LastParticleCount();
}
