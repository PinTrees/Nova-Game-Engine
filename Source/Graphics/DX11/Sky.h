#pragma once

#include "Camera.h"

class Sky
{
public:
	Sky(ComPtr<ID3D11Device> device, const std::wstring& cubemapFilename, float skySphereRadius);
	~Sky();

	ComPtr<ID3D11ShaderResourceView> CubeMapSRV();

	void Draw(ComPtr<ID3D11DeviceContext> dc, const Camera& camera);
	// 카메라 종류와 상관없이 눈 위치 + ViewProj 로 그린다 (Game / Scene 뷰 공용)
	void Draw(ID3D11DeviceContext* dc, const XMFLOAT3& eyePos, CXMMATRIX viewProj);

private:
	ComPtr<ID3D11Buffer> _vb;
	ComPtr<ID3D11Buffer> _ib;

	ComPtr<ID3D11ShaderResourceView> _cubeMapSRV;

	uint32 _indexCount;
};
