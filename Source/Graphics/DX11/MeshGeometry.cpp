#include "pch.h"
#include "MeshGeometry.h"
#include "Utils.h"

MeshGeometry::MeshGeometry()
	: _indexBufferFormat(DXGI_FORMAT_R16_UINT), _vertexStride(0)
{
}

MeshGeometry::~MeshGeometry()
{
}

void MeshGeometry::SetIndices(ComPtr<ID3D11Device> device, const USHORT* indices, uint32 count)
{
	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(USHORT) * count;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	ibd.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = indices;

	HR(device->CreateBuffer(&ibd, &iinitData, _ib.GetAddressOf()));
}

void MeshGeometry::SetSubsetTable(std::vector<Subset>& subsetTable)
{
	_subsetTable = subsetTable;
}

void MeshGeometry::Draw(ComPtr<ID3D11DeviceContext> dc, uint32 subsetId)
{
	uint32 offset = 0;

	dc->IASetVertexBuffers(0, 1, _vb.GetAddressOf(), &_vertexStride, &offset);
	dc->IASetIndexBuffer(_ib.Get(), _indexBufferFormat, 0);

	dc->DrawIndexed(
		_subsetTable[subsetId].FaceCount * 3,
		_subsetTable[subsetId].FaceStart * 3,
		_subsetTable[subsetId].VertexStart);
}

void MeshGeometry::InstancingDraw(ComPtr<ID3D11DeviceContext> dc, uint32 subsetId, uint32 instancingSize)
{
	uint32 offset = 0;

	dc->IASetVertexBuffers(0, 1, _vb.GetAddressOf(), &_vertexStride, &offset);
	dc->IASetIndexBuffer(_ib.Get(), _indexBufferFormat, 0);

	dc->DrawIndexedInstanced(
		_subsetTable[subsetId].FaceCount * 3,
		instancingSize,
		_subsetTable[subsetId].FaceStart * 3,
		_subsetTable[subsetId].VertexStart,
		0U);
}