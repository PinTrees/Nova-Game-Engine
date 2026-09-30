#pragma once
#include "App.h"
#include "Waves.h"
#include "LightHelper.h"

class MirrorDemo : public App
{
	enum RenderOptions
	{
		Lighting = 0,
		Textures = 1,
		TexturesAndFog = 2
	};
public:
	MirrorDemo(HINSTANCE hInstance);
	~MirrorDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int x, int y);
	void OnMouseUp(WPARAM btnState, int x, int y);
	void OnMouseMove(WPARAM btnState, int x, int y);

private:
	void BuildRoomGeometryBuffers();
	void BuildSkullGeometryBuffers();

private:
	ComPtr<ID3D11Buffer> _roomVB;

	ComPtr<ID3D11Buffer> _skullVB;
	ComPtr<ID3D11Buffer> _skullIB;

	ComPtr<ID3D11ShaderResourceView> _floorDiffuseMapSRV;
	ComPtr<ID3D11ShaderResourceView> _wallDiffuseMapSRV;
	ComPtr<ID3D11ShaderResourceView> _mirrorDiffuseMapSRV;

	DirectionalLight _dirLights[3];
	Material _roomMat;
	Material _skullMat;
	Material _mirrorMat;
	Material _shadowMat;

	XMFLOAT4X4 _roomWorld;
	XMFLOAT4X4 _skullWorld;

	uint32 _skullIndexCount;
	XMFLOAT3 _skullTranslation = {0.0f, 1.0f, -5.0f};

	XMFLOAT4X4 _view;
	XMFLOAT4X4 _proj;

	RenderOptions _renderOptions = RenderOptions::Textures;

	XMFLOAT3 _eyePosW = {0, 0, 0};

	float _theta = 1.24f * XM_PI;
	float _phi = 0.42f * XM_PI;
	float _radius = 12.0f;

	POINT _lastMousePos;
};
