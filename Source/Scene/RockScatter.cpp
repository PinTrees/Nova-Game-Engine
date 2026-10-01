#include "pch.h"
#include "RockScatter.h"
#include "Terrain.h"
#include "TerrainData.h"
#include "Transform.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include <random>

namespace
{
	std::vector<RockScatter*>& Registry()
	{
		static std::vector<RockScatter*> all;
		return all;
	}

	float Saturate(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
	uint64_t Mix(uint64_t h, uint64_t v) { return h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2)); }
	uint64_t Bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

	// 무리 밀도용 값 노이즈 (0~1)
	float Hash2(int x, int z, uint32_t seed)
	{
		uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + seed * 2246822519u;
		h = (h ^ (h >> 13)) * 1274126177u;
		return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777215.0f;
	}
	float Noise2(float x, float z, uint32_t seed)
	{
		const int ix = (int)floorf(x), iz = (int)floorf(z);
		float fx = x - ix, fz = z - iz;
		fx = fx * fx * (3 - 2 * fx); fz = fz * fz * (3 - 2 * fz);
		const float a = Hash2(ix, iz, seed), b = Hash2(ix + 1, iz, seed), c = Hash2(ix, iz + 1, seed), d = Hash2(ix + 1, iz + 1, seed);
		return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fz;
	}

	Terrain* TerrainAt(float x, float z)
	{
		for (Terrain* t : Terrain::GetActiveTerrains())
		{
			const Vec3 p = t->GetPosition();
			auto data = t->GetTerrainData();
			if (data && x >= p.x && z >= p.z && x <= p.x + data->Size.x && z <= p.z + data->Size.z)
				return t;
		}
		return nullptr;
	}
}

RockScatter::RockScatter()
{
	m_InspectorTitleName = "Rock Scatter";
	Registry().push_back(this);
	Desc.ApplyPreset(RockDesc::GraniteBoulder);
}

RockScatter::~RockScatter()
{
	auto& all = Registry();
	all.erase(std::remove(all.begin(), all.end(), this), all.end());
}

const std::vector<RockScatter*>& RockScatter::All() { return Registry(); }

bool RockScatter::IsDrawable() const
{
	return m_Enabled && m_pGameObject && m_pGameObject->IsActive();
}

const std::vector<RockDesc>& RockScatter::VariantDescs()
{
	const size_t key = Desc.Hash() * 31 + (size_t)Variants;
	if (key != m_VariantKey || m_Variants.size() != (size_t)(std::max)(1, Variants))
	{
		m_VariantKey = key;
		m_Variants.clear();
		for (int v = 0; v < (std::max)(1, Variants); ++v)
		{
			RockDesc d = Desc;
			d.Params.Seed = Desc.Params.Seed * 31 + v * 7919 + 1;
			d.Invalidate();
			m_Variants.push_back(d);
		}
	}
	return m_Variants;
}

const std::vector<RockScatter::Instance>& RockScatter::Instances()
{
	if (m_pGameObject == nullptr)
		return m_Instances;
	const auto& variants = VariantDescs();
	XMFLOAT4X4 w;
	XMStoreFloat4x4(&w, m_pGameObject->GetTransform()->GetWorldMatrix());
	uint64_t key = Desc.Hash();
	for (int k = 0; k < 16; ++k)
		key = Mix(key, Bits((&w._11)[k]));
	for (float f : { ScaleMin, ScaleMax, Sink, MinSlope, MaxSlope, Clustering, ClusterSize })
		key = Mix(key, Bits(f));
	key = Mix(key, (uint64_t)Count * 131 + (uint64_t)Variants * 7 + (uint64_t)Seed * 1009 + (AlignToGround ? 1 : 0));
	for (Terrain* t : Terrain::GetActiveTerrains())
		if (auto data = t->GetTerrainData())
			key = Mix(key, (uint64_t)(uintptr_t)data.get() + data->Revision);
	if (key == m_Key)
		return m_Instances;
	m_Key = key;
	m_Instances.clear();
	std::mt19937 rng((uint32_t)Seed * 2654435761u + 7u);
	std::uniform_real_distribution<float> uni(0.0f, 1.0f);
	const XMMATRIX world = XMLoadFloat4x4(&w);
	const float rockH = Desc.Params.SizeY;
	const int target = std::clamp(Count, 0, 20000);
	for (int attempt = 0; attempt < target * 12 && (int)m_Instances.size() < target; ++attempt)
	{
		const float u = uni(rng) - 0.5f, v = uni(rng) - 0.5f;
		const float yaw = uni(rng) * XM_2PI;
		const float r = uni(rng);
		const float scale = ScaleMin + (ScaleMax - ScaleMin) * r * r;   // 작은 것이 많게
		const int variant = (int)(uni(rng) * variants.size()) % (int)variants.size();
		const float tilt = uni(rng);
		Vec3 pos = XMVector3TransformCoord(Vec3(u, 0.0f, v), world);
		// 무리: 저주파 노이즈 밀도로 받아들일지 (밀도가 낮은 곳은 드물게)
		if (Clustering > 0.0f)
		{
			const float cs = (std::max)(1.0f, ClusterSize);
			const float n = Noise2(pos.x / cs, pos.z / cs, (uint32_t)Seed) * 0.65f + Noise2(pos.x / cs * 2.3f, pos.z / cs * 2.3f, (uint32_t)Seed + 7) * 0.35f;
			const float density = powf(Saturate((n - Clustering * 0.45f) / (1.0f - Clustering * 0.45f)), 1.0f + Clustering * 2.0f);
			if (uni(rng) > density)
				continue;
		}
		Vec3 up(0, 1, 0);
		if (Terrain* t = TerrainAt(pos.x, pos.z))
		{
			pos.y = t->GetPosition().y + t->SampleHeight(pos);
			const Vec3 n = t->GetInterpolatedNormal(pos);
			const float slope = XMConvertToDegrees(acosf(std::clamp(n.y, -1.0f, 1.0f)));
			if (slope < MinSlope || slope > MaxSlope)
				continue;
			if (AlignToGround)
				up = Vec3(n.x * 0.8f, n.y, n.z * 0.8f);
		}
		up.Normalize();
		// 기울기 + 살짝 무작위로 기울임 (모두 똑바로 서 있지 않게)
		up = Vec3(up.x + (tilt - 0.5f) * 0.15f, up.y, up.z + (uni(rng) - 0.5f) * 0.15f);
		up.Normalize();
		Vec3 fwd(cosf(yaw), 0.0f, sinf(yaw));
		Vec3 right = up.Cross(fwd);
		right.Normalize();
		fwd = right.Cross(up);
		pos.y -= Sink * rockH * scale;
		Instance inst;
		inst.Variant = variant;
		inst.World = XMFLOAT4X4(right.x * scale, right.y * scale, right.z * scale, 0,
			up.x * scale, up.y * scale, up.z * scale, 0,
			fwd.x * scale, fwd.y * scale, fwd.z * scale, 0,
			pos.x, pos.y, pos.z, 1);
		m_Instances.push_back(inst);
	}
	return m_Instances;
}

