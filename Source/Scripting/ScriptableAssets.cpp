#include "pch.h"
#include "ScriptableAssets.h"
#include "ScriptEngine.h"
#include "ScriptFieldGUI.h"
#include "EditorExtensions.h"
#include "UnityGUI.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <mutex>
#include <map>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
	std::wstring FullPath(const std::string& rel)
	{
		std::wstring w = string_to_wstring(rel);
		std::replace(w.begin(), w.end(), L'/', L'\\');
		while (!w.empty() && w[0] == L'\\')
			w.erase(0, 1);
		const fs::path p(w);
		return p.is_absolute() ? p.wstring() : PathManager::GetI()->GetMovePathW(w);
	}

	std::string Lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); }); return s; }

	double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

	// 읽은 .asset 캐시 (Inspector 가 매 프레임 그린다 — 파일 시각이 바뀔 때만 다시 읽는다)
	struct Cached
	{
		fs::file_time_type Time;
		std::string Class;
		json Fields;
		bool Valid = false;
	};
	std::mutex s_Lock;
	std::map<std::wstring, Cached> s_Cache;

	// Resources 폴더 목록: (Resources 기준 경로 소문자 · 확장자 없이, 프로젝트 상대 경로)
	std::vector<std::pair<std::string, std::string>> s_Resources;
	double s_ResourcesBuilt = -1.0;

	void BuildResourceIndex()
	{
		s_Resources.clear();
		const fs::path root = PathManager::GetI()->GetMovePathW(L"Assets");
		std::error_code ec;
		if (!fs::is_directory(root, ec))
			return;
		for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
		{
			if (ec)
				break;
			if (!it->is_regular_file(ec) || _wcsicmp(it->path().extension().c_str(), L".meta") == 0)
				continue;
			const fs::path rel = fs::relative(it->path(), root, ec);
			std::vector<std::wstring> parts;
			for (const fs::path& c : rel)
				parts.push_back(c.wstring());
			// 마지막 "Resources" 폴더 아래 (Unity 와 같음 — 폴더 이름은 대소문자 무시)
			int at = -1;
			for (int i = 0; i + 1 < (int)parts.size(); ++i)
				if (_wcsicmp(parts[i].c_str(), L"Resources") == 0)
					at = i;
			if (at < 0)
				continue;
			std::wstring key;
			for (size_t i = (size_t)at + 1; i < parts.size(); ++i)
				key += (key.empty() ? L"" : L"/") + (i + 1 == parts.size() ? fs::path(parts[i]).stem().wstring() : parts[i]);
			s_Resources.push_back({ Lower(wstring_to_string(key)), "Assets/" + wstring_to_string(rel.generic_wstring()) });
		}
		std::sort(s_Resources.begin(), s_Resources.end());
		s_ResourcesBuilt = Now();
	}

	std::vector<std::string> AllAssetFiles()
	{
		std::vector<std::string> out;
		const fs::path root = PathManager::GetI()->GetMovePathW(L"Assets");
		std::error_code ec;
		for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
		{
			if (ec)
				break;
			if (it->is_regular_file(ec) && _wcsicmp(it->path().extension().c_str(), L".asset") == 0)
				out.push_back(PathManager::GetI()->GetCutSolutionPath(wstring_to_string(it->path().wstring())));
		}
		std::sort(out.begin(), out.end());
		return out;
	}
}

namespace ScriptableAssets
{
	bool Read(const std::string& relPath, std::string& className, json& fields)
	{
		const std::wstring full = FullPath(relPath);
		std::error_code ec;
		const fs::file_time_type t = fs::last_write_time(full, ec);
		if (ec)
			return false;
		std::lock_guard<std::mutex> lock(s_Lock);
		Cached& c = s_Cache[full];
		if (c.Time != t || (!c.Valid && c.Class.empty()))
		{
			c = Cached();
			c.Time = t;
			std::ifstream in(full, std::ios::binary);
			json j = json::parse(in, nullptr, false);
			if (!j.is_discarded() && j.is_object() && j.contains("class") && j["class"].is_string())
			{
				c.Class = j["class"].get<std::string>();
				c.Fields = j.contains("fields") && j["fields"].is_object() ? j["fields"] : json::object();
				c.Valid = true;
			}
			else
				c.Class = "\x01";   // 읽었지만 ScriptableObject 아님 (다시 읽지 않게)
		}
		if (!c.Valid)
			return false;
		className = c.Class;
		fields = c.Fields;
		return true;
	}

	bool Write(const std::string& relPath, const std::string& className, const json& fields)
	{
		const std::wstring full = FullPath(relPath);
		json j = { { "type", "ScriptableObject" }, { "class", className }, { "fields", fields } };
		std::error_code ec;
		fs::create_directories(fs::path(full).parent_path(), ec);
		{
			std::ofstream out(full, std::ios::binary | std::ios::trunc);
			if (!out)
				return false;
			out << j.dump(2);
		}
		{
			std::lock_guard<std::mutex> lock(s_Lock);
			Cached& c = s_Cache[full];
			c.Time = fs::last_write_time(full, ec);
			c.Class = className;
			c.Fields = fields;
			c.Valid = true;
		}
		ScriptEngine::NotifyAssetChanged(relPath);   // Play 중이면 읽어 둔 인스턴스에 새 값
		return true;
	}

