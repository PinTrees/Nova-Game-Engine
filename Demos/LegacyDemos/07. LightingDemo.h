#pragma once
#include "App.h"
#include "Waves.h"
#include "LightHelper.h"

class LightingDemo : public App
{
	struct Vertex
	{
		XMFLOAT3 pos;
		XMFLOAT3 normal;
	};
public:
	LightingDemo(HINSTANCE hInstance);
	~LightingDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int x, int y);
	void OnMouseUp(WPARAM btnState, int x, int y);
	void OnMouseMove(WPARAM btnState, int x, int y);

private:
	float GetHillHeight(float x, float z) { return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z)); }
	XMFLOAT3 GetHillNormal(float x, float z) const;

	void BuildLandGeometryBuffers();
	void BuildWavesGeometryBuffers();
	void BuildFX();
	void BuildVertexLayout();

private:
	ComPtr<ID3D11Buffer> _landVB;
	ComPtr<ID3D11Buffer> _landIB;
	ComPtr<ID3D11Buffer> _wavesVB;
	ComPtr<ID3D11Buffer> _wavesIB;

	Waves _waves;
	DirectionalLight _dirLight;
	PointLight _pointLight;
	SpotLight _spotLight;
	Material _landMat;
	Material _wavesMat;

	ComPtr<ID3DX11Effect> _fx;
	ComPtr<ID3DX11EffectTechnique> _tech;
	ComPtr<ID3DX11EffectMatrixVariable> _fxWorldViewProj;
	ComPtr<ID3DX11EffectMatrixVariable> _fxWorld;
	ComPtr<ID3DX11EffectMatrixVariable> _fxWorldInvTranspose;
	ComPtr<ID3DX11EffectVectorVariable> _fxEyePosW;
	ComPtr<ID3DX11EffectVariable> _fxDirLight;
	ComPtr<ID3DX11EffectVariable> _fxPointLight;
	ComPtr<ID3DX11EffectVariable> _fxSpotLight;
	ComPtr<ID3DX11EffectVariable> _fxMaterial;

	ComPtr<ID3D11InputLayout> _inputLayout;
	
	ComPtr<ID3D11RasterizerState> _wireframeRS;

	// Define transformations from local spaces to world space.
	XMFLOAT4X4 _landWorld;
	XMFLOAT4X4 _wavesWorld;
	
	XMFLOAT4X4 _view;
	XMFLOAT4X4 _proj;

	uint32 _landIndexCount = 0;

	XMFLOAT3 _eyePosW = XMFLOAT3(0, 0, 0);

	float _theta = 1.5f * XM_PI;
	float _phi = 0.1f * XM_PI;
	float _radius = 80.f;

	POINT _lastMousePos;
};