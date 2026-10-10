#include "pch.h"
#include "PCGMeshAsset.h"
#include "Mesh.h"
#include "SkinnedMesh.h"
#include "SkinnedData.h"
#include "ResourceManager.h"
#include "UMaterial.h"
#include "FBXLoader.h"
#include "PathManager.h"
#include "EditorLog.h"
#include "Utils.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>

namespace PCG
{
	namespace
	{
		std::map<std::string, std::unique_ptr<MeshAsset>> s_Assets;

		std::shared_ptr<UMaterial> LoadMat(const std::wstring& path)
		{
			if (path.empty())
				return UMaterial::GetDefault();
			auto m = ResourceManager::GetI()->LoadMaterial(wstring_to_string(path));
			return m ? m : UMaterial::GetDefault();
		}

		// FBX 재질 칸 (경로) — FBXLoader::ExtractMaterials 는 FBX 를 Assimp 로 다시 읽는다 (큰 나무 수백 ms). 처음 한 번만, 그 뒤는 옆 파일에서
		std::vector<std::wstring> MaterialSlots(const std::wstring& relW)
		{
			const std::wstring full = PathManager::GetI()->GetMovePathW(relW);
			const std::wstring cache = full + L".materials.json";
			std::error_code ec;
			const auto srcTime = std::filesystem::last_write_time(full, ec);
			if (!ec)
			{
				std::error_code ec2;
				const auto cacheTime = std::filesystem::last_write_time(cache, ec2);
				if (!ec2 && cacheTime >= srcTime)
				{
					std::ifstream in(cache);
					json j = json::parse(in, nullptr, false);
					if (j.is_array())
					{
						std::vector<std::wstring> out;
						for (const json& v : j)
							out.push_back(string_to_wstring(v.get<std::string>()));
						return out;
					}
				}
			}
			std::vector<std::wstring> out = FBXLoader::ExtractMaterials(relW);
			json j = json::array();
			for (const std::wstring& m : out)
				j.push_back(wstring_to_string(m));
			std::ofstream o(cache);
			o << j.dump();
			return out;
		}

