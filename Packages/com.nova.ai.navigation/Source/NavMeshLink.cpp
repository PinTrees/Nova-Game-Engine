#include "pch.h"
#include "NavMeshLink.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"

namespace
{
	std::vector<NavMeshLink*> s_Links;

	bool ActiveInHierarchy(GameObject* go)
	{
		for (GameObject* g = go; g; g = g->GetParent())
			if (!g->IsActive())
				return false;
		return true;
	}
}

NavMeshLink::NavMeshLink()
{
	m_InspectorTitleName = "NavMesh Link";
	s_Links.push_back(this);
}

NavMeshLink::~NavMeshLink()
{
	s_Links.erase(std::remove(s_Links.begin(), s_Links.end(), this), s_Links.end());
}

const std::vector<NavMeshLink*>& NavMeshLink::All() { return s_Links; }

void NavMeshLink::Collect(std::vector<NavLink>& out)
{
	out.clear();
	for (NavMeshLink* l : s_Links)
	{
		if (l->m_pGameObject == nullptr || !l->IsEnabled() || !ActiveInHierarchy(l->m_pGameObject))
			continue;
		NavLink link;
		link.Start = l->WorldStart();
		link.End = l->WorldEnd();
		link.Width = l->Width;
		link.Bidirectional = l->Bidirectional;
		out.push_back(link);
	}
}

Vec3 NavMeshLink::ToWorld(const Vec3& local, uint64 target) const
{
	if (target != 0)
		if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
			if (GameObject* go = scene->FindByFileID(target))
				return go->GetTransform()->GetPosition();
	if (m_pGameObject == nullptr)
		return local;
	Transform* t = m_pGameObject->GetTransform();
	return t->GetPosition() + Vec3::Transform(local, t->GetRotation());
}

Vec3 NavMeshLink::WorldStart() const { return ToWorld(StartPoint, StartTransform); }
Vec3 NavMeshLink::WorldEnd() const { return ToWorld(EndPoint, EndTransform); }

void NavMeshLink::OnInspectorGUI()
{
	UnityGUI::GameObjectField("Start Transform", &StartTransform);
	if (StartTransform == 0)
		UnityGUI::Vector3("Start Point", &StartPoint.x);
	UnityGUI::GameObjectField("End Transform", &EndTransform);
	if (EndTransform == 0)
		UnityGUI::Vector3("End Point", &EndPoint.x);
	ImGui::Dummy(ImVec2(0, 2));
	const float w = ImGui::GetContentRegionAvail().x;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + w - 80.0f);
	if (ImGui::Button("Swap", ImVec2(80, 0)))
	{
		std::swap(StartPoint, EndPoint);
		std::swap(StartTransform, EndTransform);
	}
	if (UnityGUI::Float("Width", &Width)) Width = (std::max)(0.0f, Width);
	UnityGUI::Toggle("Bidirectional", &Bidirectional);
	UnityGUI::HelpBox("Put each end on (or just above) a baked NavMesh. Agents walking a path through the link are moved from start to end.", false);
}

void NavMeshLink::OnDrawGizmos()
{
	if (m_pGameObject == nullptr || !SceneViewOverlay::IsActive() || SelectionManager::GetSelectedObjectType() != SelectionType::GAMEOBJECT ||
		SelectionManager::GetSelectedGameObject() != m_pGameObject)
		return;
	const Vec3 a = WorldStart(), b = WorldEnd();
	const float len = Vec3::Distance(Vec3(a.x, 0, a.z), Vec3(b.x, 0, b.z));
	const ImU32 c = IM_COL32(255, 196, 0, 255);
	Vec3 prev = a;
	for (int k = 1; k <= 16; ++k)
	{
		const float u = k / 16.0f;
		const Vec3 p = a + (b - a) * u + Vec3(0.0f, len * 0.25f * 4.0f * u * (1.0f - u), 0.0f);
		SceneViewOverlay::DrawLine(XMFLOAT3(prev.x, prev.y, prev.z), XMFLOAT3(p.x, p.y, p.z), c, 2.0f);
		prev = p;
	}
	// 끝점 표시 (작은 십자)
	for (const Vec3& p : { a, b })
	{
		const float r = (std::max)(0.15f, Width * 0.5f);
		SceneViewOverlay::DrawLine(XMFLOAT3(p.x - r, p.y, p.z), XMFLOAT3(p.x + r, p.y, p.z), c, 2.0f);
		SceneViewOverlay::DrawLine(XMFLOAT3(p.x, p.y, p.z - r), XMFLOAT3(p.x, p.y, p.z + r), c, 2.0f);
		SceneViewOverlay::DrawLine(XMFLOAT3(p.x, p.y, p.z), XMFLOAT3(p.x, p.y + 0.5f, p.z), c, 2.0f);
	}
}

void NavMeshLink::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	if (auto it = map.find(StartTransform); it != map.end()) StartTransform = it->second;
	if (auto it = map.find(EndTransform); it != map.end()) EndTransform = it->second;
}

GENERATE_COMPONENT_FUNC_TOJSON(NavMeshLink)
{
	json j;
	j["type"] = "NavMeshLink";
	j["enabled"] = m_Enabled;
	j["startPoint"] = { StartPoint.x, StartPoint.y, StartPoint.z };
	j["endPoint"] = { EndPoint.x, EndPoint.y, EndPoint.z };
	j["startTransform"] = StartTransform;
	j["endTransform"] = EndTransform;
	j["width"] = Width;
	j["bidirectional"] = Bidirectional;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(NavMeshLink)
{
	auto vec = [&](const char* key, Vec3& v) {
		if (j.contains(key) && j[key].is_array() && j[key].size() == 3)
			v = Vec3(j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>());
	};
	m_Enabled = j.value("enabled", true);
	vec("startPoint", StartPoint);
	vec("endPoint", EndPoint);
	StartTransform = j.value("startTransform", (uint64)0);
	EndTransform = j.value("endTransform", (uint64)0);
	Width = j.value("width", 0.0f);
	Bidirectional = j.value("bidirectional", true);
}
