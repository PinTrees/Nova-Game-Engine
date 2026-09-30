#pragma once
#include "App.h"
#include "Waves.h"
#include "LightHelper.h"

class VecAddDemo : public App
{
	struct Data
	{
		XMFLOAT3 v1;
		XMFLOAT2 v2;
	};
public:
	VecAddDemo(HINSTANCE hInstance);
	~VecAddDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void DoComputeWork();

private:
	void BuildBuffersAndViews();

private:
	ComPtr<ID3D11Buffer> _outputBuffer;
	ComPtr<ID3D11Buffer> _outputDebugBuffer;

	ComPtr<ID3D11ShaderResourceView> _inputASRV;
	ComPtr<ID3D11ShaderResourceView> _inputBSRV;
	ComPtr<ID3D11UnorderedAccessView> _outputUAV;

	uint32 _numElements = 32;
};
