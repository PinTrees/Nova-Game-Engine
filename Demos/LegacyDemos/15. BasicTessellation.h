#pragma once
#include "App.h"
#include "Waves.h"
#include "LightHelper.h"
#include "BlurFilter.h"

class BasicTessellation : public App
{
public:
	BasicTessellation(HINSTANCE hInstance);
	~BasicTessellation();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int32 x, int32 y);
	void OnMouseUp(WPARAM btnState, int32 x, int32 y);
	void OnMouseMove(WPARAM btnState, int32 x, int32 y);

private:
	void BuildQuadPatchBuffer();

private:
	ComPtr<ID3D11Buffer> _quadPatchVB;

	XMFLOAT4X4 _view;
	XMFLOAT4X4 _proj;

	XMFLOAT3 _eyePosW = {0.f, 0.f, 0.f};

	float _theta = 1.3f * XM_PI;
	float _phi = 0.2f * XM_PI;
	float _radius = 80.f;

	POINT _lastMousePos;
};