#include "pch.h"
#include "Rock.h"
#include "RockRenderer.h"

namespace
{
	std::vector<Rock*>& Registry()
	{
		static std::vector<Rock*> all;
		return all;
	}
}

Rock::Rock()
{
	m_InspectorTitleName = "Rock";
	Registry().push_back(this);
}

Rock::~Rock()
{
	auto& all = Registry();
	all.erase(std::remove(all.begin(), all.end(), this), all.end());
}

const std::vector<Rock*>& Rock::All() { return Registry(); }

bool Rock::IsDrawable() const
{
	return m_Enabled && m_pGameObject && m_pGameObject->IsActiveInHierarchy();
}

bool Rock::GetLocalBounds(Vec3& bmin, Vec3& bmax)
{
	RockRenderer::MeshInfo info;
	if (!RockRenderer::GetMeshInfo(Desc, info) || info.Vertices == 0)
		return false;
	bmin = info.BoundsMin;
	bmax = info.BoundsMax;
	return true;
}

bool Rock::RaycastLocal(const Vec3& o, const Vec3& r, float& bestT)
{
	RockRenderer::MeshInfo info;
	if (!RockRenderer::GetMeshInfo(Desc, info) || info.Positions == nullptr)
		return false;
	// 범위 상자 먼저 (바위 메시는 삼각형이 많다)
	float tmin = 0.0f, tmax = bestT;
	const float ro[3] = { o.x, o.y, o.z }, rd[3] = { r.x, r.y, r.z };
	const float lo[3] = { info.BoundsMin.x, info.BoundsMin.y, info.BoundsMin.z }, hi[3] = { info.BoundsMax.x, info.BoundsMax.y, info.BoundsMax.z };
	for (int a = 0; a < 3; ++a)
	{
		if (fabsf(rd[a]) < 1e-12f)
		{
			if (ro[a] < lo[a] || ro[a] > hi[a]) return false;
			continue;
		}
		float t1 = (lo[a] - ro[a]) / rd[a], t2 = (hi[a] - ro[a]) / rd[a];
		if (t1 > t2) std::swap(t1, t2);
		tmin = (std::max)(tmin, t1);
		tmax = (std::min)(tmax, t2);
		if (tmin > tmax) return false;
	}
	bool hit = false;
	const auto& P = *info.Positions;
	const auto& I = *info.Indices;
	for (size_t n = 0; n + 3 <= I.size(); n += 3)
	{
		const Vec3 a(P[I[n]]), b(P[I[n + 1]]), c(P[I[n + 2]]);
		const Vec3 e1 = b - a, e2 = c - a;
		const Vec3 p = r.Cross(e2);
		const float det = e1.Dot(p);
		if (fabsf(det) < 1e-12f)
			continue;
		const float inv = 1.0f / det;
		const Vec3 s = o - a;
		const float u = s.Dot(p) * inv;
		if (u < 0.0f || u > 1.0f)
			continue;
		const Vec3 q = s.Cross(e1);
		const float v = r.Dot(q) * inv;
		if (v < 0.0f || u + v > 1.0f)
			continue;
		const float t = e2.Dot(q) * inv;
		if (t > 0.0f && t < bestT)
		{
			bestT = t;
			hit = true;
		}
	}
	return hit;
}

void Rock::OnInspectorGUI()
{
	char stats[200] = {};
	RockRenderer::MeshInfo l0, l1, l2;
	if (RockRenderer::GetMeshInfo(Desc, l0, 0) && RockRenderer::GetMeshInfo(Desc, l1, 1) && RockRenderer::GetMeshInfo(Desc, l2, 2))
		snprintf(stats, sizeof(stats), "LOD0 %d triangles (%.0f ms), LOD1 %d, LOD2 %d\nSize %.1f x %.1f x %.1f m",
			l0.Triangles, l0.Ms, l1.Triangles, l2.Triangles, l0.BoundsMax.x - l0.BoundsMin.x, l0.BoundsMax.y - l0.BoundsMin.y, l0.BoundsMax.z - l0.BoundsMin.z);
	Desc.DrawInspector(stats[0] ? stats : nullptr);
}

GENERATE_COMPONENT_FUNC_TOJSON(Rock)
{
	json j = Desc.ToJson();
	SERIALIZE_TYPE(j, Rock);
	j["enabled"] = m_Enabled;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Rock)
{
	m_Enabled = j.value("enabled", true);
	Desc.FromJson(j);
}
