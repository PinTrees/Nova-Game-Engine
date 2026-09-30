#include "pch.h"
#include "07. LightingDemo.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"


LightingDemo::LightingDemo(HINSTANCE hInstance) : App(hInstance)
{
	_mainWindowCaption = L"Lighting Demo";

	_lastMousePos.x = 0;
	_lastMousePos.y = 0;

	XMMATRIX I = ::XMMatrixIdentity();
	::XMStoreFloat4x4(&_landWorld, I);
	::XMStoreFloat4x4(&_wavesWorld, I);
	::XMStoreFloat4x4(&_view, I);
	::XMStoreFloat4x4(&_proj, I);

	XMMATRIX wavesOffset = ::XMMatrixTranslation(0.0f, -3.0f, 0.0f);
	::XMStoreFloat4x4(&_wavesWorld, wavesOffset);

	// Directional light.
	_dirLight.Ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	_dirLight.Diffuse = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	_dirLight.Specular = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	_dirLight.Direction = XMFLOAT3(0.57735f, -0.57735f, 0.57735f);

	// Point light--position is changed every frame to animate in UpdateScene function.
	_pointLight.Ambient = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
	_pointLight.Diffuse = XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f);
	_pointLight.Specular = XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f);
	_pointLight.Att = XMFLOAT3(0.0f, 0.1f, 0.0f);
	_pointLight.Range = 25.0f;

	// Spot light--position and direction changed every frame to animate in UpdateScene function.
	_spotLight.Ambient = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	_spotLight.Diffuse = XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f);
	_spotLight.Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_spotLight.Att = XMFLOAT3(1.0f, 0.0f, 0.0f);
	_spotLight.Spot = 96.0f;
	_spotLight.Range = 10000.0f;

	_landMat.Ambient = XMFLOAT4(0.48f, 0.77f, 0.46f, 1.0f);
	_landMat.Diffuse = XMFLOAT4(0.48f, 0.77f, 0.46f, 1.0f);
	_landMat.Specular = XMFLOAT4(0.2f, 0.2f, 0.2f, 16.0f);

	_wavesMat.Ambient = XMFLOAT4(0.137f, 0.42f, 0.556f, 1.0f);
	_wavesMat.Diffuse = XMFLOAT4(0.137f, 0.42f, 0.556f, 1.0f);
	_wavesMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 96.0f);
}

LightingDemo::~LightingDemo()
{

}

bool LightingDemo::Init()
{
	if (!App::Init())
		return false;

	_waves.Init(160, 160, 1.0f, 0.03f, 3.25f, 0.4f);
	BuildLandGeometryBuffers();
	BuildWavesGeometryBuffers();
	BuildFX();
	BuildVertexLayout();

	return true;
}

void LightingDemo::OnResize()
{
	App::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * XM_PI, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&_proj, P);
}

void LightingDemo::UpdateScene(float dt)
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

	Vertex* v = reinterpret_cast<Vertex*>(mappedData.pData);
	for (uint32 i = 0; i < _waves.VertexCount(); ++i)
	{
		v[i].pos = _waves[i];
		v[i].normal = _waves.Normal(i);
	}

	_deviceContext->Unmap(_wavesVB.Get(), 0);

	//
	// Animate the lights.
	//

	// Circle light over the land surface.
	_pointLight.Position.x = 70.0f * cosf(0.2f * _timer.TotalTime());
	_pointLight.Position.z = 70.0f * sinf(0.2f * _timer.TotalTime());
	_pointLight.Position.y = MathHelper::Max(GetHillHeight(_pointLight.Position.x, _pointLight.Position.z), -3.0f) + 10.0f;

	// The spotlight takes on the camera position and is aimed in the
	// same direction the camera is looking.  In this way, it looks
	// like we are holding a flashlight.
	_spotLight.Position = _eyePosW;
	::XMStoreFloat3(&_spotLight.Direction, ::XMVector3Normalize(target - pos));
}

void LightingDemo::RenderApplication()
{
	_deviceContext->ClearRenderTargetView(_renderTargetView.Get(), reinterpret_cast<const float*>(&Colors::LightSteelBlue));
	_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	_deviceContext->IASetInputLayout(_inputLayout.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	//_deviceContext->RSSetState(_wireframeRS.Get());

	uint32 stride = sizeof(Vertex);
	uint32 offset = 0;

	// Set constants
	XMMATRIX view = ::XMLoadFloat4x4(&_view);
	XMMATRIX proj = ::XMLoadFloat4x4(&_proj);
	XMMATRIX viewProj = view * proj;

	// Set per frame constants.
	_fxDirLight->SetRawValue(&_dirLight, 0, sizeof(_dirLight));
	_fxPointLight->SetRawValue(&_pointLight, 0, sizeof(_pointLight));
	_fxSpotLight->SetRawValue(&_spotLight, 0, sizeof(_spotLight));
	_fxEyePosW->SetRawValue(&_eyePosW, 0, sizeof(_eyePosW));

	D3DX11_TECHNIQUE_DESC techDesc;
	_tech->GetDesc(&techDesc);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
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

		_fxWorld->SetMatrix(reinterpret_cast<float*>(&world));
		_fxWorldInvTranspose->SetMatrix(reinterpret_cast<float*>(&worldInvTranspose));
		_fxWorldViewProj->SetMatrix(reinterpret_cast<float*>(&worldViewProj));
		_fxMaterial->SetRawValue(&_landMat, 0, sizeof(_landMat));

		_tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
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

		_fxWorld->SetMatrix(reinterpret_cast<float*>(&world));
		_fxWorldInvTranspose->SetMatrix(reinterpret_cast<float*>(&worldInvTranspose));
		_fxWorldViewProj->SetMatrix(reinterpret_cast<float*>(&worldViewProj));
		_fxMaterial->SetRawValue(&_wavesMat, 0, sizeof(_wavesMat));

		_tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(3 * _waves.TriangleCount(), 0, 0);
	}

	HR(_swapChain->Present(0, 0));
}

