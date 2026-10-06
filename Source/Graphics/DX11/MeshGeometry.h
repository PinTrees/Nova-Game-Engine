#pragma once
#include "Utils.h"

// BlendShape (모프 타깃 · Unity 의 Mesh BlendShape): 바뀌는 정점만 위치 · 법선 차이
struct BlendShapeData
{
	string Name;
	std::vector<uint32> Index;      // 정점 번호 (메시 전체 정점 배열에서)
	std::vector<XMFLOAT3> DPos;     // 위치 차이 (가중치 100 % 일 때)
	std::vector<XMFLOAT3> DNrm;     // 법선 차이
};

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
	void SetVertices(ComPtr<GfxDevice> device, const VertexType* vertices, uint32 count);
	// 매 프레임 바뀌는 정점 (천): 처음 한 번 동적 버퍼로 바꾸고 덮어쓴다 (Map · Discard)
	template <typename VertexType>
	void UpdateVertices(GfxDevice* device, GfxContext* dc, const VertexType* vertices, uint32 count);

	void SetIndices(ComPtr<GfxDevice> device, const USHORT* indices, uint32 count);

	void SetSubsetTable(std::vector<Subset>& subsetTable);

	void Draw(ComPtr<GfxContext> dc, uint32 subsetId);
	// 정점 버퍼만 바꿔서 (BlendShape 를 섞은 렌더러 자기 버퍼) — null 이면 원래 것
	void Draw(GfxContext* dc, uint32 subsetId, GfxBuffer* vertexBuffer);
	void InstancingDraw(ComPtr<GfxContext> dc, uint32 subsetId, uint32 instancingSize);
	// GPU 가 고른 인스턴스로 그리기 (OcclusionCulling 의 간접 그리기): 정점 · 인덱스 버퍼만 묶는다
	void BindForInstancing(GfxContext* dc);
	const Subset& GetSubset(uint32 subsetId) const { return _subsetTable[subsetId]; }

	size_t GpuBytes() const;   // 정점 + 인덱스 버퍼 (Profiler 메모리)
private:
	ComPtr<GfxBuffer> _vb;
	ComPtr<GfxBuffer> _ib;

	bool _dynamic = false;
	uint32 _dynamicCount = 0;

	DXGI_FORMAT _indexBufferFormat; // Always 16-bit
	uint32 _vertexStride;

	std::vector<Subset> _subsetTable;
};

template <typename VertexType>
void MeshGeometry::SetVertices(ComPtr<GfxDevice> device, const VertexType* vertices, uint32 count)
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
	_dynamic = false;
}

template <typename VertexType>
void MeshGeometry::UpdateVertices(GfxDevice* device, GfxContext* dc, const VertexType* vertices, uint32 count)
{
	if (count == 0 || device == nullptr || dc == nullptr)
		return;
	if (!_dynamic || _dynamicCount != count)
	{
		D3D11_BUFFER_DESC vbd = {};
		vbd.Usage = D3D11_USAGE_DYNAMIC;
		vbd.ByteWidth = sizeof(VertexType) * count;
		vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		D3D11_SUBRESOURCE_DATA init = {};
		init.pSysMem = vertices;
		ComPtr<GfxBuffer> vb;
		if (FAILED(device->CreateBuffer(&vbd, &init, vb.GetAddressOf())))
			return;
		_vb = vb;
		_vertexStride = sizeof(VertexType);
		_dynamic = true;
		_dynamicCount = count;
		return;
	}
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(dc->Map(_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return;
	memcpy(mapped.pData, vertices, sizeof(VertexType) * count);
	dc->Unmap(_vb.Get(), 0);
}
