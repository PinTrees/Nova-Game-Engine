#include "pch.h"
#include "Sky.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "Vertex.h"
#include "Utils.h"

Sky::Sky(ComPtr<ID3D11Device> device, const std::wstring& cubemapFilename, float skySphereRadius)
{
	_cubeMapSRV = Utils::LoadTexture(device, cubemapFilename);

	GeometryGenerator::MeshData sphere;
	GeometryGenerator geoGen;
	geoGen.CreateSphere(skySphereRadius, 30, 30, sphere);

	std::vector<XMFLOAT3> vertices(sphere.vertices.size());

	for (size_t i = 0; i < sphere.vertices.size(); ++i)
	{
		vertices[i] = sphere.vertices[i].position;
	}

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(XMFLOAT3) * vertices.size();
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	vbd.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = &vertices[0];

	HR(device->CreateBuffer(&vbd, &vinitData, _vb.GetAddressOf()));

	_indexCount = sphere.indices.size();

	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(USHORT) * _indexCount;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.StructureByteStride = 0;
	ibd.MiscFlags = 0;

	std::vector<USHORT> indices16;
	indices16.assign(sphere.indices.begin(), sphere.indices.end());

	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &indices16[0];

	HR(device->CreateBuffer(&ibd, &iinitData, _ib.GetAddressOf()));
}

Sky::~Sky()
{
}

ComPtr<ID3D11ShaderResourceView> Sky::CubeMapSRV()
{
	return _cubeMapSRV;
}

void Sky::Draw(ComPtr<ID3D11DeviceContext> dc, const Camera& camera)
{
	// center Sky about eye in world space
	XMFLOAT3 eyePos = camera.GetPosition();
	XMMATRIX T = ::XMMatrixTranslation(eyePos.x, eyePos.y, eyePos.z);

	XMMATRIX WVP = ::XMMatrixMultiply(T, camera.ViewProj());

	Effects::SkyFX->SetWorldViewProj(WVP);
	Effects::SkyFX->SetCubeMap(_cubeMapSRV.Get());

	uint32 stride = sizeof(XMFLOAT3);
	uint32 offset = 0;
	dc->IASetVertexBuffers(0, 1, _vb.GetAddressOf(), &stride, &offset);
	dc->IASetIndexBuffer(_ib.Get(), DXGI_FORMAT_R16_UINT, 0);
	dc->IASetInputLayout(InputLayouts::Pos.Get());
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3DX11_TECHNIQUE_DESC techDesc;
	Effects::SkyFX->SkyTech->GetDesc(&techDesc);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		ComPtr<ID3DX11EffectPass> pass = Effects::SkyFX->SkyTech->GetPassByIndex(p);

		pass->Apply(0, dc.Get());

		dc->DrawIndexed(_indexCount, 0, 0);
	}
}