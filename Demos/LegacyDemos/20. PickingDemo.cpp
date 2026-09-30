#include "pch.h"
#include "20. PickingDemo.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "Vertex.h"
#include "RenderStates.h"


PickingDemo::PickingDemo(HINSTANCE hInstance)
	: App(hInstance)
{
	_mainWindowCaption = L"Picking Demo";

	_lastMousePos.x = 0;
	_lastMousePos.y = 0;

	//_camera.SetPosition(0.0f, 2.0f, -15.0f);

	XMMATRIX MeshScale = ::XMMatrixScaling(0.5f, 0.5f, 0.5f);
	XMMATRIX MeshOffset = ::XMMatrixTranslation(0.0f, 1.0f, 0.0f);
	::XMStoreFloat4x4(&_meshWorld, ::XMMatrixMultiply(MeshScale, MeshOffset));

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

	_meshMat.Ambient = XMFLOAT4(0.4f, 0.4f, 0.4f, 1.0f);
	_meshMat.Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
	_meshMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 16.0f);

	_pickedTriangleMat.Ambient = XMFLOAT4(0.0f, 0.8f, 0.4f, 1.0f);
	_pickedTriangleMat.Diffuse = XMFLOAT4(0.0f, 0.8f, 0.4f, 1.0f);
	_pickedTriangleMat.Specular = XMFLOAT4(0.0f, 0.0f, 0.0f, 16.0f);
}

PickingDemo::~PickingDemo()
{
	Effects::DestroyAll();
	InputLayouts::DestroyAll();
}

bool PickingDemo::Init()
{
	if (!App::Init())
		return false;

	// Must init Effects first since InputLayouts depend on shader signatures.
	Effects::InitAll(_device, L"../Shaders/10. Basic.fx");
	InputLayouts::InitAll(_device);
	RenderStates::InitAll(_device);

	BuildMeshGeometryBuffers();

	return true;
}

void PickingDemo::OnResize()
{
	App::OnResize();

	_camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void PickingDemo::UpdateScene(float dt)
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
}

void PickingDemo::RenderApplication()
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

	ComPtr<ID3DX11EffectTechnique> activeMeshTech = Effects::BasicFX->Light3Tech;

	D3DX11_TECHNIQUE_DESC techDesc;
	activeMeshTech->GetDesc(&techDesc);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		// Draw the Mesh.

		if (GetAsyncKeyState('1') & 0x8000)
			_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

		_deviceContext->IASetVertexBuffers(0, 1, _meshVB.GetAddressOf(), &stride, &offset);
		_deviceContext->IASetIndexBuffer(_meshIB.Get(), DXGI_FORMAT_R32_UINT, 0);

		XMMATRIX world = XMLoadFloat4x4(&_meshWorld);
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * view * proj;

		Effects::BasicFX->SetWorld(world);
		Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BasicFX->SetWorldViewProj(worldViewProj);
		Effects::BasicFX->SetMaterial(_meshMat);

		activeMeshTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_meshIndexCount, 0, 0);

		// Restore default
		_deviceContext->RSSetState(0);

		// Draw just the picked triangle again with a different material to highlight it.

		if (_pickedTriangle != -1)
		{
			// Change depth test from < to <= so that if we draw the same triangle twice, it will still pass
			// the depth test.  This is because we redraw the picked triangle with a different material
			// to highlight it.  

			_deviceContext->OMSetDepthStencilState(RenderStates::LessEqualDSS.Get(), 0);

			Effects::BasicFX->SetMaterial(_pickedTriangleMat);
			activeMeshTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
			_deviceContext->DrawIndexed(3, 3 * _pickedTriangle, 0);

			// restore default
			_deviceContext->OMSetDepthStencilState(0, 0);
		}
	}

	HR(_swapChain->Present(0, 0));
}

void PickingDemo::OnMouseDown(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		_lastMousePos.x = x;
		_lastMousePos.y = y;

		SetCapture(_hMainWnd);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		Pick(x, y);
	}
}

void PickingDemo::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void PickingDemo::OnMouseMove(WPARAM btnState, int x, int y)
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