void LightingDemo::OnMouseDown(WPARAM btnState, int x, int y)
{
	_lastMousePos.x = x;
	_lastMousePos.y = y;

	::SetCapture(_hMainWnd);
}

void LightingDemo::OnMouseUp(WPARAM btnState, int x, int y)
{
	::ReleaseCapture();
}

void LightingDemo::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - _lastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - _lastMousePos.y));

		// Update angles based on input to orbit camera around box.
		_theta += dx;
		_phi += dy;

		// Restrict the angle mPhi.
		_phi = MathHelper::Clamp(_phi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.005 unit in the scene.
		float dx = 0.005f * static_cast<float>(x - _lastMousePos.x);
		float dy = 0.005f * static_cast<float>(y - _lastMousePos.y);

		// Update the camera radius based on input.
		_radius += dx - dy;

		// Restrict the radius.
		_radius = MathHelper::Clamp(_radius, 3.0f, 15.0f);
	}

	_lastMousePos.x = x;
	_lastMousePos.y = y;
}

DirectX::XMFLOAT3 LightingDemo::GetHillNormal(float x, float z) const
{
	// n = (-df/dx, 1, -df/dz)
	XMFLOAT3 n(
		-0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
		1.0f,
		-0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));

	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, unitNormal);

	return n;
}

void LightingDemo::BuildLandGeometryBuffers()
{
	GeometryGenerator::MeshData grid;

	GeometryGenerator geoGen;

	geoGen.CreateGrid(160.0f, 160.0f, 50, 50, grid);

	_landIndexCount = (uint32)grid.indices.size();

	//
	// Extract the vertex elements we are interested and apply the height function to
	// each vertex.  
	//

	std::vector<Vertex> vertices(grid.vertices.size());
	for (size_t i = 0; i < grid.vertices.size(); ++i)
	{
		XMFLOAT3 p = grid.vertices[i].position;

		p.y = GetHillHeight(p.x, p.z);

		vertices[i].pos = p;
		vertices[i].normal = GetHillNormal(p.x, p.z);
	}

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(Vertex) * (uint32)grid.vertices.size();
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

void LightingDemo::BuildWavesGeometryBuffers()
{
	// Create the vertex buffer.  Note that we allocate space only, as
	// we will be updating the data every time step of the simulation.

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_DYNAMIC;
	vbd.ByteWidth = sizeof(Vertex) * _waves.VertexCount();
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

	int32 k = 0;
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
	ibd.ByteWidth = sizeof(uint32) * (uint32)indices.size();
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &indices[0];
	HR(_device->CreateBuffer(&ibd, &iinitData, _wavesIB.GetAddressOf()));
}

void LightingDemo::BuildFX()
{
	DWORD shaderFlags = 0;
#if defined( DEBUG ) || defined( _DEBUG )
	shaderFlags |= D3D10_SHADER_DEBUG;
	shaderFlags |= D3D10_SHADER_SKIP_OPTIMIZATION;
#endif

	ComPtr<ID3D10Blob> compiledShader = 0;
	ComPtr<ID3D10Blob> compilationMsgs = 0;
	
	HRESULT hr = ::D3DCompileFromFile(L"../Shaders/07. Lighting.fx", 0, D3D_COMPILE_STANDARD_FILE_INCLUDE, 0, "fx_5_0", shaderFlags, 0, compiledShader.GetAddressOf(), compilationMsgs.GetAddressOf());

	// compilationMsgs can store errors or warnings.
	if (FAILED(hr))
	{
		if (compilationMsgs != 0)
			::MessageBoxA(0, (char*)compilationMsgs->GetBufferPointer(), 0, 0);

		assert(false);
	}

	HR(D3DX11CreateEffectFromMemory(compiledShader->GetBufferPointer(), compiledShader->GetBufferSize(), 0, _device.Get(), _fx.GetAddressOf()));

	_tech = _fx->GetTechniqueByName("T0");
	_fxWorldViewProj = _fx->GetVariableByName("gWorldViewProj")->AsMatrix();
	_fxWorld = _fx->GetVariableByName("gWorld")->AsMatrix();
	_fxWorldInvTranspose = _fx->GetVariableByName("gWorldInvTranspose")->AsMatrix();
	_fxEyePosW = _fx->GetVariableByName("gEyePosW")->AsVector();
	_fxDirLight = _fx->GetVariableByName("gDirLight");
	_fxPointLight = _fx->GetVariableByName("gPointLight");
	_fxSpotLight = _fx->GetVariableByName("gSpotLight");
	_fxMaterial = _fx->GetVariableByName("gMaterial");

}

void LightingDemo::BuildVertexLayout()
{
	// Create the vertex input layout.
	D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};

	// Create the input layout
	D3DX11_PASS_DESC passDesc;
	_tech->GetPassByIndex(0)->GetDesc(&passDesc);

	HR(_device->CreateInputLayout(vertexDesc, 2, passDesc.pIAInputSignature, passDesc.IAInputSignatureSize, _inputLayout.GetAddressOf()));
}