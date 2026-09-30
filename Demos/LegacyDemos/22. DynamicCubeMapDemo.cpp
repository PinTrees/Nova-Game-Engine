#include "pch.h"
#include "22. DynamicCubeMapDemo.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "Vertex.h"
#include "RenderStates.h"
#include "Sky.h"


DynamicCubeMapDemo::DynamicCubeMapDemo(HINSTANCE hInstance)
	: App(hInstance)
{
	_mainWindowCaption = L"Dynamic CubeMap Demo";

	_lastMousePos.x = 0;
	_lastMousePos.y = 0;

	//_camera.SetPosition(0.0f, 2.0f, -15.0f);

	BuildCubeFaceCamera(0.0f, 2.0f, 0.0f);

	XMMATRIX I = XMMatrixIdentity();
	XMStoreFloat4x4(&_gridWorld, I);

	XMMATRIX boxScale = XMMatrixScaling(3.0f, 1.0f, 3.0f);
	XMMATRIX boxOffset = XMMatrixTranslation(0.0f, 0.5f, 0.0f);
	XMStoreFloat4x4(&_boxWorld, XMMatrixMultiply(boxScale, boxOffset));

	XMMATRIX centerSphereScale = XMMatrixScaling(2.0f, 2.0f, 2.0f);
	XMMATRIX centerSphereOffset = XMMatrixTranslation(0.0f, 2.0f, 0.0f);
	XMStoreFloat4x4(&_centerSphereWorld, XMMatrixMultiply(centerSphereScale, centerSphereOffset));

	for (int i = 0; i < 5; ++i)
	{
		XMStoreFloat4x4(&_cylWorld[i * 2 + 0], XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f));
		XMStoreFloat4x4(&_cylWorld[i * 2 + 1], XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f));

		XMStoreFloat4x4(&_sphereWorld[i * 2 + 0], XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f));
		XMStoreFloat4x4(&_sphereWorld[i * 2 + 1], XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f));
	}

	_dirLights[0].Ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	_dirLights[0].Diffuse = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	_dirLights[0].Specular = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	_dirLights[0].Direction = XMFLOAT3(0.57735f, -0.57735f, 0.57735f);

	_dirLights[1].Ambient = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	_dirLights[1].Diffuse = XMFLOAT4(0.20f, 0.20f, 0.20f, 1.0f);
	_dirLights[1].Specular = XMFLOAT4(0.25f, 0.25f, 0.25f, 1.0f);
	_dirLights[1].Direction = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);

	_dirLights[2].Ambient = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	_dirLights[2].Diffuse = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	_dirLights[2].Specular = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	_dirLights[2].Direction = XMFLOAT3(0.0f, -0.707f, -0.707f);

	_gridMat.Ambient = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
	_gridMat.Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
	_gridMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 16.0f);
	_gridMat.Reflect = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

	_cylinderMat.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_cylinderMat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_cylinderMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 16.0f);
	_cylinderMat.Reflect = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

	_sphereMat.Ambient = XMFLOAT4(0.6f, 0.8f, 1.0f, 1.0f);
	_sphereMat.Diffuse = XMFLOAT4(0.6f, 0.8f, 1.0f, 1.0f);
	_sphereMat.Specular = XMFLOAT4(0.9f, 0.9f, 0.9f, 16.0f);
	_sphereMat.Reflect = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

	_boxMat.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_boxMat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_boxMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 16.0f);
	_boxMat.Reflect = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

	_skullMat.Ambient = XMFLOAT4(0.4f, 0.4f, 0.4f, 1.0f);
	_skullMat.Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
	_skullMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 16.0f);
	_skullMat.Reflect = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

	_centerSphereMat.Ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	_centerSphereMat.Diffuse = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	_centerSphereMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 16.0f);
	_centerSphereMat.Reflect = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
}

DynamicCubeMapDemo::~DynamicCubeMapDemo()
{
	Effects::DestroyAll();
	InputLayouts::DestroyAll();
}

