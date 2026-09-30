#include "pch.h"
#include "10. BlendDemo.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "Vertex.h"
#include "RenderStates.h"

BlendDemo::BlendDemo(HINSTANCE hInstance) : App(hInstance)
{
	_mainWindowCaption = L"Blend Demo";
	_enable4xMsaa = false;

	_lastMousePos.x = 0;
	_lastMousePos.y = 0;

	XMMATRIX I = XMMatrixIdentity();
	::XMStoreFloat4x4(&_landWorld, I);
	::XMStoreFloat4x4(&_wavesWorld, I);
	::XMStoreFloat4x4(&_view, I);
	::XMStoreFloat4x4(&_proj, I);

	XMMATRIX boxScale = ::XMMatrixScaling(15.0f, 15.0f, 15.0f);
	XMMATRIX boxOffset = ::XMMatrixTranslation(8.0f, 5.0f, -15.0f);
	::XMStoreFloat4x4(&_boxWorld, boxScale * boxOffset);

	XMMATRIX grassTexScale = ::XMMatrixScaling(5.0f, 5.0f, 0.0f);
	::XMStoreFloat4x4(&_grassTexTransform, grassTexScale);

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

	_landMat.Ambient = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	_landMat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_landMat.Specular = XMFLOAT4(0.2f, 0.2f, 0.2f, 16.0f);

	_wavesMat.Ambient = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	_wavesMat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_wavesMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 32.0f);

	_boxMat.Ambient = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	_boxMat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_boxMat.Specular = XMFLOAT4(0.4f, 0.4f, 0.4f, 16.0f);
}

BlendDemo::~BlendDemo()
{
	_deviceContext->ClearState();

	Effects::DestroyAll();
	InputLayouts::DestroyAll();
	RenderStates::DestroyAll();
}

bool BlendDemo::Init()
{
	if (!App::Init())
		return false;

	_waves.Init(160, 160, 1.0f, 0.03f, 5.0f, 0.3f);

	// Must init Effects first since InputLayouts depend on shader signatures.
	Effects::InitAll(_device, L"../Shaders/10. Basic.fx");
	InputLayouts::InitAll(_device);
	RenderStates::InitAll(_device);

	{
		DirectX::TexMetadata md;
		DirectX::ScratchImage img;
		HRESULT hr = ::LoadFromWICFile(L"../Resources/Textures/grass.dds", WIC_FLAGS_NONE, &md, img);
		CHECK(hr);

		hr = ::CreateShaderResourceView(_device.Get(), img.GetImages(), img.GetImageCount(), md, _grassMapSRV.GetAddressOf());
		CHECK(hr);
	}

	{
		DirectX::TexMetadata md;
		DirectX::ScratchImage img;
		HRESULT hr = ::LoadFromWICFile(L"../Resources/Textures/water2.dds", WIC_FLAGS_NONE, &md, img);
		CHECK(hr);

		hr = ::CreateShaderResourceView(_device.Get(), img.GetImages(), img.GetImageCount(), md, _wavesMapSRV.GetAddressOf());
		CHECK(hr);
	}

	{
		DirectX::TexMetadata md;
		DirectX::ScratchImage img;
		HRESULT hr = ::LoadFromWICFile(L"../Resources/Textures/WireFence.dds", WIC_FLAGS_NONE, &md, img);
		CHECK(hr);

		hr = ::CreateShaderResourceView(_device.Get(), img.GetImages(), img.GetImageCount(), md, _boxMapSRV.GetAddressOf());
		CHECK(hr);
	}

	BuildLandGeometryBuffers();
	BuildWaveGeometryBuffers();
	BuildCrateGeometryBuffers();

	return true;
}

void BlendDemo::OnResize()
{
	App::OnResize();

	XMMATRIX P = ::XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	::XMStoreFloat4x4(&_proj, P);
}

