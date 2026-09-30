#pragma once
#include "App.h"

class SkullDemo : public App
{
	struct Vertex
	{
		XMFLOAT3 pos;
		XMFLOAT4 color;
	};
public:
	SkullDemo(HINSTANCE hInstance);
	~SkullDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int x, int y);
	void OnMouseUp(WPARAM btnState, int x, int y);
	void OnMouseMove(WPARAM btnState, int x, int y);

private:
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
	
	ComPtr<ID3D11RasterizerState> _wireframeRS;

	// Define transformations from local spaces to world space.
	XMFLOAT4X4 _skullWorld;
	uint32 _skullIndexCount = 0;

	XMFLOAT4X4 _view;
	XMFLOAT4X4 _proj;

	float _theta = 1.5f * XM_PI;
	float _phi = 0.1f * XM_PI;
	float _radius = 15.0f;

	POINT _lastMousePos;
};