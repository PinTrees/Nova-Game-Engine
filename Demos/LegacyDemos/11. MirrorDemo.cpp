#include "pch.h"
#include "11. MirrorDemo.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "Vertex.h"
#include "RenderStates.h"

MirrorDemo::MirrorDemo(HINSTANCE hInstance) : App(hInstance)
{
	_mainWindowCaption = L"Mirror Demo";
	_enable4xMsaa = false;

	_lastMousePos.x = 0;
	_lastMousePos.y = 0;

	XMMATRIX I = ::XMMatrixIdentity();
	::XMStoreFloat4x4(&_roomWorld, I);
	::XMStoreFloat4x4(&_view, I);
	::XMStoreFloat4x4(&_proj, I);

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

	_roomMat.Ambient = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	_roomMat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_roomMat.Specular = XMFLOAT4(0.4f, 0.4f, 0.4f, 16.0f);

	_skullMat.Ambient = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	_skullMat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_skullMat.Specular = XMFLOAT4(0.4f, 0.4f, 0.4f, 16.0f);

	// Reflected material is transparent so it blends into mirror.
	_mirrorMat.Ambient = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	_mirrorMat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	_mirrorMat.Specular = XMFLOAT4(0.4f, 0.4f, 0.4f, 16.0f);

	_shadowMat.Ambient = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	_shadowMat.Diffuse = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f);
	_shadowMat.Specular = XMFLOAT4(0.0f, 0.0f, 0.0f, 16.0f);
}

MirrorDemo::~MirrorDemo()
{
	_deviceContext->ClearState();

	Effects::DestroyAll();
	InputLayouts::DestroyAll();
	RenderStates::DestroyAll();
}

bool MirrorDemo::Init()
{
	if (!App::Init())
		return false;

	// Must init Effects first since InputLayouts depend on shader signatures.
	Effects::InitAll(_device, L"../Shaders/10. Basic.fx");
	InputLayouts::InitAll(_device);
	RenderStates::InitAll(_device);

	{
		DirectX::TexMetadata md;
		DirectX::ScratchImage img;
		HRESULT hr = ::LoadFromWICFile(L"../Resources/Textures/checkboard.dds", WIC_FLAGS_NONE, &md, img);
		CHECK(hr);

		hr = ::CreateShaderResourceView(_device.Get(), img.GetImages(), img.GetImageCount(), md, _floorDiffuseMapSRV.GetAddressOf());
		CHECK(hr);
	}

	{
		DirectX::TexMetadata md;
		DirectX::ScratchImage img;
		HRESULT hr = ::LoadFromWICFile(L"../Resources/Textures/brick01.dds", WIC_FLAGS_NONE, &md, img);
		CHECK(hr);

		hr = ::CreateShaderResourceView(_device.Get(), img.GetImages(), img.GetImageCount(), md, _wallDiffuseMapSRV.GetAddressOf());
		CHECK(hr);
	}

	{
		DirectX::TexMetadata md;
		DirectX::ScratchImage img;
		HRESULT hr = ::LoadFromWICFile(L"../Resources/Textures/ice.dds", WIC_FLAGS_NONE, &md, img);
		CHECK(hr);

		hr = ::CreateShaderResourceView(_device.Get(), img.GetImages(), img.GetImageCount(), md, _mirrorDiffuseMapSRV.GetAddressOf());
		CHECK(hr);
	}

	BuildRoomGeometryBuffers();
	BuildSkullGeometryBuffers();

	return true;
}

void MirrorDemo::OnResize()
{
	App::OnResize();

	XMMATRIX P = ::XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	::XMStoreFloat4x4(&_proj, P);
}

void MirrorDemo::UpdateScene(float dt)
{
	// Convert Spherical to Cartesian coordinates.
	float x = _radius * sinf(_phi) * cosf(_theta);
	float z = _radius * sinf(_phi) * sinf(_theta);
	float y = _radius * cosf(_phi);

	_eyePosW = XMFLOAT3(x, y, z);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX V = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&_view, V);

	//
	// Switch the render mode based in key input.
	//
	if (GetAsyncKeyState('1') & 0x8000)
		_renderOptions = RenderOptions::Lighting;

	if (GetAsyncKeyState('2') & 0x8000)
		_renderOptions = RenderOptions::Textures;

	if (GetAsyncKeyState('3') & 0x8000)
		_renderOptions = RenderOptions::TexturesAndFog;


	//
	// Allow user to move box.
	//
	if (GetAsyncKeyState('A') & 0x8000)
		_skullTranslation.x -= 1.0f * dt;

	if (GetAsyncKeyState('D') & 0x8000)
		_skullTranslation.x += 1.0f * dt;

	if (GetAsyncKeyState('W') & 0x8000)
		_skullTranslation.y += 1.0f * dt;

	if (GetAsyncKeyState('S') & 0x8000)
		_skullTranslation.y -= 1.0f * dt;

	// Don't let user move below ground plane.
	_skullTranslation.y = MathHelper::Max(_skullTranslation.y, 0.0f);

	// Update the new world matrix.
	XMMATRIX skullRotate = XMMatrixRotationY(0.5f * MathHelper::Pi);
	XMMATRIX skullScale = XMMatrixScaling(0.45f, 0.45f, 0.45f);
	XMMATRIX skullOffset = XMMatrixTranslation(_skullTranslation.x, _skullTranslation.y, _skullTranslation.z);
	XMStoreFloat4x4(&_skullWorld, skullRotate * skullScale * skullOffset);
}

