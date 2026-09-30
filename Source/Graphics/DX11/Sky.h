#pragma once

#include "Camera.h"

class Sky
{
public:
	Sky(ComPtr<ID3D11Device> device, const std::wstring& cubemapFilename, float skySphereRadius);
	~Sky();

	ComPtr<ID3D11ShaderResourceView> CubeMapSRV();

	void Draw(ComPtr<ID3D11DeviceContext> dc, const Camera& camera);

private:
	ComPtr<ID3D11Buffer> _vb;
	ComPtr<ID3D11Buffer> _ib;

	ComPtr<ID3D11ShaderResourceView> _cubeMapSRV;

	uint32 _indexCount;
};
