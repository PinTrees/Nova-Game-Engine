#include "pch.h"
#include "AdaptiveProbeVolume.h"
#include "ProbeVolumes.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include "Transform.h"
#include "GameObject.h"

namespace
{
	std::vector<AdaptiveProbeVolume*>& Registry()
	{
		static std::vector<AdaptiveProbeVolume*> r;
		return r;
	}

	void Box(const Vec3& mn, const Vec3& mx, ImU32 color)
	{
		XMFLOAT3 c[8];
		for (int i = 0; i < 8; ++i)
			c[i] = XMFLOAT3((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
		static const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
		for (const auto& e : edges)
			SceneViewOverlay::DrawLine(c[e[0]], c[e[1]], color, 1.0f);
	}
}

AdaptiveProbeVolume::AdaptiveProbeVolume()
{
	m_InspectorTitleName = "Adaptive Probe Volume";
	Registry().push_back(this);
}

AdaptiveProbeVolume::~AdaptiveProbeVolume()
{
	auto& r = Registry();
	r.erase(std::remove(r.begin(), r.end(), this), r.end());
}

const std::vector<AdaptiveProbeVolume*>& AdaptiveProbeVolume::All()
{
	return Registry();
}

bool AdaptiveProbeVolume::IsActiveInHierarchy() const
{
	for (GameObject* g = m_pGameObject; g != nullptr; g = g->GetParent())
		if (!g->IsActive())
			return false;
	return m_pGameObject != nullptr;
}

void AdaptiveProbeVolume::Bounds(Vec3& min, Vec3& max) const
{
	const XMFLOAT3 p = m_pGameObject->GetTransform()->GetPosition();
	const Vec3 h = Vec3((std::max)(m_Size.x, 0.0f), (std::max)(m_Size.y, 0.0f), (std::max)(m_Size.z, 0.0f)) * 0.5f;
	min = Vec3(p.x, p.y, p.z) - h;
	max = Vec3(p.x, p.y, p.z) + h;
}

void AdaptiveProbeVolume::OnInspectorGUI()
{
	using namespace UnityGUI;
	static const char* kModes[] = { "Global", "Local" };
	int mode = (int)m_Mode;
	if (Dropdown("Mode", &mode, kModes, 2)) m_Mode = (Mode)mode;
	if (m_Mode == Mode::Local)
	{
		if (UnityGUI::Vector3("Size", &m_Size.x))
		{
			m_Size.x = (std::max)(0.1f, m_Size.x);
			m_Size.y = (std::max)(0.1f, m_Size.y);
			m_Size.z = (std::max)(0.1f, m_Size.z);
		}
	}
	HelpBox("Realtime: no baking. Probes follow the camera (32 x 16 x 32 per cascade) and update every frame from the scene and its lights.", false);

	Spacing(4.0f);
	Label("Probe Placement", 0, true);
	if (Float("Probe Spacing", &m_ProbeSpacing, 1)) m_ProbeSpacing = std::clamp(m_ProbeSpacing, 0.25f, 16.0f);
	static const char* kCascades[] = { "1", "2", "3" };
	int cascades = m_Cascades - 1;
	if (Dropdown("Cascades", &cascades, kCascades, 3, 1)) m_Cascades = cascades + 1;
	{
		// 덮는 범위 (가장 큰 단계)
		const float s = m_ProbeSpacing * (float)(1 << (2 * (m_Cascades - 1)));
		char buf[96];
		snprintf(buf, sizeof(buf), "%.0f x %.0f x %.0f m", s * 32.0f, s * 16.0f, s * 32.0f);
		ValueLabel("Coverage", buf, 1);
	}

	Spacing(4.0f);
	Label("Realtime Update", 0, true);
	static const char* kRays[] = { "16", "32", "64" };
	int rays = m_Rays <= 16 ? 0 : (m_Rays <= 32 ? 1 : 2);
	if (Dropdown("Rays Per Probe", &rays, kRays, 3, 1)) m_Rays = 16 << rays;
	if (Slider("Update Speed", &m_UpdateSpeed, 0.02f, 1.0f, 1)) m_UpdateSpeed = std::clamp(m_UpdateSpeed, 0.02f, 1.0f);
	if (Slider("Validity Threshold", &m_ValidityThreshold, 0.05f, 1.0f, 1)) m_ValidityThreshold = std::clamp(m_ValidityThreshold, 0.05f, 1.0f);

	Spacing(4.0f);
	Label("Probe Volumes Options", 0, true);
	if (Float("Intensity Multiplier", &m_Intensity, 1)) m_Intensity = (std::max)(0.0f, m_Intensity);
	if (Float("Normal Bias", &m_NormalBias, 1)) m_NormalBias = (std::max)(0.0f, m_NormalBias);
	if (Float("View Bias", &m_ViewBias, 1)) m_ViewBias = (std::max)(0.0f, m_ViewBias);

	const std::string stats = ProbeVolumes::StatusText();
	if (!stats.empty())
		ValueLabel("Status", stats.c_str());
}

void AdaptiveProbeVolume::OnDrawGizmos()
{
	if (!m_Enabled || !m_pGameObject || !SceneViewOverlay::IsActive() ||
		SelectionManager::GetSelectedObjectType() != SelectionType::GAMEOBJECT || SelectionManager::GetSelectedGameObject() != m_pGameObject)
		return;
	if (m_Mode == Mode::Local)
	{
		Vec3 mn, mx;
		Bounds(mn, mx);
		Box(mn, mx, IM_COL32(120, 220, 255, 255));
	}
	// 지금 단계들 (가까울수록 진하게)
	for (int c = 0; c < m_Cascades; ++c)
	{
		Vec3 mn, mx;
		if (ProbeVolumes::CascadeBox(c, mn, mx))
			Box(mn, mx, IM_COL32(120, 255, 160, 200 - c * 60));
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(AdaptiveProbeVolume)
{
	json j;
	SERIALIZE_TYPE(j, AdaptiveProbeVolume);
	j["enabled"] = m_Enabled;
	j["mode"] = m_Mode == Mode::Local ? "Local" : "Global";
	j["size"] = { m_Size.x, m_Size.y, m_Size.z };
	j["probeSpacing"] = m_ProbeSpacing;
	j["cascades"] = m_Cascades;
	j["raysPerProbe"] = m_Rays;
	j["updateSpeed"] = m_UpdateSpeed;
	j["intensityMultiplier"] = m_Intensity;
	j["normalBias"] = m_NormalBias;
	j["viewBias"] = m_ViewBias;
	j["validityThreshold"] = m_ValidityThreshold;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(AdaptiveProbeVolume)
{
	m_Enabled = j.value("enabled", true);
	const std::string mode = j.contains("mode") && j["mode"].is_string() ? j["mode"].get<std::string>() : std::string("Global");
	m_Mode = _stricmp(mode.c_str(), "Local") == 0 ? Mode::Local : Mode::Global;
	if (j.contains("size") && j["size"].is_array() && j["size"].size() == 3)
		for (int i = 0; i < 3; ++i) (&m_Size.x)[i] = (std::max)(0.1f, j["size"][i].get<float>());
	m_ProbeSpacing = std::clamp(j.value("probeSpacing", 1.0f), 0.25f, 16.0f);
	m_Cascades = std::clamp(j.value("cascades", 3), 1, 3);
	const int rays = j.value("raysPerProbe", 32);
	m_Rays = rays <= 16 ? 16 : (rays <= 32 ? 32 : 64);
	m_UpdateSpeed = std::clamp(j.value("updateSpeed", 0.1f), 0.02f, 1.0f);
	m_Intensity = (std::max)(0.0f, j.value("intensityMultiplier", 1.0f));
	m_NormalBias = (std::max)(0.0f, j.value("normalBias", 0.33f));
	m_ViewBias = (std::max)(0.0f, j.value("viewBias", 0.0f));
	m_ValidityThreshold = std::clamp(j.value("validityThreshold", 0.5f), 0.05f, 1.0f);
}
