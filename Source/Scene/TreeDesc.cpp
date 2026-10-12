#include "pch.h"
#include "TreeDesc.h"
#include "UnityGUI.h"
#include "PlantLibrary.h"

using json = nlohmann::json;

// ================================================================== 프리셋 (PlantLibrary 의 종 — 모양 · 색 · 바람 · LOD 전부)
void TreeDesc::ApplyPreset(int preset)
{
	const int seed = Params.Seed;
	if (const PlantLibrary::Species* sp = PlantLibrary::At(preset))
	{
		const bool shadows = CastShadows;
		*this = sp->Desc;
		CastShadows = shadows && sp->Desc.CastShadows;
	}
	Preset = preset;
	Params.Seed = seed;
	Invalidate();
}

// ================================================================== 키
const std::string& TreeDesc::MeshKey() const
{
	if (m_KeyDirty)
	{
		m_MeshKey = Params.ToJson().dump();
		json look = ToJson();
		look.erase("shape");
		m_Hash = std::hash<std::string>{}(m_MeshKey) ^ (std::hash<std::string>{}(look.dump()) * 1099511628211ull);
		m_KeyDirty = false;
	}
	return m_MeshKey;
}

size_t TreeDesc::Hash() const
{
	MeshKey();
	return m_Hash;
}

// ================================================================== 저장
static json Color4(const XMFLOAT4& c) { return { c.x, c.y, c.z, c.w }; }
static void ReadColor(const json& j, const char* key, XMFLOAT4& c)
{
	if (j.contains(key) && j[key].is_array() && j[key].size() >= 3)
	{
		c.x = j[key][0].get<float>(); c.y = j[key][1].get<float>(); c.z = j[key][2].get<float>();
	}
}

json TreeDesc::ToJson() const
{
	json j;
	j["preset"] = Preset;
	j["shape"] = Params.ToJson();
	j["bark"] = { { "color", Color4(BarkColor) }, { "mossColor", Color4(MossColor) }, { "moss", Moss }, { "ridgeDepth", RidgeDepth },
		{ "ridgeFrequency", RidgeFrequency }, { "smoothness", BarkSmoothness }, { "fleck", BarkFleck } };
	if (Pattern != BarkPattern::Cracks)
		j["bark"]["pattern"] = { { "kind", (int)Pattern }, { "spacing", PatternSpacing }, { "depth", PatternDepth }, { "color", Color4(PatternColor) } };
	if (!BarkMaterial.empty())
		j["bark"]["material"] = BarkMaterial;
	j["leaves"] = { { "color", Color4(LeafColor) }, { "color2", Color4(LeafColor2) }, { "variation", LeafVariation },
		{ "transmission", LeafTransmission }, { "smoothness", LeafSmoothness }, { "length", LeafLength } };
	if (Params.FlowerPetals > 0)
		j["flowers"] = { { "color", Color4(FlowerColor) }, { "center", Color4(FlowerCenter) } };
	j["wind"] = { { "strength", WindStrength }, { "direction", WindDirection }, { "trunkSway", TrunkSway },
		{ "branchSway", BranchSway }, { "leafFlutter", LeafFlutter } };
	j["lod"] = { { "lodDistance", LodDistance }, { "billboardDistance", BillboardDistance }, { "cullDistance", CullDistance } };
	j["castShadows"] = CastShadows;
	return j;
}

void TreeDesc::FromJson(const json& j)
{
	Preset = j.value("preset", 0);
	// 모양 값이 없으면(손으로 쓴 씬 등) 프리셋 값 + 저장된 seed
	if (!j.contains("shape"))
	{
		ApplyPreset(Preset);
		Params.Seed = j.value("seed", Params.Seed);
	}
	else
		Params.FromJson(j["shape"]);
	if (j.contains("bark"))
	{
		const json& b = j["bark"];
		ReadColor(b, "color", BarkColor); ReadColor(b, "mossColor", MossColor);
		Moss = b.value("moss", Moss); RidgeDepth = b.value("ridgeDepth", RidgeDepth); RidgeFrequency = b.value("ridgeFrequency", RidgeFrequency);
		BarkMaterial = b.value("material", BarkMaterial);
		BarkSmoothness = b.value("smoothness", BarkSmoothness); BarkFleck = b.value("fleck", BarkFleck);
		if (b.contains("pattern"))
		{
			const json& pt = b["pattern"];
			Pattern = (BarkPattern)std::clamp(pt.value("kind", 0), 0, 3);
			PatternSpacing = pt.value("spacing", PatternSpacing); PatternDepth = pt.value("depth", PatternDepth);
			ReadColor(pt, "color", PatternColor);
		}
	}
	if (j.contains("leaves"))
	{
		const json& l = j["leaves"];
		ReadColor(l, "color", LeafColor); ReadColor(l, "color2", LeafColor2);
		LeafVariation = l.value("variation", LeafVariation); LeafTransmission = l.value("transmission", LeafTransmission);
		LeafSmoothness = l.value("smoothness", LeafSmoothness); LeafLength = l.value("length", LeafLength);
	}
	if (j.contains("flowers"))
	{
		ReadColor(j["flowers"], "color", FlowerColor);
		ReadColor(j["flowers"], "center", FlowerCenter);
	}
	if (j.contains("wind"))
	{
		const json& w = j["wind"];
		WindStrength = w.value("strength", WindStrength); WindDirection = w.value("direction", WindDirection);
		TrunkSway = w.value("trunkSway", TrunkSway); BranchSway = w.value("branchSway", BranchSway); LeafFlutter = w.value("leafFlutter", LeafFlutter);
	}
	if (j.contains("lod"))
	{
		const json& l = j["lod"];
		LodDistance = l.value("lodDistance", LodDistance); BillboardDistance = l.value("billboardDistance", BillboardDistance);
		CullDistance = l.value("cullDistance", CullDistance);
	}
	CastShadows = j.value("castShadows", true);
	Invalidate();
}

