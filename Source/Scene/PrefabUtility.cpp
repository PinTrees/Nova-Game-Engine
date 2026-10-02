#include "pch.h"
#include "TagsAndLayers.h"
#include "PrefabUtility.h"

namespace
{
	// ---- 에셋 캐시 (파일이 바뀌면 다시 읽는다) ----
	struct AssetVersion
	{
		int Revision = 0;
		json Root;
		std::map<uint64, const json*> ById;   // fileID → 오브젝트 JSON (Root 안을 가리킨다)
	};
	// 에셋 = 최근 버전들 (인스턴스는 자기를 만든 버전과 비교해 오버라이드를 계산한다)
	struct AssetEntry
	{
		std::filesystem::file_time_type Stamp;
		std::deque<std::unique_ptr<AssetVersion>> Versions;   // 마지막이 현재
		int NextRevision = 1;
		const AssetVersion& Latest() const { return *Versions.back(); }
		json& Root() { return Versions.back()->Root; }
		const std::map<uint64, const json*>& ById() const { return Versions.back()->ById; }
	};
	std::map<std::string, AssetEntry>& Assets()
	{
		static std::map<std::string, AssetEntry> assets;
		return assets;
	}

	std::string NormalizePath(std::string path)
	{
		std::replace(path.begin(), path.end(), '/', '\\');
		// "Assets\\\\X" 처럼 겹친 구분자는 하나로 (같은 에셋이 두 이름으로 캐시되지 않게)
		for (size_t p = path.find("\\\\", 1); p != std::string::npos; p = path.find("\\\\", p))
			path.erase(p, 1);
		if (std::filesystem::path(path).is_absolute())
			return path;
		const size_t first = path.find_first_not_of('\\');
		path.erase(0, first == std::string::npos ? path.size() : first);
		return path;
	}

	std::wstring FilePath(const std::string& path)
	{
		std::filesystem::path p(string_to_wstring(path));
		return p.is_absolute() ? p.wstring() : PathManager::GetI()->GetMovePathW(string_to_wstring(path));
	}

	void Index(const json& obj, std::map<uint64, const json*>& out)
	{
		if (obj.contains("fileID") && obj["fileID"].is_number_unsigned())
			out[obj["fileID"].get<uint64>()] = &obj;
		if (obj.contains("children"))
			for (const auto& c : obj["children"])
				Index(c, out);
	}

	const AssetEntry* LoadAsset(const std::string& rawPath)
	{
		const std::string path = NormalizePath(rawPath);
		const std::wstring file = FilePath(path);
		std::error_code ec;
		const auto stamp = std::filesystem::last_write_time(file, ec);
		if (ec)
			return nullptr;
		auto it = Assets().find(path);
		if (it != Assets().end() && it->second.Stamp == stamp && !it->second.Versions.empty())
			return &it->second;
		std::ifstream in(file, std::ios::binary);
		json j = json::parse(in, nullptr, false);
		if (j.is_discarded() || !j.contains("root"))
			return nullptr;
		AssetEntry& e = Assets()[path];
		auto v = std::make_unique<AssetVersion>();
		v->Revision = e.NextRevision++;
		v->Root = j["root"];
		if (j.value("layerFormat", 1) < TagsAndLayers::kLayerFormat)
			TagsAndLayers::MigrateLegacyObjectJson(v->Root);   // 예전 레이어 번호 → Unity 번호
		Index(v->Root, v->ById);
		e.Versions.push_back(std::move(v));
		while (e.Versions.size() > 8)
			e.Versions.pop_front();
		e.Stamp = stamp;
		EditorLog::Write("Prefab", "loaded asset %s revision %d (%zu objects)", path.c_str(), e.Latest().Revision, e.ById().size());
		return &e;
	}

	// latestOnly = false 면 인스턴스를 만든 버전(link.Revision)에서 찾는다
	const json* FindSource(const PrefabLink& link, bool latestOnly = true)
	{
		const AssetEntry* asset = LoadAsset(link.Asset);
		if (asset == nullptr)
			return nullptr;
		const AssetVersion* version = &asset->Latest();
		if (!latestOnly && link.Revision > 0)
			for (const auto& v : asset->Versions)
				if (v->Revision == link.Revision)
					version = v.get();
		auto it = version->ById.find(link.Source);
		return it == version->ById.end() ? nullptr : it->second;
	}

