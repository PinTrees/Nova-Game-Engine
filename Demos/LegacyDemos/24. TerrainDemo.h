#pragma once
#include "App.h"
#include "Waves.h"
#include "LightHelper.h"
#include "BlurFilter.h"
#include "Camera.h"
#include "Vertex.h"
#include "Terrain.h"

class TerrainDemo : public App
{
public:
	TerrainDemo(HINSTANCE hInstance);
	~TerrainDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();

	void OnMouseDown(WPARAM btnState, int x, int y);
	void OnMouseUp(WPARAM btnState, int x, int y);
	void OnMouseMove(WPARAM btnState, int x, int y);

private:
	shared_ptr<class Sky> _sky;
	Terrain _terrain;

	DirectionalLight _dirLights[3];

	Camera _camera;

	bool _walkCamMode = false;

	POINT _lastMousePos;
};