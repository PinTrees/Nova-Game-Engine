#pragma once
#include "App.h"
#include "Waves.h"

class WavesDemo : public App
{
	struct Vertex
	{
		XMFLOAT3 pos;
		XMFLOAT4 color;
	};
public:
	WavesDemo(HINSTANCE hInstance);
	~WavesDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int x, int y);
	void OnMouseUp(WPARAM btnState, int x, int y);
	void OnMouseMove(WPARAM btnState, int x, int y);

private:
	float GetHeight(float x, float z) { return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z)); }

	void BuildLandGeometryBuffers();
	void BuildWavesGeometryBuffers();
	void BuildFX();
	void BuildVertexLayout();

private:
	ComPtr<ID3D11Buffer> _landVB;
	ComPtr<ID3D11Buffer> _landIB;
	ComPtr<ID3D11Buffer> _wavesVB;
	ComPtr<ID3D11Buffer> _wavesIB;

	ComPtr<ID3DX11Effect> _fx;
	ComPtr<ID3DX11EffectTechnique> _tech;
	ComPtr<ID3DX11EffectMatrixVariable> _fxWorldViewProj;

	ComPtr<ID3D11InputLayout> _inputLayout;
	
	ComPtr<ID3D11RasterizerState> _wireframeRS;

	// Define transformations from local spaces to world space.
	XMFLOAT4X4 _gridWorld;
	XMFLOAT4X4 _wavesWorld;
	uint32 _gridIndexCount = 0;
	Waves _waves;

	XMFLOAT4X4 _view;
	XMFLOAT4X4 _proj;

	float _theta = 1.5f * XM_PI;
	float _phi = 0.1f * XM_PI;
	float _radius = 15.0f;

	POINT _lastMousePos;
};