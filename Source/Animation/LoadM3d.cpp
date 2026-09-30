#include "pch.h"
#include "LoadM3d.h"

bool M3DLoader::LoadM3d(const std::string& filename,
	std::vector<Vertex::PosNormalTexTan2>& vertices,
	std::vector<USHORT>& indices,
	std::vector<MeshGeometry::Subset>& subsets,
	std::vector<M3dMaterial>& mats)
{
	std::ifstream fin(filename);

	uint32 numMaterials = 0;
	uint32 numVertices = 0;
	uint32 numTriangles = 0;
	uint32 numBones = 0;
	uint32 numAnimationClips = 0;

	std::string ignore;

	if (fin)
	{
		fin >> ignore; // file header text
		fin >> ignore >> numMaterials;
		fin >> ignore >> numVertices;
		fin >> ignore >> numTriangles;
		fin >> ignore >> numBones;
		fin >> ignore >> numAnimationClips;

		ReadMaterials(fin, numMaterials, mats);
		ReadSubsetTable(fin, numMaterials, subsets);
		ReadVertices(fin, numVertices, vertices);
		ReadTriangles(fin, numTriangles, indices);

		return true;
	}
	return false;
}

bool M3DLoader::LoadM3d(const std::string& filename,
	std::vector<Vertex::PosNormalTexTanSkinned>& vertices,
	std::vector<USHORT>& indices,
	std::vector<MeshGeometry::Subset>& subsets,
	std::vector<M3dMaterial>& mats,
	SkinnedData& skinInfo)
{
	std::ifstream fin(filename);

	uint32 numMaterials = 0;
	uint32 numVertices = 0;
	uint32 numTriangles = 0;
	uint32 numBones = 0;
	uint32 numAnimationClips = 0;

	std::string ignore;

	if (fin)
	{
		fin >> ignore; // file header text
		fin >> ignore >> numMaterials;
		fin >> ignore >> numVertices;
		fin >> ignore >> numTriangles;
		fin >> ignore >> numBones;
		fin >> ignore >> numAnimationClips;

		std::vector<XMFLOAT4X4> boneOffsets;
		std::vector<int32> boneIndexToParentIndex;
		std::map<std::string, AnimationClip> animations;

		ReadMaterials(fin, numMaterials, mats);
		ReadSubsetTable(fin, numMaterials, subsets);
		ReadSkinnedVertices(fin, numVertices, vertices);
		ReadTriangles(fin, numTriangles, indices);
		ReadBoneOffsets(fin, numBones, boneOffsets);
		ReadBoneHierarchy(fin, numBones, boneIndexToParentIndex);
		ReadAnimationClips(fin, numBones, numAnimationClips, animations);

		skinInfo.Set(boneIndexToParentIndex, boneOffsets, animations);

		return true;
	}
	return false;
}

void M3DLoader::ReadMaterials(std::ifstream& fin, uint32 numMaterials, std::vector<M3dMaterial>& mats)
{
	std::string ignore;
	mats.resize(numMaterials);

	std::string diffuseMapName;
	std::string normalMapName;

	fin >> ignore; // materials header text
	for (uint32 i = 0; i < numMaterials; ++i)
	{
		fin >> ignore >> mats[i].Mat.Ambient.x >> mats[i].Mat.Ambient.y >> mats[i].Mat.Ambient.z;
		fin >> ignore >> mats[i].Mat.Diffuse.x >> mats[i].Mat.Diffuse.y >> mats[i].Mat.Diffuse.z;
		fin >> ignore >> mats[i].Mat.Specular.x >> mats[i].Mat.Specular.y >> mats[i].Mat.Specular.z;
		fin >> ignore >> mats[i].Mat.Specular.w;
		fin >> ignore >> mats[i].Mat.Reflect.x >> mats[i].Mat.Reflect.y >> mats[i].Mat.Reflect.z;
		fin >> ignore >> mats[i].AlphaClip;
		fin >> ignore >> mats[i].EffectTypeName;
		fin >> ignore >> diffuseMapName;
		fin >> ignore >> normalMapName;

		mats[i].DiffuseMapName.resize(diffuseMapName.size(), ' ');
		mats[i].NormalMapName.resize(normalMapName.size(), ' ');
		std::copy(diffuseMapName.begin(), diffuseMapName.end(), mats[i].DiffuseMapName.begin());
		std::copy(normalMapName.begin(), normalMapName.end(), mats[i].NormalMapName.begin());
	}
}