void MirrorDemo::RenderApplication()
{
	_deviceContext->ClearRenderTargetView(_renderTargetView.Get(), reinterpret_cast<const float*>(&Colors::Black));
	_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	_deviceContext->IASetInputLayout(InputLayouts::Basic32.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };

	uint32 stride = sizeof(Vertex::Basic32);
	uint32 offset = 0;

	XMMATRIX view = XMLoadFloat4x4(&_view);
	XMMATRIX proj = XMLoadFloat4x4(&_proj);
	XMMATRIX viewProj = view * proj;

	// Set per frame constants.
	Effects::BasicFX->SetDirLights(_dirLights);
	Effects::BasicFX->SetEyePosW(_eyePosW);
	Effects::BasicFX->SetFogColor(Colors::Black);
	Effects::BasicFX->SetFogStart(2.0f);
	Effects::BasicFX->SetFogRange(40.0f);

	// Skull doesn't have texture coordinates, so we can't texture it.
	ComPtr<ID3DX11EffectTechnique> activeTech;
	ComPtr<ID3DX11EffectTechnique> activeSkullTech;

	switch (_renderOptions)
	{
	case RenderOptions::Lighting:
		activeTech = Effects::BasicFX->Light3Tech;
		activeSkullTech = Effects::BasicFX->Light3Tech;
		break;
	case RenderOptions::Textures:
		activeTech = Effects::BasicFX->Light3TexTech;
		activeSkullTech = Effects::BasicFX->Light3Tech;
		break;
	case RenderOptions::TexturesAndFog:
		activeTech = Effects::BasicFX->Light3TexFogTech;
		activeSkullTech = Effects::BasicFX->Light3FogTech;
		break;
	}

	D3DX11_TECHNIQUE_DESC techDesc;

	//
	// Draw the floor and walls to the back buffer as normal.
	//

	activeTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		ID3DX11EffectPass* pass = activeTech->GetPassByIndex(p);

		_deviceContext->IASetVertexBuffers(0, 1, _roomVB.GetAddressOf(), &stride, &offset);

		// Set per object constants.
		XMMATRIX world = ::XMLoadFloat4x4(&_roomWorld);
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * view * proj;

		Effects::BasicFX->SetWorld(world);
		Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BasicFX->SetWorldViewProj(worldViewProj);
		Effects::BasicFX->SetTexTransform(XMMatrixIdentity());
		Effects::BasicFX->SetMaterial(_roomMat);

		// Floor
		Effects::BasicFX->SetDiffuseMap(_floorDiffuseMapSRV.Get());
		pass->Apply(0, _deviceContext.Get());
		_deviceContext->Draw(6, 0);

		// Wall
		Effects::BasicFX->SetDiffuseMap(_wallDiffuseMapSRV.Get());
		pass->Apply(0, _deviceContext.Get());
		_deviceContext->Draw(18, 6);
	}

	//
	// Draw the skull to the back buffer as normal.
	//

	activeSkullTech->GetDesc(&techDesc);
	for (UINT p = 0; p < techDesc.Passes; ++p)
	{
		ID3DX11EffectPass* pass = activeSkullTech->GetPassByIndex(p);

		_deviceContext->IASetVertexBuffers(0, 1, _skullVB.GetAddressOf(), &stride, &offset);
		_deviceContext->IASetIndexBuffer(_skullIB.Get(), DXGI_FORMAT_R32_UINT, 0);

		XMMATRIX world = ::XMLoadFloat4x4(&_skullWorld);
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * view * proj;

		Effects::BasicFX->SetWorld(world);
		Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BasicFX->SetWorldViewProj(worldViewProj);
		Effects::BasicFX->SetMaterial(_skullMat);

		pass->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_skullIndexCount, 0, 0);
	}

	//
	// Draw the mirror to stencil buffer only.
	//

	activeTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		ID3DX11EffectPass* pass = activeTech->GetPassByIndex(p);

		_deviceContext->IASetVertexBuffers(0, 1, _roomVB.GetAddressOf(), &stride, &offset);

		// Set per object constants.
		XMMATRIX world = ::XMLoadFloat4x4(&_roomWorld);
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * view * proj;

		Effects::BasicFX->SetWorld(world);
		Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BasicFX->SetWorldViewProj(worldViewProj);
		Effects::BasicFX->SetTexTransform(XMMatrixIdentity());

		// Do not write to render target.
		_deviceContext->OMSetBlendState(RenderStates::NoRenderTargetWritesBS.Get(), blendFactor, 0xffffffff);

		// Render visible mirror pixels to stencil buffer.
		// Do not write mirror depth to depth buffer at this point, otherwise it will occlude the reflection.
		_deviceContext->OMSetDepthStencilState(RenderStates::MarkMirrorDSS.Get(), 1);

		pass->Apply(0, _deviceContext.Get());
		_deviceContext->Draw(6, 24);

		// Restore states.
		_deviceContext->OMSetDepthStencilState(0, 0);
		_deviceContext->OMSetBlendState(0, blendFactor, 0xffffffff);
	}


	//
	// Draw the skull reflection.
	//
	activeSkullTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		ComPtr<ID3DX11EffectPass> pass = activeSkullTech->GetPassByIndex(p);

		_deviceContext->IASetVertexBuffers(0, 1, _skullVB.GetAddressOf(), &stride, &offset);
		_deviceContext->IASetIndexBuffer(_skullIB.Get(), DXGI_FORMAT_R32_UINT, 0);

		XMVECTOR mirrorPlane = ::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
		XMMATRIX R = ::XMMatrixReflect(mirrorPlane);
		XMMATRIX world = ::XMLoadFloat4x4(&_skullWorld) * R;
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * view * proj;

		Effects::BasicFX->SetWorld(world);
		Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BasicFX->SetWorldViewProj(worldViewProj);
		Effects::BasicFX->SetMaterial(_skullMat);

		// Cache the old light directions, and reflect the light directions.
		XMFLOAT3 oldLightDirections[3];
		for (int i = 0; i < 3; ++i)
		{
			oldLightDirections[i] = _dirLights[i].Direction;

			XMVECTOR lightDir = XMLoadFloat3(&_dirLights[i].Direction);
			XMVECTOR reflectedLightDir = XMVector3TransformNormal(lightDir, R);
			XMStoreFloat3(&_dirLights[i].Direction, reflectedLightDir);
		}

		Effects::BasicFX->SetDirLights(_dirLights);

		// Cull clockwise triangles for reflection.
		_deviceContext->RSSetState(RenderStates::CullClockwiseRS.Get());

		// Only draw reflection into visible mirror pixels as marked by the stencil buffer. 
		_deviceContext->OMSetDepthStencilState(RenderStates::DrawReflectionDSS.Get(), 1);
		pass->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_skullIndexCount, 0, 0);

		// Restore default states.
		_deviceContext->RSSetState(0);
		_deviceContext->OMSetDepthStencilState(0, 0);

		// Restore light directions.
		for (int i = 0; i < 3; ++i)
		{
			_dirLights[i].Direction = oldLightDirections[i];
		}

		Effects::BasicFX->SetDirLights(_dirLights);
	}

	//
	// Draw the mirror to the back buffer as usual but with transparency
	// blending so the reflection shows through.
	// 

	activeTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		ID3DX11EffectPass* pass = activeTech->GetPassByIndex(p);

		_deviceContext->IASetVertexBuffers(0, 1, _roomVB.GetAddressOf(), &stride, &offset);

		// Set per object constants.
		XMMATRIX world = ::XMLoadFloat4x4(&_roomWorld);
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * view * proj;

		Effects::BasicFX->SetWorld(world);
		Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BasicFX->SetWorldViewProj(worldViewProj);
		Effects::BasicFX->SetTexTransform(XMMatrixIdentity());
		Effects::BasicFX->SetMaterial(_mirrorMat);
		Effects::BasicFX->SetDiffuseMap(_mirrorDiffuseMapSRV.Get());

		// Mirror
		_deviceContext->OMSetBlendState(RenderStates::TransparentBS.Get(), blendFactor, 0xffffffff);
		pass->Apply(0, _deviceContext.Get());
		_deviceContext->Draw(6, 24);
	}

	//
	// Draw the skull shadow.
	//
	activeSkullTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		ID3DX11EffectPass* pass = activeSkullTech->GetPassByIndex(p);

		_deviceContext->IASetVertexBuffers(0, 1, _skullVB.GetAddressOf(), &stride, &offset);
		_deviceContext->IASetIndexBuffer(_skullIB.Get(), DXGI_FORMAT_R32_UINT, 0);

		XMVECTOR shadowPlane = ::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // xz plane
		XMVECTOR toMainLight = -::XMLoadFloat3(&_dirLights[0].Direction);
		XMMATRIX S = ::XMMatrixShadow(shadowPlane, toMainLight);
		XMMATRIX shadowOffsetY = ::XMMatrixTranslation(0.0f, 0.001f, 0.0f);

		// Set per object constants.
		XMMATRIX world = ::XMLoadFloat4x4(&_skullWorld) * S * shadowOffsetY;
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * view * proj;

		Effects::BasicFX->SetWorld(world);
		Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BasicFX->SetWorldViewProj(worldViewProj);
		Effects::BasicFX->SetMaterial(_shadowMat);

		_deviceContext->OMSetDepthStencilState(RenderStates::NoDoubleBlendDSS.Get(), 0);
		pass->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_skullIndexCount, 0, 0);

		// Restore default states.
		_deviceContext->OMSetBlendState(0, blendFactor, 0xffffffff);
		_deviceContext->OMSetDepthStencilState(0, 0);
	}

	HR(_swapChain->Present(0, 0));
}

