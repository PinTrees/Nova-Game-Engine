#include "pch.h"
#include "Tree.h"
#include "TreeRenderer.h"
#include "UnityGUI.h"

namespace
{
	std::vector<Tree*>& Registry()
	{
		static std::vector<Tree*> all;
		return all;
	}
}

Tree::Tree()
{
	m_InspectorTitleName = "Tree";
	Registry().push_back(this);
}

Tree::~Tree()
{
	auto& all = Registry();
	all.erase(std::remove(all.begin(), all.end(), this), all.end());
}

const std::vector<Tree*>& Tree::All() { return Registry(); }

bool Tree::IsDrawable() const
{
	return m_Enabled && m_pGameObject && m_pGameObject->IsActiveInHierarchy();
}

void Tree::UpdateAll()
{
	TreeRenderer::UpdateTime();
}

bool Tree::GetLocalBounds(Vec3& bmin, Vec3& bmax)
{
	TreeRenderer::MeshInfo info;
	if (!TreeRenderer::GetMeshInfo(Desc, info) || info.Vertices == 0)
		return false;
	bmin = info.BoundsMin;
	bmax = info.BoundsMax;
	return true;
}

bool Tree::RaycastLocal(const Vec3& o, const Vec3& r, float& bestT)
{
	TreeRenderer::MeshInfo info;
	if (!TreeRenderer::GetMeshInfo(Desc, info) || info.Positions == nullptr)
		return false;
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

void Tree::OnInspectorGUI()
{
	char stats[200] = {};
	TreeRenderer::MeshInfo full, mid;
	if (TreeRenderer::GetMeshInfo(Desc, full, 0) && TreeRenderer::GetMeshInfo(Desc, mid, 1))
		snprintf(stats, sizeof(stats), "LOD0 %d vertices, %d triangles (%d branches, %d leaf cards)\nLOD1 %d vertices, %d triangles\nBillboard 2 triangles",
			full.Vertices, full.Triangles, full.Branches, full.LeafCards, mid.Vertices, mid.Triangles);
	Desc.DrawInspector(stats[0] ? stats : nullptr);
}

GENERATE_COMPONENT_FUNC_TOJSON(Tree)
{
	json j = Desc.ToJson();
	SERIALIZE_TYPE(j, Tree);
	j["enabled"] = m_Enabled;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Tree)
{
	m_Enabled = j.value("enabled", true);
	Desc.FromJson(j);
}
