// com.nova.weather 진입점: .weather 에셋 · CLI (nova weather) + C# API (Runtime/Weather.cs 가 DllImport("NovaWeather") 로 부른다)
#include "pch.h"
#include "WeatherController.h"
#include "WeatherFx.h"
#include "WeatherState.h"
#include "CliServer.h"
#include "EditorExtensions.h"
#include "UnityGUI.h"

NOVA_PACKAGE_EXPORT const char* NovaPackage_Abi() { return NOVA_PACKAGE_ABI_VERSION; }

namespace
{
	constexpr const char* kPackage = "com.nova.weather";

	json State(const WeatherController* w)
	{
		const WeatherParams& p = w->Current();
		const WeatherState& s = WeatherState::Get();
		json j = p.ToJson();
		j.erase("nova_weather_profile");
		j["profile"] = w->TargetProfile();
		j["progress"] = w->TransitionProgress();
		j["strikes"] = w->StrikeCount();
		j["instances"] = WeatherController::InstanceCount();
		j["flash"] = s.Flash;
		j["sunIntensity"] = s.SunIntensity;
		j["windStrength"] = s.WindStrength;
		float levels[3];
		const bool playing = w->SoundLevels(levels);
		j["sound"] = { { "playing", playing }, { "rainLight", levels[0] }, { "rainHeavy", levels[1] }, { "wind", levels[2] } };
		if (!w->ProfileError().empty())
			j["error"] = w->ProfileError();
		return j;
	}

