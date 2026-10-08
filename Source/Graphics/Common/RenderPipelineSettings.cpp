#include "pch.h"
#include "RenderPipelineSettings.h"
#include "VolumeProfile.h"
#include "ClusteredLighting.h"
#include "DeferredRenderer.h"
#include "CliServer.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
	bool s_Loaded = false;
	std::string s_DefaultProfile;
	RenderPipelineSettings::RenderingPath s_Path = RenderPipelineSettings::RenderingPath::ForwardPlus;

	std::wstring SettingsFile()
	{
		return PathManager::GetI()->GetMovePathW(L"ProjectSettings\\GraphicsSettings.json");
	}

	void Load()
	{
		s_Loaded = true;
		s_DefaultProfile.clear();
		s_Path = RenderPipelineSettings::RenderingPath::ForwardPlus;
		std::ifstream in(SettingsFile());
		if (in)
		{
			json j = json::parse(in, nullptr, false);
			if (!j.is_discarded() && j.is_object())
			{
				s_DefaultProfile = j.value("defaultVolumeProfile", std::string());
				RenderPipelineSettings::RenderingPathFromName(j.value("renderingPath", std::string("Forward+")), s_Path);
			}
		}
		ClusteredLighting::SetEnabled(s_Path != RenderPipelineSettings::RenderingPath::Forward);
	}

	void Save()
	{
		const fs::path file(SettingsFile());
		std::error_code ec;
		fs::create_directories(file.parent_path(), ec);
		json j = json::object();
		{
			// 다른 키가 있으면 보존
			std::ifstream in(file);
			if (in)
			{
				json old = json::parse(in, nullptr, false);
				if (!old.is_discarded() && old.is_object())
					j = old;
			}
		}
		j["defaultVolumeProfile"] = s_DefaultProfile;
		j["renderingPath"] = RenderPipelineSettings::RenderingPathName(s_Path);
		std::ofstream os(file, std::ios::trunc);
		if (os)
			os << j.dump(4);
	}
}

namespace RenderPipelineSettings
{
	const std::string& DefaultVolumeProfilePath()
	{
		if (!s_Loaded)
			Load();
		return s_DefaultProfile;
	}

	void SetDefaultVolumeProfilePath(const std::string& path)
	{
		if (!s_Loaded)
			Load();
		s_DefaultProfile = path;
		Save();
		EditorLog::Write("Volume", "default volume profile = %s", path.empty() ? "(none)" : path.c_str());
	}

	std::shared_ptr<VolumeProfile> DefaultVolumeProfile()
	{
		const std::string& path = DefaultVolumeProfilePath();
		auto profile = path.empty() ? nullptr : VolumeProfile::Load(path);
		// 나중에 생긴 효과(예: Shadows)가 예전 기본 프로파일에 없으면 기본값으로 채워 둔다
		if (profile)
		{
			bool added = false;
			for (const std::string& type : VolumeComponent::Types())
				if (!profile->Has(type)) { profile->Add(type); added = true; }
			if (added)
				profile->Save();
		}
		return profile;
	}

	std::shared_ptr<VolumeProfile> EnsureDefaultVolumeProfile()
	{
		if (auto existing = DefaultVolumeProfile())
			return existing;
		const std::string path = VolumeProfile::CreateAsset("Assets\\Settings\\", "DefaultVolumeProfile");
		auto profile = VolumeProfile::Load(path);
		if (profile == nullptr)
			return nullptr;
		// Unity 처럼 모든 효과를 기본값으로 담아 둔다 (여기 값이 모든 씬의 기준값)
		for (const std::string& type : VolumeComponent::Types())
			profile->Add(type);
		profile->Save();
		SetDefaultVolumeProfilePath(path);
		return profile;
	}

	std::string EnsureSampleSceneProfile()
	{
		const std::string path = "Assets\\Settings\\SampleSceneProfile.volumeprofile";
		std::error_code ec;
		if (std::filesystem::exists(PathManager::GetI()->GetMovePathW(string_to_wstring(path)), ec))
			return path;
		const std::string created = VolumeProfile::CreateAsset("Assets\\Settings\\", "SampleSceneProfile");
		auto profile = VolumeProfile::Load(created);
		if (profile == nullptr)
			return std::string();
		auto set = [](VolumeComponent* c, const char* key, float v) {
			if (VolumeParameter* p = c ? c->Find(key) : nullptr)
			{
				p->Override = true;
				p->Value[0] = v;
			}
		};
		// Bloom: 밝은 곳(더하기 입자, 빛나는 색)이 번진다
		VolumeComponent* bloom = profile->Add("Bloom");
		set(bloom, "threshold", 0.9f);
		set(bloom, "intensity", 1.0f);
		set(bloom, "scatter", 0.7f);
		set(profile->Add("Tonemapping"), "mode", 1.0f);   // Neutral
		set(profile->Add("Vignette"), "intensity", 0.2f);
		profile->Save();
		EditorLog::Write("Volume", "sample scene profile created %s", created.c_str());
		return created;
	}

	void Reload()
	{
		Load();
	}

	RenderingPath GetRenderingPath()
	{
		if (!s_Loaded)
			Load();
		return s_Path;
	}

	void SetRenderingPath(RenderingPath path)
	{
		if (!s_Loaded)
			Load();
		s_Path = path;
		ClusteredLighting::SetEnabled(path != RenderingPath::Forward);   // Forward = 앞의 빛 4 개만 (URP 의 Forward 처럼)
		Save();
		EditorLog::Write("Graphics", "rendering path = %s", RenderingPathName(path));
	}

	const char* RenderingPathName(RenderingPath path)
	{
		switch (path)
		{
		case RenderingPath::Forward: return "Forward";
		case RenderingPath::Deferred: return "Deferred";
		default: return "Forward+";
		}
	}

	bool RenderingPathFromName(const std::string& name, RenderingPath& out)
	{
		std::string n;
		for (char c : name) if (c != ' ' && c != '_' && c != '-') n += (char)tolower((unsigned char)c);
		if (n == "forward") { out = RenderingPath::Forward; return true; }
		if (n == "forward+" || n == "forwardplus") { out = RenderingPath::ForwardPlus; return true; }
		if (n == "deferred") { out = RenderingPath::Deferred; return true; }
		return false;
	}

	void RegisterEditor()
	{
		CliServer::Register("renderpath", "Rendering Path (URP): {op: get|set, path?: Forward|Forward+|Deferred}",
			[](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
				if (args.value("op", std::string("get")) == "set")
				{
					RenderingPath p;
					const std::string name = args.value("path", args.value("value", std::string()));
					if (!RenderingPathFromName(name, p))
					{
						error = "unknown rendering path '" + name + "' (Forward, Forward+, Deferred)";
						return false;
					}
					SetRenderingPath(p);
				}
				result = { { "renderingPath", RenderingPathName(GetRenderingPath()) }, { "deferred", DeferredRenderer::Info() } };
				return true;
			});
	}
}
