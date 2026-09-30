#include "pch.h"
#include "TerrainCollider.h"
#include "Terrain.h"
#include "TerrainData.h"
#include "TerrainEditor.h"
#include "UnityGUI.h"

TerrainCollider::TerrainCollider()
{
	m_InspectorTitleName = "Terrain Collider";
}

TerrainCollider::~TerrainCollider()
{
}

void TerrainCollider::SetTerrainData(const std::string& path)
{
	m_Data = path.empty() ? nullptr : TerrainData::Load(path);
	m_DataPath = m_Data ? m_Data->Path : path;
}

std::shared_ptr<TerrainData> TerrainCollider::GetEffectiveData() const
{
	if (m_Data)
		return m_Data;
	if (m_pGameObject)
		if (Terrain* terrain = m_pGameObject->GetComponent<Terrain>())
			return terrain->GetTerrainData();
	return nullptr;
}

void TerrainCollider::OnInspectorGUI()
{
	UnityGUI::Toggle("Provides Contacts", &m_ProvidesContacts);
	UnityGUI::ObjectField("Material", "None (Physics Material)");
	std::string path = m_DataPath;
	if (TerrainEditor::TerrainDataField("Terrain Data", path, "##tcdata"))
		SetTerrainData(path);
	UnityGUI::Toggle("Enable Tree Colliders", &m_EnableTreeColliders);
	DrawLayerOverrides();
}

GENERATE_COMPONENT_FUNC_TOJSON(TerrainCollider)
{
	json j;
	j["type"] = "TerrainCollider";
	j["terrainData"] = m_DataPath;
	j["enableTreeColliders"] = m_EnableTreeColliders;
	SerializeCommon(j);
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(TerrainCollider)
{
	m_EnableTreeColliders = j.value("enableTreeColliders", true);
	SetTerrainData(j.value("terrainData", std::string()));
	DeserializeCommon(j);
}
