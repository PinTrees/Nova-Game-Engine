#pragma once

#include "MeshGeometry.h"
#include "LightHelper.h"
#include "Vertex.h"
#include "SkinnedData.h"

struct M3dMaterial
{
	Material Mat;
	bool AlphaClip;
	std::string EffectTypeName;
	std::wstring DiffuseMapName;
	std::wstring NormalMapName;
};

class M3DLoader
{
public:
	bool LoadM3d(const std::string& filename,
		std::vector<Vertex::PosNormalTexTan2>& vertices,
		std::vector<USHORT>& indices,
		std::vector<MeshGeometry::Subset>& subsets,
		std::vector<M3dMaterial>& mats);

	bool LoadM3d(const std::string& filename,
		std::vector<Vertex::PosNormalTexTanSkinned>& vertices,
		std::vector<USHORT>& indices,
		std::vector<MeshGeometry::Subset>& subsets,
		std::vector<M3dMaterial>& mats,
		SkinnedData& skinInfo);

private:
	void ReadMaterials(std::ifstream& fin, uint32 numMaterials, std::vector<M3dMaterial>& mats);
	void ReadSubsetTable(std::ifstream& fin, uint32 numSubsets, std::vector<MeshGeometry::Subset>& subsets);
	void ReadVertices(std::ifstream& fin, uint32 numVertices, std::vector<Vertex::PosNormalTexTan2>& vertices);
	void ReadVertices(std::ifstream& fin, uint32 numVertices, std::vector<Vertex::PosNormalTexTanSkinned>& vertices);
	void ReadSkinnedVertices(std::ifstream& fin, uint32 numVertices, std::vector<Vertex::PosNormalTexTanSkinned>& vertices);
	void ReadTriangles(std::ifstream& fin, uint32 numTriangles, std::vector<USHORT>& indices);

	void ReadBoneOffsets(std::ifstream& fin, uint32 numBones, std::vector<XMFLOAT4X4>& boneOffsets);
	void ReadBoneHierarchy(std::ifstream& fin, uint32 numBones, std::vector<int32>& boneIndexToParentIndex);
	void ReadAnimationClips(std::ifstream& fin, uint32 numBones, uint32 numAnimationClips, std::map<std::string, AnimationClip>& animations);
	void ReadBoneKeyframes(std::ifstream& fin, uint32 numBones, BoneAnimation& boneAnimation);
};