void RockScatter::OnInspectorGUI()
{
	using namespace UnityGUI;
	Int("Count", &Count);
	Count = std::clamp(Count, 0, 20000);
	Int("Variants", &Variants);
	Variants = std::clamp(Variants, 1, 16);
	Int("Seed", &Seed);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
	if (ImGui::Button("Shuffle", ImVec2(120, 0)))
		Seed = (int)((Seed * 1103515245u + 12345u) & 0x7FFF);
	Slider("Scale Min", &ScaleMin, 0.05f, 5.0f);
	Slider("Scale Max", &ScaleMax, 0.05f, 5.0f);
	ScaleMax = (std::max)(ScaleMax, ScaleMin);
	Slider("Sink", &Sink, 0.0f, 0.8f);
	Toggle("Align To Ground", &AlignToGround);
	Slider("Min Slope", &MinSlope, 0.0f, 90.0f);
	Slider("Max Slope", &MaxSlope, 0.0f, 90.0f);
	Slider("Clustering", &Clustering, 0.0f, 1.0f);
	Slider("Cluster Size", &ClusterSize, 2.0f, 300.0f);
	char info[160];
	snprintf(info, sizeof(info), "%d rocks placed, %d shape variants (all drawn with instancing).\nArea = Transform scale X / Z (m).", (int)Instances().size(), (int)VariantDescs().size());
	HelpBox(info, false);
	if (Foldout("Rock", 0, true, false))
		Desc.DrawInspector();
}

// 선택하면 영역 사각형을 지면을 따라
void RockScatter::OnDrawGizmos()
{
	if (!SceneViewOverlay::IsActive() || SelectionManager::GetSelectedGameObject() != m_pGameObject || m_pGameObject == nullptr)
		return;
	const XMMATRIX world = m_pGameObject->GetTransform()->GetWorldMatrix();
	auto ground = [&](float u, float v) {
		Vec3 p = XMVector3TransformCoord(Vec3(u, 0, v), world);
		if (Terrain* t = TerrainAt(p.x, p.z))
			p.y = t->GetPosition().y + t->SampleHeight(p);
		return XMFLOAT3(p.x, p.y + 0.3f, p.z);
	};
	const int n = 24;
	for (int side = 0; side < 4; ++side)
		for (int i = 0; i < n; ++i)
		{
			auto at = [&](int k) {
				const float t = (float)k / n - 0.5f;
				switch (side)
				{
				case 0: return ground(t, -0.5f);
				case 1: return ground(0.5f, t);
				case 2: return ground(-t, 0.5f);
				default: return ground(-0.5f, -t);
				}
			};
			SceneViewOverlay::DrawLine(at(i), at(i + 1), IM_COL32(200, 190, 170, 220), 1.5f);
		}
}

GENERATE_COMPONENT_FUNC_TOJSON(RockScatter)
{
	json j;
	SERIALIZE_TYPE(j, RockScatter);
	j["enabled"] = m_Enabled;
	j["rock"] = Desc.ToJson();
	j["count"] = Count;
	j["variants"] = Variants;
	j["seed"] = Seed;
	j["scaleMin"] = ScaleMin;
	j["scaleMax"] = ScaleMax;
	j["sink"] = Sink;
	j["alignToGround"] = AlignToGround;
	j["minSlope"] = MinSlope;
	j["maxSlope"] = MaxSlope;
	j["clustering"] = Clustering;
	j["clusterSize"] = ClusterSize;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(RockScatter)
{
	m_Enabled = j.value("enabled", true);
	if (j.contains("rock") && j["rock"].is_object())
		Desc.FromJson(j["rock"]);
	Count = j.value("count", Count);
	Variants = j.value("variants", Variants);
	Seed = j.value("seed", Seed);
	ScaleMin = j.value("scaleMin", ScaleMin);
	ScaleMax = j.value("scaleMax", ScaleMax);
	Sink = j.value("sink", Sink);
	AlignToGround = j.value("alignToGround", AlignToGround);
	MinSlope = j.value("minSlope", MinSlope);
	MaxSlope = j.value("maxSlope", MaxSlope);
	Clustering = j.value("clustering", Clustering);
	ClusterSize = j.value("clusterSize", ClusterSize);
	m_Key = ~0ull;
}
