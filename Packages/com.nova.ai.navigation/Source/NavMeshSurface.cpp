#include "pch.h"
#include "NavMeshSurface.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include "UndoSystem.h"
#include "Debug.h"
#include <filesystem>

namespace
{
	std::vector<NavMeshSurface*> s_All;

	bool IsSelected(GameObject* go)
	{
		return go && SceneViewOverlay::IsActive() && SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT &&
			SelectionManager::GetSelectedGameObject() == go;
	}

	bool ActiveInHierarchy(GameObject* go)
	{
		for (GameObject* g = go; g; g = g->GetParent())
			if (!g->IsActive())
				return false;
		return true;
	}
}

NavMeshSurface::NavMeshSurface()
{
	m_InspectorTitleName = "NavMesh Surface";
	s_All.push_back(this);
}

NavMeshSurface::~NavMeshSurface()
{
	s_All.erase(std::remove(s_All.begin(), s_All.end(), this), s_All.end());
}

const std::vector<NavMeshSurface*>& NavMeshSurface::All() { return s_All; }

const NavGrid* NavMeshSurface::FindGrid(const Vec3& p)
{
	const NavGrid* fallback = nullptr;
	for (NavMeshSurface* s : s_All)
	{
		if (s->GetGameObject() == nullptr || !s->IsEnabled() || !ActiveInHierarchy(s->GetGameObject()))
			continue;
		const NavGrid* g = s->GetGrid();
		if (g == nullptr || g->IsEmpty())
			continue;
		const float cs = g->Settings.CellSize;
		if (p.x >= g->Origin.x && p.z >= g->Origin.z && p.x <= g->Origin.x + g->Width * cs && p.z <= g->Origin.z + g->Depth * cs)
			return g;
		if (fallback == nullptr)
			fallback = g;
	}
	return fallback;
}

std::wstring NavMeshSurface::DefaultDataPath() const
{
	// 씬 옆 폴더 (Unity: Assets/Scenes/<씬>/NavMesh-<오브젝트>.asset)
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	std::wstring dir = L"Assets\\";
	if (scene && !scene->GetScenePath().empty())
	{
		std::filesystem::path sp(scene->GetScenePath());
		dir = (sp.parent_path() / sp.stem()).wstring() + L"\\";
	}
	std::string name = m_pGameObject ? m_pGameObject->GetName() : std::string("Surface");
	for (char& c : name)
		if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
			c = '_';
	std::wstring path = dir + L"NavMesh-" + string_to_wstring(name) + L".navgrid";
	std::replace(path.begin(), path.end(), L'/', L'\\');
	return path;
}

const NavGrid* NavMeshSurface::GetGrid()
{
	if (DataPath.empty())
		return m_Grid && !m_Grid->IsEmpty() ? m_Grid.get() : nullptr;
	if (m_LoadedPath != DataPath)
	{
		m_LoadedPath = DataPath;   // 실패해도 매 프레임 다시 읽지 않게
		auto g = std::make_shared<NavGrid>();
		if (g->Load(PathManager::GetI()->GetMovePathW(string_to_wstring(DataPath))))
			m_Grid = g;
		else
		{
			m_Grid.reset();
			EditorLog::Write("NavMesh", "could not load %s", DataPath.c_str());
		}
	}
	return m_Grid ? m_Grid.get() : nullptr;
}

