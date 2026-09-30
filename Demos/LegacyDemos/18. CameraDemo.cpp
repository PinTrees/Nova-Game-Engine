#include "pch.h"
#include "18. CameraDemo.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "Vertex.h"
#include "RenderStates.h"


CameraDemo::CameraDemo(HINSTANCE hInstance)
	: App(hInstance)
{
	_mainWindowCaption = L"Camera Demo";

	_lastMousePos.x = 0;
	_lastMousePos.y = 0;

	//_camera.SetPosition(0.0f, 2.0f, -15.0f);

	XMMATRIX I = ::XMMatrixIdentity();
	
	::XMStoreFloat4x4(&_gridWorld, I);

	XMMATRIX boxScale = ::XMMatrixScaling(3.0f, 1.0f, 3.0f);
	XMMATRIX boxOffset = ::XMMatrixTranslation(0.0f, 0.5f, 0.0f);
	::XMStoreFloat4x4(&_boxWorld, ::XMMatrixMultiply(boxScale, boxOffset));

	XMMATRIX skullScale = ::XMMatrixScaling(0.5f, 0.5f, 0.5f);
	XMMATRIX skullOffset = ::XMMatrixTranslation(0.0f, 1.0f, 0.0f);
	::XMStoreFloat4x4(&_skullWorld, ::XMMatrixMultiply(skullScale, skullOffset));

	for (int i = 0; i < 5; ++i)
	{
		::XMStoreFloat4x4(&_cylWorld[i * 2 + 0], ::XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f));
		::XMStoreFloat4x4(&_cylWorld[i * 2 + 1], ::XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f));

		::XMStoreFloat4x4(&_sphereWorld[i * 2 + 0], ::XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f));
		::XMStoreFloat4x4(&_sphereWorld[i * 2 + 1], ::XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f));
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

	_cylinderMat.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_cylinderMat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_cylinderMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 16.0f);

	_sphereMat.Ambient = XMFLOAT4(0.6f, 0.8f, 0.9f, 1.0f);
	_sphereMat.Diffuse = XMFLOAT4(0.6f, 0.8f, 0.9f, 1.0f);
	_sphereMat.Specular = XMFLOAT4(0.9f, 0.9f, 0.9f, 16.0f);

	_boxMat.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_boxMat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_boxMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 16.0f);

	_skullMat.Ambient = XMFLOAT4(0.4f, 0.4f, 0.4f, 1.0f);
	_skullMat.Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
	_skullMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 16.0f);
}

CameraDemo::~CameraDemo()
{
	Effects::DestroyAll();
	InputLayouts::DestroyAll();
}

bool CameraDemo::Init()
{
	if (!App::Init())
		return false;

	// Must init Effects first since InputLayouts depend on shader signatures.
	Effects::InitAll(_device, L"../Shaders/10. Basic.fx");
	InputLayouts::InitAll(_device);

	_floorTexSRV = Utils::LoadTexture(_device, L"../Resources/Textures/floor.dds");
	_stoneTexSRV = Utils::LoadTexture(_device, L"../Resources/Textures/stone.dds");
	_brickTexSRV = Utils::LoadTexture(_device, L"../Resources/Textures/bricks.dds");

	BuildShapeGeometryBuffers();
	BuildSkullGeometryBuffers();

	return true;
}

