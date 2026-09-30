#include "pch.h"
#include "27. AmbientOcclusionDemo.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "Vertex.h"
#include "RenderStates.h"
#include "Sky.h"
#include "ShadowMap.h"

AmbientOcclusionDemo::AmbientOcclusionDemo(HINSTANCE hInstance)
	: App(hInstance)
{
	_mainWindowCaption = L"Ambient Occlusion";

	_lastMousePos.x = 0;
	_lastMousePos.y = 0;

	//_camera.SetPosition(0.0f, 5.0f, -5.0f);
	_camera.LookAt(
		XMFLOAT3(-4.0f, 4.0f, -4.0f),
		XMFLOAT3(0.0f, 2.2f, 0.0f),
		XMFLOAT3(0.0f, 1.0f, 0.0f));

	XMMATRIX skullScale = ::XMMatrixScaling(0.5f, 0.5f, 0.5f);
	XMMATRIX skullOffset = ::XMMatrixTranslation(0.0f, 1.0f, 0.0f);
	::XMStoreFloat4x4(&_skullWorld, ::XMMatrixMultiply(skullScale, skullOffset));
}

AmbientOcclusionDemo::~AmbientOcclusionDemo()
{
	Effects::DestroyAll();
	InputLayouts::DestroyAll();
}

bool AmbientOcclusionDemo::Init()
{
	if (!App::Init())
		return false;

	// Must init Effects first since InputLayouts depend on shader signatures.
	Effects::InitAll(_device, L"../Shaders/26. Basic.fx");
	InputLayouts::InitAll(_device);

	BuildSkullGeometryBuffers();

	return true;
}

void AmbientOcclusionDemo::OnResize()
{
	App::OnResize();

	_camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void AmbientOcclusionDemo::UpdateScene(float dt)
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
}

void AmbientOcclusionDemo::RenderApplication()
{
	_deviceContext->ClearRenderTargetView(_renderTargetView.Get(), reinterpret_cast<const float*>(&Colors::Silver));
	_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	_deviceContext->IASetInputLayout(InputLayouts::AmbientOcclusion.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	uint32 stride = sizeof(Vertex::AmbientOcclusion);
	uint32 offset = 0;

	_camera.UpdateViewMatrix();

	XMMATRIX view = _camera.View();
	XMMATRIX proj = _camera.Proj();
	XMMATRIX viewProj = _camera.ViewProj();

	ComPtr<ID3DX11EffectTechnique> activeSkullTech = Effects::AmbientOcclusionFX->AmbientOcclusionTech;

	D3DX11_TECHNIQUE_DESC techDesc;
	activeSkullTech->GetDesc(&techDesc);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		// Draw the skull.

		_deviceContext->IASetVertexBuffers(0, 1, _skullVB.GetAddressOf(), &stride, &offset);
		_deviceContext->IASetIndexBuffer(_skullIB.Get(), DXGI_FORMAT_R32_UINT, 0);

		XMMATRIX world = XMLoadFloat4x4(&_skullWorld);
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * view * proj;

		Effects::AmbientOcclusionFX->SetWorldViewProj(worldViewProj);

		activeSkullTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_skullIndexCount, 0, 0);
	}

	HR(_swapChain->Present(0, 0));
}

void AmbientOcclusionDemo::OnMouseDown(WPARAM btnState, int32 x, int32 y)
{
	_lastMousePos.x = x;
	_lastMousePos.y = y;

	SetCapture(_hMainWnd);
}

void AmbientOcclusionDemo::OnMouseUp(WPARAM btnState, int32 x, int32 y)
{
	ReleaseCapture();
}

void AmbientOcclusionDemo::OnMouseMove(WPARAM btnState, int32 x, int32 y)
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

void AmbientOcclusionDemo::BuildVertexAmbientOcclusion(
	std::vector<Vertex::AmbientOcclusion>& vertices,
	const std::vector<uint32>& indices)
{
	uint32 vcount = vertices.size();
	uint32 tcount = indices.size() / 3;

	std::vector<XMFLOAT3> positions(vcount);
	for (uint32 i = 0; i < vcount; ++i)
		positions[i] = vertices[i].Pos;
		
	Octree octree;
	octree.Build(positions, indices);

	// For each vertex, count how many triangles contain the vertex.
	std::vector<int32> vertexSharedCount(vcount);

	for (uint32 i = 0; i < tcount; ++i)
	{
		uint32 i0 = indices[i * 3 + 0];
		uint32 i1 = indices[i * 3 + 1];
		uint32 i2 = indices[i * 3 + 2];

		XMVECTOR v0 = ::XMLoadFloat3(&vertices[i0].Pos);
		XMVECTOR v1 = ::XMLoadFloat3(&vertices[i1].Pos);
		XMVECTOR v2 = ::XMLoadFloat3(&vertices[i2].Pos);

		XMVECTOR edge0 = v1 - v0;
		XMVECTOR edge1 = v2 - v0;

		XMVECTOR normal = ::XMVector3Normalize(::XMVector3Cross(edge0, edge1));

		XMVECTOR centroid = (v0 + v1 + v2) / 3.0f;

		// Offset to avoid self intersection.
		centroid += 0.001f * normal;

		const int32 NumSampleRays = 32;
		float numUnoccluded = 0;

		for (int32 j = 0; j < NumSampleRays; ++j)
		{
			XMVECTOR randomDir = MathHelper::RandHemisphereUnitVec3(normal);

			// TODO: Technically we should not count intersections that are far 
			// away as occluding the triangle, but this is OK for demo.
			if (!octree.RayOctreeIntersect(centroid, randomDir))
			{
				numUnoccluded++;
			}
		}

		float ambientAccess = numUnoccluded / NumSampleRays;

		// Average with vertices that share this face.
		vertices[i0].AmbientAccess += ambientAccess;
		vertices[i1].AmbientAccess += ambientAccess;
		vertices[i2].AmbientAccess += ambientAccess;

		vertexSharedCount[i0]++;
		vertexSharedCount[i1]++;
		vertexSharedCount[i2]++;
	}

	// Finish average by dividing by the number of samples we added.
	for (uint32 i = 0; i < vcount; ++i)
	{
		vertices[i].AmbientAccess /= vertexSharedCount[i];
	}
}

void AmbientOcclusionDemo::BuildSkullGeometryBuffers()
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

	std::vector<Vertex::AmbientOcclusion> vertices(vcount);
	for (uint32 i = 0; i < vcount; ++i)
	{
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
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


	BuildVertexAmbientOcclusion(vertices, indices);


	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(Vertex::AmbientOcclusion) * vcount;
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

