#include "pch.h"
#include "19. InstancingAndCullingDemo.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "Vertex.h"
#include "RenderStates.h"


InstancingAndCullingDemo::InstancingAndCullingDemo(HINSTANCE hInstance)
	: App(hInstance)
{
	_mainWindowCaption= L"Instancing and Culling Demo";

	srand((unsigned int)time((time_t*)NULL));

	_lastMousePos.x = 0;
	_lastMousePos.y = 0;

	//_camera.SetPosition(0.0f, 2.0f, -15.0f);

	XMMATRIX I = ::XMMatrixIdentity();

	XMMATRIX skullScale = ::XMMatrixScaling(0.5f, 0.5f, 0.5f);
	XMMATRIX skullOffset = ::XMMatrixTranslation(0.0f, 1.0f, 0.0f);
	::XMStoreFloat4x4(&_skullWorld, ::XMMatrixMultiply(skullScale, skullOffset));

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

	_skullMat.Ambient = XMFLOAT4(0.4f, 0.4f, 0.4f, 1.0f);
	_skullMat.Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
	_skullMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 16.0f);
}

InstancingAndCullingDemo::~InstancingAndCullingDemo()
{
	Effects::DestroyAll();
	InputLayouts::DestroyAll();
}

bool InstancingAndCullingDemo::Init()
{
	if (!App::Init())
		return false;

	// Must init Effects first since InputLayouts depend on shader signatures.
	Effects::InitAll(_device, L"../Shaders/10. Basic.fx");
	InputLayouts::InitAll(_device);

	BuildSkullGeometryBuffers();
	BuildInstancedBuffer();

	return true;
}

void InstancingAndCullingDemo::OnResize()
{
	App::OnResize();

	_camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

	// Build the frustum from the projection matrix in view space.
	_camFrustum = BoundingFrustum(_camera.Proj());
	//ComputeFrustumFromProjection(&_camFrustum, &_camera.Proj());
}

void InstancingAndCullingDemo::UpdateScene(float dt)
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

	if (GetAsyncKeyState('1') & 0x8000)
		_frustumCullingEnabled = true;

	if (GetAsyncKeyState('2') & 0x8000)
		_frustumCullingEnabled = false;

	//
	// Perform frustum culling.
	//

	_camera.UpdateViewMatrix();
	_visibleObjectCount = 0;

	if (_frustumCullingEnabled)
	{
		XMVECTOR detView = XMMatrixDeterminant(_camera.View());
		XMMATRIX invView = XMMatrixInverse(&detView, _camera.View());

		D3D11_MAPPED_SUBRESOURCE mappedData;
		_deviceContext->Map(_instancedBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData);

		InstancedData* dataView = reinterpret_cast<InstancedData*>(mappedData.pData);

		for (uint32 i = 0; i < _instancedData.size(); ++i)
		{
			XMMATRIX W = ::XMLoadFloat4x4(&_instancedData[i].World);
			XMVECTOR D = ::XMMatrixDeterminant(W);
			XMMATRIX invWorld = ::XMMatrixInverse(&D, W);

			// View space to the object's local space.
			XMMATRIX toLocal = ::XMMatrixMultiply(invView, invWorld);

			// Decompose the matrix into its individual parts.
			XMVECTOR scale;
			XMVECTOR rotQuat;
			XMVECTOR translation;
			::XMMatrixDecompose(&scale, &rotQuat, &translation, toLocal);

			// Transform the camera frustum from view space to the object's local space.
			BoundingFrustum localspaceFrustum;

			_camFrustum.Transform(localspaceFrustum, XMVectorGetX(scale), rotQuat, translation);
			//XNA::TransformFrustum(&localspaceFrustum, &_camFrustum, );

			// Perform the box/frustum intersection test in local space.
			if (localspaceFrustum.Contains(_skullBox))
			{
				// Write the instance data to dynamic VB of the visible objects.
				dataView[_visibleObjectCount++] = _instancedData[i];
			}
		}

		_deviceContext->Unmap(_instancedBuffer.Get(), 0);
	}
	else // No culling enabled, draw all objects.
	{
		D3D11_MAPPED_SUBRESOURCE mappedData;
		_deviceContext->Map(_instancedBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData);

		InstancedData* dataView = reinterpret_cast<InstancedData*>(mappedData.pData);

		for (uint32 i = 0; i < _instancedData.size(); ++i)
		{
			dataView[_visibleObjectCount++] = _instancedData[i];
		}

		_deviceContext->Unmap(_instancedBuffer.Get(), 0);
	}

	std::wostringstream outs;
	outs.precision(6);
	outs << L"Instancing and Culling Demo" <<
		L"    " << _visibleObjectCount <<
		L" objects visible out of " << _instancedData.size();

	_mainWindowCaption = outs.str();
}

