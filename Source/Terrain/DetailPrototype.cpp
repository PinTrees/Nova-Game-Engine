#include "pch.h"
#include "DetailPrototype.h"
#include "UnityGUI.h"

namespace
{
	const char* kKinds[] = { "Grass", "Flower", "Pebble" };

	void C4(nlohmann::json& j, const char* k, const XMFLOAT4& c) { j[k] = { c.x, c.y, c.z }; }
	void R4(const nlohmann::json& j, const char* k, XMFLOAT4& c)
	{
		if (j.contains(k) && j[k].is_array() && j[k].size() >= 3)
			c = XMFLOAT4(j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>(), 1.0f);
	}
	template <typename T> void Get(const nlohmann::json& j, const char* k, T& v)
	{
		if (j.contains(k) && !j[k].is_null())
			v = j[k].get<T>();
	}
	bool ColorRow(const char* label, XMFLOAT4& c)
	{
		float rgba[4] = { c.x, c.y, c.z, 1.0f };
		if (!UnityGUI::Color(label, rgba, 1))
			return false;
		c = XMFLOAT4(rgba[0], rgba[1], rgba[2], 1.0f);
		return true;
	}
}

std::vector<std::string> DetailPrototype::ListPresets()
{
	std::vector<std::string> out;
	std::error_code ec;
	const std::wstring root = PathManager::GetI()->GetMovePathW(L"Resources\\Packages\\Terrain\\Details\\");
	for (const auto& e : std::filesystem::directory_iterator(root, ec))
		if (e.is_regular_file() && e.path().extension() == L".detail")
			out.push_back("Resources\\Packages\\Terrain\\Details\\" + wstring_to_string(e.path().filename().wstring()));
	std::sort(out.begin(), out.end());
	return out;
}

bool DetailPrototype::LoadPreset(const std::string& path, DetailPrototype& out)
{
	std::ifstream is(PathManager::GetI()->GetMovePathW(string_to_wstring(path)));
	if (!is)
		return false;
	const nlohmann::json j = nlohmann::json::parse(is, nullptr, false);
	if (!j.is_object())
		return false;
	const int seed = out.Seed;
	out = DetailPrototype();
	out.FromJson(j);
	out.Seed = seed;
	out.Invalidate();
	return true;
}

nlohmann::json DetailPrototype::ToJson() const
{
	nlohmann::json j;
	j["name"] = Name;
	nlohmann::json s;
	s["type"] = (int)Type;
	s["seed"] = Seed;
	s["height"] = Height;
	s["radius"] = Radius;
	s["blades"] = Blades;
	s["bladeWidth"] = BladeWidth;
	s["bend"] = Bend;
	s["lean"] = Lean;
	s["heads"] = Heads;
	s["petals"] = Petals;
	s["headSize"] = HeadSize;
	j["shape"] = s;
	j["minScale"] = MinScale;
	j["maxScale"] = MaxScale;
	j["widthVariation"] = WidthVariation;
	C4(j, "healthyColor", HealthyColor);
	C4(j, "dryColor", DryColor);
	C4(j, "flowerColor", FlowerColor);
	C4(j, "centerColor", CenterColor);
	j["noiseSpread"] = NoiseSpread;
	j["dryAmount"] = DryAmount;
	j["colorVariation"] = ColorVariation;
	j["tipColor"] = TipColor;
	j["translucency"] = Translucency;
	j["smoothness"] = Smoothness;
	j["density"] = Density;
	j["maxSlope"] = MaxSlope;
	j["layerMask"] = LayerMask;
	j["groundAlign"] = GroundAlign;
	j["windResponse"] = WindResponse;
	j["castShadows"] = CastShadows;
	return j;
}

void DetailPrototype::FromJson(const nlohmann::json& j)
{
	if (!j.is_object())
		return;
	Get(j, "name", Name);
	if (j.contains("shape") && j["shape"].is_object())
	{
		const auto& s = j["shape"];
		int type = (int)Type;
		Get(s, "type", type);
		Type = (Kind)std::clamp(type, 0, 2);
		Get(s, "seed", Seed);
		Get(s, "height", Height);
		Get(s, "radius", Radius);
		Get(s, "blades", Blades);
		Get(s, "bladeWidth", BladeWidth);
		Get(s, "bend", Bend);
		Get(s, "lean", Lean);
		Get(s, "heads", Heads);
		Get(s, "petals", Petals);
		Get(s, "headSize", HeadSize);
	}
	Get(j, "minScale", MinScale);
	Get(j, "maxScale", MaxScale);
	Get(j, "widthVariation", WidthVariation);
	R4(j, "healthyColor", HealthyColor);
	R4(j, "dryColor", DryColor);
	R4(j, "flowerColor", FlowerColor);
	R4(j, "centerColor", CenterColor);
	Get(j, "noiseSpread", NoiseSpread);
	Get(j, "dryAmount", DryAmount);
	Get(j, "colorVariation", ColorVariation);
	Get(j, "tipColor", TipColor);
	Get(j, "translucency", Translucency);
	Get(j, "smoothness", Smoothness);
	Get(j, "density", Density);
	Get(j, "maxSlope", MaxSlope);
	Get(j, "layerMask", LayerMask);
	Get(j, "groundAlign", GroundAlign);
	Get(j, "windResponse", WindResponse);
	Get(j, "castShadows", CastShadows);
	Blades = std::clamp(Blades, 1, 64);
	Heads = std::clamp(Heads, 0, 8);
	Petals = std::clamp(Petals, 3, 16);
	Invalidate();
}

