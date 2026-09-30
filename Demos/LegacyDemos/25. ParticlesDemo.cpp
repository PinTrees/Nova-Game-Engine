#include "pch.h"
#include "25. ParticlesDemo.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "Vertex.h"
#include "RenderStates.h"
#include "Sky.h"

ParticlesDemo::ParticlesDemo(HINSTANCE hInstance)
	: App(hInstance)
{
	_mainWindowCaption = L"Particles Demo";
	_enable4xMsaa = false;

	_lastMousePos.x = 0;
	_lastMousePos.y = 0;

	_camera.GetGameObject()->GetTransform()->SetPosition(0.0f, 2.0f, 100.0f);

	_dirLights[0].Ambient = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
	_dirLights[0].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_dirLights[0].Specular = XMFLOAT4(0.8f, 0.8f, 0.7f, 1.0f);
	_dirLights[0].Direction = XMFLOAT3(0.707f, -0.707f, 0.0f);

	_dirLights[1].Ambient = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	_dirLights[1].Diffuse = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	_dirLights[1].Specular = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	_dirLights[1].Direction = XMFLOAT3(0.57735f, -0.57735f, 0.57735f);

	_dirLights[2].Ambient = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	_dirLights[2].Diffuse = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	_dirLights[2].Specular = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	_dirLights[2].Direction = XMFLOAT3(-0.57735f, -0.57735f, -0.57735f);
}

ParticlesDemo::~ParticlesDemo()
{
	_deviceContext->ClearState();

	Effects::DestroyAll();
	InputLayouts::DestroyAll();
	RenderStates::DestroyAll();
}

bool ParticlesDemo::Init()
{
	if (!App::Init())
		return false;

	// Must init Effects first since InputLayouts depend on shader signatures.
	Effects::InitAll(_device, L"../Shaders/22. Basic.fx");
	InputLayouts::InitAll(_device);
	RenderStates::InitAll(_device);

	_sky = make_shared<Sky>(_device, L"../Resources/Textures/grasscube1024.dds", 5000.0f);

	Terrain::InitInfo tii;
	tii.heightMapFilename = L"../Resources/Textures/terrain.raw";
	tii.layerMapFilename0 = L"../Resources/Textures/grass.dds";
	tii.layerMapFilename1 = L"../Resources/Textures/darkdirt.dds";
	tii.layerMapFilename2 = L"../Resources/Textures/stone.dds";
	tii.layerMapFilename3 = L"../Resources/Textures/lightdirt.dds";
	tii.layerMapFilename4 = L"../Resources/Textures/snow.dds";
	tii.blendMapFilename = L"../Resources/Textures/blend.dds";
	tii.heightScale = 50.0f;
	tii.heightmapWidth = 2049;
	tii.heightmapHeight = 2049;
	tii.cellSpacing = 0.5f;

	_terrain.Init(_device, _deviceContext, tii);

	_randomTexSRV = Utils::CreateRandomTexture1DSRV(_device);

	std::vector<std::wstring> flares;
	flares.push_back(L"../Resources/Textures/flare0.dds");
	_flareTexSRV = Utils::CreateTexture2DArraySRV(_device, _deviceContext, flares);

	_fire.Init(_device, Effects::FireFX, _flareTexSRV, _randomTexSRV, 500);
	_fire.SetEmitPos(XMFLOAT3(0.0f, 1.0f, 120.0f));

	std::vector<std::wstring> raindrops;
	raindrops.push_back(L"../Resources/Textures/raindrop.dds");
	_rainTexSRV = Utils::CreateTexture2DArraySRV(_device, _deviceContext, raindrops);

	_rain.Init(_device, Effects::RainFX, _rainTexSRV, _randomTexSRV, 10000);

	return true;
}

void ParticlesDemo::OnResize()
{
	App::OnResize();

	_camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 3000.0f);
}

void ParticlesDemo::UpdateScene(float dt)
{
	//
	// Control the camera.
	//
	if (GetAsyncKeyState('W') & 0x8000)
		_camera.Walk(10.0f * dt);

	if (GetAsyncKeyState('S') & 0x8000)
		_camera.Walk(-10.0f * dt);

	if (GetAsyncKeyState('A') & 0x8000)
		_camera.Strafe(-10.0f * dt);

	if (GetAsyncKeyState('D') & 0x8000)
		_camera.Strafe(10.0f * dt);

	//
	// Walk/fly mode
	//
	if (GetAsyncKeyState('2') & 0x8000)
		_walkCamMode = true;
	if (GetAsyncKeyState('3') & 0x8000)
		_walkCamMode = false;

	// 
	// Clamp camera to terrain surface in walk mode.
	//
	if (_walkCamMode)
	{
		XMFLOAT3 camPos = _camera.GetPosition();
		float y = _terrain.GetHeight(camPos.x, camPos.z);
		_camera.GetGameObject()->GetComponent<Transform>()->SetPosition(camPos.x, y + 2.0f, camPos.z);
	}

	//
	// Reset particle systems.
	//
	if (GetAsyncKeyState('R') & 0x8000)
	{
		_fire.Reset();
		_rain.Reset();
	}

	_fire.Update(dt, _timer.TotalTime());
	_rain.Update(dt, _timer.TotalTime());

	_camera.UpdateViewMatrix();
}

void ParticlesDemo::RenderApplication()
{
	_deviceContext->ClearRenderTargetView(_renderTargetView.Get(), reinterpret_cast<const float*>(&Colors::Silver));
	_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	_deviceContext->IASetInputLayout(InputLayouts::Basic32.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };

	if (GetAsyncKeyState('1') & 0x8000)
		_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

	_terrain.Draw(_deviceContext, _camera, _dirLights);

	_deviceContext->RSSetState(0);

	_sky->Draw(_deviceContext, _camera);


	// Draw particle systems last so it is blended with scene.
	_fire.SetEyePos(_camera.GetPosition());
	_fire.Draw(_deviceContext, _camera);
	_deviceContext->OMSetBlendState(0, blendFactor, 0xffffffff); // restore default

	_rain.SetEyePos(_camera.GetPosition());
	_rain.SetEmitPos(_camera.GetPosition());
	_rain.Draw(_deviceContext, _camera);

	// restore default states.
	_deviceContext->RSSetState(0);
	_deviceContext->OMSetDepthStencilState(0, 0);
	_deviceContext->OMSetBlendState(0, blendFactor, 0xffffffff);

	HR(_swapChain->Present(0, 0));
}

void ParticlesDemo::OnMouseDown(WPARAM btnState, int32 x, int32 y)
{
	_lastMousePos.x = x;
	_lastMousePos.y = y;

	SetCapture(_hMainWnd);
}

void ParticlesDemo::OnMouseUp(WPARAM btnState, int32 x, int32 y)
{
	ReleaseCapture();
}

void ParticlesDemo::OnMouseMove(WPARAM btnState, int32 x, int32 y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - _lastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - _lastMousePos.y));

		_camera.Pitch(dy);
		_camera.RotateY(dx);
	}

	_lastMousePos.x = x;
	_lastMousePos.y = y;
}
