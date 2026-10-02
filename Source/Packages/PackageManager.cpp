#include "pch.h"
#include "Debug.h"
#include "PackageManager.h"
#include "AddComponentMenu.h"
#include "MissingComponent.h"
#include "ScriptEngine.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
	struct LoadState
	{
		std::vector<HMODULE> Modules;
		std::vector<std::string> Types;   // 불러올 때 새로 등록된 컴포넌트 타입 (내릴 때 등록 해제)
		std::string Error;
		bool Done = false;
	};

	std::vector<PackageInfo> s_Registry;
	std::vector<PackageInfo> s_Embedded;
	std::vector<PackageInfo> s_Local;                  // manifest 의 file: 패키지
	std::map<std::string, std::string> s_Manifest;    // 이름 → 버전
	std::map<std::string, LoadState> s_Loaded;
	std::set<std::string> s_PendingUnload;             // 쓰는 중이라 다음 시작 때 빠지는 패키지
	bool s_Initialized = false;

	std::string Trim(std::string s)
	{
		while (!s.empty() && (unsigned char)s.back() <= ' ') s.pop_back();
		size_t i = 0;
		while (i < s.size() && (unsigned char)s[i] <= ' ') ++i;
		return s.substr(i);
	}

	std::string ReadText(const fs::path& p)
	{
		std::ifstream in(p, std::ios::binary);
		return in ? std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()) : std::string();
	}

	std::wstring ProjectPackagesDir()
	{
		std::wstring root = PathManager::GetI()->GetMovePathW(L"");
		return root + L"Packages";
	}

	bool ReadPackage(const fs::path& folder, PackageInfo& out)
	{
		std::error_code ec;
		const fs::path file = folder / L"package.json";
		if (!fs::exists(file, ec))
			return false;
		json j = json::parse(ReadText(file), nullptr, false);
		if (j.is_discarded() || !j.is_object() || !j.contains("name"))
		{
			EditorLog::Write("Packages", "bad package.json: %s", wstring_to_string(file.wstring()).c_str());
			return false;
		}
		out = PackageInfo{};
		out.Name = j.value("name", "");
		out.DisplayName = j.value("displayName", out.Name);
		out.Version = j.value("version", "0.0.0");
		out.Description = j.value("description", "");
		out.Author = j.contains("author") && j["author"].is_object() ? j["author"].value("name", "") : j.value("author", "");
		out.Category = j.value("category", "");
		out.Documentation = j.value("documentationUrl", "");
		if (j.contains("keywords") && j["keywords"].is_array())
			for (const auto& k : j["keywords"]) if (k.is_string()) out.Keywords.push_back(k.get<std::string>());
		if (j.contains("dependencies") && j["dependencies"].is_object())
			for (auto it = j["dependencies"].begin(); it != j["dependencies"].end(); ++it)
				out.Dependencies.push_back({ it.key(), it.value().is_string() ? it.value().get<std::string>() : std::string() });
		if (j.contains("native") && j["native"].is_array())
			for (const auto& n : j["native"]) if (n.is_string()) out.Native.push_back(n.get<std::string>());
		if (j.contains("components") && j["components"].is_array())
			for (const auto& c : j["components"])
			{
				if (!c.is_object() || !c.contains("type")) continue;
				PackageComponentInfo ci;
				ci.Type = c.value("type", "");
				ci.Display = c.value("display", ci.Type);
				ci.Category = c.value("category", out.DisplayName);
				ci.Icon = c.value("icon", "script_cs");
				ci.Single = c.value("single", true);
				out.Components.push_back(ci);
			}
		out.Folder = folder.wstring();
		return !out.Name.empty();
	}

	void ScanFolder(const std::wstring& dir, std::vector<PackageInfo>& out, bool embedded)
	{
		out.clear();
		std::error_code ec;
		if (!fs::exists(dir, ec))
			return;
		for (const auto& e : fs::directory_iterator(dir, ec))
		{
			PackageInfo p;
			if (e.is_directory(ec) && ReadPackage(e.path(), p))
			{
				p.Embedded = embedded;
				out.push_back(std::move(p));
			}
		}
		std::sort(out.begin(), out.end(), [](const PackageInfo& a, const PackageInfo& b) { return a.DisplayName < b.DisplayName; });
	}

	void ReadManifest()
	{
		s_Manifest.clear();
		json j = json::parse(ReadText(PackageManager::ManifestPath()), nullptr, false);
		if (j.is_discarded() || !j.is_object() || !j.contains("dependencies") || !j["dependencies"].is_object())
			return;
		for (auto it = j["dependencies"].begin(); it != j["dependencies"].end(); ++it)
			s_Manifest[it.key()] = it.value().is_string() ? it.value().get<std::string>() : std::string();
	}

	bool WriteManifest(std::string& error)
	{
		json j = json::object();
		json deps = json::object();
		for (const auto& kv : s_Manifest)
			deps[kv.first] = kv.second;
		j["dependencies"] = deps;
		std::error_code ec;
		fs::create_directories(ProjectPackagesDir(), ec);
		std::ofstream out(PackageManager::ManifestPath(), std::ios::binary | std::ios::trunc);
		if (!out)
		{
			error = "cannot write " + wstring_to_string(PackageManager::ManifestPath());
			return false;
		}
		out << j.dump(2) << "\n";
		return true;
	}

	// 불러온 뒤: 씬에서 자리만 지키던 MissingComponent 를 실제 컴포넌트로
	void ResolveMissing()
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr)
			return;
		int fixedCount = 0;
		for (GameObject* go : scene->GetAllGameObjects())
		{
			std::vector<MissingComponent*> missing;
			for (const auto& c : go->GetComponents())
				if (auto* m = dynamic_cast<MissingComponent*>(c.get()))
					missing.push_back(m);
			for (MissingComponent* m : missing)
			{
				auto real = ComponentFactory::Instance().CreateComponent(m->GetMissingType());
				if (real == nullptr)
					continue;
				real->fromJson(m->GetData());
				go->ReplaceComponent(m, real);
				++fixedCount;
			}
		}
		if (fixedCount > 0)
			EditorLog::Write("Packages", "%d missing component(s) restored", fixedCount);
	}

	bool LoadPackage(const PackageInfo& p)
	{
		LoadState& st = s_Loaded[p.Name];
		if (st.Done)
			return st.Error.empty();
		st.Done = true;
		const std::vector<std::string> before = ComponentFactory::Instance().GetComponentTypes();
		const fs::path plugins = fs::path(p.Folder) / L"Plugins";
		for (const std::string& dll : p.Native)
		{
			const fs::path file = plugins / string_to_wstring(dll);
			std::error_code ec;
			if (!fs::exists(file, ec))
			{
				st.Error = "Plugins/" + dll + " not found (the package is not built)";
				break;
			}
			// ABI: 같은 엔진 버전·구성(Debug/Release)으로 빌드한 DLL 만 (다르면 불러오는 순간 메모리가 깨질 수 있다)
			fs::path abiFile = file;
			abiFile += L".abi";
			const std::string abi = Trim(ReadText(abiFile));
			if (abi != NOVA_PACKAGE_ABI_VERSION)
			{
				st.Error = dll + " was built for '" + (abi.empty() ? std::string("unknown") : abi) + "', this engine is '" NOVA_PACKAGE_ABI_VERSION "'";
				break;
			}
			HMODULE m = ::LoadLibraryExW(file.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
			if (m == nullptr)
			{
				st.Error = dll + ": LoadLibrary failed (error " + std::to_string(::GetLastError()) + ")";
				break;
			}
			st.Modules.push_back(m);
			using AbiFn = const char* (*)();
			if (auto f = (AbiFn)::GetProcAddress(m, "NovaPackage_Abi"))
				if (strcmp(f(), NOVA_PACKAGE_ABI_VERSION) != 0)
				{
					st.Error = dll + ": ABI mismatch (" + f() + ")";
					break;
				}
			using VoidFn = void (*)();
			if (auto f = (VoidFn)::GetProcAddress(m, "NovaPackage_OnLoad"))
				f();
		}
		// 이 패키지가 등록한 컴포넌트 타입 (내릴 때 지운다)
		for (const std::string& t : ComponentFactory::Instance().GetComponentTypes())
			if (std::find(before.begin(), before.end(), t) == before.end())
				st.Types.push_back(t);
		for (const PackageComponentInfo& c : p.Components)
			AddComponentMenu::RegisterExtra(c.Type, c.Display, c.Category, c.Icon, c.Single);
		if (st.Error.empty())
			EditorLog::Write("Packages", "loaded %s %s (%zu dll, %zu component types)", p.Name.c_str(), p.Version.c_str(), st.Modules.size(), st.Types.size());
		else
		{
			EditorLog::Write("Packages", "FAILED %s: %s", p.Name.c_str(), st.Error.c_str());
			Debug::LogError("Package '" + p.Name + "' failed to load: " + st.Error);
		}
		return st.Error.empty();
	}

	// 씬에서 이 타입들을 쓰는 컴포넌트가 있는지
	bool InUse(const std::vector<std::string>& types)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr || types.empty())
			return false;
		for (GameObject* go : scene->GetAllGameObjects())
			for (const auto& c : go->GetComponents())
				if (c && std::find(types.begin(), types.end(), c->GetType()) != types.end())
					return true;
		return false;
	}

	bool UnloadPackage(const std::string& name)
	{
		auto it = s_Loaded.find(name);
		if (it == s_Loaded.end())
			return true;
		LoadState& st = it->second;
		if (Application::IsPlaying() || InUse(st.Types))
			return false;
		for (const std::string& t : st.Types)
			ComponentFactory::Instance().UnregisterComponent(t);
		if (const PackageInfo* p = PackageManager::Find(name))
			for (const PackageComponentInfo& c : p->Components)
				AddComponentMenu::UnregisterExtra(c.Type);
		for (auto m = st.Modules.rbegin(); m != st.Modules.rend(); ++m)
		{
			using VoidFn = void (*)();
			if (auto f = (VoidFn)::GetProcAddress(*m, "NovaPackage_OnUnload"))
				f();
			::FreeLibrary(*m);
		}
		s_Loaded.erase(it);
		EditorLog::Write("Packages", "unloaded %s", name.c_str());
		return true;
	}

	std::vector<const PackageInfo*> Wanted()
	{
		std::vector<const PackageInfo*> out;
		for (const PackageInfo& p : s_Embedded)
			out.push_back(&p);
		for (const auto& kv : s_Manifest)
		{
			if (std::any_of(out.begin(), out.end(), [&](const PackageInfo* p) { return p->Name == kv.first; }))
				continue;   // 프로젝트에 넣은(embedded) 것이 우선
			if (kv.second.rfind("file:", 0) == 0)
			{
				auto l = std::find_if(s_Local.begin(), s_Local.end(), [&](const PackageInfo& p) { return p.Name == kv.first; });
				if (l != s_Local.end())
					out.push_back(&*l);
				else
					EditorLog::Write("Packages", "manifest: %s %s — folder not found", kv.first.c_str(), kv.second.c_str());
				continue;
			}
			auto r = std::find_if(s_Registry.begin(), s_Registry.end(), [&](const PackageInfo& p) { return p.Name == kv.first; });
			if (r != s_Registry.end())
				out.push_back(&*r);
			else
				EditorLog::Write("Packages", "manifest: %s %s not found in the registry", kv.first.c_str(), kv.second.c_str());
		}
		return out;
	}
}

