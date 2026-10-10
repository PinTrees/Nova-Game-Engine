#pragma once
#include "AtmospherePass.h"

struct DirectionalLight;

// 물 그리기 (46. Water.fx). 불투명 물체·하늘 다음, 격자·입자 전에 화면마다 한 번.
//  1) 지금까지의 화면 색을 복사 (굴절용)  2) 물 바디마다 그리기 (깊이 버퍼는 읽기 전용으로 묶고 같은 깊이를 SRV 로 읽음)
//  3) 물 깊이만 한 번 더 써서 뒤에 그리는 입자·격자가 물에 가려지게
namespace WaterRenderer
{
	struct View
	{
		GfxContext* Context = nullptr;
		XMMATRIX ViewMatrix, Proj;
		XMFLOAT3 Eye;
		GfxRenderTargetView* Target = nullptr;
		GfxDepthStencilView* Depth = nullptr;            // 쓰기 (마지막 깊이 패스)
		GfxDepthStencilView* DepthReadOnly = nullptr;    // 물 그릴 때 (SRV 와 같이 묶을 수 있게)
		GfxShaderResourceView* DepthSRV = nullptr;
		D3D11_VIEWPORT Viewport = {};
		const DirectionalLight* Sun = nullptr;
		GfxShaderResourceView* Sky = nullptr;            // 하늘 큐브맵 (반사)
		// 첫 방향광(해)의 캐스케이드 그림자 (없으면 SunShadow = nullptr → 그림자 없음)
		GfxShaderResourceView* SunShadow = nullptr;      // Texture2DArray (캐스케이드 = 조각)
		XMMATRIX SunShadowTransforms[8];   // 해 (방향광 0) 의 캐스케이드 (최대 8 — ShadowMap::kMaxCascades)
		XMFLOAT4 CascadeSpheres[8] = {};
		XMFLOAT4 ShadowParams = {};                         // x 캐스케이드 수, z 흐려지기 시작, w 1/폭
		XMFLOAT4 SunShadowData = {};                        // x Strength, y 필터
		const AtmospherePass::Params* Atmosphere = nullptr;  // Volume 의 안개·대기 (없으면 안 입힘)
	};

	void Draw(const View& view);
	int LastDrawCount();   // 지난 Draw 에서 그린 물 바디 수 (검사용)
}
