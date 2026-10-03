#include "pch.h"
#include "ReflectionProbe.h"
#include "ReflectionProbes.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include "Transform.h"
#include "PathManager.h"

namespace
{
	std::vector<ReflectionProbe*>& Registry()
	{
		static std::vector<ReflectionProbe*> r;
		return r;
	}
}

ReflectionProbe::ReflectionProbe()
{
	m_InspectorTitleName = "Reflection Probe";
	Registry().push_back(this);
}

ReflectionProbe::~ReflectionProbe()
{
	auto& r = Registry();
	r.erase(std::remove(r.begin(), r.end(), this), r.end());
	ReflectionProbes::Forget(this);
}

const std::vector<ReflectionProbe*>& ReflectionProbe::All()
{
	return Registry();
}

bool ReflectionProbe::IsActiveInHierarchy() const
{
	for (GameObject* g = m_pGameObject; g != nullptr; g = g->GetParent())
		if (!g->IsActive())
			return false;
	return m_pGameObject != nullptr;
}

Vec3 ReflectionProbe::CapturePosition() const
{
	const XMFLOAT3 p = m_pGameObject->GetTransform()->GetPosition();
	return Vec3(p.x, p.y, p.z);
}

void ReflectionProbe::Bounds(Vec3& min, Vec3& max) const
{
	const Vec3 c = CapturePosition() + m_Center;
	const Vec3 h = Vec3((std::max)(m_Size.x, 0.0f), (std::max)(m_Size.y, 0.0f), (std::max)(m_Size.z, 0.0f)) * 0.5f;
	min = c - h;
	max = c + h;
}

void ReflectionProbe::OnInspectorGUI()
{
	using namespace UnityGUI;
	const uint32 before = m_SettingsVersion;
	// Unity 의 Type 목록 순서 (Baked · Custom · Realtime) ↔ 값 (Baked 0 · Realtime 1 · Custom 2)
	static const char* kTypes[] = { "Baked", "Custom", "Realtime" };
	static const Mode kTypeOf[] = { Mode::Baked, Mode::Custom, Mode::Realtime };
	int type = m_Mode == Mode::Baked ? 0 : (m_Mode == Mode::Custom ? 1 : 2);
	if (Dropdown("Type", &type, kTypes, 3))
	{
		m_Mode = kTypeOf[type];
		++m_SettingsVersion;
	}
	if (m_Mode == Mode::Realtime)
	{
		static const char* kRefresh[] = { "On Awake", "Every Frame", "Via Scripting" };
		int refresh = (int)m_Refresh;
		if (Dropdown("Refresh Mode", &refresh, kRefresh, 3)) m_Refresh = (Refresh)refresh;
		static const char* kSlicing[] = { "All Faces At Once", "Individual Faces", "No Time Slicing" };
		int slicing = (int)m_TimeSlicing;
		if (Dropdown("Time Slicing", &slicing, kSlicing, 3)) m_TimeSlicing = (TimeSlicing)slicing;
	}
	else if (m_Mode == Mode::Custom)
	{
		// 큐브맵 (.dds) — Project 창에서 끌어 놓기
		const std::string text = m_CustomCubemap.empty() ? std::string("None (Cubemap)") : std::filesystem::path(m_CustomCubemap).filename().string();
		ObjectField("Cubemap", text.c_str(), 0, "texture");
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
			{
				std::string dropped(static_cast<const char*>(payload->Data));
				const std::string root = wstring_to_string(PathManager::GetI()->GetContentPathW());
				if (_strnicmp(dropped.c_str(), root.c_str(), root.size()) == 0)
					dropped = dropped.substr(root.size());
				std::replace(dropped.begin(), dropped.end(), '\\', '/');
				if (std::filesystem::path(dropped).extension() == ".dds")
				{
					m_CustomCubemap = dropped;
					++m_SettingsVersion;
				}
			}
			ImGui::EndDragDropTarget();
		}
	}

	Spacing(4.0f);
	Label("Runtime Settings", 0, true);
	if (Int("Importance", &m_Importance, 1)) m_Importance = (std::max)(0, m_Importance);
	if (Float("Intensity", &m_Intensity, 1)) m_Intensity = (std::max)(0.0f, m_Intensity);
	Toggle("Box Projection", &m_BoxProjection, 1);
	if (Float("Blend Distance", &m_BlendDistance, 1)) m_BlendDistance = (std::max)(0.0f, m_BlendDistance);
	if (UnityGUI::Vector3("Box Size", &m_Size.x, false, 1))
	{
		m_Size.x = (std::max)(0.0f, m_Size.x);
		m_Size.y = (std::max)(0.0f, m_Size.y);
		m_Size.z = (std::max)(0.0f, m_Size.z);
	}
	UnityGUI::Vector3("Box Offset", &m_Center.x, false, 1);

	if (m_Mode != Mode::Custom)
	{
		Spacing(4.0f);
		Label("Cubemap Capture Settings", 0, true);
		static const char* kRes[] = { "16", "32", "64", "128", "256", "512", "1024", "2048" };
		int res = 0;
		while (res < 7 && (16 << res) < m_Resolution) ++res;
		if (Dropdown("Resolution", &res, kRes, 8, 1)) { m_Resolution = 16 << res; ++m_SettingsVersion; }
		if (Toggle("HDR", &m_Hdr, 1)) ++m_SettingsVersion;
		if (Float("Shadow Distance", &m_ShadowDistance, 1)) { m_ShadowDistance = (std::max)(0.0f, m_ShadowDistance); ++m_SettingsVersion; }
		static const char* kClear[] = { "Skybox", "Solid Color" };
		if (Dropdown("Clear Flags", &m_ClearFlags, kClear, 2, 1)) ++m_SettingsVersion;
		if (UnityGUI::Color("Background", m_Background, 1)) ++m_SettingsVersion;
		if (MaskField("Culling Mask", &m_CullingMask, 1)) ++m_SettingsVersion;
		// Camera 와 같은 두 줄 (Near / Far)
		if (Float("Clipping Planes", &m_Near, 1, "Near"))
		{
			m_Near = (std::max)(0.01f, m_Near);
			m_Far = (std::max)(m_Near + 0.01f, m_Far);
			++m_SettingsVersion;
		}
		if (Float("", &m_Far, 1, "Far"))
		{
			m_Far = (std::max)(m_Near + 0.01f, m_Far);
			++m_SettingsVersion;
		}
	}

	if (m_Mode == Mode::Baked)
	{
		Spacing(4.0f);
		ValueLabel("Baked Texture", m_BakedTexture.empty() ? "None (press Bake)" : m_BakedTexture.c_str());
		if (CenterButton("Bake"))
			ReflectionProbes::RequestBake(this);
	}
	else if (m_Mode == Mode::Realtime && m_Refresh == Refresh::ViaScripting)
	{
		if (CenterButton("Render Probe"))
			ReflectionProbes::RequestRender(this);
	}
	if (before != m_SettingsVersion)
		ReflectionProbes::RequestRender(this);
}

