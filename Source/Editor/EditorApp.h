#pragma once
#include "App.h"
#include "Waves.h"
#include "LightHelper.h"
#include "BlurFilter.h"
#include "Camera.h"
#include "Vertex.h"
#include "Octree.h"
#include "TextureMgr.h"

class EditorApp : public App
{
public:
	EditorApp(HINSTANCE hInstance);
	~EditorApp();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	virtual void OnSceneRender(ID3D11RenderTargetView* renderTargetView, Camera* camera) override; 
	virtual void _Editor_OnSceneRender(ID3D11RenderTargetView* renderTargetView, EditorCamera* camera) override;

	void OnMouseDown(WPARAM btnState, int32 x, int32 y);
	void OnMouseUp(WPARAM btnState, int32 x, int32 y);
	void OnMouseMove(WPARAM btnState, int32 x, int32 y);

private:
	void DrawSceneToSsaoNormalDepthMap();
	void DrawSceneToShadowMap();
	void DrawScreenQuad(ComPtr<ID3D11ShaderResourceView> srv);
	void BuildShadowTransform();
	void BuildScreenQuadGeometryBuffers();

private:
	TextureMgr _texMgr;

	shared_ptr<class Sky> _sky;

	shared_ptr<class Mesh> _treeModel;
	shared_ptr<class Mesh> _baseModel;
	shared_ptr<class Mesh> _stairsModel;
	shared_ptr<class Mesh> _pillar1Model;
	shared_ptr<class Mesh> _pillar2Model;
	shared_ptr<class Mesh> _pillar3Model;
	shared_ptr<class Mesh> _pillar4Model;
	shared_ptr<class Mesh> _rockModel;

	shared_ptr<class Mesh> _Fbx;


	std::vector<MeshInstance> _modelInstances;
	std::vector<MeshInstance> _alphaClippedModelInstances;

	ComPtr<ID3D11Buffer> _screenQuadVB;
	ComPtr<ID3D11Buffer> _screenQuadIB;

	BoundingSphere _sceneBounds;

	//static const int SMapSize = 2048;
	//shared_ptr<class ShadowMap> _smap;
	XMFLOAT4X4 _lightView;
	XMFLOAT4X4 _lightProj;
	XMFLOAT4X4 _shadowTransform;

	//shared_ptr<class Ssao> _ssao;

	float _lightRotationAngle = 0.f;
	XMFLOAT3 _originalLightDir[3];
	DirectionalLight _dirLights[3];

	//Camera _camera;

	POINT _lastMousePos;
};