	json ResolvedFields(const std::string& className, const json& fields)
	{
		json out = json::object();
		if (const ScriptEngine::ClassInfo* ci = ScriptEngine::FindScriptableClass(className))
			for (const ScriptEngine::FieldInfo& f : ci->Fields)
				out[f.Name] = fields.is_object() && fields.contains(f.Name) ? fields[f.Name] : f.Default;
		// 클래스에서 사라진 필드도 그대로 둔다 (Unity 처럼 이름을 되돌리면 값이 돌아온다)
		if (fields.is_object())
			for (auto it = fields.begin(); it != fields.end(); ++it)
				if (!out.contains(it.key()))
					out[it.key()] = it.value();
		return out;
	}

	std::string Create(const std::string& path, const std::string& className, std::string* error)
	{
		const ScriptEngine::ClassInfo* ci = ScriptEngine::FindScriptableClass(className);
		if (ci == nullptr || ci->Abstract)
		{
			if (error) *error = ci ? "the class is abstract" : "no ScriptableObject class '" + className + "' (compiled scripts)";
			return std::string();
		}
		fs::path full = FullPath(path.empty() ? "Assets" : path);
		std::error_code ec;
		if (_wcsicmp(full.extension().c_str(), L".asset") != 0)
		{
			fs::create_directories(full, ec);
			const std::wstring base = string_to_wstring(ci->HasCreateMenu ? ci->FileName : "New " + ci->Name);
			fs::path file = full / (base + L".asset");
			for (int i = 1; fs::exists(file, ec); ++i)
				file = full / (base + L" " + std::to_wstring(i) + L".asset");
			full = file;
		}
		const std::string rel = PathManager::GetI()->GetCutSolutionPath(wstring_to_string(full.wstring()));
		if (!Write(rel, ci->FullName, ResolvedFields(ci->FullName, json::object())))
		{
			if (error) *error = "could not write " + rel;
			return std::string();
		}
		return rel;
	}

	std::vector<std::string> FindAll(const std::string& baseClass)
	{
		std::vector<std::string> out;
		for (const std::string& rel : AllAssetFiles())
		{
			std::string cls;
			json fields;
			if (Read(rel, cls, fields) && (baseClass.empty() || ScriptEngine::IsScriptableSubclass(cls, baseClass)))
				out.push_back(rel);
		}
		return out;
	}

	std::vector<std::string> FindResources(const std::string& path, bool all)
	{
		std::string want = Lower(path);
		std::replace(want.begin(), want.end(), '\\', '/');
		while (!want.empty() && want.front() == '/') want.erase(0, 1);
		while (!want.empty() && want.back() == '/') want.pop_back();
		std::lock_guard<std::mutex> lock(s_Lock);
		auto collect = [&]() {
			std::vector<std::string> out;
			for (const auto& r : s_Resources)
				if (all ? (want.empty() || r.first == want || (r.first.size() > want.size() && r.first.compare(0, want.size(), want) == 0 && r.first[want.size()] == '/')) : r.first == want)
					out.push_back(r.second);
			return out;
		};
		// 편집기는 파일이 늘 수 있다 — 못 찾았거나 오래됐으면 다시 훑는다 (1 초에 한 번까지). 게임은 한 번
		if (s_ResourcesBuilt < 0.0)
			BuildResourceIndex();
		std::vector<std::string> out = collect();
		if (!Application::IsPlayer() && (out.empty() || all) && Now() - s_ResourcesBuilt > 1.0)
		{
			BuildResourceIndex();
			out = collect();
		}
		return out;
	}

	void RegisterEditor()
	{
		if (Application::IsPlayer())
			return;
		EditorExtensions::AssetType t;
		t.Owner = "core.scriptableobject";
		t.Extension = ".asset";
		t.Icon = "scriptable_object";
		t.Inspector = [](const std::string& relPath) {
			std::string cls;
			json fields;
			const std::string title = wstring_to_string(fs::path(string_to_wstring(relPath)).stem().wstring());
			if (!Read(relPath, cls, fields))
			{
				UnityGUI::Label(title.c_str(), 0, true);
				UnityGUI::HelpBox("This .asset is not a ScriptableObject asset (JSON with a \"class\").", true);
				return;
			}
			UnityGUI::Label(title.c_str(), 0, true);
			const ScriptEngine::ClassInfo* ci = ScriptEngine::FindScriptableClass(cls);
			UnityGUI::ValueLabel("Script", (ci ? ci->Name : cls).c_str());
			if (ci == nullptr)
			{
				UnityGUI::HelpBox(ScriptEngine::IsCompiling() ? "Compiling scripts..." :
					"The associated script can not be loaded.\nPlease fix any compile errors\nand assign a valid script.", !ScriptEngine::IsCompiling());
				return;
			}
			json values = ResolvedFields(cls, fields);
			if (!ScriptFieldGUI::Fields(ci->Fields, values, "so:" + relPath).empty())
				Write(relPath, cls, values);
		};
		EditorExtensions::RegisterAssetType(t);
	}
}

// ---------------------------------------------------------------- C# (NovaEngine.Interop.AssetNative)
NOVA_PACKAGE_EXPORT const char* NovaAsset_Resolve(const char* path)
{
	thread_local std::string out;
	out.clear();
	if (path == nullptr || *path == 0)
		return out.c_str();
	const std::wstring full = FullPath(path);
	std::error_code ec;
	if (fs::is_regular_file(full, ec))
		out = wstring_to_string(full);
	return out.c_str();
}

NOVA_PACKAGE_EXPORT const char* NovaAsset_FindResources(const char* path, int all)
{
	thread_local std::string out;
	out.clear();
	for (const std::string& f : ScriptableAssets::FindResources(path ? path : "", all != 0))
		out += f + "\n";
	return out.c_str();
}
