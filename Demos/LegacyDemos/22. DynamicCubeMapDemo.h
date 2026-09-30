#pragma once
#include "App.h"
#include "Waves.h"
#include "LightHelper.h"
#include "BlurFilter.h"
#include "Camera.h"
#include "Vertex.h"

class DynamicCubeMapDemo : public App
{
public:
	DynamicCubeMapDemo(HINSTANCE hInstance);
	~DynamicCubeMapDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int x, int y);
	void OnMouseUp(WPARAM btnState, int x, int y);
	void OnMouseMove(WPARAM btnState, int x, int y);

private:
	void DrawScene(const Camera& camera, bool drawSkull);
	void BuildCubeFaceCamera(float x, float y, float z);
	void BuildDynamicCubeMapViews();
	void BuildShapeGeometryBuffers();
	void BuildSkullGeometryBuffers();

private:
	shared_ptr<class Sky> _sky;

	ComPtr<ID3D11Buffer> _shapesVB;
	ComPtr<ID3D11Buffer> _shapesIB;
	ComPtr<ID3D11Buffer> _skullVB;
	ComPtr<ID3D11Buffer> _skullIB;

	ComPtr<ID3D11ShaderResourceView> _floorTexSRV;
	ComPtr<ID3D11ShaderResourceView> _stoneTexSRV;
	ComPtr<ID3D11ShaderResourceView> _brickTexSRV;

	ComPtr<ID3D11DepthStencilView> _dynamicCubeMapDSV;
	ComPtr<ID3D11RenderTargetView> _dynamicCubeMapRTV[6];
	ComPtr<ID3D11ShaderResourceView> _dynamicCubeMapSRV;
	D3D11_VIEWPORT _cubeMapViewport;

	static const int CubeMapSize = 256;

	DirectionalLight _dirLights[3];
	Material _gridMat;
	Material _boxMat;
	Material _cylinderMat;
	Material _sphereMat;
	Material _skullMat;
	Material _centerSphereMat;

	// Define transformations from local spaces to world space.
	XMFLOAT4X4 _sphereWorld[10];
	XMFLOAT4X4 _cylWorld[10];
	XMFLOAT4X4 _boxWorld;
	XMFLOAT4X4 _gridWorld;
	XMFLOAT4X4 _skullWorld;
	XMFLOAT4X4 _centerSphereWorld;

	int32 _boxVertexOffset;
	int32 _gridVertexOffset;
	int32 _sphereVertexOffset;
	int32 _cylinderVertexOffset;

	uint32 _boxIndexOffset;
	uint32 _gridIndexOffset;
	uint32 _sphereIndexOffset;
	uint32 _cylinderIndexOffset;
	uint32 _boxIndexCount;
	uint32 _gridIndexCount;
	uint32 _sphereIndexCount;
	uint32 _cylinderIndexCount;
	uint32 _skullIndexCount = 0;
	uint32 _lightCount = 3;

	Camera _camera;
	Camera _cubeMapCamera[6];

	POINT _lastMousePos;
};