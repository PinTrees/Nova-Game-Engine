#include "pch.h"
#include "RenderLayers.h"
#include "Effects.h"
#include "Terrain.h"
#include "TreeRenderer.h"
#include "TerrainData.h"
#include "TerrainEditor.h"
#include "SceneViewOverlay.h"

std::vector<Terrain*> Terrain::s_Active;

Terrain::Terrain()
{
	m_InspectorTitleName = "Terrain";
	s_Active.push_back(this);
}

Terrain::~Terrain()
{
	s_Active.erase(std::remove(s_Active.begin(), s_Active.end(), this), s_Active.end());
}

void Terrain::SetTerrainData(const std::string& path)
{
	m_Data = path.empty() ? nullptr : TerrainData::Load(path);
	m_DataPath = m_Data ? m_Data->Path : path;
}

void Terrain::SetTerrainData(std::shared_ptr<TerrainData> data)
{
	m_Data = data;
	m_DataPath = data ? data->Path : std::string();
}

// ------------------------------------------------------------------ Unity API
void Terrain::PrewarmStaged()
{
	if (m_Data)
		TreeRenderer::PrewarmTerrain(*m_Data, GetPosition());
}

Vec3 Terrain::GetPosition() const
{
	return m_pGameObject ? m_pGameObject->GetTransform()->GetPosition() : Vec3::Zero;
}

float Terrain::SampleHeight(const Vec3& worldPosition) const
{
	if (m_Data == nullptr)
		return 0.0f;
	const Vec3 local = worldPosition - GetPosition();
	return m_Data->GetHeight(local.x, local.z);
}

Vec3 Terrain::GetInterpolatedNormal(const Vec3& worldPosition) const
{
	if (m_Data == nullptr)
		return Vec3(0.0f, 1.0f, 0.0f);
	const Vec3 local = worldPosition - GetPosition();
	return m_Data->GetNormal(local.x, local.z);
}

bool Terrain::Raycast(const Vec3& origin, const Vec3& direction, float maxDistance, Vec3& hitPoint) const
{
	if (m_Data == nullptr)
		return false;
	const Vec3 pos = GetPosition();
	float t = 0.0f;
	if (!m_Data->Raycast(origin - pos, direction, maxDistance, t))
		return false;
	hitPoint = origin + direction * t;
	return true;
}

// ------------------------------------------------------------------ 그리기
void Terrain::DrawPass(TerrainRenderer::Pass pass, bool editor)
{
	if (m_Data == nullptr || !m_Draw || !m_Enabled || (m_pGameObject && !m_pGameObject->IsActiveInHierarchy()))
		return;
	if (pass == TerrainRenderer::Pass::Main && m_ShadowCasting == ShadowCasting::ShadowsOnly)
		return;
	if (pass == TerrainRenderer::Pass::Shadow && m_ShadowCasting == ShadowCasting::Off)
		return;
	if (!RenderLayers::Visible(m_pGameObject))
		return;   // Camera / Light 의 Culling Mask
	const uint32 layerBit = 1u << ((m_pGameObject ? m_pGameObject->GetLayerIndex() : 0) & 31);
	RenderLayers::SetObjectLayer(Effects::InstancedBasicFX.get(), layerBit);   // Light.cullingMask
	TerrainRenderer::Draw(*m_Data, GetPosition(), pass, m_PixelError, pass == TerrainRenderer::Pass::Main ? (editor ? &m_EditorStats : &m_GameStats) : nullptr,
		m_HeightBlend ? m_HeightTransition : 0.0f);
	RenderLayers::SetObjectLayer(Effects::InstancedBasicFX.get(), ~0u);
}

void Terrain::Render() { DrawPass(TerrainRenderer::Pass::Main, false); }
void Terrain::_Editor_Render() { DrawPass(TerrainRenderer::Pass::Main, true); }
void Terrain::RenderShadow() { DrawPass(TerrainRenderer::Pass::Shadow, RenderManager::GetI()->RenderingEditorView); }
void Terrain::RenderShadowNormal() { DrawPass(TerrainRenderer::Pass::NormalDepth, false); }
void Terrain::_Editor_RenderShadowNormal() { DrawPass(TerrainRenderer::Pass::NormalDepth, true); }

