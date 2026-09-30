#include "pch.h"
#include "MeshCollider.h"
#include "MeshFilter.h"
#include "MeshRenderer.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"

MeshCollider::MeshCollider()
{
	m_InspectorTitleName = "Mesh Collider";
}

MeshCollider::~MeshCollider()
{
}

Mesh* MeshCollider::GetMesh() const
{
	if (m_pGameObject == nullptr)
		return nullptr;
	if (MeshFilter* mf = m_pGameObject->GetComponent<MeshFilter>())
		if (mf->GetMesh())
			return mf->GetMesh().get();
	if (MeshRenderer* mr = m_pGameObject->GetComponent<MeshRenderer>())
		if (auto m = mr->GetMesh())
			return m.get();
	return nullptr;
}

int MeshCollider::GetSubsetIndex() const
{
	if (m_pGameObject != nullptr)
		if (MeshFilter* mf = m_pGameObject->GetComponent<MeshFilter>())
			return mf->GetSubsetIndex();
	return 0;
}

void MeshCollider::OnDrawGizmos()
{
	if (!ShouldDrawGizmo())
		return;
	Mesh* mesh = GetMesh();
	if (mesh == nullptr || mesh->Vertices.empty() || mesh->Indices.empty())
		return;

	Matrix world = m_pGameObject->GetTransform()->GetWorldMatrix();
	const size_t maxTriangles = 4000;   // 큰 메시는 일부만 표시
	auto drawRange = [&](size_t start, size_t count, uint32 base) {
		for (size_t n = start; n + 3 <= start + count && n + 3 <= mesh->Indices.size() && n / 3 < maxTriangles; n += 3)
		{
			Vec3 p[3];
			for (int k = 0; k < 3; ++k)
			{
				size_t vi = (std::min)((size_t)mesh->Indices[n + k] + base, mesh->Vertices.size() - 1);
				p[k] = Vec3::Transform(Vec3(mesh->Vertices[vi].pos), world);
			}
			for (int k = 0; k < 3; ++k)
				SceneViewOverlay::DrawLine(XMFLOAT3(p[k].x, p[k].y, p[k].z), XMFLOAT3(p[(k + 1) % 3].x, p[(k + 1) % 3].y, p[(k + 1) % 3].z), GizmoColor(), 1.0f);
		}
	};
	if (mesh->Subsets.empty())
		drawRange(0, mesh->Indices.size(), 0);
	else
		for (const auto& sub : mesh->Subsets)
			drawRange((size_t)sub.FaceStart * 3, (size_t)sub.FaceCount * 3, sub.VertexStart);
}

void MeshCollider::OnInspectorGUI()
{
	UnityGUI::Toggle("Convex", &m_Convex);
	if (m_Convex)
		UnityGUI::Toggle("Is Trigger", &m_IsTrigger);
	UnityGUI::Toggle("Provides Contacts", &m_ProvidesContacts);
	static const char* cooking[] = { "Everything", "Nothing", "Cook for Faster Simulation", "Enable Mesh Cleaning", "Weld Colocated Vertices", "Use Fast Midphase" };
	UnityGUI::Dropdown("Cooking Options", &m_CookingOptions, cooking, 6);
	UnityGUI::ObjectField("Material", "None (Physics Material)");

	std::string meshName = "None (Mesh)";
	if (m_pGameObject != nullptr)
		if (MeshFilter* mf = m_pGameObject->GetComponent<MeshFilter>())
		{
			std::wstring path = mf->GetMeshPath();
			if (path.rfind(L"builtin:", 0) == 0)
				meshName = wstring_to_string(path.substr(8));
			else if (!path.empty())
				meshName = wstring_to_string(std::filesystem::path(path).stem().wstring());
		}
	UnityGUI::ObjectField("Mesh", meshName.c_str(), 0, "mesh_small");
	DrawLayerOverrides();
}

GENERATE_COMPONENT_FUNC_TOJSON(MeshCollider)
{
	json j;
	j["type"] = "MeshCollider";
	j["convex"] = m_Convex;
	j["cookingOptions"] = m_CookingOptions;
	SerializeCommon(j);
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(MeshCollider)
{
	m_Convex = j.value("convex", false);
	m_CookingOptions = j.value("cookingOptions", 0);
	DeserializeCommon(j);
}