void PickingDemo::BuildMeshGeometryBuffers()
{
	std::ifstream fin("../Resources/Models/car.txt");

	if (!fin)
	{
		MessageBox(0, L"../Resources/Models/car.txt not found.", 0, 0);
		return;
	}

	uint32 vcount = 0;
	uint32 tcount = 0;
	std::string ignore;

	fin >> ignore >> vcount;
	fin >> ignore >> tcount;
	fin >> ignore >> ignore >> ignore >> ignore;

	XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
	XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

	XMVECTOR vMin = ::XMLoadFloat3(&vMinf3);
	XMVECTOR vMax = ::XMLoadFloat3(&vMaxf3);

	_meshVertices.resize(vcount);

	for (uint32 i = 0; i < vcount; ++i)
	{
		fin >> _meshVertices[i].pos.x >> _meshVertices[i].pos.y >> _meshVertices[i].pos.z;
		fin >> _meshVertices[i].normal.x >> _meshVertices[i].normal.y >> _meshVertices[i].normal.z;

		XMVECTOR P = ::XMLoadFloat3(&_meshVertices[i].pos);

		vMin = ::XMVectorMin(vMin, P);
		vMax = ::XMVectorMax(vMax, P);
	}

	::XMStoreFloat3(&_meshBox.Center, 0.5f * (vMin + vMax));
	::XMStoreFloat3(&_meshBox.Extents, 0.5f * (vMax - vMin));

	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	_meshIndexCount = 3 * tcount;
	_meshIndices.resize(_meshIndexCount);

	for (uint32 i = 0; i < tcount; ++i)
	{
		fin >> _meshIndices[i * 3 + 0] >> _meshIndices[i * 3 + 1] >> _meshIndices[i * 3 + 2];
	}

	fin.close();

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(Vertex::Basic32) * vcount;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = &_meshVertices[0];
	HR(_device->CreateBuffer(&vbd, &vinitData, _meshVB.GetAddressOf()));

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(uint32) * _meshIndexCount;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &_meshIndices[0];
	HR(_device->CreateBuffer(&ibd, &iinitData, _meshIB.GetAddressOf()));
}

void PickingDemo::Pick(int32 sx, int32 sy)
{
	XMMATRIX P = _camera.Proj();

	Matrix m = P;
	// Compute picking ray in view space.
	float vx = (+2.0f * sx / _clientWidth - 1.0f) / m(0, 0); // P(0, 0);
	float vy = (-2.0f * sy / _clientHeight + 1.0f) / m(1, 1); // P(1, 1);

	// Ray definition in view space.
	XMVECTOR rayOrigin = ::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMVECTOR rayDir = ::XMVectorSet(vx, vy, 1.0f, 0.0f);

	// Tranform ray to local space of Mesh.
	XMMATRIX V = _camera.View();
	XMVECTOR D1 = ::XMMatrixDeterminant(V);
	XMMATRIX invView = ::XMMatrixInverse(&D1, V);

	XMMATRIX W = ::XMLoadFloat4x4(&_meshWorld);
	XMVECTOR D2 = ::XMMatrixDeterminant(W);
	XMMATRIX invWorld = ::XMMatrixInverse(&D2, W);

	XMMATRIX toLocal = ::XMMatrixMultiply(invView, invWorld);

	rayOrigin = ::XMVector3TransformCoord(rayOrigin, toLocal);
	rayDir = ::XMVector3TransformNormal(rayDir, toLocal);

	// Make the ray direction unit length for the intersection tests.
	rayDir = ::XMVector3Normalize(rayDir);

	// If we hit the bounding box of the Mesh, then we might have picked a Mesh triangle,
	// so do the ray/triangle tests.
	//
	// If we did not hit the bounding box, then it is impossible that we hit 
	// the Mesh, so do not waste effort doing ray/triangle tests.

	// Assume we have not picked anything yet, so init to -1.
	_pickedTriangle = -1;
	float tmin = 0.0f;

	if (_meshBox.Intersects(rayOrigin, rayDir, tmin))
	{
		// Find the nearest ray/triangle intersection.
		tmin = MathHelper::Infinity;
		for (UINT i = 0; i < _meshIndices.size() / 3; ++i)
		{
			// Indices for this triangle.
			UINT i0 = _meshIndices[i * 3 + 0];
			UINT i1 = _meshIndices[i * 3 + 1];
			UINT i2 = _meshIndices[i * 3 + 2];

			// Vertices for this triangle.
			XMVECTOR v0 = ::XMLoadFloat3(&_meshVertices[i0].pos);
			XMVECTOR v1 = ::XMLoadFloat3(&_meshVertices[i1].pos);
			XMVECTOR v2 = ::XMLoadFloat3(&_meshVertices[i2].pos);

			// We have to iterate over all the triangles in order to find the nearest intersection.
			float t = 0.0f;

			if (TriangleTests::Intersects(rayOrigin, rayDir, v0, v1, v2, t))
			{
				if (t < tmin)
				{
					// This is the new nearest picked triangle.
					tmin = t;
					_pickedTriangle = i;
				}
			}
		}
	}
}