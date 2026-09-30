#pragma once
#include "App.h"
#include "Waves.h"
#include "LightHelper.h"
#include "BlurFilter.h"
#include "Camera.h"
#include "Vertex.h"

class PickingDemo : public App
{
public:
	PickingDemo(HINSTANCE hInstance);
	~PickingDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int x, int y);
	void OnMouseUp(WPARAM btnState, int x, int y);
	void OnMouseMove(WPARAM btnState, int x, int y);

private:
	void BuildMeshGeometryBuffers();
	void Pick(int32 sx, int32 sy);

private:
	ComPtr<ID3D11Buffer> _meshVB;
	ComPtr<ID3D11Buffer> _meshIB;

	// Keep system memory copies of the Mesh geometry for picking.
	std::vector<Vertex::Basic32> _meshVertices;
	std::vector<uint32> _meshIndices;

	BoundingBox _meshBox;

	DirectionalLight _dirLights[3];
	Material _meshMat;
	Material _pickedTriangleMat;

	// Define transformations from local spaces to world space.
	XMFLOAT4X4 _meshWorld;

	uint32 _meshIndexCount = 0;
	uint32 _pickedTriangle = -1;

	Camera _camera;
	POINT _lastMousePos;
};
