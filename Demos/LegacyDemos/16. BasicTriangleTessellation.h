#pragma once
#include "App.h"
#include "Waves.h"
#include "LightHelper.h"
#include "BlurFilter.h"

class BasicTriangleTessellation : public App
{
public:
	BasicTriangleTessellation(HINSTANCE hInstance);
	~BasicTriangleTessellation();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int x, int y);
	void OnMouseUp(WPARAM btnState, int x, int y);
	void OnMouseMove(WPARAM btnState, int x, int y);

private:
	void BuildTriPatchBuffers();

private:
	ComPtr<ID3D11Buffer> mTriPatchVB;

	XMFLOAT4X4 mView;
	XMFLOAT4X4 mProj;

	XMFLOAT3 mEyePosW = { 0.0f, 0.0f, 0.0f };

	float mTheta = 1.5f * XM_PI;
	float mPhi = 0.01f * XM_PI;
	float mRadius = 30.f;

	POINT mLastMousePos;
};
