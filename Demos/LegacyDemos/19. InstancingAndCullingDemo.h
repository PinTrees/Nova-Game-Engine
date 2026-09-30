#pragma once
#include "App.h"
#include "Waves.h"
#include "LightHelper.h"
#include "BlurFilter.h"
#include "Camera.h"

class InstancingAndCullingDemo : public App
{
	struct InstancedData
	{
		XMFLOAT4X4 World;
		XMFLOAT4 Color;
	};

public:
	InstancingAndCullingDemo(HINSTANCE hInstance);
	~InstancingAndCullingDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int32 x, int32 y);
	void OnMouseUp(WPARAM btnState, int32 x, int32 y);
	void OnMouseMove(WPARAM btnState, int32 x, int32 y);

private:
	void BuildSkullGeometryBuffers();
	void BuildInstancedBuffer();

private:
	ComPtr<ID3D11Buffer> _skullVB;
	ComPtr<ID3D11Buffer> _skullIB;
	ComPtr<ID3D11Buffer> _instancedBuffer;

	// Bounding box of the skull.
	BoundingBox _skullBox;
	BoundingFrustum _camFrustum;

	uint32 _visibleObjectCount = 0;

	// Keep a system memory copy of the world matrices for culling.
	std::vector<InstancedData> _instancedData;

	bool _frustumCullingEnabled = true;

	DirectionalLight _dirLights[3];
	Material _skullMat;

	// Define transformations from local spaces to world space.
	XMFLOAT4X4 _skullWorld;

	uint32 _skullIndexCount;

	Camera _camera;

	POINT _lastMousePos;
};