#include "pch.h"
#include "MeshFilter.h"
#include "UnityGUI.h"
#include "GameObjectFactory.h"
#include "MeshSelectEditorDialog.h"
#include "Mesh.h"

MeshFilter::MeshFilter()
{
	m_InspectorTitleName = "Mesh Filter";
}

MeshFilter::~MeshFilter()
{
}

std::string MeshFilter::InspectorTitle() const
{
	if (m_Mesh)
		return m_Mesh->Name + " (Mesh Filter)";
	return "Mesh Filter";
}

void MeshFilter::OnInspectorGUI()
{
	std::string name = m_Mesh ? m_Mesh->Name : "None (Mesh)";
	if (UnityGUI::ObjectField("Mesh", name.c_str(), 0, "mesh_small"))
		ImGui::OpenPopup("##meshpick");

	// 내장 도형 + 프로젝트 메시 선택
	ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.19f, 0.19f, 0.19f, 1.0f));
	if (ImGui::BeginPopup("##meshpick"))
	{
		static const PrimitiveType kTypes[] = { PrimitiveType::Cube, PrimitiveType::Sphere, PrimitiveType::Capsule, PrimitiveType::Cylinder, PrimitiveType::Plane, PrimitiveType::Quad };
		static const char* kNames[] = { "Cube", "Sphere", "Capsule", "Cylinder", "Plane", "Quad" };
		for (int i = 0; i < 6; ++i)
		{
			if (ImGui::Selectable(kNames[i]))
				SetMesh(GameObjectFactory::GetPrimitiveMesh(kTypes[i]), GameObjectFactory::GetBuiltinMeshPath(kTypes[i]), 0);
		}
		ImGui::Separator();
		if (ImGui::Selectable("Select from project..."))
		{
			shared_ptr<SkinnedMesh> dummy;
			MeshSelectEditorDialog::Open(m_Mesh, dummy, m_MeshPath, m_MeshSubsetIndex, MESH_SELECT_DIALOG_TYPE::MESH_STATIC);
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleColor();
}

GENERATE_COMPONENT_FUNC_TOJSON(MeshFilter)
{
	json j;
	SERIALIZE_TYPE(j, MeshFilter);
	SERIALIZE_INT(j, m_MeshSubsetIndex, "subsetIndex");
	SERIALIZE_WSTRING(j, m_MeshPath, "meshPath");
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(MeshFilter)
{
	DE_SERIALIZE_INT(j, m_MeshSubsetIndex, "subsetIndex");
	DE_SERIALIZE_WSTRING(j, m_MeshPath, "meshPath");
	if (m_MeshPath != L"")
		m_Mesh = ResourceManager::GetI()->LoadMesh(m_MeshPath, m_MeshSubsetIndex);
}