// ------------------------------------------------------------------ Scene 뷰: LOD 노드 경계 (깊이별 색)
void Terrain::OnDrawGizmos()
{
	if (!m_ShowLodNodes || m_Data == nullptr || !SceneViewOverlay::IsActive())
		return;
	static const ImU32 kColors[] = { IM_COL32(255, 70, 70, 255), IM_COL32(255, 150, 40, 255), IM_COL32(255, 230, 60, 255), IM_COL32(90, 230, 90, 255),
		IM_COL32(60, 210, 230, 255), IM_COL32(90, 120, 255, 255), IM_COL32(200, 90, 255, 255) };
	const Vec3 origin = GetPosition();
	for (const XMINT3& leaf : m_EditorStats.DrawnLeaves)
	{
		const int cells = m_Data->NodeCells(leaf.x);
		const float x0 = leaf.y * cells * m_Data->CellSizeX(), z0 = leaf.z * cells * m_Data->CellSizeZ();
		const float w = cells * m_Data->CellSizeX(), l = cells * m_Data->CellSizeZ();
		const ImU32 color = kColors[(std::min)(leaf.x, 6)];
		// 네 변을 지형 표면을 따라 (조금 띄워서)
		const Vec3 corners[5] = { Vec3(x0, 0, z0), Vec3(x0 + w, 0, z0), Vec3(x0 + w, 0, z0 + l), Vec3(x0, 0, z0 + l), Vec3(x0, 0, z0) };
		for (int e = 0; e < 4; ++e)
		{
			const int steps = 8;
			XMFLOAT3 prev;
			for (int k = 0; k <= steps; ++k)
			{
				const Vec3 p = corners[e] + (corners[e + 1] - corners[e]) * ((float)k / steps);
				const XMFLOAT3 cur(origin.x + p.x, origin.y + m_Data->GetHeight(p.x, p.z) + 0.3f, origin.z + p.z);
				if (k > 0)
					SceneViewOverlay::DrawLine(prev, cur, color, 1.5f);
				prev = cur;
			}
		}
	}
}

// ------------------------------------------------------------------ Inspector
void Terrain::OnInspectorGUI()
{
	TerrainEditor::DrawInspector(this);
}

GENERATE_COMPONENT_FUNC_TOJSON(Terrain)
{
	json j;
	SERIALIZE_TYPE(j, Terrain);
	j["enabled"] = m_Enabled;
	j["terrainData"] = m_DataPath;
	j["draw"] = m_Draw;
	j["pixelError"] = m_PixelError;
	j["basemapDistance"] = m_BasemapDistance;
	j["groupingID"] = m_GroupingID;
	j["autoConnect"] = m_AutoConnect;
	j["shadowCastingMode"] = (int)m_ShadowCasting;
	j["showLodNodes"] = m_ShowLodNodes;
	j["heightBasedBlend"] = m_HeightBlend;
	j["heightTransition"] = m_HeightTransition;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Terrain)
{
	m_Enabled = j.value("enabled", true);
	m_Draw = j.value("draw", true);
	m_PixelError = j.value("pixelError", 5.0f);
	m_BasemapDistance = j.value("basemapDistance", 1000.0f);
	m_GroupingID = j.value("groupingID", 0);
	m_AutoConnect = j.value("autoConnect", true);
	m_ShadowCasting = (ShadowCasting)j.value("shadowCastingMode", 1);
	m_ShowLodNodes = j.value("showLodNodes", false);
	m_HeightBlend = j.value("heightBasedBlend", false);
	m_HeightTransition = std::clamp(j.value("heightTransition", 0.2f), 0.001f, 1.0f);
	SetTerrainData(j.value("terrainData", std::string()));
}
