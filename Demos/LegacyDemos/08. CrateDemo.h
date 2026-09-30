#pragma once
#include "App.h"
#include "Waves.h"
#include "LightHelper.h"

class CrateDemo : public App
{
public:
	CrateDemo(HINSTANCE hInstance);
	~CrateDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int x, int y);
	void OnMouseUp(WPARAM btnState, int x, int y);
	void OnMouseMove(WPARAM btnState, int x, int y);

private:
	void BuildGeometryBuffers();

private:
	ComPtr<ID3D11Buffer> _boxVB;
	ComPtr<ID3D11Buffer> _boxIB;

	ComPtr<ID3D11ShaderResourceView> _diffuseMapSRV;

	DirectionalLight _dirLights[3];
	Material _boxMat;

	XMFLOAT4X4 _texTransform;
	XMFLOAT4X4 _boxWorld;

	XMFLOAT4X4 _view;
	XMFLOAT4X4 _proj;

	int32 _boxVertexOffset;
	uint32 _boxIndexOffset;
	uint32 _boxIndexCount;

	XMFLOAT3 _eyePosW = {0, 0, 0};

	float _theta = 1.3f * XM_PI;
	float _phi =  0.4f * XM_PI;
	float _radius = 2.5f;

	POINT _lastMousePos;
};