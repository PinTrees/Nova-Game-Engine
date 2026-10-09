#pragma once
#include "MemoryStats.h"

class UMaterial;
class Mesh;
class SkinnedMesh;
struct AnimationClip; 
class SkeletonAvataData;

class NOVA_API ResourceManager
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
	// 불러온 모델의 원본 (전체 경로, 수정 시각) — 원본이 바뀌면 다시 가져온다 (Unity 처럼)
	map<string, std::pair<std::string, std::filesystem::file_time_type>>	m_ModelSources;

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

	// Import Settings 를 바꾼 뒤: 이 파일(상대 경로)로 불러 둔 텍스처 · 메시 · 클립 · 스켈레톤을 캐시에서 뺀다.
	// 이미 받은 shared_ptr 은 그대로 살아 있다 (씬을 다시 만들면 새로 불러온다)
	void ForgetAsset(const std::string& relativePath);
	// 원본 (FBX · VRM · GLB) 이 불러온 뒤 바뀐 모델의 상대 경로 (부를 때마다 확인하고 시각을 새로 적는다)
	std::vector<std::string> TakeChangedModels();
	// 불러 둔 재질의 텍스처를 다시 연결 (텍스처를 다시 가져온 뒤)
	void ReloadMaterialTextures();

	// Profiler 메모리: 불러 둔 텍스처 / 메시(스킨 포함)의 GPU 크기
	void CollectMemory(std::vector<MemoryStats::Item>& textures, std::vector<MemoryStats::Item>& meshes) const;

	// 불러오기 통계 — 캐시에 없어 실제로 불러온 것 (씬 스트리밍 측정: nova scenestream stats)
	//  종류별 ms 는 안에서 부른 것을 포함한다 (재질 = 그 텍스처도). TotalMs 는 맨 바깥 부름만 더한다 (겹쳐 세지 않는다)
	struct LoadStats { uint32_t Textures = 0, Materials = 0, MeshFiles = 0; double TextureMs = 0, MaterialMs = 0, MeshFileMs = 0, TotalMs = 0; };
	const LoadStats& Stats() const { return m_Stats; }
	void ResetStats() { m_Stats = LoadStats(); }
private:
	LoadStats m_Stats;
};