namespace PackageManager
{
	std::wstring RegistryFolder() { return PathManager::GetI()->GetEnginePathW() + L"Packages"; }
	std::wstring ManifestPath() { return ProjectPackagesDir() + L"\\manifest.json"; }

	void Refresh()
	{
		ScanFolder(RegistryFolder(), s_Registry, false);
		ScanFolder(ProjectPackagesDir(), s_Embedded, true);
		// 빌드된 게임: 엔진 폴더 = 프로젝트 폴더 (<제품>_Data) → 같은 패키지가 두 목록에 → embedded 만 남긴다
		s_Registry.erase(std::remove_if(s_Registry.begin(), s_Registry.end(), [](const PackageInfo& r) {
			return std::any_of(s_Embedded.begin(), s_Embedded.end(), [&](const PackageInfo& e) { return e.Name == r.Name; });
		}), s_Registry.end());
		ReadManifest();
		s_Local.clear();
		for (const auto& kv : s_Manifest)
		{
			if (kv.second.rfind("file:", 0) != 0)
				continue;
			fs::path folder(string_to_wstring(kv.second.substr(5)));
			if (folder.is_relative())
				folder = fs::path(ProjectPackagesDir()) / folder;
			PackageInfo p;
			std::error_code ec;
			if (ReadPackage(fs::weakly_canonical(folder, ec), p))
			{
				p.Local = true;
				s_Local.push_back(std::move(p));
			}
		}
	}

