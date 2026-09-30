#pragma once
#include "App.h"


class BoxDemo : public App
{
	struct Vertex
	{
		XMFLOAT3 Pos;
		XMFLOAT4 Color;
	};

public:
	BoxDemo(HINSTANCE hInstance);
	~BoxDemo();

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
	ComPtr<ID3D11Buffer> _vertexBuffer;
	ComPtr<ID3D11Buffer> _indexBuffer;

	ComPtr<ID3DX11Effect> _fx;
	ComPtr<ID3DX11EffectTechnique> _tech;
	ComPtr<ID3DX11EffectMatrixVariable> _fxWorldViewProj;

	ComPtr<ID3D11InputLayout> _inputLayout;

	XMFLOAT4X4 _world;
	XMFLOAT4X4 _view;
	XMFLOAT4X4 _proj;

	float _theta = 1.5f * XM_PI;
	float _phi = 0.25f * XM_PI;
	float _radius = 5.0f;

	POINT _lastMousePos;
};