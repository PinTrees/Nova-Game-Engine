#include "pch.h"
#include "ResourceManager.h"
#include "GameObjectFactory.h"
#include "Utils.h"
#include "HumanoidAvatar.h"

SINGLE_BODY(ResourceManager)

ResourceManager::ResourceManager()
{

}

ResourceManager::~ResourceManager()
{
	m_TextureSRV.clear();
}

void ResourceManager::Init(ComPtr<GfxDevice> device)
{
	m_Device = device;
}

void ResourceManager::CollectMemory(std::vector<MemoryStats::Item>& textures, std::vector<MemoryStats::Item>& meshes) const
{
	for (const auto& [path, srv] : m_TextureSRV)
		textures.push_back({ wstring_to_string(std::filesystem::path(path).filename().wstring()), MemoryStats::ViewBytes(srv.Get()) });
	for (const auto& [key, mesh] : m_Meshs)
		if (mesh)
			meshes.push_back({ wstring_to_string(std::filesystem::path(std::get<0>(key)).filename().wstring()) + " #" + std::to_string(std::get<1>(key)), mesh->ModelMesh.GpuBytes() });
	for (const auto& [key, mesh] : m_SkinnedMeshs)
		if (mesh)
			meshes.push_back({ wstring_to_string(std::filesystem::path(std::get<0>(key)).filename().wstring()) + " #" + std::to_string(std::get<1>(key)) + " (skinned)", mesh->ModelMesh.GpuBytes() });
}

void ResourceManager::Destroy()
{
	m_TextureSRV.clear();
	m_Materials.clear();
	m_SkinnedMeshs.clear();

	m_FbxFiles.clear();
	m_MeshFiles.clear();  
	m_Meshs.clear(); 
	m_AnimationClips.clear(); 
	m_SkeletonAvatas.clear();
}

namespace
{
	// 캐시 키 비교용: 구분자 \, 소문자, 앞의 \ 없음
	std::string AssetKey(std::string p)
	{
		std::replace(p.begin(), p.end(), '/', '\\');
		std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		const size_t first = p.find_first_not_of('\\');
		return first == std::string::npos ? std::string() : p.substr(first);
	}
}

void ResourceManager::ForgetAsset(const std::string& relativePath)
{
	const std::string key = AssetKey(relativePath);
	auto same = [&](const std::string& p) { return AssetKey(p) == key; };
	auto sameW = [&](const std::wstring& p) { return AssetKey(wstring_to_string(p)) == key; };
	for (auto it = m_TextureSRV.begin(); it != m_TextureSRV.end();)
		it = sameW(it->first) ? m_TextureSRV.erase(it) : std::next(it);
	// 스켈레톤의 Humanoid 아바타도 (다시 가져오면 새 스켈레톤 — 같은 주소가 다시 쓰여도 옛 매핑을 쓰지 않게)
	for (auto* files : { &m_MeshFiles, &m_FbxFiles })
		for (auto it = files->begin(); it != files->end();)
		{
			if (!same(it->first)) { ++it; continue; }
			if (it->second)
				for (const auto& s : it->second->Avatas)
					if (s) Humanoid::Forget(*s);
			it = files->erase(it);
		}
	for (auto it = m_SkeletonAvatas.begin(); it != m_SkeletonAvatas.end();)
	{
		if (!same(std::get<0>(it->first))) { ++it; continue; }
		if (it->second) Humanoid::Forget(*it->second);
		it = m_SkeletonAvatas.erase(it);
	}
	for (auto it = m_Meshs.begin(); it != m_Meshs.end();)
		it = sameW(std::get<0>(it->first)) ? m_Meshs.erase(it) : std::next(it);
	for (auto it = m_SkinnedMeshs.begin(); it != m_SkinnedMeshs.end();)
		it = sameW(std::get<0>(it->first)) ? m_SkinnedMeshs.erase(it) : std::next(it);
	for (auto it = m_AnimationClips.begin(); it != m_AnimationClips.end();)
		it = same(std::get<0>(it->first)) ? m_AnimationClips.erase(it) : std::next(it);
}

void ResourceManager::ReloadMaterialTextures()
{
	for (auto& [path, material] : m_Materials)
		if (material)
			material->ReloadTextures();
}

ComPtr<GfxShaderResourceView> ResourceManager::LoadTexture(wstring filename)
{
	ComPtr<GfxShaderResourceView> srv;

	wstring path = PathManager::GetI()->GetMovePathW(filename);

	if (m_TextureSRV.find(filename) != m_TextureSRV.end())
	{
		srv = m_TextureSRV[filename];
	}
	else
	{
		srv = Utils::LoadTexture(m_Device, path.c_str());
		m_TextureSRV[filename] = srv;
		if (srv == nullptr)
		{
			std::error_code ec;
			EditorLog::Write("Texture", "load failed: %s -> %s (file exists: %d)", wstring_to_string(filename).c_str(), wstring_to_string(path).c_str(), (int)std::filesystem::exists(path, ec));
		}
	}

	return srv;
}

