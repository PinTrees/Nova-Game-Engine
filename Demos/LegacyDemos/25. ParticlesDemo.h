#pragma once
#include "App.h"
#include "Waves.h"
#include "LightHelper.h"
#include "BlurFilter.h"
#include "Camera.h"
#include "Vertex.h"
#include "Terrain.h"
#include "ParticleSystem.h"

class ParticlesDemo : public App
{
public:
	ParticlesDemo(HINSTANCE hInstance);
	~ParticlesDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int32 x, int32 y);
	void OnMouseUp(WPARAM btnState, int32 x, int32 y);
	void OnMouseMove(WPARAM btnState, int32 x, int32 y);

private:
	shared_ptr<class Sky> _sky;
	Terrain _terrain;

	ComPtr<ID3D11ShaderResourceView> _flareTexSRV;
	ComPtr<ID3D11ShaderResourceView> _rainTexSRV;
	ComPtr<ID3D11ShaderResourceView> _randomTexSRV;

	ParticleSystem _fire;
	ParticleSystem _rain;

	DirectionalLight _dirLights[3];

	Camera _camera;

	bool _walkCamMode = false;

	POINT _lastMousePos;
};