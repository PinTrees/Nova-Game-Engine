#include "pch.h"
#include "VolumeProfile.h"
#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{
	constexpr float kUnbounded = -1.0e9f;

	VolumeParameter P(const char* key, const char* label, VolumeParamKind kind, float v0, float minV = 0.0f, float maxV = 1.0f)
	{
		VolumeParameter p;
		p.Key = key;
		p.Label = label;
		p.Kind = kind;
		p.Min = minV;
		p.Max = maxV;
		p.Value[0] = v0;
		return p;
	}
	VolumeParameter PColor(const char* key, const char* label, float r, float g, float b)
	{
		VolumeParameter p = P(key, label, VolumeParamKind::Color, r);
		p.Value[1] = g; p.Value[2] = b; p.Value[3] = 1.0f;
		return p;
	}
	VolumeParameter PEnum(const char* key, const char* label, std::vector<std::string> items, int v)
	{
		VolumeParameter p = P(key, label, VolumeParamKind::Enum, (float)v, 0.0f, (float)items.size() - 1);
		p.Items = std::move(items);
		return p;
	}

	std::string NormalizePath(std::string path)
	{
		std::replace(path.begin(), path.end(), '/', '\\');
		if (!fs::path(path).is_absolute())
		{
			const size_t first = path.find_first_not_of('\\');
			path.erase(0, first == std::string::npos ? path.size() : first);
		}
		return path;
	}

	std::wstring FilePath(const std::string& path)
	{
		fs::path p(string_to_wstring(path));
		return p.is_absolute() ? p.wstring() : PathManager::GetI()->GetMovePathW(string_to_wstring(path));
	}

	struct CacheEntry
	{
		std::shared_ptr<VolumeProfile> Profile;
		fs::file_time_type Stamp;
	};
	std::map<std::string, CacheEntry>& Cache()
	{
		static std::map<std::string, CacheEntry> cache;
		return cache;
	}

	fs::file_time_type StampOf(const std::string& path)
	{
		std::error_code ec;
		auto t = fs::last_write_time(FilePath(path), ec);
		return ec ? fs::file_time_type() : t;
	}
}

// ================================================================== VolumeComponent
VolumeParameter* VolumeComponent::Find(const std::string& key)
{
	for (VolumeParameter& p : Params)
		if (p.Key == key)
			return &p;
	return nullptr;
}

const VolumeParameter* VolumeComponent::Find(const std::string& key) const
{
	for (const VolumeParameter& p : Params)
		if (p.Key == key)
			return &p;
	return nullptr;
}

float VolumeComponent::F(const std::string& key) const { const VolumeParameter* p = Find(key); return p ? p->F() : 0.0f; }
bool VolumeComponent::B(const std::string& key) const { const VolumeParameter* p = Find(key); return p && p->B(); }
int VolumeComponent::I(const std::string& key) const { const VolumeParameter* p = Find(key); return p ? p->I() : 0; }
const float* VolumeComponent::V(const std::string& key) const
{
	static const float kZero[4] = {};
	const VolumeParameter* p = Find(key);
	return p ? p->Value : kZero;
}

const std::vector<std::string>& VolumeComponent::Types()
{
	static const std::vector<std::string> kTypes = {
		"Bloom", "ChromaticAberration", "ColorAdjustments", "FilmGrain", "Tonemapping", "Vignette", "WhiteBalance",
		"Shadows" };
	return kTypes;
}