		bool Load(MeshAsset& a)
		{
			const auto t0 = std::chrono::steady_clock::now();
			auto file = ResourceManager::GetI()->LoadMeshFile(a.Path);
			const double fileMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			if (!file || file->Meshs.empty())
				return false;
			const std::wstring relW = string_to_wstring(a.Path);
			std::vector<std::wstring> materials;
			if (_wcsicmp(std::filesystem::path(relW).extension().wstring().c_str(), L".fbx") == 0)
				materials = MaterialSlots(relW);

			// 노드 전역 변환 (Model Placement 와 같은 식: 노드 위치에 Scale Factor, 최상위에 파일 단위 · 노드 0 변환)
			const SkeletonAvataData* tree = file->Avatas.empty() ? nullptr : file->Avatas[0].get();
			const int nodes = tree ? (int)tree->NodeNames.size() : 0;
			const float sf = file->ScaleFactor > 0.0f ? file->ScaleFactor : 1.0f;
			std::vector<XMFLOAT4X4> global((size_t)(std::max)(nodes, 1));
			XMStoreFloat4x4(&global[0], XMMatrixIdentity());
			std::vector<int> meshNode(file->Meshs.size(), -1);
			if (nodes > 0)
			{
				const float fileScale = tree->UnitScale / sf;
				auto localOf = [&](int n) {
					XMFLOAT4X4 m = tree->BindLocal[(size_t)n];
					m._41 *= sf; m._42 *= sf; m._43 *= sf;
					return XMLoadFloat4x4(&m);
				};
				const XMMATRIX top = localOf(0) * XMMatrixScaling(fileScale, fileScale, fileScale);
				XMStoreFloat4x4(&global[0], top);
				for (int n = 1; n < nodes; ++n)
				{
					const int p = tree->BoneHierarchy[(size_t)n];
					const XMMATRIX parent = (p <= 0 || p >= n) ? top : XMLoadFloat4x4(&global[(size_t)p]);
					XMStoreFloat4x4(&global[(size_t)n], localOf(n) * parent);
				}
				std::map<std::string, std::vector<int>> byName;
				for (int n = 0; n < nodes; ++n)
					byName[tree->NodeNames[(size_t)n]].push_back(n);
				for (size_t i = 0; i < file->Meshs.size(); ++i)
				{
					auto it = byName.find(file->Meshs[i]->Name);
					if (it != byName.end() && !it->second.empty())
					{
						meshNode[i] = it->second.front();
						if (it->second.size() > 1)
							it->second.erase(it->second.begin());
					}
					else
						meshNode[i] = 0;
				}
			}

			// LOD: 노드 (없으면 메시) 이름의 _LODn. 충돌 메시 (UCX_ · UBX_ · USP_) 는 뺀다
			static const std::regex kLod(R"(^(.*)_LOD(\d+)$)", std::regex::icase);
			struct Item { int Mesh; int Level; };
			std::vector<Item> items;
			int maxLevel = 0;
			for (size_t i = 0; i < file->Meshs.size(); ++i)
			{
				const std::string nodeName = nodes > 0 && meshNode[i] > 0 ? tree->NodeNames[(size_t)meshNode[i]] : file->Meshs[i]->Name;
				if (nodeName.rfind("UCX_", 0) == 0 || nodeName.rfind("UBX_", 0) == 0 || nodeName.rfind("USP_", 0) == 0)
					continue;
				std::smatch m;
				int level = -1;
				if (std::regex_match(nodeName, m, kLod) || std::regex_match(file->Meshs[i]->Name, m, kLod))
					level = std::stoi(m[2].str());
				items.push_back({ (int)i, level });
				maxLevel = (std::max)(maxLevel, level);
			}
			if (items.empty())
				return false;
			a.Levels.resize((size_t)maxLevel + 1);
			float minY = FLT_MAX, maxY = -FLT_MAX, radius = 0.0f;
			for (const Item& it : items)
			{
				const std::shared_ptr<Mesh>& mesh = file->Meshs[(size_t)it.Mesh];
				MeshPart part;
				part.MeshPtr = mesh;
				part.Model = global[(size_t)(meshNode[(size_t)it.Mesh] < 0 ? 0 : meshNode[(size_t)it.Mesh])];
				for (const auto& subset : mesh->Subsets)
				{
					const std::shared_ptr<UMaterial> mat = LoadMat(subset.MaterialIndex < materials.size() ? materials[subset.MaterialIndex] : std::wstring());
					part.Materials.push_back(mat);
					part.TwoSided.push_back(mat && mat->GetPbr().AlphaClip != 0);
				}
				// 높이 · 반지름: LOD 0 (또는 LOD 없는 메시) 의 정점
				if (it.Level <= 0)
				{
					const XMMATRIX model = XMLoadFloat4x4(&part.Model);
					const size_t step = (std::max)((size_t)1, mesh->Vertices.size() / 2000);
					for (size_t v = 0; v < mesh->Vertices.size(); v += step)
					{
						const XMVECTOR p = XMVector3TransformCoord(XMLoadFloat3(&mesh->Vertices[v].pos), model);
						minY = (std::min)(minY, XMVectorGetY(p));
						maxY = (std::max)(maxY, XMVectorGetY(p));
						radius = (std::max)(radius, sqrtf(XMVectorGetX(p) * XMVectorGetX(p) + XMVectorGetZ(p) * XMVectorGetZ(p)));
					}
				}
				int tris = 0;
				for (const auto& subset : mesh->Subsets)
					tris += (int)subset.FaceCount;
				if (it.Level < 0)
				{
					// LOD 이름이 없는 메시: 모든 LOD 에 (LOD 가 없는 모델이면 LOD 하나)
					for (MeshLevel& l : a.Levels)
					{
						l.Parts.push_back(part);
						l.Triangles += tris;
					}
				}
				else
				{
					a.Levels[(size_t)it.Level].Parts.push_back(std::move(part));
					a.Levels[(size_t)it.Level].Triangles += tris;
				}
			}
			a.Levels.erase(std::remove_if(a.Levels.begin(), a.Levels.end(), [](const MeshLevel& l) { return l.Parts.empty(); }), a.Levels.end());
			if (a.Levels.empty())
				return false;
			a.Height = maxY > minY ? maxY - (std::min)(minY, 0.0f) : 1.0f;
			a.Radius = (std::max)(0.05f, radius);

			// LOD 전환 높이: <모델>.lod.json (Unity 프리팹의 LOD Group 값), 없으면 0.5 · 0.25 · …
			std::vector<float> heights;
			{
				std::ifstream in(PathManager::GetI()->GetMovePathW(relW + L".lod.json"));
				if (in)
				{
					json j = json::parse(in, nullptr, false);
					if (j.is_array())
						for (const json& v : j)
							if (v.is_number())
								heights.push_back(v.get<float>());
				}
			}
			for (size_t l = 0; l < a.Levels.size(); ++l)
				a.Levels[l].ScreenHeight = l < heights.size() ? heights[l] : 0.5f / (float)(1u << l);
			EditorLog::Write("PCG", "mesh %s: %zu LODs, height %.1f m, LOD0 %d tris (%.0f ms, model file %.0f ms)", a.Path.c_str(), a.Levels.size(), a.Height,
				a.Levels[0].Triangles, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(), fileMs);
			return true;
		}
	}

