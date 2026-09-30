#pragma once
#include "App.h"

class ShapesDemo : public App
{
	struct Vertex
	{
		XMFLOAT3 pos;
		XMFLOAT4 color;
	};
public:
	ShapesDemo(HINSTANCE hInstance);
	~ShapesDemo();

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
	XMFLOAT4X4 _sphereWorld[10];
	XMFLOAT4X4 _cylWorld[10];
	XMFLOAT4X4 _boxWorld;
	XMFLOAT4X4 _gridWorld;
	XMFLOAT4X4 _centerSphere;

	XMFLOAT4X4 _view;
	XMFLOAT4X4 _proj;

	int32 _boxVertexOffset = 0;
	int32 _gridVertexOffset = 0;
	int32 _sphereVertexOffset = 0;
	int32 _cylinderVertexOffset = 0;

	uint32 _boxIndexOffset = 0;
	uint32 _gridIndexOffset = 0;
	uint32 _sphereIndexOffset = 0;
	uint32 _cylinderIndexOffset = 0;

	uint32 _boxIndexCount = 0;
	uint32 _gridIndexCount = 0;
	uint32 _sphereIndexCount = 0;
	uint32 _cylinderIndexCount = 0;

	float _theta = 1.5f * XM_PI;
	float _phi = 0.1f * XM_PI;
	float _radius = 15.0f;

	POINT _lastMousePos;
};