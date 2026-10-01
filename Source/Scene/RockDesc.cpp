#include "pch.h"
#include "RockDesc.h"
#include "UnityGUI.h"

namespace
{
	const char* kPresets[] = { "Limestone Cliff", "Limestone Block", "Granite Boulder", "Sandstone Ledge", "Basalt Spire", "River Pebble" };
	const char* kShapes[] = { "Boulder", "Block", "Cliff", "Slab", "Spire" };

	void C4(nlohmann::json& j, const char* k, const XMFLOAT4& c) { j[k] = { c.x, c.y, c.z }; }
	void R4(const nlohmann::json& j, const char* k, XMFLOAT4& c)
	{
		if (j.contains(k) && j[k].is_array() && j[k].size() >= 3)
			c = XMFLOAT4(j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>(), 1.0f);
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

const char* RockDesc::PresetName(int preset) { return kPresets[std::clamp(preset, 0, PresetCount - 1)]; }

void RockDesc::ApplyPreset(int preset)
{
	PresetIndex = std::clamp(preset, 0, PresetCount - 1);
	const int seed = Params.Seed;
	RockParams p;
	p.Seed = seed;
	using S = RockParams::Shape;
	switch (PresetIndex)
	{
	case LimestoneCliff:
		// 붉은사막·남유럽 석회암 절벽: 평평한 윗면, 깊은 세로 절리로 갈라진 기둥, 수평 지층의 턱과 홈, 빗물 자국
		p.Kind = S::Cliff; p.SizeX = 40; p.SizeY = 24; p.SizeZ = 14; p.Roundness = 0.04f; p.Facets = 16; p.FacetDepth = 0.16f;
		p.Warp = 0.35f; p.EdgeSoftness = 0.02f; p.ColumnDepth = 2.2f;
		p.Strata = 10; p.StrataDepth = 0.9f; p.StrataGroove = 0.3f; p.StrataTilt = 2.5f; p.Cracks = 10; p.CrackDepth = 3.5f; p.CrackWidth = 0.45f;
		p.Noise = 0.3f; p.NoiseScale = 3.0f; p.Taper = 0.03f; p.Detail = 1.6f;
		BaseColor = { 0.72f, 0.69f, 0.62f, 1 }; StrataColor = { 0.60f, 0.55f, 0.47f, 1 }; StrataContrast = 0.55f; Moss = 0.5f;
		MossColor = { 0.31f, 0.37f, 0.16f, 1 }; DetailScale = 5.0f; DetailStrength = 1.1f; Smoothness = 0.12f;
		break;
	case LimestoneBlock:
		// 무너져 내린 석회암 덩어리: 절리 면(거의 직각) + 얕은 지층
		p.Kind = S::Block; p.SizeX = 5; p.SizeY = 3.4f; p.SizeZ = 4.4f; p.Roundness = 0.06f; p.Facets = 13; p.FacetDepth = 0.22f;
		p.Warp = 0.14f; p.EdgeSoftness = 0.05f;
		p.Strata = 3; p.StrataDepth = 0.1f; p.StrataGroove = 0.06f; p.StrataTilt = 4; p.Cracks = 2; p.CrackDepth = 0.6f; p.CrackWidth = 0.08f; p.ColumnDepth = 0.25f;
		p.Noise = 0.08f; p.NoiseScale = 1.0f;
		BaseColor = { 0.72f, 0.69f, 0.62f, 1 }; StrataColor = { 0.61f, 0.56f, 0.48f, 1 }; StrataContrast = 0.4f; Moss = 0.4f;
		MossColor = { 0.31f, 0.37f, 0.16f, 1 }; DetailScale = 2.2f; DetailStrength = 1.0f; Smoothness = 0.12f;
		break;
	case GraniteBoulder:
		p.Kind = S::Boulder; p.SizeX = 3; p.SizeY = 2.2f; p.SizeZ = 2.6f; p.Roundness = 0.55f; p.Facets = 14; p.FacetDepth = 0.24f;
		p.Warp = 0.24f; p.EdgeSoftness = 0.08f;
		p.Cracks = 1; p.CrackDepth = 0.4f; p.CrackWidth = 0.05f; p.Noise = 0.06f; p.NoiseScale = 0.8f;
		BaseColor = { 0.50f, 0.48f, 0.46f, 1 }; StrataColor = { 0.44f, 0.43f, 0.42f, 1 }; StrataContrast = 0.15f; Moss = 0.4f;
		MossColor = { 0.29f, 0.36f, 0.15f, 1 }; DetailScale = 1.6f; DetailStrength = 0.8f; Smoothness = 0.2f;
		break;
	case SandstoneLedge:
		p.Kind = S::Slab; p.SizeX = 10; p.SizeY = 2.8f; p.SizeZ = 6; p.Roundness = 0.15f; p.Facets = 12; p.FacetDepth = 0.2f;
		p.Warp = 0.16f; p.EdgeSoftness = 0.05f;
		p.Strata = 5; p.StrataDepth = 0.3f; p.StrataGroove = 0.12f; p.StrataTilt = 6; p.Cracks = 3; p.CrackDepth = 0.7f; p.CrackWidth = 0.1f;
		p.Noise = 0.1f; p.NoiseScale = 1.4f;
		BaseColor = { 0.68f, 0.52f, 0.39f, 1 }; StrataColor = { 0.58f, 0.41f, 0.30f, 1 }; StrataContrast = 0.6f; Moss = 0.2f;
		MossColor = { 0.38f, 0.38f, 0.20f, 1 }; DetailScale = 2.5f; DetailStrength = 0.9f; Smoothness = 0.1f;
		break;
	case BasaltSpire:
		// 현무암 주상절리 기둥 (육각 단면) — 위가 깨져 비스듬히
		p.Kind = S::Spire; p.SizeX = 3; p.SizeY = 12; p.SizeZ = 3; p.Roundness = 0.06f; p.Facets = 9; p.FacetDepth = 0.06f;
		p.Warp = 0.06f; p.EdgeSoftness = 0.03f;
		p.Strata = 6; p.StrataDepth = 0.04f; p.StrataGroove = 0.08f; p.Cracks = 2; p.CrackDepth = 0.5f; p.CrackWidth = 0.05f;
		p.Noise = 0.05f; p.NoiseScale = 1.0f; p.Taper = 0.15f;
		BaseColor = { 0.24f, 0.23f, 0.23f, 1 }; StrataColor = { 0.20f, 0.19f, 0.19f, 1 }; StrataContrast = 0.2f; Moss = 0.25f;
		MossColor = { 0.27f, 0.33f, 0.15f, 1 }; DetailScale = 2.0f; DetailStrength = 1.0f; Smoothness = 0.22f;
		break;
	default:   // RiverPebble
		p.Kind = S::Boulder; p.SizeX = 0.8f; p.SizeY = 0.42f; p.SizeZ = 0.6f; p.Roundness = 0.95f; p.Facets = 3; p.FacetDepth = 0.05f;
		p.Warp = 0.15f; p.EdgeSoftness = 0.2f;
		p.Noise = 0.015f; p.NoiseScale = 0.4f; p.Detail = 0.5f;
		BaseColor = { 0.45f, 0.44f, 0.42f, 1 }; StrataColor = { 0.38f, 0.37f, 0.36f, 1 }; StrataContrast = 0.1f; Moss = 0.1f;
		MossColor = { 0.29f, 0.35f, 0.16f, 1 }; DetailScale = 0.6f; DetailStrength = 0.4f; Smoothness = 0.35f;
		break;
	}
	Params = p;
	Invalidate();
}

nlohmann::json RockDesc::ToJson() const
{
	nlohmann::json j;
	const RockParams& p = Params;
	j["preset"] = PresetIndex;
	j["shape"] = { { "kind", (int)p.Kind }, { "seed", p.Seed }, { "size", { p.SizeX, p.SizeY, p.SizeZ } }, { "roundness", p.Roundness },
		{ "facets", p.Facets }, { "facetDepth", p.FacetDepth }, { "strata", p.Strata }, { "strataDepth", p.StrataDepth },
		{ "strataGroove", p.StrataGroove }, { "strataTilt", p.StrataTilt }, { "cracks", p.Cracks }, { "crackDepth", p.CrackDepth },
		{ "crackWidth", p.CrackWidth }, { "columnDepth", p.ColumnDepth }, { "noise", p.Noise }, { "noiseScale", p.NoiseScale }, { "warp", p.Warp }, { "edgeSoftness", p.EdgeSoftness }, { "taper", p.Taper }, { "detail", p.Detail } };
	C4(j, "baseColor", BaseColor);
	C4(j, "strataColor", StrataColor);
	C4(j, "mossColor", MossColor);
	j["strataContrast"] = StrataContrast;
	j["moss"] = Moss;
	j["detailStrength"] = DetailStrength;
	j["detailScale"] = DetailScale;
	j["smoothness"] = Smoothness;
	j["colorVariation"] = ColorVariation;
	j["lodBias"] = LodBias;
	j["castShadows"] = CastShadows;
	return j;
}

void RockDesc::FromJson(const nlohmann::json& j)
{
	ApplyPreset(j.value("preset", PresetIndex));
	if (j.contains("shape") && j["shape"].is_object())
	{
		const auto& s = j["shape"];
		RockParams& p = Params;
		p.Kind = (RockParams::Shape)std::clamp(s.value("kind", (int)p.Kind), 0, (int)RockParams::Shape::Count - 1);
		p.Seed = s.value("seed", p.Seed);
		if (s.contains("size") && s["size"].is_array() && s["size"].size() >= 3)
		{
			p.SizeX = s["size"][0].get<float>(); p.SizeY = s["size"][1].get<float>(); p.SizeZ = s["size"][2].get<float>();
		}
		p.Roundness = s.value("roundness", p.Roundness);
		p.Facets = s.value("facets", p.Facets);
		p.FacetDepth = s.value("facetDepth", p.FacetDepth);
		p.Strata = s.value("strata", p.Strata);
		p.StrataDepth = s.value("strataDepth", p.StrataDepth);
		p.StrataGroove = s.value("strataGroove", p.StrataGroove);
		p.StrataTilt = s.value("strataTilt", p.StrataTilt);
		p.Cracks = s.value("cracks", p.Cracks);
		p.CrackDepth = s.value("crackDepth", p.CrackDepth);
		p.CrackWidth = s.value("crackWidth", p.CrackWidth);
		p.ColumnDepth = s.value("columnDepth", p.ColumnDepth);
		p.Noise = s.value("noise", p.Noise);
		p.Warp = s.value("warp", p.Warp);
		p.EdgeSoftness = s.value("edgeSoftness", p.EdgeSoftness);
		p.NoiseScale = s.value("noiseScale", p.NoiseScale);
		p.Taper = s.value("taper", p.Taper);
		p.Detail = s.value("detail", p.Detail);
	}
	R4(j, "baseColor", BaseColor);
	R4(j, "strataColor", StrataColor);
	R4(j, "mossColor", MossColor);
	StrataContrast = j.value("strataContrast", StrataContrast);
	Moss = j.value("moss", Moss);
	DetailStrength = j.value("detailStrength", DetailStrength);
	DetailScale = j.value("detailScale", DetailScale);
	Smoothness = j.value("smoothness", Smoothness);
	ColorVariation = j.value("colorVariation", ColorVariation);
	LodBias = j.value("lodBias", LodBias);
	CastShadows = j.value("castShadows", CastShadows);
	Invalidate();
}

bool RockDesc::DrawInspector(const char* statsText)
{
	using namespace UnityGUI;
	const std::string before = ToJson().dump();
	RockParams& p = Params;
	int preset = PresetIndex;
	if (Dropdown("Preset", &preset, kPresets, PresetCount))
		ApplyPreset(preset);
	Int("Seed", &p.Seed, 0);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
	if (ImGui::Button("Randomize", ImVec2(120, 0)))
		p.Seed = (int)((p.Seed * 1103515245u + 12345u) & 0x7FFF);
	Label("Shape", 0, true);
	int kind = (int)p.Kind;
	if (Dropdown("Kind", &kind, kShapes, (int)RockParams::Shape::Count, 1))
		p.Kind = (RockParams::Shape)kind;
	float size[3] = { p.SizeX, p.SizeY, p.SizeZ };
	if (UnityGUI::Vector3("Size", size, false, 1))
	{
		p.SizeX = (std::max)(0.1f, size[0]); p.SizeY = (std::max)(0.1f, size[1]); p.SizeZ = (std::max)(0.1f, size[2]);
	}
	Slider("Roundness", &p.Roundness, 0.0f, 1.0f, 1);
	Int("Facets", &p.Facets, 1);
	p.Facets = std::clamp(p.Facets, 0, 32);
	Slider("Facet Depth", &p.FacetDepth, 0.0f, 0.6f, 1);
	Slider("Warp", &p.Warp, 0.0f, 0.6f, 1);
	Slider("Edge Softness", &p.EdgeSoftness, 0.0f, 0.3f, 1);
	Slider("Taper", &p.Taper, 0.0f, 0.8f, 1);
	Label("Strata", 0, true);
	Int("Layers", &p.Strata, 1);
	p.Strata = std::clamp(p.Strata, 0, 40);
	Slider("Ledge Depth", &p.StrataDepth, 0.0f, 3.0f, 1);
	Slider("Groove", &p.StrataGroove, 0.0f, 2.0f, 1);
	Slider("Tilt", &p.StrataTilt, -45.0f, 45.0f, 1);
	Label("Cracks", 0, true);
	Int("Count", &p.Cracks, 1);
	p.Cracks = std::clamp(p.Cracks, 0, 24);
	Slider("Depth", &p.CrackDepth, 0.0f, 5.0f, 1);
	Slider("Width", &p.CrackWidth, 0.01f, 1.0f, 1);
	Slider("Column Depth", &p.ColumnDepth, 0.0f, 6.0f, 1);
	Label("Surface", 0, true);
	Slider("Roughness", &p.Noise, 0.0f, 1.0f, 1);
	Slider("Roughness Scale", &p.NoiseScale, 0.1f, 10.0f, 1);
	Slider("Mesh Detail", &p.Detail, 0.25f, 2.0f, 1);
	Label("Material", 0, true);
	ColorRow("Base Color", BaseColor);
	ColorRow("Strata Color", StrataColor);
	Slider("Strata Contrast", &StrataContrast, 0.0f, 1.0f, 1);
	ColorRow("Moss Color", MossColor);
	Slider("Moss", &Moss, 0.0f, 1.0f, 1);
	Slider("Detail Strength", &DetailStrength, 0.0f, 2.0f, 1);
	Slider("Detail Scale", &DetailScale, 0.2f, 20.0f, 1);
	Slider("Smoothness", &Smoothness, 0.0f, 1.0f, 1);
	Slider("Color Variation", &ColorVariation, 0.0f, 0.5f, 1);
	Label("Rendering", 0, true);
	Slider("LOD Bias", &LodBias, 0.25f, 4.0f, 1);
	Toggle("Cast Shadows", &CastShadows, 1);
	if (statsText)
		HelpBox(statsText, false);
	const bool changed = ToJson().dump() != before;
	if (changed)
		Invalidate();
	return changed;
}

const std::string& RockDesc::MeshKey() const
{
	if (m_KeyDirty)
	{
		m_KeyDirty = false;
		nlohmann::json j = ToJson();
		m_MeshKey = j["shape"].dump();
		m_Hash = std::hash<std::string>()(j.dump());
	}
	return m_MeshKey;
}

size_t RockDesc::Hash() const
{
	MeshKey();
	return m_Hash;
}