	MeshAsset* GetMeshAsset(const std::string& path, bool allowLoad, bool* loaded)
	{
		if (loaded)
			*loaded = false;
		auto it = s_Assets.find(path);
		if (it == s_Assets.end())
		{
			// 처음: 재질의 텍스처 디코드 · 모델 캐시 파일 읽기를 작업 스레드에 (메인은 다음 프레임들에 올리기만)
			auto a = std::make_unique<MeshAsset>();
			a->Path = path;
			a->RequestFrame = (uint32_t)ImGui::GetFrameCount();
			const std::wstring relW = string_to_wstring(path);
			const std::wstring full = PathManager::GetI()->GetMovePathW(relW);
			Utils::PrefetchFile(full + L".mesh");
			if (_wcsicmp(std::filesystem::path(relW).extension().wstring().c_str(), L".fbx") == 0)
				for (const std::wstring& mat : MaterialSlots(relW))
				{
					std::ifstream in(PathManager::GetI()->GetMovePathW(mat));
					const json j = json::parse(in, nullptr, false);
					if (!j.is_object())
						continue;
					for (const char* key : { "BaseMapPath", "NormalMapPath", "MetallicMapPath", "OcclusionMapPath", "EmissionMapPath" })
					{
						const std::string tex = j.value(key, std::string());
						if (tex.empty() || ResourceManager::GetI()->HasTexture(string_to_wstring(tex)))
							continue;
						const std::wstring texFull = PathManager::GetI()->GetMovePathW(string_to_wstring(tex));
						if (std::find(a->Pending.begin(), a->Pending.end(), texFull) != a->Pending.end())
							continue;
						Utils::PrefetchTexture(texFull);
						a->Pending.push_back(texFull);
					}
				}
			it = s_Assets.emplace(path, std::move(a)).first;
			return nullptr;
		}
		MeshAsset& a = *it->second;
		if (a.Failed)
			return nullptr;
		if (a.Stage == 1)
			return &a;
		if (!allowLoad)
			return nullptr;
		// 디코드가 다 끝났으면 (10 초 넘게 걸리면 그냥) 올린다
		bool ready = (uint32_t)ImGui::GetFrameCount() - a.RequestFrame > 600u;
		if (!ready)
		{
			ready = true;
			for (const std::wstring& p : a.Pending)
				if (!Utils::PrefetchReady(p))
				{
					ready = false;
					break;
				}
		}
		if (!ready)
			return nullptr;
		a.Pending.clear();
		if (loaded)
			*loaded = true;
		if (!Load(a))
		{
			a.Failed = true;
			EditorLog::Write("PCG", "mesh %s: could not load (no meshes)", path.c_str());
			return nullptr;
		}
		a.Stage = 1;
		return &a;
	}

	int LoadedMeshAssets()
	{
		int n = 0;
		for (const auto& kv : s_Assets)
			n += kv.second->Stage == 1;
		return n;
	}

	void ClearMeshAssets() { s_Assets.clear(); }
}