std::unique_ptr<VolumeComponent> VolumeComponent::Create(const std::string& type)
{
	using K = VolumeParamKind;
	auto c = std::make_unique<VolumeComponent>();
	c->Type = type;
	c->Category = "Post-processing";
	if (type == "Bloom")
	{
		// Unity URP Bloom 기본값
		c->DisplayName = "Bloom";
		c->Params = {
			P("threshold", "Threshold", K::Float, 0.9f, 0.0f),
			P("intensity", "Intensity", K::Float, 0.0f, 0.0f),
			P("scatter", "Scatter", K::Clamped, 0.7f, 0.0f, 1.0f),
			PColor("tint", "Tint", 1.0f, 1.0f, 1.0f),
			P("clamp", "Clamp", K::Float, 65472.0f, 0.0f),
			P("highQualityFiltering", "High Quality Filtering", K::Bool, 0.0f),
			PEnum("downscale", "Downscale", { "Half", "Quarter" }, 0),
			P("maxIterations", "Max Iterations", K::Int, 6.0f, 2.0f, 8.0f),
		};
	}
	else if (type == "Tonemapping")
	{
		c->DisplayName = "Tonemapping";
		c->Params = { PEnum("mode", "Mode", { "None", "Neutral", "ACES" }, 0) };
	}
	else if (type == "ColorAdjustments")
	{
		c->DisplayName = "Color Adjustments";
		c->Params = {
			P("postExposure", "Post Exposure", K::Float, 0.0f, kUnbounded),
			P("contrast", "Contrast", K::Clamped, 0.0f, -100.0f, 100.0f),
			PColor("colorFilter", "Color Filter", 1.0f, 1.0f, 1.0f),
			P("hueShift", "Hue Shift", K::Clamped, 0.0f, -180.0f, 180.0f),
			P("saturation", "Saturation", K::Clamped, 0.0f, -100.0f, 100.0f),
		};
	}
	else if (type == "WhiteBalance")
	{
		c->DisplayName = "White Balance";
		c->Params = {
			P("temperature", "Temperature", K::Clamped, 0.0f, -100.0f, 100.0f),
			P("tint", "Tint", K::Clamped, 0.0f, -100.0f, 100.0f),
		};
	}
	else if (type == "Vignette")
	{
		c->DisplayName = "Vignette";
		VolumeParameter center = P("center", "Center", K::Vector2, 0.5f);
		center.Value[1] = 0.5f;
		c->Params = {
			PColor("color", "Color", 0.0f, 0.0f, 0.0f),
			center,
			P("intensity", "Intensity", K::Clamped, 0.0f, 0.0f, 1.0f),
			P("smoothness", "Smoothness", K::Clamped, 0.2f, 0.01f, 1.0f),
			P("rounded", "Rounded", K::Bool, 0.0f),
		};
	}
	else if (type == "ChromaticAberration")
	{
		c->DisplayName = "Chromatic Aberration";
		c->Params = { P("intensity", "Intensity", K::Clamped, 0.0f, 0.0f, 1.0f) };
	}
	else if (type == "Shadows")
	{
		// 그림자 (URP 파이프라인 에셋의 Shadows 항목 + HDRP 의 Shadows Volume 처럼 장소마다 바꿀 수 있게)
		//  Split 은 Max Distance 에 대한 비율 (캐스케이드 수가 적으면 앞쪽 것만 쓴다)
		c->DisplayName = "Shadows";
		c->Category = "Shadowing";
		c->Params = {
			P("maxDistance", "Max Distance", K::Float, 50.0f, 0.0f),
			P("cascadeCount", "Cascade Count", K::Int, 4.0f, 1.0f, 4.0f),
			P("split1", "Split 1", K::Clamped, 0.067f, 0.0f, 1.0f),
			P("split2", "Split 2", K::Clamped, 0.2f, 0.0f, 1.0f),
			P("split3", "Split 3", K::Clamped, 0.467f, 0.0f, 1.0f),
			P("lastBorder", "Last Border", K::Clamped, 0.2f, 0.0f, 1.0f),
			PEnum("resolution", "Resolution", { "512", "1024", "2048", "4096" }, 2),
			P("depthBias", "Depth Bias", K::Clamped, 1.0f, 0.0f, 10.0f),
			P("normalBias", "Normal Bias", K::Clamped, 1.0f, 0.0f, 10.0f),
			P("softShadows", "Soft Shadows", K::Bool, 1.0f),
			PEnum("softQuality", "Quality", { "Low", "Medium", "High" }, 1),
		};
	}
	else if (type == "FilmGrain")
	{
		c->DisplayName = "Film Grain";
		c->Params = {
			PEnum("type", "Type", { "Thin 1", "Thin 2", "Medium 1", "Medium 2", "Medium 3", "Medium 4", "Medium 5", "Medium 6", "Large 01", "Large 02" }, 0),
			P("intensity", "Intensity", K::Clamped, 0.0f, 0.0f, 1.0f),
			P("response", "Response", K::Clamped, 0.8f, 0.0f, 1.0f),
		};
	}
	else
		return nullptr;
	return c;
}

