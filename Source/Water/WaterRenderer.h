#pragma once

struct DirectionalLight;

// 물 그리기 (46. Water.fx). 불투명 물체·하늘 다음, 격자·입자 전에 화면마다 한 번.
//  1) 지금까지의 화면 색을 복사 (굴절용)  2) 물 바디마다 그리기 (깊이 버퍼는 읽기 전용으로 묶고 같은 깊이를 SRV 로 읽음)
//  3) 물 깊이만 한 번 더 써서 뒤에 그리는 입자·격자가 물에 가려지게
namespace WaterRenderer
{
	struct View
	{
		ID3D11DeviceContext* Context = nullptr;
		XMMATRIX ViewMatrix, Proj;
		XMFLOAT3 Eye;
		ID3D11RenderTargetView* Target = nullptr;
		ID3D11DepthStencilView* Depth = nullptr;            // 쓰기 (마지막 깊이 패스)
		ID3D11DepthStencilView* DepthReadOnly = nullptr;    // 물 그릴 때 (SRV 와 같이 묶을 수 있게)
		ID3D11ShaderResourceView* DepthSRV = nullptr;
		D3D11_VIEWPORT Viewport = {};
		const DirectionalLight* Sun = nullptr;
		ID3D11ShaderResourceView* Sky = nullptr;            // 하늘 큐브맵 (반사)
	};

	void Draw(const View& view);
	int LastDrawCount();   // 지난 Draw 에서 그린 물 바디 수 (검사용)
}