	PrefabLink ReadLink(const json& obj)
	{
		PrefabLink link;
		if (obj.contains("prefab") && obj["prefab"].is_object())
		{
			const json& p = obj["prefab"];
			link.Asset = p.value("asset", std::string());
			link.Source = p.value("source", (uint64)0);
			link.Root = p.value("root", false);
		}
		return link;
	}

	// 컴포넌트 목록 → "타입#순번" 키
	std::vector<std::pair<std::string, const json*>> ComponentKeys(const json& obj)
	{
		std::vector<std::pair<std::string, const json*>> out;
		std::map<std::string, int> count;
		if (obj.contains("components"))
			for (const auto& c : obj["components"])
			{
				if (!c.is_object() || !c.contains("type"))
					continue;
				const std::string type = c["type"].get<std::string>();
				out.push_back({ type + "#" + std::to_string(count[type]++), &c });
			}
		return out;
	}

	// 항상 인스턴스 값을 쓰고 오버라이드로 보이지 않는 필드
	bool AlwaysInstance(const std::string& compKey, const std::string& field, bool root)
	{
		if (compKey.rfind("Transform#", 0) == 0)
		{
			// 월드 값은 부모에서 다시 계산되는 파생 값
			if (field == "m_Position" || field == "m_EulerAngles" || field == "m_Scale")
				return true;
			// 루트의 위치/회전은 인스턴스마다 다르다 (Unity 와 같음)
			if (root && (field == "m_LocalPosition" || field == "m_LocalRotation" || field == "m_LocalEulerAngles" || field == "m_LocalEulerRadians"))
				return true;
		}
		return false;
	}

	const char* kObjectFields[] = { "name", "active", "tag", "layer", "static" };

	// 에셋 오브젝트 JSON 을 새 인스턴스용으로 복사 (새 fileID, 연결 정보)
	json MakeInstanceJson(const json& assetObj, const std::string& assetPath, bool root)
	{
		json j = assetObj;
		j.erase("prefab");
		const uint64 source = assetObj.value("fileID", (uint64)0);
		j["fileID"] = GameObject::NewFileID();
		j["prefab"] = { { "asset", assetPath }, { "source", source }, { "root", root } };
		if (j.contains("children"))
		{
			json kids = json::array();
			for (const auto& c : assetObj["children"])
				kids.push_back(MakeInstanceJson(c, assetPath, false));
			j["children"] = kids;
		}
		return j;
	}

	// 인스턴스 오브젝트 하나를 현재 에셋 + 오버라이드로 다시 조립 (자식 포함)
	json MergeObject(const json& inst, const std::string& assetPath)
	{
		const PrefabLink link = ReadLink(inst);
		const json* src = link.IsValid() ? FindSource(link) : nullptr;
		if (src == nullptr)
			return inst;   // 에셋에서 사라졌거나 연결 없음: 저장된 값 그대로

		std::set<std::string> overrides;
		if (inst.contains("prefab") && inst["prefab"].contains("overrides"))
			for (const auto& o : inst["prefab"]["overrides"])
				overrides.insert(o.get<std::string>());

		json out = *src;
		out.erase("children");
		out["fileID"] = inst.value("fileID", GameObject::NewFileID());
		out["prefab"] = inst["prefab"];
		out["prefab"].erase("merged");

		for (const char* f : kObjectFields)
			if ((overrides.count(std::string("GameObject/") + f) || (link.Root && std::string(f) == "name")) && inst.contains(f))
				out[f] = inst[f];

		// 컴포넌트: 에셋 순서대로, 오버라이드 필드는 인스턴스 값
		const auto instComps = ComponentKeys(inst);
		std::map<std::string, const json*> instByKey(instComps.begin(), instComps.end());
		json comps = json::array();
		for (const auto& [key, comp] : ComponentKeys(*src))
		{
			if (overrides.count("-" + key))
				continue;   // 인스턴스에서 제거한 컴포넌트
			json c = *comp;
			auto it = instByKey.find(key);
			if (it != instByKey.end())
				for (auto field = it->second->begin(); field != it->second->end(); ++field)
					if (field.key() != "type" && (overrides.count(key + "/" + field.key()) || AlwaysInstance(key, field.key(), link.Root)))
						c[field.key()] = field.value();
			comps.push_back(c);
		}
		for (const auto& [key, comp] : instComps)
			if (overrides.count("+" + key))
				comps.push_back(*comp);   // 인스턴스에만 추가한 컴포넌트
		out["components"] = comps;

		// 자식: 인스턴스 쪽 자식을 따라가고(같은 에셋 연결이면 다시 조립), 에셋에 새로 생긴 자식은 추가
		json kids = json::array();
		std::set<uint64> covered;
		if (inst.contains("children"))
			for (const auto& c : inst["children"])
			{
				const PrefabLink cl = ReadLink(c);
				if (cl.IsValid() && cl.Asset == link.Asset && !cl.Root)
				{
					covered.insert(cl.Source);
					kids.push_back(MergeObject(c, assetPath));
				}
				else
					kids.push_back(c);   // 인스턴스에서 추가한 오브젝트 (또는 다른 프리팹)
			}
		if (src->contains("children"))
			for (const auto& c : (*src)["children"])
				if (!covered.count(c.value("fileID", (uint64)0)))
					kids.push_back(MakeInstanceJson(c, link.Asset, false));
		out["children"] = kids;
		return out;
	}

