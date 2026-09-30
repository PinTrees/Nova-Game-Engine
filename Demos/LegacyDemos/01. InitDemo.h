#pragma once
#include "App.h"

class InitDemo : public App
{
public:
	InitDemo(HINSTANCE hInstance);
	~InitDemo();

	bool Init();
	void OnResize();
	void UpdateScene(float dt);
	void RenderApplication();
};