bool DynamicCubeMapDemo::Init()
{
	if (!App::Init())
		return false;

	// Must init Effects first since InputLayouts depend on shader signatures.
	Effects::InitAll(_device, L"../Shaders/22. Basic.fx");
	InputLayouts::InitAll(_device);

	_sky = make_shared<Sky>(_device, L"../Resources/Textures/sunsetcube1024.dds", 5000.0f);

	_floorTexSRV = Utils::LoadTexture(_device, L"../Resources/Textures/floor.dds");
	_stoneTexSRV = Utils::LoadTexture(_device, L"../Resources/Textures/stone.dds");
	_brickTexSRV = Utils::LoadTexture(_device, L"../Resources/Textures/bricks.dds");

	BuildDynamicCubeMapViews();
	BuildShapeGeometryBuffers();
	BuildSkullGeometryBuffers();

	return true;
}

void DynamicCubeMapDemo::OnResize()
{
	App::OnResize();

	_camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void DynamicCubeMapDemo::UpdateScene(float dt)
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
	// Switch the number of lights based on key presses.
	//
	if (GetAsyncKeyState('0') & 0x8000)
		_lightCount = 0;

	if (GetAsyncKeyState('1') & 0x8000)
		_lightCount = 1;

	if (GetAsyncKeyState('2') & 0x8000)
		_lightCount = 2;

	if (GetAsyncKeyState('3') & 0x8000)
		_lightCount = 3;

	//
	// Animate the skull around the center sphere.
	//

	XMMATRIX skullScale = ::XMMatrixScaling(0.2f, 0.2f, 0.2f);
	XMMATRIX skullOffset = ::XMMatrixTranslation(3.0f, 2.0f, 0.0f);
	XMMATRIX skullLocalRotate = ::XMMatrixRotationY(2.0f * _timer.TotalTime());
	XMMATRIX skullGlobalRotate = ::XMMatrixRotationY(0.5f * _timer.TotalTime());
	::XMStoreFloat4x4(&_skullWorld, skullScale * skullLocalRotate * skullOffset * skullGlobalRotate);

	_camera.UpdateViewMatrix();
}

