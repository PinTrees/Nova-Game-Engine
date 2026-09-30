#pragma once
#include "App.h"
#include "Waves.h"
#include "LightHelper.h"
#include "BlurFilter.h"

class Camera;

class CameraDemo : public App
{
public:
	CameraDemo(HINSTANCE hInstance);
	~CameraDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int x, int y);
	void OnMouseUp(WPARAM btnState, int x, int y);
	void OnMouseMove(WPARAM btnState, int x, int y);

private:
	void BuildShapeGeometryBuffers();
	void BuildSkullGeometryBuffers();

private:
	ComPtr<ID3D11Buffer> _shapesVB;
	ComPtr<ID3D11Buffer> _shapesIB;
	ComPtr<ID3D11Buffer> _skullVB;
	ComPtr<ID3D11Buffer> _skullIB;

	ComPtr<ID3D11ShaderResourceView> _floorTexSRV;
	ComPtr<ID3D11ShaderResourceView> _stoneTexSRV;
	ComPtr<ID3D11ShaderResourceView> _brickTexSRV;

	DirectionalLight _dirLights[3];
	Material _gridMat;
	Material _boxMat;
	Material _cylinderMat;
	Material _sphereMat;
	Material _skullMat;

	// Define transformations from local spaces to world space.
	XMFLOAT4X4 _sphereWorld[10];
	XMFLOAT4X4 _cylWorld[10];
	XMFLOAT4X4 _boxWorld;
	XMFLOAT4X4 _gridWorld;
	XMFLOAT4X4 _skullWorld;

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

	uint32 _skullIndexCount = 0;
	uint32 _lightCount = 3;

	Camera _camera;

	POINT _lastMousePos;
};