void ReflectionProbe::OnDrawGizmos()
{
	if (!m_Enabled || !m_pGameObject || !SceneViewOverlay::IsActive() ||
		SelectionManager::GetSelectedObjectType() != SelectionType::GAMEOBJECT || SelectionManager::GetSelectedGameObject() != m_pGameObject)
		return;
	// 영향 상자 (주황, Unity 와 비슷한 색) + 안쪽 Blend Distance 상자 + 찍는 점
	auto box = [](const Vec3& mn, const Vec3& mx, ImU32 color) {
		XMFLOAT3 c[8];
		for (int i = 0; i < 8; ++i)
			c[i] = XMFLOAT3((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
		static const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
		for (const auto& e : edges)
			SceneViewOverlay::DrawLine(c[e[0]], c[e[1]], color, 1.0f);
	};
	Vec3 mn, mx;
	Bounds(mn, mx);
	box(mn, mx, IM_COL32(255, 200, 90, 255));
	const Vec3 b(m_BlendDistance, m_BlendDistance, m_BlendDistance);
	const Vec3 in0 = mn + b, in1 = mx - b;
	if (m_BlendDistance > 0.0f && in0.x < in1.x && in0.y < in1.y && in0.z < in1.z)
		box(in0, in1, IM_COL32(255, 200, 90, 110));
	const Vec3 p = CapturePosition();
	const float s = 0.25f;
	SceneViewOverlay::DrawLine(XMFLOAT3(p.x - s, p.y, p.z), XMFLOAT3(p.x + s, p.y, p.z), IM_COL32(255, 255, 255, 255), 1.5f);
	SceneViewOverlay::DrawLine(XMFLOAT3(p.x, p.y - s, p.z), XMFLOAT3(p.x, p.y + s, p.z), IM_COL32(255, 255, 255, 255), 1.5f);
	SceneViewOverlay::DrawLine(XMFLOAT3(p.x, p.y, p.z - s), XMFLOAT3(p.x, p.y, p.z + s), IM_COL32(255, 255, 255, 255), 1.5f);
}

GENERATE_COMPONENT_FUNC_TOJSON(ReflectionProbe)
{
	json j;
	SERIALIZE_TYPE(j, ReflectionProbe);
	static const char* kModes[] = { "Baked", "Realtime", "Custom" };
	static const char* kRefresh[] = { "OnAwake", "EveryFrame", "ViaScripting" };
	static const char* kSlicing[] = { "AllFacesAtOnce", "IndividualFaces", "NoTimeSlicing" };
	j["enabled"] = m_Enabled;
	j["mode"] = kModes[(int)m_Mode];
	j["refreshMode"] = kRefresh[(int)m_Refresh];
	j["timeSlicing"] = kSlicing[(int)m_TimeSlicing];
	j["importance"] = m_Importance;
	j["intensity"] = m_Intensity;
	j["boxProjection"] = m_BoxProjection;
	j["blendDistance"] = m_BlendDistance;
	j["size"] = { m_Size.x, m_Size.y, m_Size.z };
	j["center"] = { m_Center.x, m_Center.y, m_Center.z };
	j["resolution"] = m_Resolution;
	j["hdr"] = m_Hdr;
	j["shadowDistance"] = m_ShadowDistance;
	j["clearFlags"] = m_ClearFlags == 1 ? "SolidColor" : "Skybox";
	j["backgroundColor"] = { m_Background[0], m_Background[1], m_Background[2], m_Background[3] };
	j["cullingMask"] = m_CullingMask;
	j["nearClipPlane"] = m_Near;
	j["farClipPlane"] = m_Far;
	j["customCubemap"] = m_CustomCubemap;
	j["bakedTexture"] = m_BakedTexture;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(ReflectionProbe)
{
	auto vec = [&](const char* key, float* out, int n) {
		if (j.contains(key) && j[key].is_array() && (int)j[key].size() == n)
			for (int i = 0; i < n; ++i) out[i] = j[key][i].get<float>();
	};
	auto pick = [&](const char* key, std::initializer_list<const char*> names, int def) {
		if (!j.contains(key)) return def;
		if (j[key].is_number_integer()) return std::clamp(j[key].get<int>(), 0, (int)names.size() - 1);
		const std::string s = j[key].is_string() ? j[key].get<std::string>() : std::string();
		int i = 0;
		for (const char* n : names) { if (_stricmp(s.c_str(), n) == 0) return i; ++i; }
		return def;
	};
	m_Enabled = j.value("enabled", true);
	m_Mode = (Mode)pick("mode", { "Baked", "Realtime", "Custom" }, 0);
	m_Refresh = (Refresh)pick("refreshMode", { "OnAwake", "EveryFrame", "ViaScripting" }, 0);
	m_TimeSlicing = (TimeSlicing)pick("timeSlicing", { "AllFacesAtOnce", "IndividualFaces", "NoTimeSlicing" }, 0);
	m_Importance = (std::max)(0, j.value("importance", 1));
	m_Intensity = (std::max)(0.0f, j.value("intensity", 1.0f));
	m_BoxProjection = j.value("boxProjection", false);
	m_BlendDistance = (std::max)(0.0f, j.value("blendDistance", 1.0f));
	vec("size", &m_Size.x, 3);
	vec("center", &m_Center.x, 3);
	m_Resolution = std::clamp(j.value("resolution", 128), 16, 2048);
	m_Hdr = j.value("hdr", true);
	m_ShadowDistance = (std::max)(0.0f, j.value("shadowDistance", 100.0f));
	m_ClearFlags = pick("clearFlags", { "Skybox", "SolidColor" }, 0);
	vec("backgroundColor", m_Background, 4);
	m_CullingMask = j.value("cullingMask", 0xFFFFFFFFu);
	m_Near = (std::max)(0.01f, j.value("nearClipPlane", 0.3f));
	m_Far = (std::max)(m_Near + 0.01f, j.value("farClipPlane", 1000.0f));
	m_CustomCubemap = j.value("customCubemap", std::string());
	m_BakedTexture = j.value("bakedTexture", std::string());
	++m_SettingsVersion;
}