void CameraDemo::OnResize()
{
	App::OnResize();

	_camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void CameraDemo::UpdateScene(float dt)
{
	//
	// Control the camera.
	//
	if (::GetAsyncKeyState('W') & 0x8000)
		_camera.Walk(10.0f * dt);

	if (::GetAsyncKeyState('S') & 0x8000)
		_camera.Walk(-10.0f * dt);

	if (::GetAsyncKeyState('A') & 0x8000)
		_camera.Strafe(-10.0f * dt);

	if (::GetAsyncKeyState('D') & 0x8000)
		_camera.Strafe(10.0f * dt);

	//
	// Switch the number of lights based on key presses.
	//
	if (::GetAsyncKeyState('0') & 0x8000)
		_lightCount = 0;

	if (::GetAsyncKeyState('1') & 0x8000)
		_lightCount = 1;

	if (::GetAsyncKeyState('2') & 0x8000)
		_lightCount = 2;

	if (::GetAsyncKeyState('3') & 0x8000)
		_lightCount = 3;
}

void CameraDemo::RenderApplication()
{
	_deviceContext->ClearRenderTargetView(_renderTargetView.Get(), reinterpret_cast<const float*>(&Colors::Silver));
	_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	_deviceContext->IASetInputLayout(InputLayouts::Basic32.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	uint32 stride = sizeof(Vertex::Basic32);
	uint32 offset = 0;

	_camera.UpdateViewMatrix();

	XMMATRIX view = _camera.View();
	XMMATRIX proj = _camera.Proj();
	XMMATRIX viewProj = _camera.ViewProj();

	// Set per frame constants.
	Effects::BasicFX->SetDirLights(_dirLights);
	Effects::BasicFX->SetEyePosW(_camera.GetPosition());

	// Figure out which technique to use.  Skull does not have texture coordinates,
	// so we need a separate technique for it.
	ComPtr<ID3DX11EffectTechnique> activeTexTech = Effects::BasicFX->Light1TexTech;
	ComPtr<ID3DX11EffectTechnique> activeSkullTech = Effects::BasicFX->Light1Tech;

	switch (_lightCount)
	{
	case 1:
		activeTexTech = Effects::BasicFX->Light1TexTech;
		activeSkullTech = Effects::BasicFX->Light1Tech;
		break;
	case 2:
		activeTexTech = Effects::BasicFX->Light2TexTech;
		activeSkullTech = Effects::BasicFX->Light2Tech;
		break;
	case 3:
		activeTexTech = Effects::BasicFX->Light3TexTech;
		activeSkullTech = Effects::BasicFX->Light3Tech;
		break;
	}

	D3DX11_TECHNIQUE_DESC techDesc;
	activeTexTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		_deviceContext->IASetVertexBuffers(0, 1, _shapesVB.GetAddressOf(), &stride, &offset);
		_deviceContext->IASetIndexBuffer(_shapesIB.Get(), DXGI_FORMAT_R32_UINT, 0);

		// Draw the grid.
		XMMATRIX world = XMLoadFloat4x4(&_gridWorld);
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * view * proj;

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
		for (int i = 0; i < 10; ++i)
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
		for (int i = 0; i < 10; ++i)
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

	activeSkullTech->GetDesc(&techDesc);
	for (UINT p = 0; p < techDesc.Passes; ++p)
	{
		// Draw the skull.

		_deviceContext->IASetVertexBuffers(0, 1, _skullVB.GetAddressOf(), &stride, &offset);
		_deviceContext->IASetIndexBuffer(_skullIB.Get(), DXGI_FORMAT_R32_UINT, 0);

		XMMATRIX world = XMLoadFloat4x4(&_skullWorld);
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * view * proj;

		Effects::BasicFX->SetWorld(world);
		Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BasicFX->SetWorldViewProj(worldViewProj);
		Effects::BasicFX->SetMaterial(_skullMat);

		activeSkullTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_skullIndexCount, 0, 0);
	}

	HR(_swapChain->Present(0, 0));
}

void CameraDemo::OnMouseDown(WPARAM btnState, int x, int y)
{
	_lastMousePos.x = x;
	_lastMousePos.y = y;

	SetCapture(_hMainWnd);
}

void CameraDemo::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void CameraDemo::OnMouseMove(WPARAM btnState, int x, int y)
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

void CameraDemo::BuildShapeGeometryBuffers()
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

	std::vector<UINT> indices;
	indices.insert(indices.end(), box.indices.begin(), box.indices.end());
	indices.insert(indices.end(), grid.indices.begin(), grid.indices.end());
	indices.insert(indices.end(), sphere.indices.begin(), sphere.indices.end());
	indices.insert(indices.end(), cylinder.indices.begin(), cylinder.indices.end());

	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(UINT) * totalIndexCount;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &indices[0];
	HR(_device->CreateBuffer(&ibd, &iinitData, _shapesIB.GetAddressOf()));
}

void CameraDemo::BuildSkullGeometryBuffers()
{
	std::ifstream fin("../Resources/Models/skull.txt");

	if (!fin)
	{
		MessageBox(0, L"../Resources/Models/skull.txt not found.", 0, 0);
		return;
	}

	UINT vcount = 0;
	UINT tcount = 0;
	std::string ignore;

	fin >> ignore >> vcount;
	fin >> ignore >> tcount;
	fin >> ignore >> ignore >> ignore >> ignore;

	std::vector<Vertex::Basic32> vertices(vcount);
	for (UINT i = 0; i < vcount; ++i)
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