// ================================================================== VolumeProfile
VolumeComponent* VolumeProfile::Get(const std::string& type)
{
	for (auto& c : Components)
		if (c->Type == type)
			return c.get();
	return nullptr;
}

const VolumeComponent* VolumeProfile::Get(const std::string& type) const
{
	for (const auto& c : Components)
		if (c->Type == type)
			return c.get();
	return nullptr;
}

VolumeComponent* VolumeProfile::Add(const std::string& type)
{
	if (VolumeComponent* existing = Get(type))
		return existing;   // Unity 도 같은 효과는 하나만
	auto c = VolumeComponent::Create(type);
	if (!c)
		return nullptr;
	Components.push_back(std::move(c));
	return Components.back().get();
}

void VolumeProfile::Remove(const std::string& type)
{
	Components.erase(std::remove_if(Components.begin(), Components.end(),
		[&](const std::unique_ptr<VolumeComponent>& c) { return c->Type == type; }), Components.end());
}

std::string VolumeProfile::Name() const
{
	return fs::path(Path).stem().string();
}

json VolumeProfile::ToJson() const
{
	json j;
	j["nova_volume_profile"] = 1;
	json comps = json::array();
	for (const auto& c : Components)
	{
		json jc;
		jc["type"] = c->Type;
		jc["active"] = c->Active;
		json params = json::object();
		for (const VolumeParameter& p : c->Params)
			params[p.Key] = { { "override", p.Override }, { "value", { p.Value[0], p.Value[1], p.Value[2], p.Value[3] } } };
		jc["params"] = params;
		comps.push_back(jc);
	}
	j["components"] = comps;
	return j;
}

void VolumeProfile::FromJson(const json& j)
{
	Components.clear();
	if (!j.is_object() || !j.contains("components") || !j["components"].is_array())
		return;
	for (const json& jc : j["components"])
	{
		auto c = VolumeComponent::Create(jc.value("type", std::string()));
		if (!c)
			continue;
		c->Active = jc.value("active", true);
		if (jc.contains("params") && jc["params"].is_object())
			for (VolumeParameter& p : c->Params)
			{
				if (!jc["params"].contains(p.Key))
					continue;
				const json& jp = jc["params"][p.Key];
				p.Override = jp.value("override", false);
				if (jp.contains("value") && jp["value"].is_array())
					for (int i = 0; i < 4 && i < (int)jp["value"].size(); ++i)
						p.Value[i] = jp["value"][i].get<float>();
			}
		Components.push_back(std::move(c));
	}
}

void VolumeProfile::ApplyJson(const std::string& text)
{
	json j = json::parse(text, nullptr, false);
	if (!j.is_discarded())
		FromJson(j);
}

bool VolumeProfile::Save() const
{
	const std::wstring file = FilePath(Path);
	std::error_code ec;
	fs::create_directories(fs::path(file).parent_path(), ec);
	std::ofstream os(file, std::ios::binary | std::ios::trunc);
	if (!os)
		return false;
	os << ToJson().dump(4);
	os.close();
	// 방금 쓴 파일을 "밖에서 바뀜" 으로 보고 다시 읽지 않도록 시각을 갱신
	auto it = Cache().find(Path);
	if (it != Cache().end())
		it->second.Stamp = StampOf(Path);
	return true;
}