bool NavMeshSurface::Bake(std::string& log)
{
	// 편집 중: 이번 프레임에 붙인(아직 목록에 들어가지 않은) 컴포넌트도 굽기에 넣는다 (CLI·스크립트가 붙인 직후 굽는 경우)
	if (!Application::IsPlaying())
		if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
			for (GameObject* go : scene->GetAllGameObjects())
				go->ApplyPendingComponents();
	PhysicsManager* pm = PhysicsManager::GetI();
	const bool temp = !Application::IsPlaying();
	if (temp && !pm->BeginEditQueries())
	{
		log = "could not create a physics world for baking";
		return false;
	}
	Vec3 bmin, bmax;
	bool haveBounds = true;
	if (CollectObjects == 1 && m_pGameObject)
	{
		const Matrix world = m_pGameObject->GetTransform()->GetWorldMatrix();
		bmin = Vec3(FLT_MAX, FLT_MAX, FLT_MAX);
		bmax = Vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		for (int i = 0; i < 8; ++i)
		{
			const Vec3 c = Center + Vec3((i & 1 ? 0.5f : -0.5f) * Size.x, (i & 2 ? 0.5f : -0.5f) * Size.y, (i & 4 ? 0.5f : -0.5f) * Size.z);
			const Vec3 w = Vec3::Transform(c, world);
			bmin = Vec3::Min(bmin, w);
			bmax = Vec3::Max(bmax, w);
		}
	}
	else
		haveBounds = pm->GetWorldBounds(bmin, bmax);
	auto grid = std::make_shared<NavGrid>();
	bool ok = haveBounds && grid->Bake(bmin, bmax, Settings, log);
	if (!haveBounds)
		log = "nothing to bake (no colliders in the scene)";
	if (temp)
		pm->EndEditQueries();
	if (!ok)
	{
		m_LastLog = "Bake failed: " + log;
		EditorLog::Write("NavMesh", "bake failed: %s", log.c_str());
		return false;
	}
	const std::wstring rel = DataPath.empty() ? DefaultDataPath() : string_to_wstring(DataPath);
	const std::wstring full = PathManager::GetI()->GetMovePathW(rel);
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(full).parent_path(), ec);
	if (!grid->Save(full))
	{
		log = "could not write " + wstring_to_string(full);
		m_LastLog = log;
		return false;
	}
	DataPath = wstring_to_string(rel);
	m_Grid = grid;
	m_LoadedPath = DataPath;
	m_LastLog = "Baked: " + log;
	EditorLog::Write("NavMesh", "baked %s: %s", DataPath.c_str(), log.c_str());
	if (!Application::IsPlaying() && m_pGameObject)
	{
		Undo::SetActionName("Bake NavMesh");
		Undo::Touch(m_pGameObject);
		Undo::RequestCheck();
	}
	return true;
}

void NavMeshSurface::Clear()
{
	if (!DataPath.empty())
	{
		std::error_code ec;
		std::filesystem::remove(PathManager::GetI()->GetMovePathW(string_to_wstring(DataPath)), ec);
	}
	DataPath.clear();
	m_LoadedPath.clear();
	m_Grid.reset();
	m_LastLog = "Cleared";
	if (!Application::IsPlaying() && m_pGameObject)
	{
		Undo::SetActionName("Clear NavMesh");
		Undo::Touch(m_pGameObject);
		Undo::RequestCheck();
	}
}

void NavMeshSurface::OnInspectorGUI()
{
	UnityGUI::Label("Agent", 0, true);
	if (UnityGUI::Float("Radius", &Settings.AgentRadius, 1)) Settings.AgentRadius = (std::max)(0.0f, Settings.AgentRadius);
	if (UnityGUI::Float("Height", &Settings.AgentHeight, 1)) Settings.AgentHeight = (std::max)(0.1f, Settings.AgentHeight);
	UnityGUI::Slider("Max Slope", &Settings.MaxSlope, 0.0f, 60.0f, 1);
	if (UnityGUI::Float("Step Height", &Settings.StepHeight, 1)) Settings.StepHeight = (std::max)(0.0f, Settings.StepHeight);
	static const char* kCollect[] = { "All Game Objects", "Volume" };
	UnityGUI::Dropdown("Collect Objects", &CollectObjects, kCollect, 2);
	if (CollectObjects == 1)
	{
		UnityGUI::Vector3("Size", &Size.x);
		UnityGUI::Vector3("Center", &Center.x);
	}
	if (UnityGUI::Float("Voxel Size", &Settings.CellSize)) Settings.CellSize = std::clamp(Settings.CellSize, 0.05f, 5.0f);
	UnityGUI::Toggle("Show NavMesh", &ShowNavMesh);
	UnityGUI::ValueLabel("Data", DataPath.empty() ? "None" : DataPath.c_str());
	if (const NavGrid* g = GetGrid())
	{
		char buf[96];
		snprintf(buf, sizeof(buf), "%d x %d cells, %d walkable", g->Width, g->Depth, g->WalkableCount());
		UnityGUI::ValueLabel("Baked", buf);
	}
	ImGui::Dummy(ImVec2(0, 4));
	const float w = ImGui::GetContentRegionAvail().x;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + w - 168.0f);
	if (ImGui::Button("Clear", ImVec2(80, 0)))
		Clear();
	ImGui::SameLine();
	if (ImGui::Button("Bake", ImVec2(80, 0)))
	{
		std::string log;
		if (!Bake(log))
			Debug::LogWarning("NavMesh bake failed: " + log);
	}
	if (!m_LastLog.empty())
		UnityGUI::HelpBox(m_LastLog.c_str(), m_LastLog.rfind("Baked", 0) != 0 && m_LastLog != "Cleared");
}

