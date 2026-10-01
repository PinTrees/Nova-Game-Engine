#include "pch.h"
#include "SkinnedModel.h"
#include "LoadM3d.h"

SkinnedModel::SkinnedModel(ComPtr<GfxDevice> device, TextureMgr& texMgr, const std::string& modelFilename, const std::wstring& texturePath)
{

	std::vector<M3dMaterial> mats;
	M3DLoader m3dLoader;
	m3dLoader.LoadM3d(modelFilename, Vertices, Indices, Subsets, mats, SkinnedData);

	ModelMesh.SetVertices(device, &Vertices[0], Vertices.size());
	ModelMesh.SetIndices(device, &Indices[0], Indices.size());
	ModelMesh.SetSubsetTable(Subsets);

	SubsetCount = mats.size();

	for (uint32 i = 0; i < SubsetCount; ++i)
	{
		Mat.push_back(mats[i].Mat);

		ComPtr<GfxShaderResourceView> diffuseMapSRV = texMgr.CreateTexture(texturePath + mats[i].DiffuseMapName);
		DiffuseMapSRV.push_back(diffuseMapSRV);

		ComPtr<GfxShaderResourceView> normalMapSRV = texMgr.CreateTexture(texturePath + mats[i].NormalMapName);
		NormalMapSRV.push_back(normalMapSRV);
	}
}

SkinnedModel::~SkinnedModel()
{
}

void SkinnedModelInstance::Update(float dt)
{
	TimePos += dt;
	Model->SkinnedData.GetFinalTransforms(ClipName, TimePos, FinalTransforms);

	// Loop animation
	if (TimePos > Model->SkinnedData.GetClipEndTime(ClipName))
		TimePos = 0.0f;
}