	void Init()
	{
		Refresh();
		// manifest 가 없는 프로젝트 (새 프로젝트 · 예전 프로젝트): Unity 기본 프로젝트처럼 기본 패키지를 넣어 둔다
		std::error_code ec;
		if (!Application::IsPlayer() && !fs::exists(ManifestPath(), ec) && fs::exists(PathManager::GetI()->GetMovePathW(L"Assets"), ec))
		{
			for (const char* name : { "com.nova.animation" })
				if (const PackageInfo* p = Find(name); p && !p->Embedded)
					s_Manifest[name] = p->Version;
			std::string error;
			if (!s_Manifest.empty() && WriteManifest(error))
				EditorLog::Write("Packages", "created Packages/manifest.json with the default packages");
		}
		s_Initialized = true;
		for (const PackageInfo* p : Wanted())
			LoadPackage(*p);
		EditorLog::Write("Packages", "registry %zu, project %zu (manifest %zu, embedded %zu)", s_Registry.size(), Wanted().size(), s_Manifest.size(), s_Embedded.size());
	}

	void Shutdown()
	{
		// DLL 은 프로세스가 끝날 때 내린다 (정적 소멸 순서 문제를 피한다)
	}

	const std::vector<PackageInfo>& Registry() { return s_Registry; }

	std::vector<const PackageInfo*> InProject() { return Wanted(); }

