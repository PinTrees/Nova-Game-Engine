#pragma once
#include "App.h"
#include "Waves.h"
#include "LightHelper.h"
#include "BlurFilter.h"
#include "Camera.h"
#include "Vertex.h"
#include "Terrain.h"
#include "Octree.h"

class AmbientOcclusionDemo : public App
{
public:
	AmbientOcclusionDemo(HINSTANCE hInstance);
	~AmbientOcclusionDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int32 x, int32 y);
	void OnMouseUp(WPARAM btnState, int32 x, int32 y);
	void OnMouseMove(WPARAM btnState, int32 x, int32 y);

private:
	void BuildVertexAmbientOcclusion(
		std::vector<Vertex::AmbientOcclusion>& vertices,
		const std::vector<uint32>& indices);

	void BuildSkullGeometryBuffers();

private:
	ComPtr<ID3D11Buffer> _skullVB;
	ComPtr<ID3D11Buffer> _skullIB;

	XMFLOAT4X4 _skullWorld;
	uint32 _skullIndexCount = 0;

	Camera _camera;

	POINT _lastMousePos;
};