#pragma once

// Scene 뷰 바닥 격자 (Unity 처럼 깊이 검사 → 물체 뒤의 선은 가려진다). Shaders/45. SceneGrid.fx
namespace SceneGrid
{
	// 지금 묶인 렌더 타깃/깊이 버퍼에 그린다. 그리고 나면 블렌드·깊이·래스터 상태를 되돌린다
	void Draw(ID3D11DeviceContext* dc, CXMMATRIX viewProj, const XMFLOAT3& cameraPos);
}
