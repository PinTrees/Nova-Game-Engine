#include "pch.h"
#include "08. CrateDemo.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "Vertex.h"

CrateDemo::CrateDemo(HINSTANCE hInstance) : App(hInstance)
{
	_mainWindowCaption = L"Crate Demo";

	_lastMousePos.x = 0;
	_lastMousePos.y = 0;

	XMMATRIX I = ::XMMatrixIdentity();
	::XMStoreFloat4x4(&_boxWorld, I);
	::XMStoreFloat4x4(&_texTransform, I);
	::XMStoreFloat4x4(&_view, I);
	::XMStoreFloat4x4(&_proj, I);

	_dirLights[0].Ambient = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
	_dirLights[0].Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
	_dirLights[0].Specular = XMFLOAT4(0.6f, 0.6f, 0.6f, 16.0f);
	_dirLights[0].Direction = XMFLOAT3(0.707f, -0.707f, 0.0f);

	_dirLights[1].Ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	_dirLights[1].Diffuse = XMFLOAT4(1.4f, 1.4f, 1.4f, 1.0f);
	_dirLights[1].Specular = XMFLOAT4(0.3f, 0.3f, 0.3f, 16.0f);
	_dirLights[1].Direction = XMFLOAT3(-0.707f, 0.0f, 0.707f);

	_boxMat.Ambient = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	_boxMat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_boxMat.Specular = XMFLOAT4(0.6f, 0.6f, 0.6f, 16.0f);
}

CrateDemo::~CrateDemo()
{
	Effects::DestroyAll();
	InputLayouts::DestroyAll();
}

bool CrateDemo::Init()
{
	if (!App::Init())
		return false;

	// Must init Effects first since InputLayouts depend on shader signatures.
	Effects::InitAll(_device, L"../Shaders/08. Basic.fx");
	InputLayouts::InitAll(_device);

	DirectX::TexMetadata md;
	DirectX::ScratchImage img;
	HRESULT hr = ::LoadFromWICFile(L"../Resources/Textures/WoodCrate01.dds", WIC_FLAGS_NONE, &md, img);
	CHECK(hr);

	// 여러 버전이 있음. (~CreateShaderResourceViewEx)
	hr = ::CreateShaderResourceView(_device.Get(), img.GetImages(), img.GetImageCount(), md, _diffuseMapSRV.GetAddressOf());
	CHECK(hr);

	BuildGeometryBuffers();

	return true;
}

void CrateDemo::OnResize()
{
	App::OnResize();

	XMMATRIX P = ::XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	::XMStoreFloat4x4(&_proj, P);
}

void CrateDemo::UpdateScene(float dt)
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
}

void CrateDemo::RenderApplication()
{
	_deviceContext->ClearRenderTargetView(_renderTargetView.Get(), reinterpret_cast<const float*>(&Colors::LightSteelBlue));
	_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	_deviceContext->IASetInputLayout(InputLayouts::Basic32.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	uint32 stride = sizeof(Vertex::Basic32);
	uint32 offset = 0;

	XMMATRIX view = XMLoadFloat4x4(&_view);
	XMMATRIX proj = XMLoadFloat4x4(&_proj);
	XMMATRIX viewProj = view * proj;

	// Set per frame constants.
	Effects::BasicFX->SetDirLights(_dirLights);
	Effects::BasicFX->SetEyePosW(_eyePosW);

	ComPtr<ID3DX11EffectTechnique> activeTech = Effects::BasicFX->Light2TexTech;

	D3DX11_TECHNIQUE_DESC techDesc;
	activeTech->GetDesc(&techDesc);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		_deviceContext->IASetVertexBuffers(0, 1, _boxVB.GetAddressOf(), &stride, &offset);
		_deviceContext->IASetIndexBuffer(_boxIB.Get(), DXGI_FORMAT_R32_UINT, 0);

		// Draw the box.
		XMMATRIX world = ::XMLoadFloat4x4(&_boxWorld);
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * view * proj;

		Effects::BasicFX->SetWorld(world);
		Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BasicFX->SetWorldViewProj(worldViewProj);
		Effects::BasicFX->SetTexTransform(XMLoadFloat4x4(&_texTransform));
		Effects::BasicFX->SetMaterial(_boxMat);
		Effects::BasicFX->SetDiffuseMap(_diffuseMapSRV.Get());

		activeTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_boxIndexCount, _boxIndexOffset, _boxVertexOffset);
	}

	HR(_swapChain->Present(0, 0));
}

void CrateDemo::OnMouseDown(WPARAM btnState, int x, int y)
{
	_lastMousePos.x = x;
	_lastMousePos.y = y;

	SetCapture(_hMainWnd);
}

void CrateDemo::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void CrateDemo::OnMouseMove(WPARAM btnState, int x, int y)
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
		// Make each pixel correspond to 0.01 unit in the scene.
		float dx = 0.01f * static_cast<float>(x - _lastMousePos.x);
		float dy = 0.01f * static_cast<float>(y - _lastMousePos.y);

		// Update the camera radius based on input.
		_radius += dx - dy;

		// Restrict the radius.
		_radius = MathHelper::Clamp(_radius, 1.0f, 15.0f);
	}

	_lastMousePos.x = x;
	_lastMousePos.y = y;
}

void CrateDemo::BuildGeometryBuffers()
{
	GeometryGenerator::MeshData box;

	GeometryGenerator geoGen;
	geoGen.CreateBox(1.0f, 1.0f, 1.0f, box);

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	_boxVertexOffset = 0;

	// Cache the index count of each object.
	_boxIndexCount = box.indices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	_boxIndexOffset = 0;

	uint32 totalVertexCount = box.vertices.size();

	uint32 totalIndexCount = _boxIndexCount;

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

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(Vertex::Basic32) * totalVertexCount;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = &vertices[0];
	HR(_device->CreateBuffer(&vbd, &vinitData, _boxVB.GetAddressOf()));

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	std::vector<uint32> indices;
	indices.insert(indices.end(), box.indices.begin(), box.indices.end());

	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(uint32) * totalIndexCount;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &indices[0];
	HR(_device->CreateBuffer(&ibd, &iinitData, _boxIB.GetAddressOf()));
}