	void StripLinks(json& obj)
	{
		obj.erase("prefab");
		if (obj.contains("children"))
			for (auto& c : obj["children"])
				StripLinks(c);
	}

	void WalkObjects(GameObject* go, const std::function<void(GameObject*)>& fn)
	{
		fn(go);
		for (GameObject* c : go->GetChildren())
			WalkObjects(c, fn);
	}

	bool WriteAsset(const std::string& path, const json& root)
	{
		const std::wstring file = FilePath(path);
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
		std::ofstream os(file, std::ios::binary | std::ios::trunc);
		if (!os)
			return false;
		json j;
		j["nova_prefab"] = 1;
		j["layerFormat"] = TagsAndLayers::kLayerFormat;
		j["root"] = root;
		os << j.dump(2);
		os.close();
		LoadAsset(path);   // 파일 시각이 바뀌었으므로 새 버전으로 추가된다
		return true;
	}

	// 씬을 현재 상태 그대로 다시 만든다 → 인스턴스들이 에셋에서 다시 조립된다
	void RebuildScene()
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr)
			return;
		json j = *scene;
		SceneManager::GetI()->RestoreSceneState(j.dump());
	}

	json* FindJsonByFileID(json& obj, uint64 id)
	{
		if (obj.value("fileID", (uint64)0) == id)
			return &obj;
		if (obj.contains("children"))
			for (auto& c : obj["children"])
				if (json* r = FindJsonByFileID(c, id))
					return r;
		return nullptr;
	}
}

namespace PrefabUtility
{
	bool IsPrefabAssetPath(const std::string& path)
	{
		return std::filesystem::path(path).extension() == ".prefab";
	}

	int CountAssetObjects(const std::string& assetPath)
	{
		const AssetEntry* a = LoadAsset(assetPath);
		return a ? (int)a->ById().size() : 0;
	}

	int CurrentRevision(const std::string& assetPath)
	{
		const AssetEntry* a = LoadAsset(assetPath);
		return a ? a->Latest().Revision : 0;
	}

	GameObject* GetInstanceRoot(GameObject* go)
	{
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
			if (g->GetPrefabLink().IsValid() && g->GetPrefabLink().Root)
				return g;
		return nullptr;
	}

	bool IsPartOfPrefabInstance(const GameObject* go)
	{
		return go && go->GetPrefabLink().IsValid();
	}

