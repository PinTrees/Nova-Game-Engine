#include "pch.h"
#include "NavMeshSurface.h"
#include "NavMeshLink.h"
#include "NavMeshAgent.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include "UndoSystem.h"
#include "Debug.h"
#include "Physics2DComponents.h"
#include "SpriteBatch.h"
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

std::shared_ptr<NavData> NavMeshSurface::FindDataShared(const Vec3& p)
{
	std::shared_ptr<NavData> fallback;
	for (NavMeshSurface* s : s_All)
	{
		if (s->GetGameObject() == nullptr || !s->IsEnabled() || !ActiveInHierarchy(s->GetGameObject()))
			continue;
		const NavData* g = s->GetData();
		if (g == nullptr || g->IsEmpty())
			continue;
		if (g->ContainsWorld(p))   // 2D 표면은 (x, y) 로
			return s->m_Data;
		if (fallback == nullptr)
			fallback = s->m_Data;
	}
	return fallback;
}

const NavData* NavMeshSurface::FindData(const Vec3& p)
{
	return FindDataShared(p).get();
}

namespace
{
	bool SameLinks(const std::vector<NavLink>& a, const std::vector<NavLink>& b)
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); ++i)
			if (Vec3::DistanceSquared(a[i].Start, b[i].Start) > 1e-6f || Vec3::DistanceSquared(a[i].End, b[i].End) > 1e-6f ||
				a[i].Width != b[i].Width || a[i].Bidirectional != b[i].Bidirectional)
				return false;
		return true;
	}
}

void NavMeshSurface::LastUpdate()
{
	NavData* data = GetData() ? m_Data.get() : nullptr;
	if (data == nullptr)
		return;
	std::vector<NavLink> links;
	NavMeshLink::Collect(links);
	if (!SameLinks(links, data->GetLinks()))
		data->SetLinks(links);
	data->UpdateObstacles();
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
	std::wstring path = dir + L"NavMesh-" + string_to_wstring(name) + L".navmesh";
	std::replace(path.begin(), path.end(), L'/', L'\\');
	return path;
}

const NavData* NavMeshSurface::GetData()
{
	if (DataPath.empty())
		return m_Data && !m_Data->IsEmpty() ? m_Data.get() : nullptr;
	if (m_LoadedPath != DataPath)
	{
		m_LoadedPath = DataPath;   // 실패해도 매 프레임 다시 읽지 않게
		auto g = std::make_shared<NavData>();
		std::vector<NavLink> links;
		NavMeshLink::Collect(links);
		g->SetLinks(links);   // 타일을 만들 때 같이 넣는다
		if (g->Load(PathManager::GetI()->GetMovePathW(string_to_wstring(DataPath))))
			m_Data = g;
		else
		{
			m_Data.reset();
			EditorLog::Write("NavMesh", "could not load %s (bake again)", DataPath.c_str());
		}
	}
	return m_Data ? m_Data.get() : nullptr;
}

