#pragma once
#include "App.h"
#include "Waves.h"
#include "LightHelper.h"
#include "BlurFilter.h"
#include "Camera.h"
#include "Vertex.h"
#include "Terrain.h"
#include "Octree.h"

class SsaoDemo : public App
{
	enum RenderOptions
	{
		RenderOptionsBasic = 0,
		RenderOptionsNormalMap = 1,
		RenderOptionsDisplacementMap = 2
	};

public:
	SsaoDemo(HINSTANCE hInstance);
	~SsaoDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int32 x, int32 y);
	void OnMouseUp(WPARAM btnState, int32 x, int32 y);
	void OnMouseMove(WPARAM btnState, int32 x, int32 y);

private:
	void DrawSceneToSsaoNormalDepthMap();
	void DrawSceneToShadowMap();
	void DrawScreenQuad(ComPtr<ID3D11ShaderResourceView> srv);
	void BuildShadowTransform();
	void BuildShapeGeometryBuffers();
	void BuildSkullGeometryBuffers();
	void BuildScreenQuadGeometryBuffers();

private:
	shared_ptr<class Sky> _sky;

	ComPtr<ID3D11Buffer> _shapesVB;
	ComPtr<ID3D11Buffer> _shapesIB;
	ComPtr<ID3D11Buffer> _skullVB;
	ComPtr<ID3D11Buffer> _skullIB;
	ComPtr<ID3D11Buffer> _screenQuadVB;
	ComPtr<ID3D11Buffer> _screenQuadIB;

	ComPtr<ID3D11ShaderResourceView> _stoneTexSRV;
	ComPtr<ID3D11ShaderResourceView> _brickTexSRV;
	ComPtr<ID3D11ShaderResourceView> _stoneNormalTexSRV;
	ComPtr<ID3D11ShaderResourceView> _brickNormalTexSRV;

	BoundingSphere _sceneBounds;

	static const int SMapSize = 2048;
	shared_ptr<class ShadowMap> _smap;
	XMFLOAT4X4 _lightView;
	XMFLOAT4X4 _lightProj;
	XMFLOAT4X4 _shadowTransform;

	shared_ptr<class Ssao> _ssao;

	float _lightRotationAngle = 0.f;
	XMFLOAT3 _originalLightDir[3];
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

	RenderOptions _renderOptions = RenderOptionsNormalMap;

	Camera _camera;

	POINT _lastMousePos;
};