	bool SaveAsPrefabAssetAndConnect(GameObject* root, const std::string& rawPath)
	{
		if (root == nullptr)
			return false;
		const std::string path = NormalizePath(rawPath);
		json j = *root;
		StripLinks(j);   // 안에 있던 다른 프리팹 인스턴스는 풀어서 저장 (중첩 프리팹 미지원)
		// 에셋의 루트는 원점 (Unity 도 에셋 루트 위치는 인스턴스가 따로 가진다)
		if (!WriteAsset(path, j))
			return false;
		const int revision = CurrentRevision(path);
		WalkObjects(root, [&](GameObject* g) {
			PrefabLink link;
			link.Asset = path;
			link.Source = g->GetFileID();   // 에셋 오브젝트의 fileID = 저장한 순간의 fileID
			link.Root = (g == root);
			link.Revision = revision;
			g->SetPrefabLink(link);
		});
		EditorLog::Write("Prefab", "created %s from '%s'", path.c_str(), root->GetName().c_str());
		return true;
	}

	GameObject* InstantiatePrefab(const std::string& rawPath, Scene* scene, GameObject* parent)
	{
		const std::string path = NormalizePath(rawPath);
		const AssetEntry* asset = LoadAsset(path);
		if (asset == nullptr || scene == nullptr)
		{
			EditorLog::Write("Prefab", "instantiate failed: %s", path.c_str());
			return nullptr;
		}
		json j = MakeInstanceJson(asset->Latest().Root, path, true);
		j["prefab"]["merged"] = true;   // 방금 에셋에서 만들었으니 다시 조립할 필요 없음
		GameObject* go = new GameObject();
		from_json(j, *go);
		scene->AddRootGameObject(go);
		if (parent)
			go->SetParent(parent, false);
		EditorLog::Write("Prefab", "instantiated %s as '%s'", path.c_str(), go->GetName().c_str());
		return go;
	}

	std::vector<std::string> ComputeOverrides(const json& inst, const PrefabLink& link)
	{
		std::vector<std::string> out;
		const json* src = FindSource(link, false);
		if (src == nullptr)
			return out;
		for (const char* f : kObjectFields)
		{
			if (link.Root && std::string(f) == "name")
				continue;
			if (inst.contains(f) && (!src->contains(f) || (*src)[f] != inst[f]))
				out.push_back(std::string("GameObject/") + f);
		}
		const auto srcComps = ComponentKeys(*src);
		std::map<std::string, const json*> srcByKey(srcComps.begin(), srcComps.end());
		std::set<std::string> instKeys;
		for (const auto& [key, comp] : ComponentKeys(inst))
		{
			instKeys.insert(key);
			auto it = srcByKey.find(key);
			if (it == srcByKey.end())
			{
				out.push_back("+" + key);
				continue;
			}
			for (auto field = comp->begin(); field != comp->end(); ++field)
			{
				if (field.key() == "type" || AlwaysInstance(key, field.key(), link.Root))
					continue;
				if (!it->second->contains(field.key()) || (*it->second)[field.key()] != field.value())
					out.push_back(key + "/" + field.key());
			}
		}
		for (const auto& [key, comp] : srcComps)
			if (!instKeys.count(key))
				out.push_back("-" + key);
		return out;
	}

	bool NeedsMerge(const json& obj)
	{
		const PrefabLink link = ReadLink(obj);
		return link.IsValid() && link.Root && !obj["prefab"].value("merged", false);
	}

	json MergeWithAsset(const json& instanceRootJson)
	{
		json out = MergeObject(instanceRootJson, ReadLink(instanceRootJson).Asset);
		out["prefab"]["merged"] = true;
		return out;
	}

	std::vector<std::string> GetOverrideDescriptions(GameObject* instanceRoot)
	{
		std::vector<std::string> out;
		if (instanceRoot == nullptr)
			return out;
		WalkObjects(instanceRoot, [&](GameObject* g) {
			if (!g->GetPrefabLink().IsValid())
			{
				out.push_back(g->GetName() + ": (added GameObject)");
				return;
			}
			json j = *g;
			for (const auto& o : ComputeOverrides(j, g->GetPrefabLink()))
				out.push_back(g->GetName() + ": " + o);
		});
		return out;
	}

	std::set<std::string> GetOverriddenComponentKeys(GameObject* go)
	{
		std::set<std::string> keys;
		if (go == nullptr || !go->GetPrefabLink().IsValid())
			return keys;
		json j = *go;
		for (const auto& o : ComputeOverrides(j, go->GetPrefabLink()))
		{
			std::string k = o;
			if (!k.empty() && (k[0] == '+' || k[0] == '-'))
				k = k.substr(1);
			keys.insert(k.substr(0, k.find('/')));
		}
		return keys;
	}

