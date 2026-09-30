#pragma once
#include "Utils.h"

class MeshGeometry
{
public:
	struct Subset
	{
		Subset() :
			Id(-1),
			VertexStart(0),
			VertexCount(0),
			FaceStart(0),
			FaceCount(0),
			MaterialIndex(0),
			Name("")
		{
		}

		string Name; 
		uint32 Id;
		uint32 MaterialIndex;
		uint32 VertexStart;
		uint32 VertexCount;
		uint32 FaceStart;
		uint32 FaceCount;
	};

public:
	MeshGeometry();
	~MeshGeometry();

	template <typename VertexType>
	void SetVertices(ComPtr<ID3D11Device> device, const VertexType* vertices, uint32 count);

	void SetIndices(ComPtr<ID3D11Device> device, const USHORT* indices, uint32 count);

	void SetSubsetTable(std::vector<Subset>& subsetTable);

	void Draw(ComPtr<ID3D11DeviceContext> dc, uint32 subsetId);
	void InstancingDraw(ComPtr<ID3D11DeviceContext> dc, uint32 subsetId, uint32 instancingSize);
private:
	ComPtr<ID3D11Buffer> _vb;
	ComPtr<ID3D11Buffer> _ib;

	DXGI_FORMAT _indexBufferFormat; // Always 16-bit
	uint32 _vertexStride;

	std::vector<Subset> _subsetTable;
};

template <typename VertexType>
void MeshGeometry::SetVertices(ComPtr<ID3D11Device> device, const VertexType* vertices, uint32 count)
{
	_vertexStride = sizeof(VertexType);

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(VertexType) * count;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	vbd.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = vertices;

	HR(device->CreateBuffer(&vbd, &vinitData, _vb.GetAddressOf()));
}
