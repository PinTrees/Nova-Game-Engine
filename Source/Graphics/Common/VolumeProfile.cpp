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
	VolumeParameter ShowIf(VolumeParameter p, const char* key, int value)
	{
		p.ShowIfKey = key;
		p.ShowIfValue = value;
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
		"Bloom", "ChromaticAberration", "ColorAdjustments", "DepthOfField", "FilmGrain", "MotionBlur", "Tonemapping", "Vignette", "WhiteBalance",
		"Shadows", "Fog", "Atmosphere", "IndirectLighting", "ScreenSpaceReflection", "AmbientOcclusion", "Exposure" };
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
	else if (type == "DepthOfField")
	{
		// Unity URP Depth Of Field: Gaussian (먼 곳만, 거리로) · Bokeh (카메라처럼 초점 거리 · 렌즈 · 조리개) — 기본값 그대로
		c->DisplayName = "Depth Of Field";
		c->Params = {
			PEnum("mode", "Mode", { "Off", "Gaussian", "Bokeh" }, 0),
			ShowIf(P("gaussianStart", "Start", K::Float, 10.0f, 0.0f), "mode", 1),
			ShowIf(P("gaussianEnd", "End", K::Float, 30.0f, 0.0f), "mode", 1),
			ShowIf(P("gaussianMaxRadius", "Max Radius", K::Clamped, 1.0f, 0.5f, 1.5f), "mode", 1),
			ShowIf(P("highQualitySampling", "High Quality Sampling", K::Bool, 0.0f), "mode", 1),
			ShowIf(P("focusDistance", "Focus Distance", K::Float, 10.0f, 0.1f), "mode", 2),
			ShowIf(P("focalLength", "Focal Length", K::Clamped, 50.0f, 1.0f, 300.0f), "mode", 2),
			ShowIf(P("aperture", "Aperture", K::Clamped, 5.6f, 1.0f, 32.0f), "mode", 2),
			ShowIf(P("bladeCount", "Blade Count", K::Int, 5.0f, 3.0f, 9.0f), "mode", 2),
			ShowIf(P("bladeCurvature", "Blade Curvature", K::Clamped, 1.0f, 0.0f, 1.0f), "mode", 2),
			ShowIf(P("bladeRotation", "Blade Rotation", K::Clamped, 0.0f, -180.0f, 180.0f), "mode", 2),
		};
	}
	else if (type == "MotionBlur")
	{
		// Unity URP Motion Blur (카메라 움직임 — 깊이와 지난 프레임 카메라로 화면 속도를 되살림). Scene 뷰에는 없음 (Unity 와 같음)
		c->DisplayName = "Motion Blur";
		c->Params = {
			PEnum("mode", "Mode", { "Camera Only" }, 0),
			PEnum("quality", "Quality", { "Low", "Medium", "High" }, 0),
			P("intensity", "Intensity", K::Clamped, 0.0f, 0.0f, 1.0f),
			P("clamp", "Clamp", K::Clamped, 0.05f, 0.0f, 0.2f),
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
			// 먼 캐스케이드 캐시: Staggered = 3 번째는 2 프레임, 4 번째는 4 프레임마다 다시 그림 (Slow = 4 / 8)
			PEnum("farCascadeUpdate", "Far Cascade Update", { "Every Frame", "Staggered", "Slow" }, 1),
		};
	}
	else if (type == "Fog")
	{
		// 높이 안개 (HDRP Fog + Unreal Exponential Height Fog): Base Height 위로 지수로 옅어져 Maximum Height 에서 10 %.
		//  밀도 = 1 / Fog Attenuation Distance (그 거리에서 63 % 가려짐). 색 = 하늘 색(보는 방향의 흐린 하늘) 또는 상수 색, 해 쪽은 밝게
		c->DisplayName = "Fog";
		c->Category = "Lighting";
		c->Params = {
			P("enabled", "Enable", K::Bool, 0.0f),
			P("meanFreePath", "Fog Attenuation Distance", K::Float, 400.0f, 1.0f),
			P("baseHeight", "Base Height", K::Float, 0.0f, kUnbounded),
			P("maximumHeight", "Maximum Height", K::Float, 120.0f, kUnbounded),
			P("startDistance", "Start Distance", K::Float, 0.0f, 0.0f),
			P("maxFogDistance", "Max Fog Distance", K::Float, 5000.0f, 0.0f),
			PEnum("colorMode", "Color Mode", { "Sky Color", "Constant Color" }, 0),
			PColor("color", "Color", 0.62f, 0.70f, 0.80f),
			PColor("tint", "Tint", 1.0f, 1.0f, 1.0f),
			P("maxOpacity", "Max Opacity", K::Clamped, 1.0f, 0.0f, 1.0f),
			P("sunScattering", "Sun Scattering", K::Clamped, 0.5f, 0.0f, 2.0f),
			P("anisotropy", "Anisotropy", K::Clamped, 0.6f, 0.0f, 0.95f),
		};
	}
	else if (type == "Atmosphere")
	{
		// 대기 원근 (Unreal Sky Atmosphere 의 Aerial Perspective): 멀수록 레일리(파랑)·미(뿌연 빛) 산란으로
		//  지평선 하늘 색에 가까워지고 해 쪽이 밝게 번진다. Distance Scale = 거리 배율 (작은 지도에서도 보이게)
		c->DisplayName = "Atmosphere";
		c->Category = "Lighting";
		c->Params = {
			P("enabled", "Enable", K::Bool, 0.0f),
			P("rayleigh", "Rayleigh Scattering", K::Clamped, 1.0f, 0.0f, 10.0f),
			P("mie", "Mie Scattering (Haze)", K::Clamped, 1.0f, 0.0f, 20.0f),
			P("mieAnisotropy", "Mie Anisotropy", K::Clamped, 0.8f, 0.0f, 0.99f),
			P("distanceScale", "Distance Scale", K::Float, 8.0f, 0.0f),
			P("rayleighHeight", "Rayleigh Scale Height", K::Float, 8000.0f, 1.0f),
			P("mieHeight", "Mie Scale Height", K::Float, 1200.0f, 1.0f),
			P("sunIntensity", "Sun Intensity", K::Float, 1.0f, 0.0f),
		};
	}
	else if (type == "IndirectLighting")
	{
		// 하늘 환경광 (HDRP Indirect Lighting Controller): 하늘 큐브맵에서 오는 확산광·반사의 세기와 색
		c->DisplayName = "Indirect Lighting";
		c->Category = "Lighting";
		c->Params = {
			P("indirectDiffuse", "Indirect Diffuse Intensity", K::Float, 1.0f, 0.0f),
			P("reflection", "Reflection Intensity", K::Float, 1.0f, 0.0f),
			PColor("ambientTint", "Ambient Tint", 1.0f, 1.0f, 1.0f),
		};
	}
	else if (type == "AmbientOcclusion")
	{
		// Unity URP 의 Screen Space Ambient Occlusion (HDRP 는 Volume 의 Ambient Occlusion): 깊이 프리패스의 노멀 · 깊이로
		//  모서리 · 물체가 맞닿은 곳 · 틈의 환경광을 가린다. 기본 = 켜짐 (URP 기본 렌더러처럼 — 예전부터 늘 켜져 있었다)
		c->DisplayName = "Screen Space Ambient Occlusion";
		c->Category = "Lighting";
		c->Params = {
			P("enabled", "Enable", K::Bool, 1.0f),
			P("intensity", "Intensity", K::Clamped, 1.0f, 0.0f, 4.0f),
			P("radius", "Radius", K::Clamped, 0.5f, 0.05f, 5.0f),
			P("directLightingStrength", "Direct Lighting Strength", K::Clamped, 0.25f, 0.0f, 1.0f),
			PEnum("samples", "Samples", { "Low", "Medium", "High" }, 1),
			P("falloffDistance", "Falloff Distance", K::Float, 100.0f, 0.1f),
		};
	}
	else if (type == "ScreenSpaceReflection")
	{
		// HDRP Screen Space Reflection (URP 에는 없다): 반사 광선이 깊이 프리패스에 맞으면 그 자리의 지난 프레임 장면 색 — 못 맞으면 프로브 · 하늘
		c->DisplayName = "Screen Space Reflection";
		c->Category = "Lighting";
		c->Params = {
			P("enabled", "Enable", K::Bool, 0.0f),
			PEnum("quality", "Quality", { "Low", "Medium", "High" }, 1),
			P("minSmoothness", "Minimum Smoothness", K::Clamped, 0.9f, 0.0f, 1.0f),
			P("smoothnessFadeStart", "Smoothness Fade Start", K::Clamped, 0.9f, 0.0f, 1.0f),
			P("screenFadeDistance", "Screen Edge Fade Distance", K::Clamped, 0.1f, 0.0f, 1.0f),
			P("depthBufferThickness", "Object Thickness", K::Clamped, 0.01f, 0.0f, 1.0f),
		};
	}
	else if (type == "Exposure")
	{
		// 노출 (HDRP Exposure): Fixed = Compensation(EV) 만큼, Automatic = 화면 평균 밝기를 Middle Gray 로 맞추고
		//  (Limit Min~Max EV 안) 어두워질 때·밝아질 때 속도로 천천히 따라간다 (눈의 적응)
		c->DisplayName = "Exposure";
		c->Category = "Exposure";
		c->Params = {
			PEnum("mode", "Mode", { "Fixed", "Automatic" }, 0),
			P("compensation", "Compensation", K::Float, 0.0f, kUnbounded),
			P("limitMin", "Limit Min", K::Float, -3.0f, kUnbounded),
			P("limitMax", "Limit Max", K::Float, 3.0f, kUnbounded),
			P("middleGray", "Middle Gray", K::Clamped, 0.18f, 0.02f, 0.6f),
			P("speedUp", "Speed Dark To Light", K::Float, 3.0f, 0.0f),
			P("speedDown", "Speed Light To Dark", K::Float, 1.0f, 0.0f),
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
	if (type == "DepthOfField") return c->I("mode") != 0;
	if (type == "MotionBlur") return c->F("intensity") > 0.0f;
	if (type == "FilmGrain") return c->F("intensity") > 0.0f;
	if (type == "WhiteBalance") return c->F("temperature") != 0.0f || c->F("tint") != 0.0f;
	if (type == "Fog" || type == "Atmosphere" || type == "ScreenSpaceReflection") return c->B("enabled");
	if (type == "AmbientOcclusion") return c->B("enabled") && c->F("intensity") > 0.0f;
	if (type == "Exposure") return c->I("mode") == 1 || c->F("compensation") != 0.0f;
	if (type == "IndirectLighting")
	{
		const float* t = c->V("ambientTint");
		return c->F("indirectDiffuse") != 1.0f || c->F("reflection") != 1.0f || t[0] != 1.0f || t[1] != 1.0f || t[2] != 1.0f;
	}
	if (type == "ColorAdjustments")
	{
		const float* f = c->V("colorFilter");
		return c->F("postExposure") != 0.0f || c->F("contrast") != 0.0f || c->F("hueShift") != 0.0f || c->F("saturation") != 0.0f
			|| f[0] != 1.0f || f[1] != 1.0f || f[2] != 1.0f;
	}
	return false;
}
