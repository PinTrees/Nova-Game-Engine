#pragma once
#include "App.h"
#include "Waves.h"
#include "LightHelper.h"
#include "BlurFilter.h"
#include "Camera.h"
#include "Vertex.h"
#include "AnimationHelper.h"

class QuatDemo : public App
{
public:
	QuatDemo(HINSTANCE hInstance);
	~QuatDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int32 x, int32 y);
	void OnMouseUp(WPARAM btnState, int32 x, int32 y);
	void OnMouseMove(WPARAM btnState, int32 x, int32 y);

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

	Camera _camera;

	float _animTimePos = 0;
	BoneAnimationTemp _skullAnimation;

	POINT _lastMousePos;
};