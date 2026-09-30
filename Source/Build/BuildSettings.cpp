#include "pch.h"
#include "BuildSettings.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
	bool s_ScenesLoaded = false, s_PlayerLoaded = false;
	std::vector<BuildSettings::SceneEntry> s_Scenes;
	BuildSettings::Player s_Player;
	bool s_Development = false;
	std::string s_LastFolder;
	bool s_HasRuntimeScenes = false;
	std::vector<std::string> s_RuntimeScenes;

	std::wstring File(const wchar_t* name) { return PathManager::GetI()->GetMovePathW(std::wstring(L"ProjectSettings\\") + name); }

	json ReadJson(const std::wstring& path)
	{
		std::ifstream in(path);
		if (!in)
			return json::object();
		json j = json::parse(in, nullptr, false);
		return j.is_discarded() || !j.is_object() ? json::object() : j;
	}

	void WriteJson(const std::wstring& path, const json& j)
	{
		std::error_code ec;
		fs::create_directories(fs::path(path).parent_path(), ec);
		std::ofstream os(path, std::ios::trunc);
		if (os)
			os << j.dump(4);
	}

	void LoadScenes()
	{
		s_ScenesLoaded = true;
		const json j = ReadJson(File(L"EditorBuildSettings.json"));
		s_Scenes.clear();
		if (j.contains("scenes") && j["scenes"].is_array())
			for (const json& s : j["scenes"])
				s_Scenes.push_back({ s.value("path", std::string()), s.value("enabled", true) });
		s_Development = j.value("developmentBuild", false);
		s_LastFolder = j.value("lastBuildFolder", std::string());
	}

	void LoadPlayer()
	{
		s_PlayerLoaded = true;
		const json j = ReadJson(File(L"PlayerSettings.json"));
		s_Player.CompanyName = j.value("companyName", std::string("DefaultCompany"));
		s_Player.ProductName = j.value("productName", std::string());
		s_Player.Version = j.value("version", std::string("0.1.0"));
		s_Player.Mode = (BuildSettings::FullscreenMode)j.value("fullscreenMode", 0);
		s_Player.Width = j.value("defaultScreenWidth", 1920);
		s_Player.Height = j.value("defaultScreenHeight", 1080);
		s_Player.Resizable = j.value("resizableWindow", true);
		s_Player.RunInBackground = j.value("runInBackground", true);
	}
}

namespace BuildSettings
{
	std::vector<SceneEntry>& Scenes()
	{
		if (!s_ScenesLoaded)
			LoadScenes();
		return s_Scenes;
	}

	void SaveEditorBuild()
	{
		json scenes = json::array();
		for (const SceneEntry& s : Scenes())
			scenes.push_back({ { "path", s.Path }, { "enabled", s.Enabled } });
		WriteJson(File(L"EditorBuildSettings.json"), json{ { "scenes", scenes }, { "developmentBuild", s_Development }, { "lastBuildFolder", s_LastFolder } });
	}

	void SaveScenes() { SaveEditorBuild(); }

	std::vector<std::string> EnabledScenes()
	{
		std::vector<std::string> out;
		for (const SceneEntry& s : Scenes())
			if (s.Enabled && !s.Path.empty())
				out.push_back(s.Path);
		return out;
	}

	Player& GetPlayer()
	{
		if (!s_PlayerLoaded)
			LoadPlayer();
		return s_Player;
	}

	void SavePlayer()
	{
		const Player& p = GetPlayer();
		WriteJson(File(L"PlayerSettings.json"), json{
			{ "companyName", p.CompanyName }, { "productName", p.ProductName }, { "version", p.Version },
			{ "fullscreenMode", (int)p.Mode }, { "defaultScreenWidth", p.Width }, { "defaultScreenHeight", p.Height },
			{ "resizableWindow", p.Resizable }, { "runInBackground", p.RunInBackground } });
	}

	std::string ProductName()
	{
		const std::string& name = GetPlayer().ProductName;
		if (!name.empty())
			return name;
		fs::path root = PathManager::GetI()->GetContentPathW();
		if (!root.has_filename())
			root = root.parent_path();
		return wstring_to_string(root.filename().wstring());
	}

	bool& DevelopmentBuild()
	{
		if (!s_ScenesLoaded)
			LoadScenes();
		return s_Development;
	}

	std::string& LastBuildFolder()
	{
		if (!s_ScenesLoaded)
			LoadScenes();
		return s_LastFolder;
	}

	std::vector<std::string> RuntimeScenes()
	{
		return s_HasRuntimeScenes ? s_RuntimeScenes : EnabledScenes();
	}

	void SetRuntimeScenes(const std::vector<std::string>& scenes)
	{
		s_HasRuntimeScenes = true;
		s_RuntimeScenes = scenes;
	}
}
