#pragma once

class UMaterial;
class Mesh;
class SkinnedMesh;
struct AnimationClip; 
class SkeletonAvataData;

class ResourceManager
{
	SINGLE_HEADER(ResourceManager)

private:
	ComPtr<ID3D11Device> m_Device;
	map<wstring, ComPtr<ID3D11ShaderResourceView>>	m_TextureSRV;
	map<string, shared_ptr<UMaterial>>				m_Materials;
	map<wstring, shared_ptr<SkinnedMesh>>			m_SkinnedMeshs;

	map<string, shared_ptr<MeshFile>>						m_FbxFiles;
	map<string, shared_ptr<MeshFile>>						m_MeshFiles;
	map<tuple<wstring, int>, shared_ptr<Mesh>>				m_Meshs; 
	map<tuple<string, int>, shared_ptr<AnimationClip>>		m_AnimationClips;
	map<tuple<string, int>, shared_ptr<SkeletonAvataData>>	m_SkeletonAvatas;

public:
	void Init(ComPtr<ID3D11Device> device);
	void Destroy();

	ComPtr<ID3D11ShaderResourceView> LoadTexture(wstring filename);
	shared_ptr<UMaterial>	LoadMaterial(string filename);
	shared_ptr<Mesh>		LoadMesh(wstring filename, int index);
	shared_ptr<SkinnedMesh> LoadSkinnedMesh(wstring filename, int index);

	shared_ptr<MeshFile>			LoadFbxModel(string filename);
	shared_ptr<MeshFile>			LoadMeshFile(string filename);
	shared_ptr<AnimationClip>		LoadAnimationClip(string filename, int index);  
	shared_ptr<SkeletonAvataData>	LoadSkeletonAvata(string filepath, int index);
};

