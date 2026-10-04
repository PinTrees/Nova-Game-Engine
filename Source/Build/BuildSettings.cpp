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
	int s_Platform = 0;                 // 0 Windows, 1 Android
	std::string s_AndroidDevice, s_LastApk;
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
		s_Platform = j.value("activePlatform", std::string()) == "Android" ? 1 : 0;
		s_AndroidDevice = j.value("androidRunDevice", std::string());
		s_LastApk = j.value("lastAndroidApk", std::string());
	}

	json ApiKeys(const std::vector<GraphicsAPI>& apis)
	{
		json keys = json::array();
		for (GraphicsAPI api : apis)
			keys.push_back(GraphicsAPIToKey(api));
		return keys;
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
		s_Player.AutoGraphicsAPI = j.value("autoGraphicsAPI", true);
		s_Player.AndroidPackageName = j.value("androidPackageName", std::string());
		{
			static const char* kOr[] = { "Portrait", "PortraitUpsideDown", "LandscapeRight", "LandscapeLeft", "AutoRotation" };
			const std::string o = j.value("androidOrientation", std::string("AutoRotation"));
			s_Player.AndroidOrientation = 4;
			for (int i = 0; i < 5; ++i)
				if (o == kOr[i]) s_Player.AndroidOrientation = i;
		}
		{
			static const char* kTc[] = { "ASTC", "ETC2", "DXT", "None" };
			const std::string tc = j.value("androidTextureCompression", std::string("ASTC"));
			s_Player.AndroidTextureCompression = 0;
			for (int i = 0; i < 4; ++i)
				if (tc == kTc[i]) s_Player.AndroidTextureCompression = i;
		}
		if (j.contains("graphicsAPIs") && j["graphicsAPIs"].is_array())
		{
			s_Player.GraphicsAPIs.clear();
			for (const json& k : j["graphicsAPIs"])
				if (k.is_string())
				{
					const GraphicsAPI api = GraphicsAPIFromKey(k.get<std::string>());
					if (std::find(s_Player.GraphicsAPIs.begin(), s_Player.GraphicsAPIs.end(), api) == s_Player.GraphicsAPIs.end())
						s_Player.GraphicsAPIs.push_back(api);
				}
			if (s_Player.GraphicsAPIs.empty())
				s_Player.GraphicsAPIs = { GraphicsAPI::DirectX11 };
		}
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
		WriteJson(File(L"EditorBuildSettings.json"), json{ { "scenes", scenes }, { "developmentBuild", s_Development }, { "lastBuildFolder", s_LastFolder },
			{ "activePlatform", s_Platform == 1 ? "Android" : "Windows" }, { "androidRunDevice", s_AndroidDevice }, { "lastAndroidApk", s_LastApk } });
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
			{ "resizableWindow", p.Resizable }, { "runInBackground", p.RunInBackground },
			{ "autoGraphicsAPI", p.AutoGraphicsAPI }, { "graphicsAPIs", ApiKeys(p.GraphicsAPIs) },
			{ "androidTextureCompression", std::array<const char*, 4>{ "ASTC", "ETC2", "DXT", "None" }[std::clamp(p.AndroidTextureCompression, 0, 3)] }, { "androidPackageName", p.AndroidPackageName },
			{ "androidOrientation", std::array<const char*, 5>{ "Portrait", "PortraitUpsideDown", "LandscapeRight", "LandscapeLeft", "AutoRotation" }[std::clamp(p.AndroidOrientation, 0, 4)] } });
	}

	std::vector<GraphicsAPI> PlayerGraphicsAPIs()
	{
		const Player& p = GetPlayer();
		if (p.AutoGraphicsAPI || p.GraphicsAPIs.empty())
			return { GraphicsAPI::DirectX11 };   // Unity 의 Windows Auto 와 같이 DirectX 11 만 (OpenGL 변환기 23 MB 가 게임에 들어가지 않는다)
		return p.GraphicsAPIs;
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

	int& ActivePlatform()
	{
		if (!s_ScenesLoaded)
			LoadScenes();
		return s_Platform;
	}

	std::string& AndroidRunDevice()
	{
		if (!s_ScenesLoaded)
			LoadScenes();
		return s_AndroidDevice;
	}

	std::string& LastAndroidApk()
	{
		if (!s_ScenesLoaded)
			LoadScenes();
		return s_LastApk;
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
