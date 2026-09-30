#pragma once
#include "MeshGeometry.h"
#include "TextureMgr.h"
#include "Vertex.h"
#include "LightHelper.h"

class Mesh
{
public:
	Mesh();
	~Mesh();
	
	void Setup();

	uint32 SubsetCount;

	std::vector<Material> Mat;

	// 사용하지 않음 - 제거 예정 - UMaterial로 이전
	std::vector<ComPtr<ID3D11ShaderResourceView>> DiffuseMapSRV;
	std::vector<ComPtr<ID3D11ShaderResourceView>> NormalMapSRV;

	// Keep CPU copies of the mesh data to read from.  
	std::vector<Vertex::PosNormalTexTan2> Vertices;
	std::vector<USHORT> Indices;
	std::vector<MeshGeometry::Subset> Subsets;

	MeshGeometry ModelMesh;
	BouncingBall Ball;

	string Name;
	wstring Path;

public:
	friend void from_json(const json& j, Mesh& m);
	friend void to_json(json& j, const Mesh& m);

	void from_byte(ifstream& inStream);
	void to_byte(ofstream& outStream);
};

// 사용하지 않음 - 제거
struct MeshInstance
{
	shared_ptr<class Mesh> Model;
	XMFLOAT4X4 World;
};