	// .weather 에셋 Inspector: 값을 바꾸면 바로 저장 (장면의 Weather Controller 가 그 프로필이면 다시 넘어간다)
	void DrawProfileInspector(const std::string& path)
	{
		static std::string s_Path;
		static WeatherParams s_P;
		static std::string s_Error;
		if (s_Path != path)
		{
			s_Path = path;
			s_Error.clear();
			if (!WeatherProfiles::Find(path, s_P, s_Error))
				s_P = WeatherParams();
		}
		UnityGUI::Label("Weather Profile", 0, true);
		if (!s_Error.empty())
			UnityGUI::HelpBox(s_Error.c_str(), true);
		bool changed = false;
		changed |= UnityGUI::Slider("Rain", &s_P.Rain, 0.0f, 1.0f);
		changed |= UnityGUI::Slider("Snow", &s_P.Snow, 0.0f, 1.0f);
		changed |= UnityGUI::Slider("Wind (m/s)", &s_P.Wind, 0.0f, 30.0f);
		changed |= UnityGUI::Slider("Wind Direction", &s_P.WindDirection, 0.0f, 360.0f);
		changed |= UnityGUI::Slider("Gust", &s_P.Gust, 0.0f, 1.0f);
		changed |= UnityGUI::Slider("Clouds", &s_P.Clouds, 0.0f, 1.0f);
		changed |= UnityGUI::Slider("Fog", &s_P.Fog, 0.0f, 1.0f);
		changed |= UnityGUI::Slider("Fog Distance", &s_P.FogDistance, 20.0f, 2000.0f);
		changed |= UnityGUI::Slider("Lightning (per min)", &s_P.Lightning, 0.0f, 30.0f);
		changed |= UnityGUI::Slider("Wetness", &s_P.Wetness, 0.0f, 1.0f);
		changed |= UnityGUI::Slider("Snow Cover", &s_P.SnowCover, 0.0f, 1.0f);
		auto save = [&] {
			if (!WeatherProfiles::Save(path, s_P, s_Error))
				return;
			WeatherController* w = WeatherController::Active();
			if (w && w->TargetProfile() == path)
				w->SetProfile(path, 0.0f);   // 고치는 대로 장면에 바로
		};
		if (changed)
			save();
		UnityGUI::Spacing(6.0f);
		UnityGUI::Label("Copy a preset", 0, false);
		for (const auto& [name, p] : WeatherProfiles::Presets())
		{
			if (&name != &WeatherProfiles::Presets().front().first)
				ImGui::SameLine();
			if (ImGui::SmallButton(name.c_str()))
			{
				const float dir = s_P.WindDirection;
				s_P = p;
				s_P.WindDirection = dir;
				save();
			}
		}
	}
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnLoad()
{
	// Project 창: Create > Weather Profile (.weather)
	EditorExtensions::AssetType t;
	t.Owner = kPackage;
	t.Extension = WeatherProfiles::kExtension;
	t.Icon = "volume";
	t.CreateMenu = "Weather Profile";
	t.DefaultName = "New Weather Profile";
	t.Create = [](const std::string& path) {
		WeatherParams p;
		std::string e;
		WeatherProfiles::Find("Rain", p, e);   // 비 프로필에서 시작
		WeatherProfiles::Save(path, p, e);
	};
	t.Inspector = [](const std::string& path) { DrawProfileInspector(path); };
	EditorExtensions::RegisterAssetType(t);

	// CLI: nova weather <op> [--인자 …]
	CliServer::Register("weather", "weather op: {op: status|set|strike|list|save, profile?, seconds?, path?} (nova weather status)",
		[](const json& args, json& result, std::string& error) {
			const std::string op = args.value("op", std::string("status"));
			if (op == "list")
			{
				json presets = json::array();
				for (const auto& [n, _] : WeatherProfiles::Presets()) presets.push_back(n);
				result = { { "presets", presets }, { "assets", WeatherProfiles::FindAssets() } };
				return true;
			}
			WeatherController* w = WeatherController::Active();
			if (w == nullptr)
			{
				error = "no Weather Controller in the scene (create one: nova create --type Empty, nova add-component --type WeatherController)";
				return false;
			}
			if (op == "status")
			{
				result = State(w);
				return true;
			}
			if (op == "set")
			{
				const std::string profile = args.value("profile", std::string());
				if (profile.empty()) { error = "set needs --profile (Clear, Cloudy, Rain, Storm, Snow, Blizzard or a .weather path)"; return false; }
				const float seconds = args.contains("seconds") ? args["seconds"].get<float>() : -1.0f;
				if (!w->SetProfile(profile, seconds, &error))
					return false;
				result = State(w);
				return true;
			}
			if (op == "strike")
			{
				w->Strike();
				result = State(w);
				return true;
			}
			if (op == "save")
			{
				const std::string path = args.value("path", std::string());
				if (path.empty()) { error = "save needs --path Assets/....weather"; return false; }
				if (!WeatherProfiles::Save(path, w->Current(), error))
					return false;
				result = { { "path", path } };
				return true;
			}
			error = "unknown op '" + op + "' (status, set, strike, list, save)";
			return false;
		});
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnUnload()
{
	CliServer::Unregister("weather");
	EditorExtensions::UnregisterOwner(kPackage);
	WeatherFx::ReleaseAsset();
	WeatherState::Reset();
}

// ---- C# API (Weather 정적 클래스): 장면의 Weather Controller 하나를 다룬다
NOVA_PACKAGE_EXPORT int NovaWeather_Exists()
{
	return WeatherController::Active() != nullptr;
}

NOVA_PACKAGE_EXPORT int NovaWeather_Set(const char* profile, float seconds)
{
	WeatherController* w = WeatherController::Active();
	return w && profile && w->SetProfile(profile, seconds) ? 1 : 0;
}

NOVA_PACKAGE_EXPORT const char* NovaWeather_GetProfile()
{
	static std::string s;
	WeatherController* w = WeatherController::Active();
	s = w ? w->TargetProfile() : std::string();
	return s.c_str();
}

// 0 rain, 1 snow, 2 wind, 3 windDirection, 4 gust, 5 clouds, 6 fog, 7 fogDistance, 8 lightning, 9 wetness, 10 snowCover, 11 전환 진행 (0..1)
NOVA_PACKAGE_EXPORT float NovaWeather_GetFloat(int prop)
{
	WeatherController* w = WeatherController::Active();
	if (w == nullptr) return 0.0f;
	const WeatherParams& p = w->Current();
	switch (prop)
	{
	case 0: return p.Rain;
	case 1: return p.Snow;
	case 2: return p.Wind;
	case 3: return p.WindDirection;
	case 4: return p.Gust;
	case 5: return p.Clouds;
	case 6: return p.Fog;
	case 7: return p.FogDistance;
	case 8: return p.Lightning;
	case 9: return p.Wetness;
	case 10: return p.SnowCover;
	default: return w->TransitionProgress();
	}
}

NOVA_PACKAGE_EXPORT void NovaWeather_Strike()
{
	if (WeatherController* w = WeatherController::Active())
		w->Strike();
}
