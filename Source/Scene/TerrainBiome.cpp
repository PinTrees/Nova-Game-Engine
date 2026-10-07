#include "pch.h"
#include "TerrainBiome.h"
#include "Terrain.h"
#include "TerrainBiomes.h"
#include "Transform.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"

namespace
{
	std::vector<TerrainBiome*>& Registry()
	{
		static std::vector<TerrainBiome*> all;
		return all;
	}
}

TerrainBiome::TerrainBiome()
{
	m_InspectorTitleName = "Terrain Biome";
	Registry().push_back(this);
}

TerrainBiome::~TerrainBiome()
{
	auto& all = Registry();
	all.erase(std::remove(all.begin(), all.end(), this), all.end());
}

const std::vector<TerrainBiome*>& TerrainBiome::All() { return Registry(); }

bool TerrainBiome::IsActiveBiome() const
{
	return m_Enabled && m_pGameObject && m_pGameObject->IsActiveInHierarchy();
}

void TerrainBiome::OnInspectorGUI()
{
	using namespace UnityGUI;
	const auto& presets = TerrainBiomes::List();
	std::vector<const char*> names;
	int current = -1;
	for (int i = 0; i < (int)presets.size(); ++i)
	{
		names.push_back(presets[i].Name.c_str());
		if (presets[i].Name == Preset)
			current = i;
	}
	if (current < 0)
	{
		names.push_back(Preset.c_str());   // 목록에 없는 이름 (패키지에서 지워졌거나 오타)
		current = (int)names.size() - 1;
	}
	int sel = current;
	if (Dropdown("Preset", &sel, names.data(), (int)names.size()) && sel != current && sel < (int)presets.size())
		Preset = presets[sel].Name;
	if (const TerrainBiomes::Preset* p = TerrainBiomes::Find(Preset))
	{
		if (GfxShaderResourceView* thumb = TerrainBiomes::Thumbnail(*p))
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
			ImGui::Image((ImTextureID)thumb, ImVec2(96, 96));
			if (!p->Description.empty())
			{
				ImGui::SameLine();
				ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - 8.0f);
				ImGui::TextDisabled("%s", p->Description.c_str());
				ImGui::PopTextWrapPos();
			}
		}
	}
	else
		HelpBox("Preset not found in Resources/Packages/Terrain/Biomes.", true, 1);
	Slider("Opacity", &Opacity, 0.0f, 1.0f);
	Toggle("Affect Heights", &AffectHeights);
	Toggle("Affect Materials", &AffectMaterials);
	Label("Area", 0, true);
	Slider("Blend Size", &BlendSize, 0.01f, 1.0f, 1);
	Slider("Roundness", &Roundness, 0.0f, 1.0f, 1);
	Slider("Edge Noise", &EdgeNoise, 0.0f, 1.0f, 1);
	Int("Seed", &Seed, 1);
	Int("Order", &Order);
	HelpBox("Area = Transform scale X / Z (m), direction = rotation Y.\nInside the area the terrain uses the preset's base noise, filters and materials, blended with the terrain's own settings at the edge.", false);
}

// Scene 뷰: 선택하면 영역(바깥) + 섞기 시작(안쪽) 경계를 지형 표면을 따라 그린다
void TerrainBiome::OnDrawGizmos()
{
	if (!SceneViewOverlay::IsActive() || SelectionManager::GetSelectedGameObject() != m_pGameObject || m_pGameObject == nullptr)
		return;
	XMFLOAT4X4 w;
	XMStoreFloat4x4(&w, m_pGameObject->GetTransform()->GetWorldMatrix());
	const float sx = sqrtf(w._11 * w._11 + w._12 * w._12 + w._13 * w._13) * 0.5f;
	const float sz = sqrtf(w._31 * w._31 + w._32 * w._32 + w._33 * w._33) * 0.5f;
	const float len = sqrtf(w._11 * w._11 + w._13 * w._13);
	const float c = len > 1e-6f ? w._11 / len : 1.0f, s = len > 1e-6f ? w._13 / len : 0.0f;
	auto ground = [&](float x, float z) {
		for (Terrain* t : Terrain::GetActiveTerrains())
		{
			const Vec3 p = t->GetPosition();
			auto data = t->GetTerrainData();
			if (data && x >= p.x && z >= p.z && x <= p.x + data->Size.x && z <= p.z + data->Size.z)
				return p.y + t->SampleHeight(Vec3(x, 0, z)) + 0.5f;
		}
		return w._42;
	};
	auto ring = [&](float scale, ImU32 color, float thickness) {
		const int n = 96;
		XMFLOAT3 prev = {};
		for (int i = 0; i <= n; ++i)
		{
			const float a = XM_2PI * i / n;
			const float du = cosf(a), dv = sinf(a);
			const float m = (std::max)(fabsf(du), fabsf(dv));
			const float t = scale / (m + (1.0f - m) * std::clamp(Roundness, 0.0f, 1.0f));
			const float u = du * t * sx, v = dv * t * sz;
			const float x = w._41 + u * c - v * s, z = w._43 + u * s + v * c;
			const XMFLOAT3 cur(x, ground(x, z), z);
			if (i > 0)
				SceneViewOverlay::DrawLine(prev, cur, color, thickness);
			prev = cur;
		}
	};
	ring(1.0f, IM_COL32(90, 220, 120, 230), 2.0f);
	ring(1.0f - std::clamp(BlendSize, 0.01f, 1.0f), IM_COL32(170, 255, 190, 160), 1.0f);
}

GENERATE_COMPONENT_FUNC_TOJSON(TerrainBiome)
{
	json j;
	SERIALIZE_TYPE(j, TerrainBiome);
	j["enabled"] = m_Enabled;
	j["preset"] = Preset;
	j["opacity"] = Opacity;
	j["blendSize"] = BlendSize;
	j["roundness"] = Roundness;
	j["edgeNoise"] = EdgeNoise;
	j["affectHeights"] = AffectHeights;
	j["affectMaterials"] = AffectMaterials;
	j["seed"] = Seed;
	j["order"] = Order;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(TerrainBiome)
{
	m_Enabled = j.value("enabled", true);
	Preset = j.value("preset", Preset);
	Opacity = j.value("opacity", Opacity);
	BlendSize = j.value("blendSize", BlendSize);
	Roundness = j.value("roundness", Roundness);
	EdgeNoise = j.value("edgeNoise", EdgeNoise);
	AffectHeights = j.value("affectHeights", AffectHeights);
	AffectMaterials = j.value("affectMaterials", AffectMaterials);
	Seed = j.value("seed", Seed);
	Order = j.value("order", Order);
}
