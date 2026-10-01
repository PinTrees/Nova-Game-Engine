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

	virtual void OnSceneRender(GfxRenderTargetView* renderTargetView, Camera* camera) override; 
	virtual void _Editor_OnSceneRender(GfxRenderTargetView* renderTargetView, EditorCamera* camera) override;

	void OnMouseDown(WPARAM btnState, int32 x, int32 y);
	void OnMouseUp(WPARAM btnState, int32 x, int32 y);
	void OnMouseMove(WPARAM btnState, int32 x, int32 y);

private:
	void DrawSceneToSsaoNormalDepthMap();
	void DrawSceneToShadowMap();
	void DrawScreenQuad(ComPtr<GfxShaderResourceView> srv);
	void BuildShadowTransform();
	void BuildScreenQuadGeometryBuffers();

private:
	TextureMgr _texMgr;

	shared_ptr<class Sky> _sky;
	// Scene/Game 뷰를 그릴 때 쓰는 깊이 버퍼 (뷰 크기 이상, 커지기만 함)
	ComPtr<GfxTexture2D> _viewDepthTex;
	ComPtr<GfxDepthStencilView> _viewDepthView;
	ComPtr<GfxDepthStencilView> _viewDepthReadOnly;    // 물: 깊이를 SRV 로 읽으면서 깊이 검사
	ComPtr<GfxShaderResourceView> _viewDepthSRV;
	UINT _viewDepthW = 0, _viewDepthH = 0;
	GfxDepthStencilView* ViewDepth(UINT width, UINT height);
	void DrawWater(CXMMATRIX view, CXMMATRIX proj, const XMFLOAT3& eye, GfxRenderTargetView* target, GfxDepthStencilView* dsv,
		const D3D11_VIEWPORT& viewport, const vector<DirectionalLight>& dirLights, bool skyVisible, class ShadowMap* shadowMap, const void* shadowFrame,
		const void* atmosphere);
	// Volume 의 안개·대기 (불투명 + 하늘 다음 화면 전체 패스). 값을 돌려줘 물이 같은 값을 쓴다
	void DrawAtmosphere(const void* params, CXMMATRIX viewProj, const XMFLOAT3& eye, GfxRenderTargetView* target, GfxDepthStencilView* dsv,
		const D3D11_VIEWPORT& viewport, bool skyVisible);

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

	ComPtr<GfxBuffer> _screenQuadVB;
	ComPtr<GfxBuffer> _screenQuadIB;

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