std::shared_ptr<VolumeProfile> VolumeProfile::Load(const std::string& rawPath)
{
	const std::string path = NormalizePath(rawPath);
	if (path.empty())
		return nullptr;
	const fs::file_time_type stamp = StampOf(path);
	auto it = Cache().find(path);
	if (it != Cache().end() && it->second.Stamp == stamp)
		return it->second.Profile;

	std::ifstream in(FilePath(path), std::ios::binary);
	if (!in)
		return it != Cache().end() ? it->second.Profile : nullptr;
	json j = json::parse(in, nullptr, false);
	if (j.is_discarded())
		return it != Cache().end() ? it->second.Profile : nullptr;

	// 같은 객체를 유지한다 (Volume 과 Inspector 가 들고 있는 포인터가 계속 유효하게)
	std::shared_ptr<VolumeProfile> profile = it != Cache().end() ? it->second.Profile : std::make_shared<VolumeProfile>();
	profile->Path = path;
	profile->FromJson(j);
	Cache()[path] = { profile, stamp };
	EditorLog::Write("Volume", "profile loaded %s (%d overrides)", path.c_str(), (int)profile->Components.size());
	return profile;
}

std::string VolumeProfile::CreateAsset(const std::string& directoryOrPath, const std::string& baseName)
{
	fs::path dir(string_to_wstring(directoryOrPath));
	if (!dir.is_absolute())
		dir = PathManager::GetI()->GetMovePathW(string_to_wstring(directoryOrPath));
	if (dir.has_extension())
		dir = dir.parent_path();
	fs::path file = dir / (string_to_wstring(baseName) + L".volumeprofile");
	for (int i = 1; fs::exists(file); ++i)
		file = dir / (string_to_wstring(baseName) + L" " + std::to_wstring(i) + L".volumeprofile");

	VolumeProfile profile;
	profile.Path = NormalizePath(wstring_to_string(PathManager::GetI()->GetCutSolutionPath(file.lexically_normal().wstring())));
	profile.Save();
	EditorLog::Write("Volume", "profile created %s", profile.Path.c_str());
	return profile.Path;
}

// ================================================================== VolumeStack
void VolumeStack::Reset()
{
	if (Components.empty())
	{
		for (const std::string& type : VolumeComponent::Types())
			Components.push_back(VolumeComponent::Create(type));
		return;
	}
	for (auto& c : Components)
	{
		auto fresh = VolumeComponent::Create(c->Type);
		c->Params = fresh->Params;
	}
}

const VolumeComponent* VolumeStack::Get(const std::string& type) const
{
	for (const auto& c : Components)
		if (c->Type == type)
			return c.get();
	return nullptr;
}

VolumeComponent* VolumeStack::Get(const std::string& type)
{
	for (auto& c : Components)
		if (c->Type == type)
			return c.get();
	return nullptr;
}

bool VolumeStack::IsActive(const std::string& type) const
{
	const VolumeComponent* c = Get(type);
	if (!c)
		return false;
	if (type == "Bloom") return c->F("intensity") > 0.0f;
	if (type == "Tonemapping") return c->I("mode") != 0;
	if (type == "Vignette") return c->F("intensity") > 0.0f;
	if (type == "ChromaticAberration") return c->F("intensity") > 0.0f;
	if (type == "FilmGrain") return c->F("intensity") > 0.0f;
	if (type == "WhiteBalance") return c->F("temperature") != 0.0f || c->F("tint") != 0.0f;
	if (type == "ColorAdjustments")
	{
		const float* f = c->V("colorFilter");
		return c->F("postExposure") != 0.0f || c->F("contrast") != 0.0f || c->F("hueShift") != 0.0f || c->F("saturation") != 0.0f
			|| f[0] != 1.0f || f[1] != 1.0f || f[2] != 1.0f;
	}
	return false;
}
