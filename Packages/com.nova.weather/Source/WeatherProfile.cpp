#include "pch.h"
#include "WeatherProfile.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

WeatherParams WeatherParams::Lerp(const WeatherParams& a, const WeatherParams& b, float t)
{
	auto l = [t](float x, float y) { return x + (y - x) * t; };
	WeatherParams r;
	r.Rain = l(a.Rain, b.Rain);
	r.Snow = l(a.Snow, b.Snow);
	r.Wind = l(a.Wind, b.Wind);
	// 방향은 짧은 쪽으로 돈다
	float d = fmodf(b.WindDirection - a.WindDirection + 540.0f, 360.0f) - 180.0f;
	r.WindDirection = a.WindDirection + d * t;
	r.Gust = l(a.Gust, b.Gust);
	r.Clouds = l(a.Clouds, b.Clouds);
	r.Fog = l(a.Fog, b.Fog);
	r.FogDistance = l(a.FogDistance, b.FogDistance);
	r.Lightning = l(a.Lightning, b.Lightning);
	r.Wetness = l(a.Wetness, b.Wetness);
	r.SnowCover = l(a.SnowCover, b.SnowCover);
	return r;
}

nlohmann::json WeatherParams::ToJson() const
{
	return { { "nova_weather_profile", 1 }, { "rain", Rain }, { "snow", Snow }, { "wind", Wind }, { "windDirection", WindDirection }, { "gust", Gust },
		{ "clouds", Clouds }, { "fog", Fog }, { "fogDistance", FogDistance }, { "lightning", Lightning }, { "wetness", Wetness }, { "snowCover", SnowCover } };
}

WeatherParams WeatherParams::FromJson(const nlohmann::json& j)
{
	WeatherParams p;
	auto f = [&](const char* k, float& v, float lo, float hi) { if (j.contains(k) && j[k].is_number()) v = std::clamp(j[k].get<float>(), lo, hi); };
	f("rain", p.Rain, 0, 1);
	f("snow", p.Snow, 0, 1);
	f("wind", p.Wind, 0, 40);
	f("windDirection", p.WindDirection, -360, 360);
	f("gust", p.Gust, 0, 1);
	f("clouds", p.Clouds, 0, 1);
	f("fog", p.Fog, 0, 1);
	f("fogDistance", p.FogDistance, 5, 5000);
	f("lightning", p.Lightning, 0, 60);
	f("wetness", p.Wetness, 0, 1);
	f("snowCover", p.SnowCover, 0, 1);
	return p;
}

namespace WeatherProfiles
{
	const std::vector<std::pair<std::string, WeatherParams>>& Presets()
	{
		static const std::vector<std::pair<std::string, WeatherParams>> s = [] {
			auto make = [](float rain, float snow, float wind, float gust, float clouds, float fog, float fogDist, float lightning, float wet, float cover) {
				WeatherParams p;
				p.Rain = rain; p.Snow = snow; p.Wind = wind; p.Gust = gust; p.Clouds = clouds; p.Fog = fog; p.FogDistance = fogDist;
				p.Lightning = lightning; p.Wetness = wet; p.SnowCover = cover;
				return p;
			};
			return std::vector<std::pair<std::string, WeatherParams>>{
				{ "Clear", make(0.0f, 0.0f, 2.0f, 0.2f, 0.0f, 0.0f, 600.0f, 0.0f, 0.0f, 0.0f) },
				{ "Cloudy", make(0.0f, 0.0f, 4.0f, 0.35f, 0.6f, 0.12f, 500.0f, 0.0f, 0.0f, 0.0f) },
				{ "Rain", make(0.55f, 0.0f, 5.0f, 0.35f, 0.78f, 0.35f, 320.0f, 0.0f, 0.8f, 0.0f) },
				{ "Storm", make(1.0f, 0.0f, 13.0f, 0.75f, 1.0f, 0.55f, 170.0f, 7.0f, 1.0f, 0.0f) },
				{ "Snow", make(0.0f, 0.55f, 2.5f, 0.3f, 0.65f, 0.4f, 260.0f, 0.0f, 0.0f, 1.0f) },
				{ "Blizzard", make(0.0f, 1.0f, 15.0f, 0.8f, 0.9f, 0.85f, 70.0f, 0.0f, 0.0f, 1.0f) },
			};
		}();
		return s;
	}

	namespace
	{
		std::string Lower(std::string s)
		{
			for (char& c : s) c = (char)tolower((unsigned char)c);
			return s;
		}

		std::wstring Full(const std::string& path)
		{
			const std::wstring w = string_to_wstring(path);
			return fs::path(w).is_absolute() ? w : PathManager::GetI()->GetMovePathW(w);
		}
	}

	bool Find(const std::string& nameOrPath, WeatherParams& out, std::string& error)
	{
		for (const auto& [name, p] : Presets())
			if (Lower(name) == Lower(nameOrPath))
			{
				out = p;
				return true;
			}
		if (Lower(fs::path(nameOrPath).extension().string()) != kExtension)
		{
			error = "no weather profile '" + nameOrPath + "' (Clear, Cloudy, Rain, Storm, Snow, Blizzard or a .weather file)";
			return false;
		}
		std::ifstream in(Full(nameOrPath), std::ios::binary);
		if (!in)
		{
			error = "cannot read " + nameOrPath;
			return false;
		}
		std::stringstream ss;
		ss << in.rdbuf();
		const nlohmann::json j = nlohmann::json::parse(ss.str(), nullptr, false);
		if (j.is_discarded() || !j.is_object())
		{
			error = nameOrPath + ": invalid JSON";
			return false;
		}
		out = WeatherParams::FromJson(j);
		return true;
	}

	bool Save(const std::string& path, const WeatherParams& p, std::string& error)
	{
		const std::wstring full = Full(path);
		std::error_code ec;
		fs::create_directories(fs::path(full).parent_path(), ec);
		std::ofstream os(full, std::ios::binary | std::ios::trunc);
		if (!os)
		{
			error = "cannot write " + path;
			return false;
		}
		os << p.ToJson().dump(2);
		return true;
	}

	std::vector<std::string> FindAssets()
	{
		std::vector<std::string> found;
		const fs::path root = PathManager::GetI()->GetContentPathW();
		std::error_code ec;
		for (fs::recursive_directory_iterator it(root / L"Assets", fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
		{
			if (ec) break;
			if (it->is_regular_file(ec) && Lower(it->path().extension().string()) == kExtension)
				found.push_back(wstring_to_string(fs::relative(it->path(), root, ec).wstring()));
		}
		std::sort(found.begin(), found.end());
		return found;
	}
}
