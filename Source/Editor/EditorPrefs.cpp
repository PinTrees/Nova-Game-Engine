#include "pch.h"
#include "EditorPrefs.h"
#include <filesystem>
#include <fstream>

namespace
{
	json s_Prefs;
	bool s_Loaded = false;

	std::wstring PrefsFile()
	{
		wchar_t buf[MAX_PATH] = {};
		if (::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH) == 0)
			return L"EditorPrefs.json";
		return std::wstring(buf) + L"\\NOVA\\Editor\\EditorPrefs.json";
	}

	json& Prefs()
	{
		if (!s_Loaded)
		{
			s_Loaded = true;
			std::ifstream in(PrefsFile());
			if (in)
			{
				json j = json::parse(in, nullptr, false);
				if (!j.is_discarded() && j.is_object())
					s_Prefs = j;
			}
			if (!s_Prefs.is_object())
				s_Prefs = json::object();
		}
		return s_Prefs;
	}

	void Save()
	{
		const std::filesystem::path file(PrefsFile());
		std::error_code ec;
		std::filesystem::create_directories(file.parent_path(), ec);
		std::ofstream os(file, std::ios::trunc);
		if (os)
			os << s_Prefs.dump(4);
	}

	template <typename T>
	T Get(const std::string& key, const T& def)
	{
		const json& p = Prefs();
		if (!p.contains(key))
			return def;
		try { return p[key].get<T>(); }
		catch (...) { return def; }
	}

	template <typename T>
	void Set(const std::string& key, const T& v)
	{
		json& p = Prefs();
		if (p.contains(key) && p[key] == json(v))
			return;
		p[key] = v;
		Save();
	}
}

namespace EditorPrefs
{
	std::string GetString(const std::string& key, const std::string& d) { return Get<std::string>(key, d); }
	int GetInt(const std::string& key, int d) { return Get<int>(key, d); }
	float GetFloat(const std::string& key, float d) { return Get<float>(key, d); }
	bool GetBool(const std::string& key, bool d) { return Get<bool>(key, d); }
	void SetString(const std::string& key, const std::string& v) { Set(key, v); }
	void SetInt(const std::string& key, int v) { Set(key, v); }
	void SetFloat(const std::string& key, float v) { Set(key, v); }
	void SetBool(const std::string& key, bool v) { Set(key, v); }
	bool HasKey(const std::string& key) { return Prefs().contains(key); }
	void DeleteKey(const std::string& key) { if (Prefs().erase(key) > 0) Save(); }
}