// ================================================================== Inspector
bool TreeDesc::DrawInspector(const char* statsText)
{
	using namespace UnityGUI;
	bool changed = false;

	int presetCount = 0;
	const char* const* presets = TreeParams::PresetNames(presetCount);
	int preset = Preset;
	if (Dropdown("Preset", &preset, presets, presetCount))
	{
		ApplyPreset(preset);
		changed = true;
	}
	changed |= Int("Seed", &Params.Seed);
	if (CenterButton("New Seed"))
	{
		Params.Seed = (Params.Seed * 1103515245 + 12345) & 0x7fffff;
		changed = true;
	}
	if (statsText)
		HelpBox(statsText, false, 0);

	if (Foldout("Trunk", 0, true, false))
	{
		static const char* kCrown[] = { "Conical", "Spherical", "Hemispherical", "Cylindrical", "Tapered Cylindrical", "Flame", "Inverse Conical" };
		static const char* kForm[] = { "Tree", "Palm", "Grass", "Fern", "Rosette", "Flower" };
		int form = (int)Params.Shape;
		if (Dropdown("Form", &form, kForm, TreeParams::kForms, 1)) { Params.Shape = (TreeParams::Form)form; changed = true; }
		changed |= Slider("Height", &Params.Height, 0.05f, 60.0f, 1);
		changed |= Slider("Radius", &Params.Radius, 0.01f, 2.0f, 1);
		changed |= Slider("Tip Radius", &Params.TipRadius, 0.01f, 1.0f, 1);
		changed |= Slider("Root Flare", &Params.Flare, 0.0f, 3.0f, 1);
		changed |= Slider("Gnarl", &Params.Gnarl, 0.0f, 1.0f, 1);
		changed |= Slider("Lean", &Params.Lean, -45.0f, 45.0f, 1);
		changed |= Int("Radial Segments", &Params.RadialSegments, 1);
		int crown = (int)Params.CrownShape;
		if (Dropdown("Crown Shape", &crown, kCrown, 7, 1)) { Params.CrownShape = (TreeParams::Crown)crown; changed = true; }
	}
	if (Foldout("Branches", 0, true, false))
	{
		changed |= Int("Levels", &Params.Levels, 1);
		Params.Levels = std::clamp(Params.Levels, 0, 3);
		for (int i = 0; i < Params.Levels; ++i)
		{
			TreeParams::Level& l = Params.L[i];
			ImGui::PushID(i);
			char title[32];
			snprintf(title, sizeof(title), "Level %d", i + 1);
			Label(title, 1, true);
			changed |= Int("Count", &l.Count, 2);
			changed |= Slider("Start", &l.Start, 0.0f, 0.95f, 2);
			changed |= Slider("Angle", &l.Angle, 0.0f, 150.0f, 2);
			changed |= Slider("Angle Variance", &l.AngleVariance, 0.0f, 60.0f, 2);
			changed |= Slider("Length", &l.Length, 0.05f, 2.0f, 2);
			changed |= Slider("Length Variance", &l.LengthVariance, 0.0f, 1.0f, 2);
			changed |= Slider("Gravity", &l.Gravity, -1.0f, 1.5f, 2);
			changed |= Slider("Up", &l.Up, 0.0f, 1.0f, 2);
			changed |= Slider("Radius", &l.Radius, 0.1f, 1.0f, 2);
			changed |= Slider("Gnarl", &l.Gnarl, 0.0f, 1.0f, 2);
			ImGui::PopID();
		}
	}
	if (Foldout("Leaves", 0, true, false))
	{
		static const char* kShape[] = { "Broad", "Oval", "Needle", "Blade", "Frond", "Round", "Lobed", "Scale" };
		int leaf = (int)Params.Leaf;
		if (Dropdown("Shape", &leaf, kShape, TreeParams::kLeafShapes, 1)) { Params.Leaf = (TreeParams::LeafShape)leaf; changed = true; }
		changed |= Int("Cards per Branch", &Params.LeafCards, 1);
		changed |= Int("Leaves per Card", &Params.LeavesPerCard, 1);
		changed |= Slider("Card Size", &Params.LeafCardSize, 0.1f, 3.0f, 1);
		changed |= Slider("Start", &Params.LeafStart, 0.0f, 1.0f, 1);
		changed |= Int("Card Segments", &Params.CardSegments, 1);
		changed |= Slider("Droop", &Params.Droop, 0.0f, 3.0f, 1);
		changed |= Slider("Spread", &Params.CardSpread, 0.0f, 100.0f, 1);
		changed |= Slider("Leaf Length", &LeafLength, 0.1f, 0.5f, 1);
		changed |= UnityGUI::Color("Color", &LeafColor.x, 1);
		changed |= UnityGUI::Color("Color 2", &LeafColor2.x, 1);
		changed |= Slider("Variation", &LeafVariation, 0.0f, 1.0f, 1);
		changed |= Slider("Transmission", &LeafTransmission, 0.0f, 1.5f, 1);
		changed |= Slider("Smoothness", &LeafSmoothness, 0.0f, 1.0f, 1);
	}
	if (Foldout("Flowers", 0, false, false))
	{
		changed |= Int("Petals", &Params.FlowerPetals, 1);
		Params.FlowerPetals = std::clamp(Params.FlowerPetals, 0, 16);
		changed |= Int("Flowers", &Params.FlowerCards, 1);
		changed |= Slider("Flower Size", &Params.FlowerSize, 0.01f, 1.0f, 1);
		changed |= Int("Stems", &Params.Stems, 1);
		changed |= UnityGUI::Color("Petal Color", &FlowerColor.x, 1);
		changed |= UnityGUI::Color("Center Color", &FlowerCenter.x, 1);
	}
	if (Foldout("Bark", 0, true, false))
	{
		changed |= UnityGUI::Color("Color", &BarkColor.x, 1);
		changed |= Slider("Ridge Depth", &RidgeDepth, 0.0f, 2.0f, 1);
		changed |= Slider("Ridge Frequency", &RidgeFrequency, 1.0f, 40.0f, 1);
		changed |= Slider("Fleck", &BarkFleck, -1.0f, 1.0f, 1);
		changed |= Slider("Smoothness", &BarkSmoothness, 0.0f, 1.0f, 1);
		changed |= UnityGUI::Color("Moss Color", &MossColor.x, 1);
		changed |= Slider("Moss", &Moss, 0.0f, 1.0f, 1);
		static const char* kPattern[] = { "Cracks", "Rings", "Ribs", "Leaf Bases" };
		int pattern = (int)Pattern;
		if (Dropdown("Pattern", &pattern, kPattern, 4, 1)) { Pattern = (BarkPattern)pattern; changed = true; }
		if (Pattern != BarkPattern::Cracks)
		{
			changed |= Slider("Pattern Spacing", &PatternSpacing, 0.02f, 2.0f, 1);
			changed |= Slider("Pattern Depth", &PatternDepth, 0.0f, 1.0f, 1);
			changed |= UnityGUI::Color("Pattern Color", &PatternColor.x, 1);
		}
	}
	if (Foldout("Wind", 0, true, false))
	{
		changed |= Slider("Strength", &WindStrength, 0.0f, 2.0f, 1);
		changed |= Slider("Direction", &WindDirection, -180.0f, 180.0f, 1);
		changed |= Slider("Trunk Sway", &TrunkSway, 0.0f, 2.0f, 1);
		changed |= Slider("Branch Sway", &BranchSway, 0.0f, 1.0f, 1);
		changed |= Slider("Leaf Flutter", &LeafFlutter, 0.0f, 0.2f, 1);
	}
	if (Foldout("LOD", 0, true, false))
	{
		// 거리 = 카메라 거리 / 나무 크기 배율. 경계 앞뒤 10% 는 화면 디더로 섞는다
		changed |= Slider("Mesh LOD Distance", &LodDistance, 5.0f, 300.0f, 1);
		changed |= Slider("Billboard Distance", &BillboardDistance, 10.0f, 1000.0f, 1);
		changed |= Slider("Cull Distance", &CullDistance, 50.0f, 5000.0f, 1);
		BillboardDistance = (std::max)(BillboardDistance, LodDistance + 1.0f);
		CullDistance = (std::max)(CullDistance, BillboardDistance + 1.0f);
	}
	if (Foldout("Lighting", 0, true, false))
		changed |= Toggle("Cast Shadows", &CastShadows, 1);

	if (changed)
		Invalidate();
	return changed;
}