void BlendDemo::UpdateScene(float dt)
{
	// Convert Spherical to Cartesian coordinates.
	float x = _radius * sinf(_phi) * cosf(_theta);
	float z = _radius * sinf(_phi) * sinf(_theta);
	float y = _radius * cosf(_phi);

	_eyePosW = XMFLOAT3(x, y, z);

	// Build the view matrix.
	XMVECTOR pos = ::XMVectorSet(x, y, z, 1.0f);
	XMVECTOR target = ::XMVectorZero();
	XMVECTOR up = ::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX V = ::XMMatrixLookAtLH(pos, target, up);
	::XMStoreFloat4x4(&_view, V);

	//
	// Every quarter second, generate a random wave.
	//
	static float t_base = 0.0f;
	if ((_timer.TotalTime() - t_base) >= 0.25f)
	{
		t_base += 0.25f;

		DWORD i = 5 + rand() % (_waves.RowCount() - 10);
		DWORD j = 5 + rand() % (_waves.ColumnCount() - 10);

		float r = MathHelper::RandF(1.0f, 2.0f);

		_waves.Disturb(i, j, r);
	}

	_waves.Update(dt);

	//
	// Update the wave vertex buffer with the new solution.
	//

	D3D11_MAPPED_SUBRESOURCE mappedData;
	HR(_deviceContext->Map(_wavesVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData));

	Vertex::Basic32* v = reinterpret_cast<Vertex::Basic32*>(mappedData.pData);
	for (uint32 i = 0; i < _waves.VertexCount(); ++i)
	{
		v[i].pos = _waves[i];
		v[i].normal = _waves.Normal(i);

		// Derive tex-coords in [0,1] from position.
		v[i].tex.x = 0.5f + _waves[i].x / _waves.Width();
		v[i].tex.y = 0.5f - _waves[i].z / _waves.Depth();
	}

	_deviceContext->Unmap(_wavesVB.Get(), 0);

	//
	// Animate water texture coordinates.
	//

	// Tile water texture.
	XMMATRIX wavesScale = ::XMMatrixScaling(5.0f, 5.0f, 0.0f);

	// Translate texture over time.
	_waterTexOffset.y += 0.05f * dt;
	_waterTexOffset.x += 0.1f * dt;
	XMMATRIX wavesOffset = ::XMMatrixTranslation(_waterTexOffset.x, _waterTexOffset.y, 0.0f);

	// Combine scale and translation.
	::XMStoreFloat4x4(&_waterTexTransform, wavesScale * wavesOffset);

	if (::GetAsyncKeyState('1') & 0x8000)
		_renderOptions = RenderOptions::Lighting;

	if (::GetAsyncKeyState('2') & 0x8000)
		_renderOptions = RenderOptions::Textures;

	if (::GetAsyncKeyState('3') & 0x8000)
		_renderOptions = RenderOptions::TexturesAndFog;
}

