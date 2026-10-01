#pragma once
#include "MemoryStats.h"

class UMaterial;
class Mesh;
class SkinnedMesh;
struct AnimationClip; 
class SkeletonAvataData;

class ResourceManager
{
	SINGLE_HEADER(ResourceManager)

private:
	ComPtr<GfxDevice> m_Device;
	map<wstring, ComPtr<GfxShaderResourceView>>	m_TextureSRV;
	map<string, shared_ptr<UMaterial>>				m_Materials;
	map<tuple<wstring, int>, shared_ptr<SkinnedMesh>>	m_SkinnedMeshs;   // (경로, 번호)

	map<string, shared_ptr<MeshFile>>						m_FbxFiles;
	map<string, shared_ptr<MeshFile>>						m_MeshFiles;
	map<tuple<wstring, int>, shared_ptr<Mesh>>				m_Meshs; 
	map<tuple<string, int>, shared_ptr<AnimationClip>>		m_AnimationClips;
	map<tuple<string, int>, shared_ptr<SkeletonAvataData>>	m_SkeletonAvatas;

public:
	void Init(ComPtr<GfxDevice> device);
	void Destroy();

	ComPtr<GfxShaderResourceView> LoadTexture(wstring filename);
	shared_ptr<UMaterial>	LoadMaterial(string filename);
	shared_ptr<Mesh>		LoadMesh(wstring filename, int index);
	shared_ptr<SkinnedMesh> LoadSkinnedMesh(wstring filename, int index);

	shared_ptr<MeshFile>			LoadFbxModel(string filename);
	shared_ptr<MeshFile>			LoadMeshFile(string filename);
	shared_ptr<AnimationClip>		LoadAnimationClip(string filename, int index);  
	shared_ptr<SkeletonAvataData>	LoadSkeletonAvata(string filepath, int index);

	// Profiler 메모리: 불러 둔 텍스처 / 메시(스킨 포함)의 GPU 크기
	void CollectMemory(std::vector<MemoryStats::Item>& textures, std::vector<MemoryStats::Item>& meshes) const;
};