void DynamicCubeMapDemo::RenderApplication()
{
	ID3D11RenderTargetView* renderTargets[1];

	// Generate the cube map.
	_deviceContext->RSSetViewports(1, &_cubeMapViewport);
	for (int i = 0; i < 6; ++i)
	{
		// Clear cube map face and depth buffer.
		_deviceContext->ClearRenderTargetView(_dynamicCubeMapRTV[i].Get(), reinterpret_cast<const float*>(&Colors::Silver));
		_deviceContext->ClearDepthStencilView(_dynamicCubeMapDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

		// Bind cube map face as render target.
		renderTargets[0] = _dynamicCubeMapRTV[i].Get();
		_deviceContext->OMSetRenderTargets(1, renderTargets, _dynamicCubeMapDSV.Get());

		// Draw the scene with the exception of the center sphere to this cube map face.
		DrawScene(_cubeMapCamera[i], false);
	}

	// Restore old viewport and render targets.
	_deviceContext->RSSetViewports(1, &_viewport);
	renderTargets[0] = _renderTargetView.Get();
	_deviceContext->OMSetRenderTargets(1, renderTargets, _depthStencilView.Get());

	// Have hardware generate lower mipmap levels of cube map.
	_deviceContext->GenerateMips(_dynamicCubeMapSRV.Get());

	// Now draw the scene as normal, but with the center sphere.
	_deviceContext->ClearRenderTargetView(_renderTargetView.Get(), reinterpret_cast<const float*>(&Colors::Silver));
	_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	DrawScene(_camera, true);

	HR(_swapChain->Present(0, 0));
}

void DynamicCubeMapDemo::OnMouseDown(WPARAM btnState, int x, int y)
{
	_lastMousePos.x = x;
	_lastMousePos.y = y;

	SetCapture(_hMainWnd);
}

void DynamicCubeMapDemo::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void DynamicCubeMapDemo::OnMouseMove(WPARAM btnState, int x, int y)
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

void DynamicCubeMapDemo::DrawScene(const Camera& camera, bool drawCenterSphere)
{
	_deviceContext->IASetInputLayout(InputLayouts::Basic32.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	uint32 stride = sizeof(Vertex::Basic32);
	uint32 offset = 0;

	XMMATRIX view = camera.View();
	XMMATRIX proj = camera.Proj();
	XMMATRIX viewProj = camera.ViewProj();

	float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// Set per frame constants.
	Effects::BasicFX->SetDirLights(_dirLights);
	Effects::BasicFX->SetEyePosW(_camera.GetPosition());

	// Figure out which technique to use.   

	ComPtr<ID3DX11EffectTechnique> activeTexTech = Effects::BasicFX->Light1TexTech;
	ComPtr<ID3DX11EffectTechnique> activeSkullTech = Effects::BasicFX->Light1Tech;
	ComPtr<ID3DX11EffectTechnique> activeReflectTech = Effects::BasicFX->Light1ReflectTech;

	switch (_lightCount)
	{
	case 1:
		activeTexTech = Effects::BasicFX->Light1TexTech;
		activeSkullTech = Effects::BasicFX->Light1Tech;
		activeReflectTech = Effects::BasicFX->Light1ReflectTech;
		break;
	case 2:
		activeTexTech = Effects::BasicFX->Light2TexTech;
		activeSkullTech = Effects::BasicFX->Light2Tech;
		activeReflectTech = Effects::BasicFX->Light2ReflectTech;
		break;
	case 3:
		activeTexTech = Effects::BasicFX->Light3TexTech;
		activeSkullTech = Effects::BasicFX->Light3Tech;
		activeReflectTech = Effects::BasicFX->Light3ReflectTech;
		break;
	}

	XMMATRIX world;
	XMMATRIX worldInvTranspose;
	XMMATRIX worldViewProj;

	//
	// Draw the skull.
	//
	D3DX11_TECHNIQUE_DESC techDesc;
	activeSkullTech->GetDesc(&techDesc);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		_deviceContext->IASetVertexBuffers(0, 1, _skullVB.GetAddressOf(), &stride, &offset);
		_deviceContext->IASetIndexBuffer(_skullIB.Get(), DXGI_FORMAT_R32_UINT, 0);

		world = XMLoadFloat4x4(&_skullWorld);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldViewProj = world * view * proj;

		Effects::BasicFX->SetWorld(world);
		Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BasicFX->SetWorldViewProj(worldViewProj);
		Effects::BasicFX->SetTexTransform(XMMatrixIdentity());
		Effects::BasicFX->SetMaterial(_skullMat);

		activeSkullTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_skullIndexCount, 0, 0);
	}

	_deviceContext->IASetVertexBuffers(0, 1, _shapesVB.GetAddressOf(), &stride, &offset);
	_deviceContext->IASetIndexBuffer(_shapesIB.Get(), DXGI_FORMAT_R32_UINT, 0);

	//
	// Draw the grid, cylinders, spheres and box without any cubemap reflection.
	// 

	activeTexTech->GetDesc(&techDesc);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		// Draw the grid.
		world = XMLoadFloat4x4(&_gridWorld);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldViewProj = world * view * proj;

		Effects::BasicFX->SetWorld(world);
		Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BasicFX->SetWorldViewProj(worldViewProj);
		Effects::BasicFX->SetTexTransform(XMMatrixScaling(6.0f, 8.0f, 1.0f));
		Effects::BasicFX->SetMaterial(_gridMat);
		Effects::BasicFX->SetDiffuseMap(_floorTexSRV.Get());

		activeTexTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_gridIndexCount, _gridIndexOffset, _gridVertexOffset);

		// Draw the box.
		world = XMLoadFloat4x4(&_boxWorld);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldViewProj = world * view * proj;

		Effects::BasicFX->SetWorld(world);
		Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BasicFX->SetWorldViewProj(worldViewProj);
		Effects::BasicFX->SetTexTransform(XMMatrixIdentity());
		Effects::BasicFX->SetMaterial(_boxMat);
		Effects::BasicFX->SetDiffuseMap(_stoneTexSRV.Get());

		activeTexTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_boxIndexCount, _boxIndexOffset, _boxVertexOffset);

		// Draw the cylinders.
		for (int32 i = 0; i < 10; ++i)
		{
			world = XMLoadFloat4x4(&_cylWorld[i]);
			worldInvTranspose = MathHelper::InverseTranspose(world);
			worldViewProj = world * view * proj;

			Effects::BasicFX->SetWorld(world);
			Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
			Effects::BasicFX->SetWorldViewProj(worldViewProj);
			Effects::BasicFX->SetTexTransform(XMMatrixIdentity());
			Effects::BasicFX->SetMaterial(_cylinderMat);
			Effects::BasicFX->SetDiffuseMap(_brickTexSRV.Get());

			activeTexTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
			_deviceContext->DrawIndexed(_cylinderIndexCount, _cylinderIndexOffset, _cylinderVertexOffset);
		}

		// Draw the spheres.
		for (int32 i = 0; i < 10; ++i)
		{
			world = XMLoadFloat4x4(&_sphereWorld[i]);
			worldInvTranspose = MathHelper::InverseTranspose(world);
			worldViewProj = world * view * proj;

			Effects::BasicFX->SetWorld(world);
			Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
			Effects::BasicFX->SetWorldViewProj(worldViewProj);
			Effects::BasicFX->SetTexTransform(XMMatrixIdentity());
			Effects::BasicFX->SetMaterial(_sphereMat);
			Effects::BasicFX->SetDiffuseMap(_stoneTexSRV.Get());

			activeTexTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
			_deviceContext->DrawIndexed(_sphereIndexCount, _sphereIndexOffset, _sphereVertexOffset);
		}
	}

	//
	// Draw the center sphere with the dynamic cube map.
	//
	if (drawCenterSphere)
	{
		activeReflectTech->GetDesc(&techDesc);
		for (UINT p = 0; p < techDesc.Passes; ++p)
		{
			// Draw the center sphere.

			world = XMLoadFloat4x4(&_centerSphereWorld);
			worldInvTranspose = MathHelper::InverseTranspose(world);
			worldViewProj = world * view * proj;

			Effects::BasicFX->SetWorld(world);
			Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
			Effects::BasicFX->SetWorldViewProj(worldViewProj);
			Effects::BasicFX->SetTexTransform(XMMatrixIdentity());
			Effects::BasicFX->SetMaterial(_centerSphereMat);
			Effects::BasicFX->SetDiffuseMap(_stoneTexSRV.Get());
			Effects::BasicFX->SetCubeMap(_dynamicCubeMapSRV.Get());

			activeReflectTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
			_deviceContext->DrawIndexed(_sphereIndexCount, _sphereIndexOffset, _sphereVertexOffset);
		}
	}

	_sky->Draw(_deviceContext, camera);

	// restore default states, as the SkyFX changes them in the effect file.
	_deviceContext->RSSetState(0);
	_deviceContext->OMSetDepthStencilState(0, 0);
}