	bool ApplyAll(GameObject* instanceRoot)
	{
		if (instanceRoot == nullptr || !instanceRoot->GetPrefabLink().IsValid())
			return false;
		const std::string path = instanceRoot->GetPrefabLink().Asset;
		const AssetEntry* old = LoadAsset(path);

		// 인스턴스에서 추가한 오브젝트는 이제 프리팹의 일부 (새 source ID)
		WalkObjects(instanceRoot, [&](GameObject* g) {
			if (!g->GetPrefabLink().IsValid() || g->GetPrefabLink().Asset != path)
			{
				PrefabLink link;
				link.Asset = path;
				link.Source = GameObject::NewFileID();
				link.Root = false;
				link.Revision = instanceRoot->GetPrefabLink().Revision;
				g->SetPrefabLink(link);
			}
		});

		// 에셋 JSON: 인스턴스 값, fileID 는 source ID 로, 루트 위치/회전은 기존 에셋 값 유지
		std::function<json(GameObject*)> build = [&](GameObject* g) {
			json j = *g;
			j["fileID"] = g->GetPrefabLink().Source;
			j.erase("prefab");
			json kids = json::array();
			for (GameObject* c : g->GetChildren())
				kids.push_back(build(c));
			j["children"] = kids;
			return j;
		};
		json root = build(instanceRoot);
		if (old)
		{
			const json& oldRoot = old->Latest().Root;
			for (auto& c : root["components"])
				if (c.value("type", std::string()) == "Transform")
					for (auto& oc : oldRoot["components"])
						if (oc.value("type", std::string()) == "Transform")
							for (const char* f : { "m_LocalPosition", "m_LocalRotation", "m_LocalEulerAngles", "m_LocalEulerRadians" })
								if (oc.contains(f)) c[f] = oc[f];
			if (oldRoot.contains("name"))
				root["name"] = oldRoot["name"];
		}
		if (!WriteAsset(path, root))
			return false;
		EditorLog::Write("Prefab", "apply all '%s' -> %s", instanceRoot->GetName().c_str(), path.c_str());
		RebuildScene();   // 모든 인스턴스가 새 에셋 값으로
		return true;
	}

	void RevertAll(GameObject* instanceRoot)
	{
		if (instanceRoot == nullptr || !instanceRoot->GetPrefabLink().IsValid())
			return;
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr)
			return;
		const uint64 rootID = instanceRoot->GetFileID();
		json j = *scene;
		// 씬 JSON 에서 이 인스턴스의 오버라이드 목록을 비우고 다시 만든다 (인스턴스에서 추가한 오브젝트도 제거)
		std::function<bool(json&)> visit = [&](json& node) -> bool {
			if (node.is_object() && node.value("fileID", (uint64)0) == rootID)
			{
				std::function<void(json&)> clear = [&](json& o) {
					if (o.contains("prefab"))
					{
						o["prefab"]["overrides"] = json::array();
						o["prefab"].erase("merged");
					}
					if (o.contains("children"))
					{
						json kept = json::array();
						for (auto& c : o["children"])
							if (c.contains("prefab")) { clear(c); kept.push_back(c); }
						o["children"] = kept;
					}
				};
				clear(node);
				return true;
			}
			if (node.is_object() || node.is_array())
				for (auto& v : node)
					if ((v.is_object() || v.is_array()) && visit(v))
						return true;
			return false;
		};
		visit(j);
		EditorLog::Write("Prefab", "revert all '%s'", instanceRoot->GetName().c_str());
		SceneManager::GetI()->RestoreSceneState(j.dump());
	}

	void UnpackCompletely(GameObject* instanceRoot)
	{
		if (instanceRoot == nullptr)
			return;
		WalkObjects(instanceRoot, [](GameObject* g) { g->SetPrefabLink(PrefabLink()); });
		EditorLog::Write("Prefab", "unpacked '%s'", instanceRoot->GetName().c_str());
	}
}
