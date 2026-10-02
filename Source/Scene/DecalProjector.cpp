#include "pch.h"
#include "DecalProjector.h"
#include "UMaterial.h"
#include "UnityGUI.h"
#include "MaterialInspector.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include "Transform.h"

namespace
{
	std::vector<DecalProjector*>& Registry()
	{
		static std::vector<DecalProjector*> r;
		return r;
	}
}

DecalProjector::DecalProjector()
{
	m_InspectorTitleName = "Decal Projector";
	Registry().push_back(this);
}

DecalProjector::~DecalProjector()
{
	auto& r = Registry();
	r.erase(std::remove(r.begin(), r.end(), this), r.end());
}

const std::vector<DecalProjector*>& DecalProjector::All()
{
	return Registry();
}

UMaterial* DecalProjector::GetMaterial()
{
	if (!m_MaterialLoaded)
	{
		m_MaterialLoaded = true;
		m_Material = m_MaterialPath.empty() ? nullptr : ResourceManager::GetI()->LoadMaterial(wstring_to_string(m_MaterialPath));
	}
	return m_Material.get();
}

Matrix DecalProjector::BoxMatrix() const
{
	// 단위 상자 → (크기) → (피벗) → GameObject
	const Matrix world = m_pGameObject->GetTransform()->GetWorldMatrix();
	return Matrix::CreateScale(m_Size) * Matrix::CreateTranslation(m_Pivot) * world;
}

void DecalProjector::OnInspectorGUI()
{
	using namespace UnityGUI;
	GetMaterial();
	if (MaterialInspector::MaterialSlot("Material", "decal:" + std::to_string((uintptr_t)this), m_Material, m_MaterialPath))
		m_MaterialLoaded = true;
	if (Float("Draw Distance", &m_DrawDistance)) m_DrawDistance = (std::max)(0.0f, m_DrawDistance);
	if (Slider("Start Fade", &m_StartFade, 0.0f, 1.0f)) m_StartFade = std::clamp(m_StartFade, 0.0f, 1.0f);
	MinMaxSlider("Angle Fade", &m_AngleFade.x, &m_AngleFade.y, 0.0f, 180.0f);
	Vector2Pair("UV Scale", "X", &m_UVScale.x, "Y", &m_UVScale.y);
	Vector2Pair("UV Offset", "X", &m_UVOffset.x, "Y", &m_UVOffset.y);
	UnityGUI::Vector3("Pivot", &m_Pivot.x);
	if (Float("Width", &m_Size.x)) m_Size.x = (std::max)(0.001f, m_Size.x);
	if (Float("Height", &m_Size.y)) m_Size.y = (std::max)(0.001f, m_Size.y);
	if (Float("Projection Depth", &m_Size.z)) m_Size.z = (std::max)(0.001f, m_Size.z);
	if (Slider("Opacity", &m_FadeFactor, 0.0f, 1.0f)) m_FadeFactor = std::clamp(m_FadeFactor, 0.0f, 1.0f);
	if (!m_Material)
		HelpBox("Pick a material: a Lit / Unlit material (Base Map alpha = coverage) or a Shader Graph with Material = Decal.", true);
	else
	{
		// Unity 처럼 컴포넌트 아래에 재질 Inspector
		MaterialInspector::WatchUndo(m_Material);
		m_Material->OnInspectorGUI(true);
	}
}

void DecalProjector::OnDrawGizmos()
{
	if (!m_Enabled || !m_pGameObject || !SceneViewOverlay::IsActive() ||
		SelectionManager::GetSelectedObjectType() != SelectionType::GAMEOBJECT || SelectionManager::GetSelectedGameObject() != m_pGameObject)
		return;
	// 상자 12 모서리 + 투영 방향 화살표
	const Matrix box = BoxMatrix();
	Vec3 c[8];
	for (int i = 0; i < 8; ++i)
		c[i] = Vec3::Transform(Vec3((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f), box);
	const ImU32 color = IM_COL32(120, 200, 255, 255);
	static const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
	auto f3 = [](const Vec3& v) { return XMFLOAT3(v.x, v.y, v.z); };
	for (const auto& e : edges)
		SceneViewOverlay::DrawLine(f3(c[e[0]]), f3(c[e[1]]), color, 1.0f);
	const Vec3 from = Vec3::Transform(Vec3(0, 0, -0.5f), box), to = Vec3::Transform(Vec3(0, 0, 0.5f), box);
	SceneViewOverlay::DrawLine(f3(from), f3(to), IM_COL32(255, 220, 90, 255), 2.0f);
}

GENERATE_COMPONENT_FUNC_TOJSON(DecalProjector)
{
	json j;
	SERIALIZE_TYPE(j, DecalProjector);
	j["enabled"] = m_Enabled;
	j["material"] = wstring_to_string(m_MaterialPath);
	j["size"] = { m_Size.x, m_Size.y, m_Size.z };
	j["pivot"] = { m_Pivot.x, m_Pivot.y, m_Pivot.z };
	j["uvScale"] = { m_UVScale.x, m_UVScale.y };
	j["uvOffset"] = { m_UVOffset.x, m_UVOffset.y };
	j["fadeFactor"] = m_FadeFactor;
	j["drawDistance"] = m_DrawDistance;
	j["startFade"] = m_StartFade;
	j["angleFade"] = { m_AngleFade.x, m_AngleFade.y };
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(DecalProjector)
{
	auto vec = [&](const char* key, float* out, int n) {
		if (j.contains(key) && j[key].is_array() && (int)j[key].size() == n)
			for (int i = 0; i < n; ++i) out[i] = j[key][i].get<float>();
	};
	m_Enabled = j.value("enabled", true);
	const std::wstring path = string_to_wstring(j.value("material", std::string()));
	if (path != m_MaterialPath)
	{
		m_MaterialPath = path;
		m_MaterialLoaded = false;
		m_Material.reset();
	}
	vec("size", &m_Size.x, 3);
	vec("pivot", &m_Pivot.x, 3);
	vec("uvScale", &m_UVScale.x, 2);
	vec("uvOffset", &m_UVOffset.x, 2);
	vec("angleFade", &m_AngleFade.x, 2);
	m_FadeFactor = j.value("fadeFactor", 1.0f);
	m_DrawDistance = j.value("drawDistance", 1000.0f);
	m_StartFade = j.value("startFade", 0.9f);
}