void NavMeshSurface::OnDrawGizmos()
{
	if (!IsSelected(m_pGameObject))
		return;
	if (CollectObjects == 1)
	{
		const Matrix world = m_pGameObject->GetTransform()->GetWorldMatrix();
		Vec3 c[8];
		for (int i = 0; i < 8; ++i)
			c[i] = Vec3::Transform(Center + Vec3((i & 1 ? 0.5f : -0.5f) * Size.x, (i & 2 ? 0.5f : -0.5f) * Size.y, (i & 4 ? 0.5f : -0.5f) * Size.z), world);
		const int e[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
		for (auto& p : e)
			SceneViewOverlay::DrawLine(XMFLOAT3(c[p[0]].x, c[p[0]].y, c[p[0]].z), XMFLOAT3(c[p[1]].x, c[p[1]].y, c[p[1]].z), IM_COL32(120, 200, 255, 200), 1.0f);
	}
	const NavGrid* g = ShowNavMesh ? GetGrid() : nullptr;
	if (g == nullptr)
		return;
	// 걸을 수 있는 칸을 반투명 파랑으로 (칸이 많으면 묶어서)
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 rmin, rmax, off;
	SceneViewOverlay::GetViewRect(rmin, rmax, off);
	dl->PushClipRect(rmin, rmax, true);
	const int stride = (std::max)(1, (int)std::ceil(std::sqrt(g->WalkableCount() / 40000.0)));
	const float cs = g->Settings.CellSize;
	const ImU32 fill = IM_COL32(60, 140, 255, 90);
	for (int z = 0; z < g->Depth; z += stride)
		for (int x = 0; x < g->Width; x += stride)
			for (int l = 0; l < NavGrid::kLayers; ++l)
			{
				const float h = g->Height(x, z, l);
				if (std::isnan(h))
					continue;
				const float x0 = g->Origin.x + x * cs, z0 = g->Origin.z + z * cs;
				const float x1 = x0 + cs * stride * 0.92f, z1 = z0 + cs * stride * 0.92f;
				const float y = h + 0.03f;
				ImVec2 p[4];
				if (SceneViewOverlay::Project(XMFLOAT3(x0, y, z0), p[0]) && SceneViewOverlay::Project(XMFLOAT3(x1, y, z0), p[1]) &&
					SceneViewOverlay::Project(XMFLOAT3(x1, y, z1), p[2]) && SceneViewOverlay::Project(XMFLOAT3(x0, y, z1), p[3]))
					dl->AddQuadFilled(p[0], p[1], p[2], p[3], fill);
			}
	dl->PopClipRect();
}

GENERATE_COMPONENT_FUNC_TOJSON(NavMeshSurface)
{
	json j;
	j["type"] = "NavMeshSurface";
	j["enabled"] = m_Enabled;
	j["agentRadius"] = Settings.AgentRadius;
	j["agentHeight"] = Settings.AgentHeight;
	j["maxSlope"] = Settings.MaxSlope;
	j["stepHeight"] = Settings.StepHeight;
	j["voxelSize"] = Settings.CellSize;
	j["collectObjects"] = CollectObjects;
	j["center"] = { Center.x, Center.y, Center.z };
	j["size"] = { Size.x, Size.y, Size.z };
	j["showNavMesh"] = ShowNavMesh;
	j["data"] = DataPath;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(NavMeshSurface)
{
	m_Enabled = j.value("enabled", true);
	Settings.AgentRadius = j.value("agentRadius", 0.5f);
	Settings.AgentHeight = j.value("agentHeight", 2.0f);
	Settings.MaxSlope = j.value("maxSlope", 45.0f);
	Settings.StepHeight = j.value("stepHeight", 0.4f);
	Settings.CellSize = j.value("voxelSize", 0.25f);
	CollectObjects = j.value("collectObjects", 0);
	if (j.contains("center") && j["center"].is_array() && j["center"].size() == 3)
		Center = Vec3(j["center"][0].get<float>(), j["center"][1].get<float>(), j["center"][2].get<float>());
	if (j.contains("size") && j["size"].is_array() && j["size"].size() == 3)
		Size = Vec3(j["size"][0].get<float>(), j["size"][1].get<float>(), j["size"][2].get<float>());
	ShowNavMesh = j.value("showNavMesh", true);
	DataPath = j.value("data", "");
}