void DynamicCubeMapDemo::BuildCubeFaceCamera(float x, float y, float z)
{
	// Generate the cube map about the given position.
	XMFLOAT3 center(x, y, z);
	XMFLOAT3 worldUp(0.0f, 1.0f, 0.0f);

	// Look along each coordinate axis.
	XMFLOAT3 targets[6] =
	{
		XMFLOAT3(x + 1.0f, y, z), // +X
		XMFLOAT3(x - 1.0f, y, z), // -X
		XMFLOAT3(x, y + 1.0f, z), // +Y
		XMFLOAT3(x, y - 1.0f, z), // -Y
		XMFLOAT3(x, y, z + 1.0f), // +Z
		XMFLOAT3(x, y, z - 1.0f)  // -Z
	};

	// Use world up vector (0,1,0) for all directions except +Y/-Y.  In these cases, we
	// are looking down +Y or -Y, so we need a different "up" vector.
	XMFLOAT3 ups[6] =
	{
		XMFLOAT3(0.0f, 1.0f, 0.0f),  // +X
		XMFLOAT3(0.0f, 1.0f, 0.0f),  // -X
		XMFLOAT3(0.0f, 0.0f, -1.0f), // +Y
		XMFLOAT3(0.0f, 0.0f, +1.0f), // -Y
		XMFLOAT3(0.0f, 1.0f, 0.0f),	 // +Z
		XMFLOAT3(0.0f, 1.0f, 0.0f)	 // -Z
	};

	for (int32 i = 0; i < 6; ++i)
	{
		_cubeMapCamera[i].LookAt(center, targets[i], ups[i]);
		_cubeMapCamera[i].SetLens(0.5f * XM_PI, 1.0f, 0.1f, 1000.0f);
		_cubeMapCamera[i].UpdateViewMatrix();
	}
}