shared_ptr<UMaterial> ResourceManager::LoadMaterial(string filename)
{
	if (UMaterial::IsBuiltinPath(filename))
		return UMaterial::GetDefault();

	shared_ptr<UMaterial> material = nullptr;

	if (m_Materials.find(filename) != m_Materials.end())
	{
		material = m_Materials[filename];
	}
	else
	{
		UMaterial* umat = UMaterial::Load(filename);
		if (umat != nullptr)
		{
			shared_ptr<UMaterial> material(umat);
			m_Materials[filename] = material;
			return material;
		}
		else
		{
			return nullptr;
		}
	}

	return material;
}

shared_ptr<Mesh> ResourceManager::LoadMesh(wstring filename, int index)
{
	// 엔진 내장 메시(builtin:Cube 등)는 파일 없이 코드로 만든다
	if (GameObjectFactory::IsBuiltinMeshPath(filename))
		return GameObjectFactory::LoadBuiltinMesh(filename);

	tuple<wstring, int> key = make_tuple(filename, index); 
	shared_ptr<Mesh> mesh = nullptr;

	if (m_Meshs.find(key) != m_Meshs.end()) 
	{
		mesh = m_Meshs[key]; 
	}
	else
	{
		const auto& meshFile = LoadMeshFile(wstring_to_string(filename));
		if (meshFile == nullptr)
			return nullptr;

		if (meshFile->Meshs.size() > index) 
		{
			mesh = shared_ptr<Mesh>(meshFile->Meshs[index]); 
			m_Meshs[key] = mesh; 
		}
	}

	return mesh;
}

shared_ptr<SkinnedMesh> ResourceManager::LoadSkinnedMesh(wstring filename, int index)
{
	// 같은 FBX 안의 여러 스킨 메시를 구분하도록 (경로, 번호) 로 캐시
	const auto key = make_tuple(filename, index);
	auto it = m_SkinnedMeshs.find(key);
	if (it != m_SkinnedMeshs.end())
		return it->second;
	auto meshFile = LoadMeshFile(wstring_to_string(filename));
	if (meshFile == nullptr || index < 0 || index >= (int)meshFile->SkinnedMeshs.size())
		return nullptr;
	return m_SkinnedMeshs[key] = meshFile->SkinnedMeshs[index];
}

shared_ptr<MeshFile> ResourceManager::LoadFbxModel(string filename)
{
	shared_ptr<MeshFile> model = nullptr;

	if (m_FbxFiles.find(filename) != m_FbxFiles.end())
	{
		model = m_FbxFiles[filename];
	}
	else
	{
		MeshFile* meshFile = MeshFile::LoadFromFbxFile(filename);
		model.reset(meshFile);
		m_FbxFiles[filename] = model;
	}

	return model;
}

shared_ptr<MeshFile> ResourceManager::LoadMeshFile(string filename)
{
	if (m_MeshFiles.find(filename) != m_MeshFiles.end())
	{
		return m_MeshFiles[filename];
	}
	else
	{
		LoadingScreen::SetStatus(L"Importing " + std::filesystem::path(string_to_wstring(filename)).filename().wstring());
		MeshFile* meshFile = MeshFile::LoadFromMetaFile(filename); 
		// ��Ÿ���� �б� ����
		if (meshFile == nullptr)
			return nullptr;

		shared_ptr<MeshFile> model(meshFile);
		// 클립마다 원래 스켈레톤 (Humanoid 리타게팅)
		if (!model->Avatas.empty())
			for (auto& clip : model->SkinnedData.AnimationClips)
				if (clip)
					clip->SourceSkeleton = model->Avatas[0];
		m_MeshFiles[filename] = model;
		return m_MeshFiles[filename];
	}
}

shared_ptr<AnimationClip> ResourceManager::LoadAnimationClip(string filename, int index)
{
	tuple<string, int> key = make_tuple(filename, index);

	if (m_AnimationClips.find(key) != m_AnimationClips.end())
	{
		return m_AnimationClips[key];
	}
	else
	{
		// Load MeshFile
		shared_ptr<MeshFile> meshFile = ResourceManager::GetI()->LoadMeshFile(filename);
		if (meshFile == nullptr)
			return nullptr;

		if (index < 0 || index >= (int)meshFile->SkinnedData.AnimationClips.size())
			return nullptr;
		return m_AnimationClips[key] = meshFile->SkinnedData.AnimationClips[index];
	}
}

shared_ptr<SkeletonAvataData> ResourceManager::LoadSkeletonAvata(string filepath, int index)
{
	tuple<string, int> key = make_tuple(filepath, index);

	if (m_SkeletonAvatas.find(key) != m_SkeletonAvatas.end())
	{
		return m_SkeletonAvatas[key];
	}
	else
	{
		// Load MeshFile
		shared_ptr<MeshFile> meshFile = ResourceManager::GetI()->LoadMeshFile(filepath);
		if (meshFile == nullptr)
			return nullptr;

		if (index < 0 || index >= (int)meshFile->Avatas.size())
			return nullptr;
		return m_SkeletonAvatas[key] = meshFile->Avatas[index]; 
	}
}