	const PackageInfo* Find(const std::string& name)
	{
		for (const PackageInfo& p : s_Embedded) if (p.Name == name) return &p;
		for (const PackageInfo& p : s_Local) if (p.Name == name) return &p;
		for (const PackageInfo& p : s_Registry) if (p.Name == name) return &p;
		return nullptr;
	}

	bool IsInProject(const std::string& name)
	{
		if (s_Manifest.count(name)) return true;
		return std::any_of(s_Embedded.begin(), s_Embedded.end(), [&](const PackageInfo& p) { return p.Name == name; });
	}

	bool IsLoaded(const std::string& name)
	{
		auto it = s_Loaded.find(name);
		return it != s_Loaded.end() && it->second.Done && it->second.Error.empty();
	}

	std::string LoadError(const std::string& name)
	{
		auto it = s_Loaded.find(name);
		return it == s_Loaded.end() ? std::string() : it->second.Error;
	}

	bool RestartRequired() { return !s_PendingUnload.empty(); }

	bool Add(const std::string& name, std::string& error)
	{
		const PackageInfo* p = Find(name);
		if (p == nullptr)
		{
			error = "package '" + name + "' is not in the registry";
			return false;
		}
		if (Application::IsPlaying())
		{
			error = "stop Play mode before changing packages";
			return false;
		}
		// 의존 패키지 먼저 (Unity 처럼 프로젝트 manifest 에도 들어간다)
		static int s_Depth = 0;
		if (s_Depth > 8)
		{
			error = "package dependencies are too deep (cycle?)";
			return false;
		}
		for (const auto& dep : p->Dependencies)
			if (!IsInProject(dep.first))
			{
				++s_Depth;
				std::string depError;
				const bool ok = Add(dep.first, depError);
				--s_Depth;
				if (!ok)
				{
					error = "dependency " + dep.first + ": " + depError;
					return false;
				}
			}
		if (!p->Embedded && !p->Local)
		{
			s_Manifest[name] = p->Version;
			if (!WriteManifest(error))
				return false;
		}
		s_PendingUnload.erase(name);
		const bool ok = LoadPackage(*p);
		ResolveMissing();
		ScriptEngine::RegenerateProjectFiles();
		ScriptEngine::RequestRecompile();   // Runtime C# 를 Assembly-CSharp 에 넣는다
		if (!ok)
			error = LoadError(name);
		Debug::Log("Package Manager: added " + p->DisplayName + " " + p->Version + (ok ? "" : " (failed to load: " + error + ")"));
		return ok;
	}