bool DetailPrototype::DrawInspector()
{
	using namespace UnityGUI;
	const std::string before = ToJson().dump();
	TextField("Name", &Name);
	int kind = (int)Type;
	if (Dropdown("Type", &kind, kKinds, 3))
		Type = (Kind)kind;
	Int("Seed", &Seed, 0);
	Label("Shape", 0, true);
	const bool pebble = Type == Kind::Pebble;
	if (!pebble)
		Slider("Height", &Height, 0.03f, 2.5f, 1);
	Slider("Clump Radius", &Radius, 0.0f, 1.0f, 1);
	Int(pebble ? "Stones" : "Blades", &Blades, 1);
	Blades = std::clamp(Blades, 1, 64);
	Slider(pebble ? "Stone Size" : "Blade Width", &BladeWidth, 0.005f, pebble ? 0.6f : 0.15f, 1);
	if (!pebble)
	{
		Slider("Bend", &Bend, 0.0f, 1.0f, 1);
		Slider("Lean", &Lean, 0.0f, 1.0f, 1);
	}
	if (Type == Kind::Flower)
	{
		Int("Flower Heads", &Heads, 1);
		Heads = std::clamp(Heads, 0, 8);
		Int("Petals", &Petals, 1);
		Petals = std::clamp(Petals, 3, 16);
		Slider("Petal Length", &HeadSize, 0.005f, 0.2f, 1);
	}
	Slider("Min Scale", &MinScale, 0.1f, 3.0f, 1);
	Slider("Max Scale", &MaxScale, 0.1f, 3.0f, 1);
	MaxScale = (std::max)(MaxScale, MinScale);
	Slider("Width Variation", &WidthVariation, 0.0f, 1.0f, 1);
	Label("Color", 0, true);
	ColorRow(pebble ? "Stone Color" : "Healthy Color", HealthyColor);
	ColorRow(pebble ? "Variation Color" : "Dry Color", DryColor);
	if (Type == Kind::Flower)
	{
		ColorRow("Flower Color", FlowerColor);
		ColorRow("Center Color", CenterColor);
	}
	Slider("Noise Spread", &NoiseSpread, 0.005f, 1.0f, 1);
	Slider(pebble ? "Variation Amount" : "Dry Amount", &DryAmount, 0.0f, 1.0f, 1);
	Slider("Color Variation", &ColorVariation, 0.0f, 0.5f, 1);
	if (!pebble)
	{
		Slider("Tip Color", &TipColor, 0.0f, 1.0f, 1);
		Slider("Translucency", &Translucency, 0.0f, 1.0f, 1);
	}
	Slider("Smoothness", &Smoothness, 0.0f, 1.0f, 1);
	Label("Placement", 0, true);
	Slider("Density", &Density, 0.05f, 40.0f, 1);
	Slider("Max Slope", &MaxSlope, 0.0f, 90.0f, 1);
	Slider("Align To Ground", &GroundAlign, 0.0f, 1.0f, 1);
	for (int i = 0; i < 4; ++i)
	{
		bool on = (LayerMask >> i) & 1u;
		char label[32];
		snprintf(label, sizeof(label), "Grows On Layer %d", i + 1);
		if (Toggle(label, &on, 1))
			LayerMask = on ? (LayerMask | (1u << i)) : (LayerMask & ~(1u << i));
	}
	Label("Rendering", 0, true);
	Slider("Wind Response", &WindResponse, 0.0f, 2.0f, 1);
	Toggle("Cast Shadows", &CastShadows, 1);
	const bool changed = ToJson().dump() != before;
	if (changed)
		Invalidate();
	return changed;
}

const std::string& DetailPrototype::MeshKey() const
{
	if (m_KeyDirty)
	{
		m_KeyDirty = false;
		const nlohmann::json j = ToJson();
		m_MeshKey = j["shape"].dump();
		m_Hash = std::hash<std::string>()(j.dump());
	}
	return m_MeshKey;
}

size_t DetailPrototype::Hash() const
{
	MeshKey();
	return m_Hash;
}

// ================================================================ DetailSettings
nlohmann::json DetailSettings::ToJson() const
{
	nlohmann::json j;
	j["distance"] = Distance;
	j["densityScale"] = DensityScale;
	j["shadowDistance"] = ShadowDistance;
	j["windSpeed"] = WindSpeed;
	j["windSize"] = WindSize;
	j["windBending"] = WindBending;
	j["windDirection"] = WindDirection;
	return j;
}

void DetailSettings::FromJson(const nlohmann::json& j)
{
	if (!j.is_object())
		return;
	Get(j, "distance", Distance);
	Get(j, "densityScale", DensityScale);
	Get(j, "shadowDistance", ShadowDistance);
	Get(j, "windSpeed", WindSpeed);
	Get(j, "windSize", WindSize);
	Get(j, "windBending", WindBending);
	Get(j, "windDirection", WindDirection);
}

size_t DetailSettings::Hash() const
{
	return std::hash<float>()(DensityScale);
}