void BlendDemo::RenderApplication()
{
	_deviceContext->ClearRenderTargetView(_renderTargetView.Get(), reinterpret_cast<const float*>(&Colors::LightSteelBlue));
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
	Effects::BasicFX->SetFogColor(Colors::Silver);
	Effects::BasicFX->SetFogStart(15.0f);
	Effects::BasicFX->SetFogRange(175.0f);

	ComPtr<ID3DX11EffectTechnique> boxTech;
	ComPtr<ID3DX11EffectTechnique> landAndWavesTech;

	switch (_renderOptions)
	{
	case RenderOptions::Lighting:
		boxTech = Effects::BasicFX->Light3Tech;
		landAndWavesTech = Effects::BasicFX->Light3Tech;
		break;
	case RenderOptions::Textures:
		boxTech = Effects::BasicFX->Light3TexAlphaClipTech;
		landAndWavesTech = Effects::BasicFX->Light3TexTech;
		break;
	case RenderOptions::TexturesAndFog:
		boxTech = Effects::BasicFX->Light3TexAlphaClipFogTech;
		landAndWavesTech = Effects::BasicFX->Light3TexFogTech;
		break;
	}

	D3DX11_TECHNIQUE_DESC techDesc;

	//
	// Draw the box with alpha clipping.
	// 

	boxTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		_deviceContext->IASetVertexBuffers(0, 1, _boxVB.GetAddressOf(), &stride, &offset);
		_deviceContext->IASetIndexBuffer(_boxIB.Get(), DXGI_FORMAT_R32_UINT, 0);

		// Set per object constants.
		XMMATRIX world = XMLoadFloat4x4(&_boxWorld);
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * view * proj;

		Effects::BasicFX->SetWorld(world);
		Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BasicFX->SetWorldViewProj(worldViewProj);
		Effects::BasicFX->SetTexTransform(XMMatrixIdentity());
		Effects::BasicFX->SetMaterial(_boxMat);
		Effects::BasicFX->SetDiffuseMap(_boxMapSRV.Get());

		_deviceContext->RSSetState(RenderStates::NoCullRS.Get());
		boxTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(36, 0, 0);

		// Restore default render state.
		_deviceContext->RSSetState(0);
	}

	//
	// Draw the hills and water with texture and fog (no alpha clipping needed).
	//

	landAndWavesTech->GetDesc(&techDesc);
	for (UINT p = 0; p < techDesc.Passes; ++p)
	{
		//
		// Draw the hills.
		//
		_deviceContext->IASetVertexBuffers(0, 1, _landVB.GetAddressOf(), &stride, &offset);
		_deviceContext->IASetIndexBuffer(_landIB.Get(), DXGI_FORMAT_R32_UINT, 0);

		// Set per object constants.
		XMMATRIX world = XMLoadFloat4x4(&_landWorld);
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * view * proj;

		Effects::BasicFX->SetWorld(world);
		Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BasicFX->SetWorldViewProj(worldViewProj);
		Effects::BasicFX->SetTexTransform(XMLoadFloat4x4(&_grassTexTransform));
		Effects::BasicFX->SetMaterial(_landMat);
		Effects::BasicFX->SetDiffuseMap(_grassMapSRV.Get());

		landAndWavesTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_landIndexCount, 0, 0);

		//
		// Draw the waves.
		//
		_deviceContext->IASetVertexBuffers(0, 1, _wavesVB.GetAddressOf(), &stride, &offset);
		_deviceContext->IASetIndexBuffer(_wavesIB.Get(), DXGI_FORMAT_R32_UINT, 0);

		// Set per object constants.
		world = XMLoadFloat4x4(&_wavesWorld);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldViewProj = world * view * proj;

		Effects::BasicFX->SetWorld(world);
		Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BasicFX->SetWorldViewProj(worldViewProj);
		Effects::BasicFX->SetTexTransform(XMLoadFloat4x4(&_waterTexTransform));
		Effects::BasicFX->SetMaterial(_wavesMat);
		Effects::BasicFX->SetDiffuseMap(_wavesMapSRV.Get());

		_deviceContext->OMSetBlendState(RenderStates::TransparentBS.Get(), blendFactor, 0xffffffff);
		landAndWavesTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(3 * _waves.TriangleCount(), 0, 0);

		// Restore default blend state
		_deviceContext->OMSetBlendState(0, blendFactor, 0xffffffff);
	}

	HR(_swapChain->Present(0, 0));
}

void BlendDemo::OnMouseDown(WPARAM btnState, int x, int y)
{
	_lastMousePos.x = x;
	_lastMousePos.y = y;

	SetCapture(_hMainWnd);
}

void BlendDemo::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void BlendDemo::OnMouseMove(WPARAM btnState, int x, int y)
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
		_radius = MathHelper::Clamp(_radius, 20.0f, 500.0f);
	}

	_lastMousePos.x = x;
	_lastMousePos.y = y;
}