	bool Remove(const std::string& name, std::string& error)
	{
		const PackageInfo* p = Find(name);
		if (p != nullptr && p->Embedded)
		{
			error = "'" + name + "' is embedded in the project (Packages/" + name + ") — delete that folder to remove it";
			return false;
		}
		if (!s_Manifest.count(name))
		{
			error = "'" + name + "' is not in the project";
			return false;
		}
		if (Application::IsPlaying())
		{
			error = "stop Play mode before changing packages";
			return false;
		}
		s_Manifest.erase(name);
		if (!WriteManifest(error))
			return false;
		if (!UnloadPackage(name))
		{
			s_PendingUnload.insert(name);   // 씬이 쓰는 중: 다음 시작부터 빠진다 (그때 그 컴포넌트는 MissingComponent 로 보존)
			Debug::LogWarning("Package Manager: " + name + " is used by the open scene — it will be removed when the editor restarts");
		}
		ScriptEngine::RegenerateProjectFiles();
		ScriptEngine::RequestRecompile();
		Debug::Log("Package Manager: removed " + name);
		return true;
	}

	bool AddFromDisk(const std::wstring& packageJsonOrFolder, std::string& error, std::string* addedName)
	{
		std::error_code ec;
		fs::path folder(packageJsonOrFolder);
		if (fs::is_regular_file(folder, ec))
			folder = folder.parent_path();
		PackageInfo p;
		if (!ReadPackage(folder, p))
		{
			error = "no valid package.json in " + wstring_to_string(folder.wstring());
			return false;
		}
		if (Application::IsPlaying())
		{
			error = "stop Play mode before changing packages";
			return false;
		}
		// 프로젝트 Packages 폴더 기준 상대 경로 (프로젝트를 옮겨도 함께 옮긴 패키지는 그대로)
		fs::path rel = fs::relative(fs::weakly_canonical(folder, ec), fs::weakly_canonical(ProjectPackagesDir(), ec), ec);
		std::wstring ref = (ec || rel.empty()) ? folder.wstring() : rel.wstring();
		std::replace(ref.begin(), ref.end(), L'\\', L'/');
		s_Manifest[p.Name] = "file:" + wstring_to_string(ref);
		if (!WriteManifest(error))
			return false;
		Refresh();
		if (addedName)
			*addedName = p.Name;
		return Add(p.Name, error);   // 불러오기 · 의존성 · 스크립트 (manifest 는 이미 file: 로)
	}

	std::string PackageForComponent(const std::string& type)
	{
		for (const std::vector<PackageInfo>* list : { &s_Embedded, &s_Local, &s_Registry })
			for (const PackageInfo& p : *list)
				for (const PackageComponentInfo& c : p.Components)
					if (c.Type == type)
						return p.Name;
		return std::string();
	}

	bool AddForComponent(const std::string& type)
	{
		if (Application::IsPlayer() || Application::IsPlaying())
			return false;
		const std::string name = PackageForComponent(type);
		if (name.empty() || IsLoaded(name))
			return false;
		std::string error;
		if (!Add(name, error))
		{
			EditorLog::Write("Packages", "scene uses %s from %s but adding it failed: %s", type.c_str(), name.c_str(), error.c_str());
			return false;
		}
		Debug::Log("Package Manager: the scene uses " + type + " → added " + name);
		return true;
	}

	std::vector<std::wstring> ScriptFolders()
	{
		std::vector<std::wstring> out;
		std::error_code ec;
		for (const PackageInfo* p : Wanted())
		{
			if (s_PendingUnload.count(p->Name))
				continue;
			const fs::path runtime = fs::path(p->Folder) / L"Runtime";
			if (fs::exists(runtime, ec))
				out.push_back(runtime.wstring());
		}
		return out;
	}
}
