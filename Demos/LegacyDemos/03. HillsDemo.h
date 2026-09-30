#pragma once
#include "App.h"

class HillsDemo : public App
{
	struct Vertex
	{
		XMFLOAT3 Pos;
		XMFLOAT4 Color;
	};
public:
	HillsDemo(HINSTANCE hInstance);
	~HillsDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int x, int y);
	void OnMouseUp(WPARAM btnState, int x, int y);
	void OnMouseMove(WPARAM btnState, int x, int y);

private:
	float GetHeight(float x, float z) { return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z)); }

	void BuildGeometryBuffers();
	void BuildFX();
	void BuildVertexLayout();

private:
	ComPtr<ID3D11Buffer> _vertedBuffer;
	ComPtr<ID3D11Buffer> _indexBuffer;

	ComPtr<ID3DX11Effect> _fx;
	ComPtr<ID3DX11EffectTechnique> _tech;
	ComPtr<ID3DX11EffectMatrixVariable> _fxWorldViewProj;

	ComPtr<ID3D11InputLayout> _inputLayout;

	// Define transformations from local spaces to world space.
	XMFLOAT4X4 _gridWorld;
	uint32 _gridIndexCount;

	XMFLOAT4X4 _view;
	XMFLOAT4X4 _proj;

	float _theta = 1.5f * XM_PI;
	float _phi = 0.1f * XM_PI;
	float _radius = 200.0f;

	POINT _lastMousePos;
};