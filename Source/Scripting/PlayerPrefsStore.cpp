#include "pch.h"
#include "PlayerPrefsStore.h"
#include "BuildSettings.h"
#include "PathManager.h"
#include "Application.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
#include "AndroidEngine.h"
#else
#include <shlobj.h>
#endif

namespace
{
	namespace fs = std::filesystem;
	using json = nlohmann::json;

	json s_Prefs = json::object();
	bool s_Loaded = false;
	bool s_Dirty = false;

	std::string SafeName(std::string s)
	{
		for (char& c : s)
			if (strchr("<>:\"/\\|?*", c) || (unsigned char)c < 32) c = '_';
		return s.empty() ? std::string("Game") : s;
	}

#if defined(__EMSCRIPTEN__)
	// 웹: 브라우저 localStorage (출처마다 — 같은 사이트의 다른 게임과 섞이지 않게 제품 이름을 붙인다)
	EM_JS(char*, NovaPrefsRead, (const char* key), {
		try {
			const v = window.localStorage.getItem(UTF8ToString(key));
			if (v === null) return 0;
			const n = lengthBytesUTF8(v) + 1;
			const p = _malloc(n);
			stringToUTF8(v, p, n);
			return p;
		} catch (e) { return 0; }
	});
	EM_JS(void, NovaPrefsWrite, (const char* key, const char* value), {
		try { window.localStorage.setItem(UTF8ToString(key), UTF8ToString(value)); } catch (e) { console.warn('NOVA PlayerPrefs:', e); }
	});
	std::string StorageKey() { return "nova.playerprefs." + BuildSettings::ProductName(); }
#else
	fs::path PrefsFile()
	{
		if (!Application::IsPlayer())
			return fs::path(PathManager::GetI()->GetMovePathW(L"Library\\PlayerPrefs.json"));
		return fs::path(string_to_wstring(PlayerPrefsStore::PersistentDataPath())) / L"PlayerPrefs.json";
	}
#endif

	void Load()
	{
		if (s_Loaded)
			return;
		s_Loaded = true;
		std::string text;
#if defined(__EMSCRIPTEN__)
		if (char* v = NovaPrefsRead(StorageKey().c_str()))
		{
			text = v;
			free(v);
		}
#else
		std::ifstream in(PrefsFile(), std::ios::binary);
		if (in)
			text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
#endif
		json j = text.empty() ? json::object() : json::parse(text, nullptr, false);
		s_Prefs = j.is_object() ? j : json::object();
	}

	void Changed() { s_Dirty = true; }
}

namespace PlayerPrefsStore
{
	std::string PersistentDataPath()
	{
#if defined(__EMSCRIPTEN__)
		return "/persistent";
#elif defined(__ANDROID__)
		return NovaAndroid::FilesDir();
#else
		PWSTR low = nullptr;
		fs::path base;
		if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &low)))
			base = low;
		::CoTaskMemFree(low);
		const fs::path dir = base / string_to_wstring(SafeName(BuildSettings::GetPlayer().CompanyName)) / string_to_wstring(SafeName(BuildSettings::ProductName()));
		std::error_code ec;
		fs::create_directories(dir, ec);
		return wstring_to_string(dir.wstring());
#endif
	}

	bool Has(const std::string& key) { Load(); return s_Prefs.contains(key); }

	void SetInt(const std::string& key, int value) { Load(); s_Prefs[key] = value; Changed(); }
	int GetInt(const std::string& key, int defaultValue)
	{
		Load();
		auto it = s_Prefs.find(key);
		return it != s_Prefs.end() && it->is_number_integer() ? it->get<int>() : defaultValue;
	}

	void SetFloat(const std::string& key, float value) { Load(); s_Prefs[key] = (double)value; Changed(); }
	float GetFloat(const std::string& key, float defaultValue)
	{
		Load();
		auto it = s_Prefs.find(key);
		return it != s_Prefs.end() && it->is_number_float() ? (float)it->get<double>() : defaultValue;
	}

	void SetString(const std::string& key, const std::string& value) { Load(); s_Prefs[key] = value; Changed(); }
	bool GetString(const std::string& key, std::string& out)
	{
		Load();
		auto it = s_Prefs.find(key);
		if (it == s_Prefs.end() || !it->is_string())
			return false;
		out = it->get<std::string>();
		return true;
	}

	void Delete(const std::string& key) { Load(); if (s_Prefs.erase(key)) Changed(); }
	void DeleteAll() { Load(); s_Prefs = json::object(); Changed(); }

	void Save()
	{
		Load();
		const std::string text = s_Prefs.dump(1);
#if defined(__EMSCRIPTEN__)
		NovaPrefsWrite(StorageKey().c_str(), text.c_str());
#else
		const fs::path file = PrefsFile();
		std::error_code ec;
		fs::create_directories(file.parent_path(), ec);
		const fs::path temp = file.wstring() + L".tmp";
		{
			std::ofstream out(temp, std::ios::binary | std::ios::trunc);
			out << text;
		}
		fs::rename(temp, file, ec);   // 쓰는 도중에 꺼져도 옛 파일이 남게
		if (ec)
		{
			std::ofstream out(file, std::ios::binary | std::ios::trunc);
			out << text;
		}
#endif
		s_Dirty = false;
	}

	void Flush()
	{
		if (s_Dirty)
			Save();
	}
}