void DynamicCubeMapDemo::BuildDynamicCubeMapViews()
{
	//
	// Cubemap is a special texture array with 6 elements.
	//

	D3D11_TEXTURE2D_DESC texDesc;
	texDesc.Width = CubeMapSize;
	texDesc.Height = CubeMapSize;
	texDesc.MipLevels = 0;
	texDesc.ArraySize = 6;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS | D3D11_RESOURCE_MISC_TEXTURECUBE;

	ComPtr<ID3D11Texture2D> cubeTex;
	HR(_device->CreateTexture2D(&texDesc, 0, cubeTex.GetAddressOf()));

	//
	// Create a render target view to each cube map face 
	// (i.e., each element in the texture array).
	// 

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
	rtvDesc.Format = texDesc.Format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
	rtvDesc.Texture2DArray.ArraySize = 1;
	rtvDesc.Texture2DArray.MipSlice = 0;

	for (int32 i = 0; i < 6; ++i)
	{
		rtvDesc.Texture2DArray.FirstArraySlice = i;
		HR(_device->CreateRenderTargetView(cubeTex.Get(), &rtvDesc, _dynamicCubeMapRTV[i].GetAddressOf()));
	}

	//
	// Create a shader resource view to the cube map.
	//

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = -1;

	HR(_device->CreateShaderResourceView(cubeTex.Get(), &srvDesc, _dynamicCubeMapSRV.GetAddressOf()));

	//
	// We need a depth texture for rendering the scene into the cubemap
	// that has the same resolution as the cubemap faces.  
	//

	D3D11_TEXTURE2D_DESC depthTexDesc;
	depthTexDesc.Width = CubeMapSize;
	depthTexDesc.Height = CubeMapSize;
	depthTexDesc.MipLevels = 1;
	depthTexDesc.ArraySize = 1;
	depthTexDesc.SampleDesc.Count = 1;
	depthTexDesc.SampleDesc.Quality = 0;
	depthTexDesc.Format = DXGI_FORMAT_D32_FLOAT;
	depthTexDesc.Usage = D3D11_USAGE_DEFAULT;
	depthTexDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	depthTexDesc.CPUAccessFlags = 0;
	depthTexDesc.MiscFlags = 0;

	ComPtr<ID3D11Texture2D> depthTex;
	HR(_device->CreateTexture2D(&depthTexDesc, 0, depthTex.GetAddressOf()));

	// Create the depth stencil view for the entire cube
	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Format = depthTexDesc.Format;
	dsvDesc.Flags = 0;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Texture2D.MipSlice = 0;
	HR(_device->CreateDepthStencilView(depthTex.Get(), &dsvDesc, _dynamicCubeMapDSV.GetAddressOf()));

	//
	// Viewport for drawing into cubemap.
	// 

	_cubeMapViewport.TopLeftX = 0.0f;
	_cubeMapViewport.TopLeftY = 0.0f;
	_cubeMapViewport.Width = (float)CubeMapSize;
	_cubeMapViewport.Height = (float)CubeMapSize;
	_cubeMapViewport.MinDepth = 0.0f;
	_cubeMapViewport.MaxDepth = 1.0f;
}

