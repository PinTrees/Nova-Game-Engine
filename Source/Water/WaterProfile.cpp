#include "pch.h"
#include "WaterProfile.h"
#include <filesystem>
#include <fstream>

namespace
{
	const char* kDir = "Resources\\Packages\\Water\\Profiles";
	std::vector<WaterProfile> s_List;
	bool s_Loaded = false;

	void Color3(const nlohmann::json& j, const char* key, float out[3])
	{
		if (j.contains(key) && j[key].is_array() && j[key].size() >= 3)
			for (int i = 0; i < 3; ++i)
				out[i] = j[key][i].get<float>();
	}

	void Load()
	{
		s_List.clear();
		s_Loaded = true;
		std::error_code ec;
		const std::wstring dir = PathManager::GetI()->GetMovePathW(string_to_wstring(kDir));
		std::vector<std::filesystem::path> files;
		for (const auto& e : std::filesystem::directory_iterator(dir, ec))
			if (e.is_regular_file() && e.path().extension() == L".waterprofile")
				files.push_back(e.path());
		std::sort(files.begin(), files.end());
		for (const auto& f : files)
		{
			std::ifstream is(f);
			const nlohmann::json j = nlohmann::json::parse(is, nullptr, false);
			if (!j.is_object())
			{
				EditorLog::Write("Water", "profile parse failed: %s", f.string().c_str());
				continue;
			}
			WaterProfile p;
			p.FromJson(j);
			if (p.Name.empty())
				p.Name = f.stem().string();
			p.Path = std::string(kDir) + "\\" + f.filename().string();
			s_List.push_back(std::move(p));
		}
		EditorLog::Write("Water", "water profiles: %zu", s_List.size());
	}
}

nlohmann::json WaterProfile::ToJson() const
{
	auto c3 = [](const float c[3]) { return std::vector<float>(c, c + 3); };
	return {
		{ "name", Name }, { "description", Description },
		{ "absorption", c3(Absorption) }, { "clarity", Clarity }, { "scatterColor", c3(ScatterColor) }, { "turbidity", Turbidity },
		{ "subsurfaceColor", c3(SubsurfaceColor) }, { "subsurface", Subsurface },
		{ "windSpeed", WindSpeed }, { "windDirection", WindDirection }, { "choppiness", Choppiness }, { "waveScale", WaveScale },
		{ "minWavelength", MinWavelength }, { "maxWavelength", MaxWavelength }, { "spread", Spread }, { "waveCount", WaveCount }, { "seed", Seed },
		{ "normalStrength", NormalStrength }, { "normalTiling", NormalTiling }, { "normalSpeed", NormalSpeed },
		{ "foamAmount", FoamAmount }, { "shoreFoam", ShoreFoam }, { "foamTiling", FoamTiling }, { "shoreWaveHeight", ShoreWaveHeight },
		{ "smoothness", Smoothness }, { "reflection", Reflection }, { "refraction", Refraction },
		{ "caustics", Caustics }, { "causticsDepth", CausticsDepth }, { "causticsTiling", CausticsTiling },
		{ "flowSpeed", FlowSpeed } };
}

void WaterProfile::FromJson(const nlohmann::json& j)
{
	Name = j.value("name", Name);
	Description = j.value("description", Description);
	Color3(j, "absorption", Absorption);
	Clarity = j.value("clarity", Clarity);
	Color3(j, "scatterColor", ScatterColor);
	Turbidity = j.value("turbidity", Turbidity);
	Color3(j, "subsurfaceColor", SubsurfaceColor);
	Subsurface = j.value("subsurface", Subsurface);
	WindSpeed = j.value("windSpeed", WindSpeed);
	WindDirection = j.value("windDirection", WindDirection);
	Choppiness = j.value("choppiness", Choppiness);
	WaveScale = j.value("waveScale", WaveScale);
	MinWavelength = j.value("minWavelength", MinWavelength);
	MaxWavelength = j.value("maxWavelength", MaxWavelength);
	Spread = j.value("spread", Spread);
	WaveCount = std::clamp(j.value("waveCount", WaveCount), 0, 32);
	Seed = j.value("seed", Seed);
	NormalStrength = j.value("normalStrength", NormalStrength);
	NormalTiling = j.value("normalTiling", NormalTiling);
	NormalSpeed = j.value("normalSpeed", NormalSpeed);
	FoamAmount = j.value("foamAmount", FoamAmount);
	ShoreFoam = j.value("shoreFoam", ShoreFoam);
	FoamTiling = j.value("foamTiling", FoamTiling);
	ShoreWaveHeight = j.value("shoreWaveHeight", ShoreWaveHeight);
	Smoothness = j.value("smoothness", Smoothness);
	Reflection = j.value("reflection", Reflection);
	Refraction = j.value("refraction", Refraction);
	Caustics = j.value("caustics", Caustics);
	CausticsDepth = j.value("causticsDepth", CausticsDepth);
	CausticsTiling = j.value("causticsTiling", CausticsTiling);
	FlowSpeed = j.value("flowSpeed", FlowSpeed);
}

namespace WaterProfiles
{
	const std::vector<WaterProfile>& List()
	{
		if (!s_Loaded)
			Load();
		return s_List;
	}

	const WaterProfile* Find(const std::string& name)
	{
		for (const WaterProfile& p : List())
			if (p.Name == name)
				return &p;
		return nullptr;
	}

	const WaterProfile& Get(const std::string& name)
	{
		static const WaterProfile s_Default;
		if (const WaterProfile* p = Find(name))
			return *p;
		return List().empty() ? s_Default : List().front();
	}

	void Reload()
	{
		Load();
	}
}