void M3DLoader::ReadSubsetTable(std::ifstream& fin, uint32 numSubsets, std::vector<MeshGeometry::Subset>& subsets)
{
	std::string ignore;
	subsets.resize(numSubsets);

	fin >> ignore; // subset header text
	for (uint32 i = 0; i < numSubsets; ++i)
	{
		fin >> ignore >> subsets[i].Id;
		fin >> ignore >> subsets[i].VertexStart;
		fin >> ignore >> subsets[i].VertexCount;
		fin >> ignore >> subsets[i].FaceStart;
		fin >> ignore >> subsets[i].FaceCount;
	}
}

void M3DLoader::ReadVertices(std::ifstream& fin, uint32 numVertices, std::vector<Vertex::PosNormalTexTan2>& vertices)
{
	std::string ignore;
	vertices.resize(numVertices);

	fin >> ignore; // vertices header text
	for (uint32 i = 0; i < numVertices; ++i)
	{
		fin >> ignore >> vertices[i].pos.x >> vertices[i].pos.y >> vertices[i].pos.z;
		fin >> ignore >> vertices[i].tangentU.x >> vertices[i].tangentU.y >> vertices[i].tangentU.z >> vertices[i].tangentU.w;
		fin >> ignore >> vertices[i].normal.x >> vertices[i].normal.y >> vertices[i].normal.z;
		fin >> ignore >> vertices[i].tex.x >> vertices[i].tex.y;
	}
}

void M3DLoader::ReadVertices(std::ifstream& fin, uint32 numVertices, std::vector<Vertex::PosNormalTexTanSkinned>& vertices)
{
	std::string ignore;
	vertices.resize(numVertices);

	fin >> ignore; // vertices header text
	for (uint32 i = 0; i < numVertices; ++i)
	{
		fin >> ignore >> vertices[i].pos.x >> vertices[i].pos.y >> vertices[i].pos.z;
		fin >> ignore >> vertices[i].tangentU.x >> vertices[i].tangentU.y >> vertices[i].tangentU.z >> vertices[i].tangentU.w;
		fin >> ignore >> vertices[i].normal.x >> vertices[i].normal.y >> vertices[i].normal.z;
		fin >> ignore >> vertices[i].tex.x >> vertices[i].tex.y;
	}
}

void M3DLoader::ReadSkinnedVertices(std::ifstream& fin, uint32 numVertices, std::vector<Vertex::PosNormalTexTanSkinned>& vertices)
{
	std::string ignore;
	vertices.resize(numVertices);

	fin >> ignore; // vertices header text
	int boneIndices[4];
	float weights[4];
	for (uint32 i = 0; i < numVertices; ++i)
	{
		fin >> ignore >> vertices[i].pos.x >> vertices[i].pos.y >> vertices[i].pos.z;
		fin >> ignore >> vertices[i].tangentU.x >> vertices[i].tangentU.y >> vertices[i].tangentU.z >> vertices[i].tangentU.w;
		fin >> ignore >> vertices[i].normal.x >> vertices[i].normal.y >> vertices[i].normal.z;
		fin >> ignore >> vertices[i].tex.x >> vertices[i].tex.y;
		fin >> ignore >> weights[0] >> weights[1] >> weights[2] >> weights[3];
		fin >> ignore >> boneIndices[0] >> boneIndices[1] >> boneIndices[2] >> boneIndices[3];

		vertices[i].weights.x = weights[0];
		vertices[i].weights.y = weights[1];
		vertices[i].weights.z = weights[2];

		vertices[i].boneIndices[0] = (BYTE)boneIndices[0];
		vertices[i].boneIndices[1] = (BYTE)boneIndices[1];
		vertices[i].boneIndices[2] = (BYTE)boneIndices[2];
		vertices[i].boneIndices[3] = (BYTE)boneIndices[3];
	}
}

