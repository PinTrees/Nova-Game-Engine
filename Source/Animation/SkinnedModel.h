#pragma once
#include "TextureMgr.h"
#include "Vertex.h"
#include "MeshGeometry.h"
#include "SkinnedData.h"
#include "Vertex.h"
#include "LightHelper.h"

class SkinnedModel
{
public:
	SkinnedModel(ComPtr<ID3D11Device> device, TextureMgr& texMgr, const std::string& modelFilename, const std::wstring& texturePath);
	~SkinnedModel();

	uint32 SubsetCount;

	std::vector<Material> Mat;
	std::vector<ComPtr<ID3D11ShaderResourceView>> DiffuseMapSRV;
	std::vector<ComPtr<ID3D11ShaderResourceView>> NormalMapSRV;

	// Keep CPU copies of the mesh data to read from.  
	std::vector<Vertex::PosNormalTexTanSkinned> Vertices;
	std::vector<USHORT> Indices;
	std::vector<MeshGeometry::Subset> Subsets;

	MeshGeometry ModelMesh;
	SkinnedData SkinnedData;
};

struct SkinnedModelInstance
{
	shared_ptr<SkinnedModel> Model;
	float TimePos;
	std::string ClipName;
	XMFLOAT4X4 World;
	std::vector<XMFLOAT4X4> FinalTransforms;

	void Update(float dt);
};