void MirrorDemo::OnMouseDown(WPARAM btnState, int x, int y)
{
	_lastMousePos.x = x;
	_lastMousePos.y = y;

	SetCapture(_hMainWnd);
}

void MirrorDemo::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void MirrorDemo::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = ::XMConvertToRadians(0.25f * static_cast<float>(x - _lastMousePos.x));
		float dy = ::XMConvertToRadians(0.25f * static_cast<float>(y - _lastMousePos.y));

		// Update angles based on input to orbit camera around box.
		_theta += dx;
		_phi += dy;

		// Restrict the angle mPhi.
		_phi = MathHelper::Clamp(_phi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.01 unit in the scene.
		float dx = 0.1f * static_cast<float>(x - _lastMousePos.x);
		float dy = 0.1f * static_cast<float>(y - _lastMousePos.y);

		// Update the camera radius based on input.
		_radius += dx - dy;

		// Restrict the radius.
		_radius = MathHelper::Clamp(_radius, 3.0f, 50.0f);
	}

	_lastMousePos.x = x;
	_lastMousePos.y = y;
}

void MirrorDemo::BuildRoomGeometryBuffers()
{
	// Create and specify geometry.  For this sample we draw a floor
	// and a wall with a mirror on it.  We put the floor, wall, and
	// mirror geometry in one vertex buffer.
	//
	//   |--------------|
	//   |              |
	//   |----|----|----|
	//   |Wall|Mirr|Wall|
	//   |    | or |    |
	//   /--------------/
	//  /   Floor      /
	// /--------------/


	Vertex::Basic32 v[30];

	// Floor: Observe we tile texture coordinates.
	v[0] = Vertex::Basic32(-3.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f);
	v[1] = Vertex::Basic32(-3.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
	v[2] = Vertex::Basic32(7.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f);

	v[3] = Vertex::Basic32(-3.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f);
	v[4] = Vertex::Basic32(7.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f);
	v[5] = Vertex::Basic32(7.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 4.0f, 4.0f);

	// Wall: Observe we tile texture coordinates, and that we
	// leave a gap in the middle for the mirror.
	v[6] = Vertex::Basic32(-3.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f);
	v[7] = Vertex::Basic32(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f);
	v[8] = Vertex::Basic32(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 0.0f);

	v[9] = Vertex::Basic32(-3.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f);
	v[10] = Vertex::Basic32(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 0.0f);
	v[11] = Vertex::Basic32(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 2.0f);

	v[12] = Vertex::Basic32(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f);
	v[13] = Vertex::Basic32(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f);
	v[14] = Vertex::Basic32(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 0.0f);

	v[15] = Vertex::Basic32(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f);
	v[16] = Vertex::Basic32(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 0.0f);
	v[17] = Vertex::Basic32(7.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 2.0f);

	v[18] = Vertex::Basic32(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f);
	v[19] = Vertex::Basic32(-3.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f);
	v[20] = Vertex::Basic32(7.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 0.0f);

	v[21] = Vertex::Basic32(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f);
	v[22] = Vertex::Basic32(7.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 0.0f);
	v[23] = Vertex::Basic32(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 1.0f);

	// Mirror
	v[24] = Vertex::Basic32(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f);
	v[25] = Vertex::Basic32(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f);
	v[26] = Vertex::Basic32(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f);

	v[27] = Vertex::Basic32(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f);
	v[28] = Vertex::Basic32(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f);
	v[29] = Vertex::Basic32(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f);

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(Vertex::Basic32) * 30;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = v;
	HR(_device->CreateBuffer(&vbd, &vinitData, _roomVB.GetAddressOf()));
}

void MirrorDemo::BuildSkullGeometryBuffers()
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
	ibd.ByteWidth = sizeof(uint32) * _skullIndexCount;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &indices[0];
	HR(_device->CreateBuffer(&ibd, &iinitData, _skullIB.GetAddressOf()));
}