void DynamicCubeMapDemo::BuildShapeGeometryBuffers()
{
	GeometryGenerator::MeshData box;
	GeometryGenerator::MeshData grid;
	GeometryGenerator::MeshData sphere;
	GeometryGenerator::MeshData cylinder;

	GeometryGenerator geoGen;
	geoGen.CreateBox(1.0f, 1.0f, 1.0f, box);
	geoGen.CreateGrid(20.0f, 30.0f, 60, 40, grid);
	geoGen.CreateSphere(0.5f, 20, 20, sphere);
	geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20, cylinder);

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	_boxVertexOffset = 0;
	_gridVertexOffset = box.vertices.size();
	_sphereVertexOffset = _gridVertexOffset + grid.vertices.size();
	_cylinderVertexOffset = _sphereVertexOffset + sphere.vertices.size();

	// Cache the index count of each object.
	_boxIndexCount = box.indices.size();
	_gridIndexCount = grid.indices.size();
	_sphereIndexCount = sphere.indices.size();
	_cylinderIndexCount = cylinder.indices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	_boxIndexOffset = 0;
	_gridIndexOffset = _boxIndexCount;
	_sphereIndexOffset = _gridIndexOffset + _gridIndexCount;
	_cylinderIndexOffset = _sphereIndexOffset + _sphereIndexCount;

	uint32 totalVertexCount =
		box.vertices.size() +
		grid.vertices.size() +
		sphere.vertices.size() +
		cylinder.vertices.size();

	uint32 totalIndexCount =
		_boxIndexCount +
		_gridIndexCount +
		_sphereIndexCount +
		_cylinderIndexCount;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	std::vector<Vertex::Basic32> vertices(totalVertexCount);

	uint32 k = 0;

	for (size_t i = 0; i < box.vertices.size(); ++i, ++k)
	{
		vertices[k].pos = box.vertices[i].position;
		vertices[k].normal = box.vertices[i].normal;
		vertices[k].tex = box.vertices[i].texC;
	}

	for (size_t i = 0; i < grid.vertices.size(); ++i, ++k)
	{
		vertices[k].pos = grid.vertices[i].position;
		vertices[k].normal = grid.vertices[i].normal;
		vertices[k].tex = grid.vertices[i].texC;
	}

	for (size_t i = 0; i < sphere.vertices.size(); ++i, ++k)
	{
		vertices[k].pos = sphere.vertices[i].position;
		vertices[k].normal = sphere.vertices[i].normal;
		vertices[k].tex = sphere.vertices[i].texC;
	}

	for (size_t i = 0; i < cylinder.vertices.size(); ++i, ++k)
	{
		vertices[k].pos = cylinder.vertices[i].position;
		vertices[k].normal = cylinder.vertices[i].normal;
		vertices[k].tex = cylinder.vertices[i].texC;
	}

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(Vertex::Basic32) * totalVertexCount;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = &vertices[0];
	HR(_device->CreateBuffer(&vbd, &vinitData, _shapesVB.GetAddressOf()));

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	std::vector<uint32> indices;
	indices.insert(indices.end(), box.indices.begin(), box.indices.end());
	indices.insert(indices.end(), grid.indices.begin(), grid.indices.end());
	indices.insert(indices.end(), sphere.indices.begin(), sphere.indices.end());
	indices.insert(indices.end(), cylinder.indices.begin(), cylinder.indices.end());

	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(uint32) * totalIndexCount;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &indices[0];
	HR(_device->CreateBuffer(&ibd, &iinitData, _shapesIB.GetAddressOf()));
}

void DynamicCubeMapDemo::BuildSkullGeometryBuffers()
{
	std::ifstream fin("../Resources/Models/skull.txt");

	if (!fin)
	{
		MessageBox(0, L"../Resources/Models/skull.txt not found.", 0, 0);
		return;
	}

	uint32 vcount = 0;
	uint32 tcount = 0;
	std::string ignore;

	fin >> ignore >> vcount;
	fin >> ignore >> tcount;
	fin >> ignore >> ignore >> ignore >> ignore;

	std::vector<Vertex::Basic32> vertices(vcount);
	for (uint32 i = 0; i < vcount; ++i)
	{
		fin >> vertices[i].pos.x >> vertices[i].pos.y >> vertices[i].pos.z;
		fin >> vertices[i].normal.x >> vertices[i].normal.y >> vertices[i].normal.z;
	}

	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	_skullIndexCount = 3 * tcount;
	std::vector<uint32> indices(_skullIndexCount);
	for (uint32 i = 0; i < tcount; ++i)
	{
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}

	fin.close();

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(Vertex::Basic32) * vcount;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = &vertices[0];
	HR(_device->CreateBuffer(&vbd, &vinitData, _skullVB.GetAddressOf()));

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(UINT) * _skullIndexCount;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &indices[0];
	HR(_device->CreateBuffer(&ibd, &iinitData, _skullIB.GetAddressOf()));
}