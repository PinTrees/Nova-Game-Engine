#pragma once

#include "Camera.h"

class Sky
{
public:
	Sky(ComPtr<GfxDevice> device, const std::wstring& cubemapFilename, float skySphereRadius);
	~Sky();

	ComPtr<GfxShaderResourceView> CubeMapSRV();

	void Draw(ComPtr<GfxContext> dc, const Camera& camera);
	// 카메라 종류와 상관없이 눈 위치 + ViewProj 로 그린다 (Game / Scene 뷰 공용)
	void Draw(GfxContext* dc, const XMFLOAT3& eyePos, CXMMATRIX viewProj);

private:
	ComPtr<GfxBuffer> _vb;
	ComPtr<GfxBuffer> _ib;

	ComPtr<GfxShaderResourceView> _cubeMapSRV;

	uint32 _indexCount;
};