void M3DLoader::ReadTriangles(std::ifstream& fin, uint32 numTriangles, std::vector<USHORT>& indices)
{
	std::string ignore;
	indices.resize(numTriangles * 3);

	fin >> ignore; // triangles header text
	for (uint32 i = 0; i < numTriangles; ++i)
	{
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}
}

void M3DLoader::ReadBoneOffsets(std::ifstream& fin, uint32 numBones, std::vector<XMFLOAT4X4>& boneOffsets)
{
	std::string ignore;
	boneOffsets.resize(numBones);

	fin >> ignore; // BoneOffsets header text
	for (uint32 i = 0; i < numBones; ++i)
	{
		fin >> ignore >>
			boneOffsets[i](0, 0) >> boneOffsets[i](0, 1) >> boneOffsets[i](0, 2) >> boneOffsets[i](0, 3) >>
			boneOffsets[i](1, 0) >> boneOffsets[i](1, 1) >> boneOffsets[i](1, 2) >> boneOffsets[i](1, 3) >>
			boneOffsets[i](2, 0) >> boneOffsets[i](2, 1) >> boneOffsets[i](2, 2) >> boneOffsets[i](2, 3) >>
			boneOffsets[i](3, 0) >> boneOffsets[i](3, 1) >> boneOffsets[i](3, 2) >> boneOffsets[i](3, 3);
	}
}

void M3DLoader::ReadBoneHierarchy(std::ifstream& fin, uint32 numBones, std::vector<int32>& boneIndexToParentIndex)
{
	std::string ignore;
	boneIndexToParentIndex.resize(numBones);

	fin >> ignore; // BoneHierarchy header text
	for (uint32 i = 0; i < numBones; ++i)
	{
		fin >> ignore >> boneIndexToParentIndex[i];
	}
}

void M3DLoader::ReadAnimationClips(std::ifstream& fin, uint32 numBones, uint32 numAnimationClips, std::map<std::string, AnimationClip>& animations)
{
	std::string ignore;
	fin >> ignore; // AnimationClips header text
	for (uint32 clipIndex = 0; clipIndex < numAnimationClips; ++clipIndex)
	{
		std::string clipName;
		fin >> ignore >> clipName;
		fin >> ignore; // {

		AnimationClip clip;
		clip.BoneAnimations.resize(numBones);

		for (uint32 boneIndex = 0; boneIndex < numBones; ++boneIndex)
		{
			ReadBoneKeyframes(fin, numBones, clip.BoneAnimations[boneIndex]);
		}
		fin >> ignore; // }

		animations[clipName] = clip;
	}
}

void M3DLoader::ReadBoneKeyframes(std::ifstream& fin, uint32 numBones, BoneAnimation& boneAnimation)
{
	std::string ignore;
	uint32 numKeyframes = 0;
	fin >> ignore >> ignore >> numKeyframes;
	fin >> ignore; // {

	boneAnimation.Keyframes.resize(numKeyframes);
	for (uint32 i = 0; i < numKeyframes; ++i)
	{
		float t = 0.0f;
		XMFLOAT3 p(0.0f, 0.0f, 0.0f);
		XMFLOAT3 s(1.0f, 1.0f, 1.0f);
		XMFLOAT4 q(0.0f, 0.0f, 0.0f, 1.0f);
		fin >> ignore >> t;
		fin >> ignore >> p.x >> p.y >> p.z;
		fin >> ignore >> s.x >> s.y >> s.z;
		fin >> ignore >> q.x >> q.y >> q.z >> q.w;

		boneAnimation.Keyframes[i].TimePos = t;
		boneAnimation.Keyframes[i].Translation = p;
		boneAnimation.Keyframes[i].Scale = s;
		boneAnimation.Keyframes[i].RotationQuat = q;
	}

	fin >> ignore; // }
}