namespace
{
	float Cross2(const Vec2& o, const Vec2& a, const Vec2& b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

	// 닫힌 고리 → 삼각형 (귀 자르기 — 오목해도 된다)
	void Triangulate(std::vector<Vec2> poly, std::vector<NavBlocker2D>& out)
	{
		if (poly.size() < 3)
			return;
		float area = 0.0f;
		for (size_t i = 0; i < poly.size(); ++i)
			area += poly[i].x * poly[(i + 1) % poly.size()].y - poly[(i + 1) % poly.size()].x * poly[i].y;
		if (area < 0.0f)
			std::reverse(poly.begin(), poly.end());   // 반시계로
		auto push = [&](const Vec2& a, const Vec2& b, const Vec2& c) { out.push_back({ { a.x, b.x, c.x }, { a.y, b.y, c.y } }); };
		int guard = (int)poly.size() * (int)poly.size() + 8;
		while (poly.size() > 3 && guard-- > 0)
		{
			bool clipped = false;
			for (size_t i = 0; i < poly.size(); ++i)
			{
				const Vec2& a = poly[(i + poly.size() - 1) % poly.size()];
				const Vec2& b = poly[i];
				const Vec2& c = poly[(i + 1) % poly.size()];
				if (Cross2(a, b, c) <= 1e-9f)
					continue;   // 오목한 꼭짓점
				bool inside = false;
				for (size_t k = 0; k < poly.size() && !inside; ++k)
				{
					const Vec2& q = poly[k];
					if (&q == &a || &q == &b || &q == &c)
						continue;
					inside = Cross2(a, b, q) > 0.0f && Cross2(b, c, q) > 0.0f && Cross2(c, a, q) > 0.0f;
				}
				if (inside)
					continue;
				push(a, b, c);
				poly.erase(poly.begin() + (long long)i);
				clipped = true;
				break;
			}
			if (!clipped)
				break;   // 꼬인 고리 — 남은 것은 부채꼴로
		}
		for (size_t i = 1; i + 1 < poly.size(); ++i)
			push(poly[0], poly[i], poly[i + 1]);
	}
}

bool NavMeshSurface::Bake2D(NavData& grid, std::string& log)
{
	// 범위: Volume = 상자의 XY, All = 2D 콜라이더 · 스프라이트를 모두 담는 사각형
	Vec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
	auto grow = [&](const Vec3& p) { mn.x = (std::min)(mn.x, p.x); mn.y = (std::min)(mn.y, p.y); mx.x = (std::max)(mx.x, p.x); mx.y = (std::max)(mx.y, p.y); };
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (CollectObjects == 1 && m_pGameObject)
	{
		const Matrix world = m_pGameObject->GetTransform()->GetWorldMatrix();
		for (int i = 0; i < 4; ++i)
			grow(Vec3::Transform(Center + Vec3((i & 1 ? 0.5f : -0.5f) * Size.x, (i & 2 ? 0.5f : -0.5f) * Size.y, 0.0f), world));
	}
	std::vector<NavBlocker2D> blockers;
	int colliders = 0;
	if (scene)
		for (GameObject* go : scene->GetAllGameObjects())
		{
			if (!ActiveInHierarchy(go))
				continue;
			// 움직이는 것 (Dynamic · Kinematic Rigidbody 2D) 은 굽지 않는다 (Unity 와 같이 정적인 것만)
			bool moving = false;
			for (GameObject* g = go; g && !moving; g = g->GetParent())
				if (Rigidbody2D* rb = g->GetComponent<Rigidbody2D>())
					moving = rb->Type != Rigidbody2D::BodyType::Static;
			const Matrix m = go->GetTransform()->GetWorldMatrix();
			if (CollectObjects == 0)
				for (const auto& c : go->GetComponents())
					if (auto* s = dynamic_cast<SpriteSource*>(c.get()); s && s->SpriteEnabled())
					{
						Vec3 bmin, bmax;
						if (s->SpriteLocalBounds(bmin, bmax))
						{
							grow(Vec3::Transform(Vec3(bmin.x, bmin.y, 0), m));
							grow(Vec3::Transform(Vec3(bmax.x, bmax.y, 0), m));
						}
					}
			if (moving)
				continue;
			for (const auto& c : go->GetComponents())
			{
				auto* col = dynamic_cast<Collider2D*>(c.get());
				if (col == nullptr || !col->IsEnabled() || col->IsTrigger)
					continue;
				std::vector<std::vector<Vec2>> loops;
				col->Outline(loops);
				const bool edge = dynamic_cast<EdgeCollider2D*>(col) != nullptr;
				for (const auto& loop : loops)
				{
					std::vector<Vec2> w;
					for (const Vec2& p : loop)
					{
						const Vec3 v = Vec3::Transform(Vec3(p.x, p.y, 0.0f), m);
						w.push_back(Vec2(v.x, v.y));
						if (CollectObjects == 0) grow(v);
					}
					if (edge)
					{
						// 선 (Edge Collider 2D): 마디마다 얇은 사각형
						for (size_t i = 0; i + 1 < w.size(); ++i)
						{
							Vec2 d = w[i + 1] - w[i];
							if (d.LengthSquared() < 1e-10f) continue;
							d.Normalize();
							const Vec2 n(-d.y * 0.05f, d.x * 0.05f);
							Triangulate({ w[i] - n, w[i + 1] - n, w[i + 1] + n, w[i] + n }, blockers);
						}
					}
					else
						Triangulate(w, blockers);
				}
				++colliders;
			}
		}
	if (mn.x > mx.x || mn.y > mx.y)
	{
		log = "nothing to bake (no 2D colliders or sprites — or set Collect Objects to Volume)";
		return false;
	}
	if (CollectObjects == 0)
	{
		const float pad = Settings.AgentRadius + Settings.CellSize * 2.0f;
		mn -= Vec2(pad, pad);
		mx += Vec2(pad, pad);
	}
	// 바닥 = 사각형 하나 (내비 공간 (x, 0, y), 위를 보게)
	const std::vector<float> verts = { mn.x, 0.0f, mn.y, mn.x, 0.0f, mx.y, mx.x, 0.0f, mx.y, mx.x, 0.0f, mn.y };
	const std::vector<int> tris = { 0, 1, 2, 0, 2, 3 };
	const Vec3 bmin(mn.x - 0.1f, -0.5f, mn.y - 0.1f), bmax(mx.x + 0.1f, Settings.AgentHeight + 0.5f, mx.y + 0.1f);
	grid.Plane2D = true;
	const bool ok = grid.Bake(verts, tris, bmin, bmax, Settings, log, &blockers);
	grid.Plane2D = true;   // Bake 가 Reset 해도
	if (ok)
		log += " (2D: " + std::to_string(colliders) + " colliders, " + std::to_string(blockers.size()) + " blocker triangles)";
	return ok;
}

bool NavMeshSurface::Bake(std::string& log)
{
	// 편집 중: 이번 프레임에 붙인(아직 목록에 들어가지 않은) 컴포넌트도 굽기에 넣는다 (CLI·스크립트가 붙인 직후 굽는 경우)
	if (!Application::IsPlaying())
		if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
			for (GameObject* go : scene->GetAllGameObjects())
				go->ApplyPendingComponents();
	if (Plane == 1)
	{
		auto grid = std::make_shared<NavData>();
		if (!Bake2D(*grid, log))
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
		m_Data = grid;
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
	// 상자 안의 정적 콜라이더 삼각형 (Unity 의 Use Geometry = Physics Colliders) → Recast
	std::vector<float> verts;
	std::vector<int> tris;
	if (haveBounds)
	{
		bmin -= Vec3(0.1f, 0.1f, 0.1f);
		bmax += Vec3(0.1f, Settings.AgentHeight, 0.1f);
		pm->CollectStaticTriangles(bmin, bmax, verts, tris);
	}
	auto grid = std::make_shared<NavData>();
	{
		std::vector<NavLink> links;
		NavMeshLink::Collect(links);
		grid->SetLinks(links);
	}
	bool ok = haveBounds && grid->Bake(verts, tris, bmin, bmax, Settings, log);
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
	m_Data = grid;
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
	m_Data.reset();
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
	static const char* kPlane[] = { "3D (XZ ground)", "2D (XY, top-down)" };
	UnityGUI::Dropdown("Plane", &Plane, kPlane, 2);
	UnityGUI::Label("Agent", 0, true);
	if (UnityGUI::Float("Radius", &Settings.AgentRadius, 1)) Settings.AgentRadius = (std::max)(0.0f, Settings.AgentRadius);
	if (Plane == 0)   // 2D 는 평면 — 높이 · 경사 · 턱이 없다
	{
		if (UnityGUI::Float("Height", &Settings.AgentHeight, 1)) Settings.AgentHeight = (std::max)(0.1f, Settings.AgentHeight);
		UnityGUI::Slider("Max Slope", &Settings.MaxSlope, 0.0f, 60.0f, 1);
		if (UnityGUI::Float("Step Height", &Settings.StepHeight, 1)) Settings.StepHeight = (std::max)(0.0f, Settings.StepHeight);
	}
	static const char* kCollect[] = { "All Game Objects", "Volume" };
	UnityGUI::Dropdown("Collect Objects", &CollectObjects, kCollect, 2);
	if (CollectObjects == 1)
	{
		UnityGUI::Vector3("Size", &Size.x);
		UnityGUI::Vector3("Center", &Center.x);
	}
	if (Plane == 1)
		UnityGUI::HelpBox("2D: the whole area is the floor; static 2D colliders and tilemap colliders are walls. Agents move on X/Y.", false);
	if (UnityGUI::Float("Voxel Size", &Settings.CellSize)) Settings.CellSize = std::clamp(Settings.CellSize, 0.05f, 5.0f);
	UnityGUI::Toggle("Show NavMesh", &ShowNavMesh);
	UnityGUI::ValueLabel("Data", DataPath.empty() ? "None" : DataPath.c_str());
	if (const NavData* g = GetData())
	{
		char buf[96];
		snprintf(buf, sizeof(buf), "%d tiles, %d polygons", g->TileCount, g->PolyCount);
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
	// Unity 처럼 에이전트를 골라도 내비 메시를 보여 준다 (상자는 표면을 골랐을 때만)
	const bool self = IsSelected(m_pGameObject);
	GameObject* sel = SceneViewOverlay::IsActive() && SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT ? SelectionManager::GetSelectedGameObject() : nullptr;
	if (!self && (sel == nullptr || sel->GetComponent<NavMeshAgent>() == nullptr))
		return;
	if (self && CollectObjects == 1)
	{
		const Matrix world = m_pGameObject->GetTransform()->GetWorldMatrix();
		Vec3 c[8];
		for (int i = 0; i < 8; ++i)
			c[i] = Vec3::Transform(Center + Vec3((i & 1 ? 0.5f : -0.5f) * Size.x, (i & 2 ? 0.5f : -0.5f) * Size.y, (i & 4 ? 0.5f : -0.5f) * Size.z), world);
		const int e[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
		for (auto& p : e)
			SceneViewOverlay::DrawLine(XMFLOAT3(c[p[0]].x, c[p[0]].y, c[p[0]].z), XMFLOAT3(c[p[1]].x, c[p[1]].y, c[p[1]].z), IM_COL32(120, 200, 255, 200), 1.0f);
	}
	// Unity 처럼 다각형을 반투명 파랑 + 바깥 가장자리 (2D 표면은 이 오브젝트의 z 앞에)
	if (m_Data && m_pGameObject)
		m_Data->Gizmo2DZ = m_pGameObject->GetTransform()->GetPosition().z;
	if (const NavData* g = ShowNavMesh ? GetData() : nullptr)
		g->DrawGizmo(IM_COL32(60, 140, 255, 90), IM_COL32(20, 70, 160, 220), 200000);
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
	j["plane"] = Plane;
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
	Plane = j.value("plane", 0);
	if (j.contains("center") && j["center"].is_array() && j["center"].size() == 3)
		Center = Vec3(j["center"][0].get<float>(), j["center"][1].get<float>(), j["center"][2].get<float>());
	if (j.contains("size") && j["size"].is_array() && j["size"].size() == 3)
		Size = Vec3(j["size"][0].get<float>(), j["size"][1].get<float>(), j["size"][2].get<float>());
	ShowNavMesh = j.value("showNavMesh", true);
	DataPath = j.value("data", "");
}