float BlendDemo::GetHillHeight(float x, float z)const
{
	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 BlendDemo::GetHillNormal(float x, float z)const
{
	// n = (-df/dx, 1, -df/dz)
	XMFLOAT3 n(
		-0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
		1.0f,
		-0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));

	XMVECTOR unitNormal = ::XMVector3Normalize(::XMLoadFloat3(&n));
	::XMStoreFloat3(&n, unitNormal);

	return n;
}

void BlendDemo::BuildLandGeometryBuffers()
{
	GeometryGenerator::MeshData grid;

	GeometryGenerator geoGen;

	geoGen.CreateGrid(160.0f, 160.0f, 50, 50, grid);

	_landIndexCount = grid.indices.size();

	//
	// Extract the vertex elements we are interested and apply the height function to
	// each vertex.  
	//

	std::vector<Vertex::Basic32> vertices(grid.vertices.size());
	for (size_t i = 0; i < grid.vertices.size(); ++i)
	{
		XMFLOAT3 p = grid.vertices[i].position;

		p.y = GetHillHeight(p.x, p.z);

		vertices[i].pos = p;
		vertices[i].normal = GetHillNormal(p.x, p.z);
		vertices[i].tex = grid.vertices[i].texC;
	}

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(Vertex::Basic32) * grid.vertices.size();
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = &vertices[0];
	HR(_device->CreateBuffer(&vbd, &vinitData, _landVB.GetAddressOf()));

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(uint32) * _landIndexCount;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &grid.indices[0];
	HR(_device->CreateBuffer(&ibd, &iinitData, _landIB.GetAddressOf()));
}

void BlendDemo::BuildWaveGeometryBuffers()
{
	// Create the vertex buffer.  Note that we allocate space only, as
	// we will be updating the data every time step of the simulation.

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_DYNAMIC;
	vbd.ByteWidth = sizeof(Vertex::Basic32) * _waves.VertexCount();
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vbd.MiscFlags = 0;
	HR(_device->CreateBuffer(&vbd, 0, _wavesVB.GetAddressOf()));


	// Create the index buffer.  The index buffer is fixed, so we only 
	// need to create and set once.

	std::vector<uint32> indices(3 * _waves.TriangleCount()); // 3 indices per face

	// Iterate over each quad.
	uint32 m = _waves.RowCount();
	uint32 n = _waves.ColumnCount();
	int k = 0;
	for (uint32 i = 0; i < m - 1; ++i)
	{
		for (DWORD j = 0; j < n - 1; ++j)
		{
			indices[k] = i * n + j;
			indices[k + 1] = i * n + j + 1;
			indices[k + 2] = (i + 1) * n + j;

			indices[k + 3] = (i + 1) * n + j;
			indices[k + 4] = i * n + j + 1;
			indices[k + 5] = (i + 1) * n + j + 1;

			k += 6; // next quad
		}
	}

	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(UINT) * indices.size();
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &indices[0];
	HR(_device->CreateBuffer(&ibd, &iinitData, _wavesIB.GetAddressOf()));
}


void BlendDemo::BuildCrateGeometryBuffers()
{
	GeometryGenerator::MeshData box;

	GeometryGenerator geoGen;
	geoGen.CreateBox(1.0f, 1.0f, 1.0f, box);

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	std::vector<Vertex::Basic32> vertices(box.vertices.size());

	for (UINT i = 0; i < box.vertices.size(); ++i)
	{
		vertices[i].pos = box.vertices[i].position;
		vertices[i].normal = box.vertices[i].normal;
		vertices[i].tex = box.vertices[i].texC;
	}

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(Vertex::Basic32) * box.vertices.size();
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = &vertices[0];
	HR(_device->CreateBuffer(&vbd, &vinitData, _boxVB.GetAddressOf()));

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(uint32) * box.indices.size();
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &box.indices[0];
	HR(_device->CreateBuffer(&ibd, &iinitData, _boxIB.GetAddressOf()));
}