void InstancingAndCullingDemo::RenderApplication()
{
	_deviceContext->ClearRenderTargetView(_renderTargetView.Get(), reinterpret_cast<const float*>(&Colors::Silver));
	_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	_deviceContext->IASetInputLayout(InputLayouts::InstancedBasic32.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	uint32 stride[2] = { sizeof(Vertex::Basic32), sizeof(InstancedData) };
	uint32 offset[2] = { 0,0 };

	ID3D11Buffer* vbs[2] = { _skullVB.Get(), _instancedBuffer.Get() };

	XMMATRIX view = _camera.View();
	XMMATRIX proj = _camera.Proj();
	XMMATRIX viewProj = _camera.ViewProj();

	// Set per frame constants.
	Effects::InstancedBasicFX->SetDirLights(_dirLights,3);
	Effects::InstancedBasicFX->SetEyePosW(_camera.GetPosition());

	ComPtr<ID3DX11EffectTechnique> activeTech = Effects::InstancedBasicFX->InstancingTech;

	D3DX11_TECHNIQUE_DESC techDesc;
	activeTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		// Draw the skull.

		_deviceContext->IASetVertexBuffers(0, 2, vbs, stride, offset);
		_deviceContext->IASetIndexBuffer(_skullIB.Get(), DXGI_FORMAT_R32_UINT, 0);

		XMMATRIX world = XMLoadFloat4x4(&_skullWorld);
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);

		//Effects::InstancedBasicFX->SetWorld(world);
		//Effects::InstancedBasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::InstancedBasicFX->SetViewProj(viewProj);
		Effects::InstancedBasicFX->SetMaterial(_skullMat);

		activeTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexedInstanced(_skullIndexCount, _visibleObjectCount, 0, 0, 0);
	}

	HR(_swapChain->Present(0, 0));
}

void InstancingAndCullingDemo::OnMouseDown(WPARAM btnState, int32 x, int32 y)
{
	_lastMousePos.x = x;
	_lastMousePos.y = y;

	SetCapture(_hMainWnd);
}

void InstancingAndCullingDemo::OnMouseUp(WPARAM btnState, int32 x, int32 y)
{
	ReleaseCapture();
}

void InstancingAndCullingDemo::OnMouseMove(WPARAM btnState, int32 x, int32 y)
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

void InstancingAndCullingDemo::BuildSkullGeometryBuffers()
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

	XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
	XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

	XMVECTOR vMin = ::XMLoadFloat3(&vMinf3);
	XMVECTOR vMax = ::XMLoadFloat3(&vMaxf3);
	std::vector<Vertex::Basic32> vertices(vcount);
	for (uint32 i = 0; i < vcount; ++i)
	{
		fin >> vertices[i].pos.x >> vertices[i].pos.y >> vertices[i].pos.z;
		fin >> vertices[i].normal.x >> vertices[i].normal.y >> vertices[i].normal.z;

		XMVECTOR P = ::XMLoadFloat3(&vertices[i].pos);

		vMin = ::XMVectorMin(vMin, P);
		vMax = ::XMVectorMax(vMax, P);
	}

	::XMStoreFloat3(&_skullBox.Center, 0.5f * (vMin + vMax));
	::XMStoreFloat3(&_skullBox.Extents, 0.5f * (vMax - vMin));

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

void InstancingAndCullingDemo::BuildInstancedBuffer()
{
	const int32 n = 5;
	_instancedData.resize(n * n * n);

	float width = 200.0f;
	float height = 200.0f;
	float depth = 200.0f;

	float x = -0.5f * width;
	float y = -0.5f * height;
	float z = -0.5f * depth;
	float dx = width / (n - 1);
	float dy = height / (n - 1);
	float dz = depth / (n - 1);

	for (int k = 0; k < n; ++k)
	{
		for (int i = 0; i < n; ++i)
		{
			for (int j = 0; j < n; ++j)
			{
				// Position instanced along a 3D grid.
				_instancedData[k * n * n + i * n + j].World = XMFLOAT4X4(
					1.0f, 0.0f, 0.0f, 0.0f,
					0.0f, 1.0f, 0.0f, 0.0f,
					0.0f, 0.0f, 1.0f, 0.0f,
					x + j * dx, y + i * dy, z + k * dz, 1.0f);

				// Random color.
				_instancedData[k * n * n + i * n + j].Color.x = MathHelper::RandF(0.0f, 1.0f);
				_instancedData[k * n * n + i * n + j].Color.y = MathHelper::RandF(0.0f, 1.0f);
				_instancedData[k * n * n + i * n + j].Color.z = MathHelper::RandF(0.0f, 1.0f);
				_instancedData[k * n * n + i * n + j].Color.w = 1.0f;
			}
		}
	}

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_DYNAMIC;
	vbd.ByteWidth = sizeof(InstancedData) * _instancedData.size();
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vbd.MiscFlags = 0;
	vbd.StructureByteStride = 0;

	HR(_device->CreateBuffer(&vbd, 0, _instancedBuffer.GetAddressOf()));
}