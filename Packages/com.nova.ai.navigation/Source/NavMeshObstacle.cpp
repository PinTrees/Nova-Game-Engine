#include "pch.h"
#include "NavMeshObstacle.h"
#include "NavMeshSurface.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"

namespace
{
	std::vector<NavMeshObstacle*> s_Obstacles;

	bool ActiveInHierarchy(GameObject* go)
	{
		for (GameObject* g = go; g; g = g->GetParent())
			if (!g->IsActive())
				return false;
		return true;
	}
}

NavMeshObstacle::NavMeshObstacle()
{
	m_InspectorTitleName = "Nav Mesh Obstacle";
	s_Obstacles.push_back(this);
}

NavMeshObstacle::~NavMeshObstacle()
{
	RemoveCarve();
	s_Obstacles.erase(std::remove(s_Obstacles.begin(), s_Obstacles.end(), this), s_Obstacles.end());
}

const std::vector<NavMeshObstacle*>& NavMeshObstacle::All() { return s_Obstacles; }

NavObstacleShape NavMeshObstacle::WorldShape() const
{
	NavObstacleShape s;
	if (m_pGameObject == nullptr)
		return s;
	Transform* t = m_pGameObject->GetTransform();
	const Vec3 scale = t->GetScale();
	const Vec3 abs(fabsf(scale.x), fabsf(scale.y), fabsf(scale.z));
	const Vec3 c = t->GetPosition() + Vec3::Transform(Vec3(Center.x * scale.x, Center.y * scale.y, Center.z * scale.z), t->GetRotation());
	s.YRadians = XMConvertToRadians(t->GetEulerAngle().y);   // Detour 는 Y 회전만
	if (Shape == 1)
	{
		s.Box = true;
		s.Center = c;
		s.HalfExtents = Vec3(Size.x * abs.x, Size.y * abs.y, Size.z * abs.z) * 0.5f;
	}
	else
	{
		s.Box = false;
		s.Radius = Radius * (std::max)(abs.x, abs.z);
		s.Height = Height * abs.y;
		s.Center = Vec3(c.x, c.y - s.Height * 0.5f, c.z);
	}
	return s;
}

bool NavMeshObstacle::PushOut(const Vec3& feet, float radius, float height, Vec3& push) const
{
	const NavObstacleShape s = WorldShape();
	if (s.Box)
	{
		if (feet.y + height < s.Center.y - s.HalfExtents.y || feet.y > s.Center.y + s.HalfExtents.y)
			return false;
		// 상자 기준 좌표 (Y 회전만): 월드 x = lx·cos + lz·sin, z = −lx·sin + lz·cos
		const float cs = cosf(s.YRadians), sn = sinf(s.YRadians);
		const float dx = feet.x - s.Center.x, dz = feet.z - s.Center.z;
		const float lx = dx * cs - dz * sn, lz = dx * sn + dz * cs;
		const float hx = s.HalfExtents.x, hz = s.HalfExtents.z;
		const float cx = std::clamp(lx, -hx, hx), cz = std::clamp(lz, -hz, hz);
		float px = lx - cx, pz = lz - cz;
		const float dist = std::sqrt(px * px + pz * pz);
		if (dist >= radius)
			return false;
		if (dist > 1e-4f)
		{
			px = px / dist * (radius - dist);
			pz = pz / dist * (radius - dist);
		}
		else if (hx - fabsf(lx) < hz - fabsf(lz))   // 안에 들어왔다: 가까운 면 쪽으로
		{
			px = (lx >= 0 ? 1.0f : -1.0f) * (hx - fabsf(lx) + radius);
			pz = 0.0f;
		}
		else
		{
			px = 0.0f;
			pz = (lz >= 0 ? 1.0f : -1.0f) * (hz - fabsf(lz) + radius);
		}
		push = Vec3(px * cs + pz * sn, 0.0f, -px * sn + pz * cs);
		return true;
	}
	if (feet.y + height < s.Center.y || feet.y > s.Center.y + s.Height)
		return false;
	Vec3 d = feet - s.Center;
	d.y = 0.0f;
	const float dist = d.Length();
	const float minDist = s.Radius + radius;
	if (dist >= minDist)
		return false;
	const Vec3 dir = dist > 1e-4f ? d / dist : Vec3(1.0f, 0.0f, 0.0f);
	push = dir * (minDist - dist);
	return true;
}

void NavMeshObstacle::AddCarve(const std::shared_ptr<NavData>& data)
{
	m_Ref = data->AddObstacle(WorldShape());
	if (m_Ref != 0)
		m_Data = data;
}

void NavMeshObstacle::RemoveCarve()
{
	if (m_Ref != 0)
		if (std::shared_ptr<NavData> d = m_Data.lock())
			d->RemoveObstacle(m_Ref);
	m_Ref = 0;
	m_Data.reset();
}

void NavMeshObstacle::LastUpdate()
{
	if (!Application::IsPlaying() || m_pGameObject == nullptr || !IsEnabled() || !ActiveInHierarchy(m_pGameObject) || !Carve)
	{
		RemoveCarve();
		m_HaveRest = false;
		return;
	}
	const NavObstacleShape shape = WorldShape();
	std::shared_ptr<NavData> data = NavMeshSurface::FindDataShared(shape.Center);
	if (data == nullptr || data != m_Data.lock())
		RemoveCarve();   // 다시 구웠거나 표면이 없어졌다 → 새 데이터에 다시 깎는다
	if (data == nullptr)
		return;
	const Vec3 pos = m_pGameObject->GetTransform()->GetPosition();
	if (!m_HaveRest)
	{
		m_RestPos = pos;
		m_RestYaw = shape.YRadians;
		m_StillTime = 0.0f;
		m_HaveRest = true;
	}
	// Move Threshold 보다 움직이면 "움직이는 중" — 깎은 구멍을 지우고 멈추기를 기다린다
	const bool moved = Vec3::Distance(pos, m_RestPos) > MoveThreshold || fabsf(shape.YRadians - m_RestYaw) > XMConvertToRadians(3.0f);
	if (moved)
	{
		m_RestPos = pos;
		m_RestYaw = shape.YRadians;
		m_StillTime = 0.0f;
		RemoveCarve();
	}
	else
		m_StillTime += (float)DT;
	if (m_Ref == 0 && (!CarveOnlyStationary || m_StillTime >= TimeToStationary))
		AddCarve(data);
}

void NavMeshObstacle::OnInspectorGUI()
{
	static const char* kShapes[] = { "Capsule", "Box" };
	UnityGUI::Dropdown("Shape", &Shape, kShapes, 2);
	UnityGUI::Vector3("Center", &Center.x);
	if (Shape == 1)
		UnityGUI::Vector3("Size", &Size.x);
	else
	{
		if (UnityGUI::Float("Radius", &Radius)) Radius = (std::max)(0.0f, Radius);
		if (UnityGUI::Float("Height", &Height)) Height = (std::max)(0.0f, Height);
	}
	UnityGUI::Toggle("Carve", &Carve);
	if (Carve)
	{
		if (UnityGUI::Float("Move Threshold", &MoveThreshold, 1)) MoveThreshold = (std::max)(0.0f, MoveThreshold);
		if (UnityGUI::Float("Time To Stationary", &TimeToStationary, 1)) TimeToStationary = (std::max)(0.0f, TimeToStationary);
		UnityGUI::Toggle("Carve Only Stationary", &CarveOnlyStationary, 1);
		if (Application::IsPlaying())
			UnityGUI::ValueLabel("State", IsCarving() ? "carving the NavMesh" : "moving (not carving)");
	}
}

void NavMeshObstacle::OnDrawGizmos()
{
	if (m_pGameObject == nullptr || !SceneViewOverlay::IsActive() || SelectionManager::GetSelectedObjectType() != SelectionType::GAMEOBJECT ||
		SelectionManager::GetSelectedGameObject() != m_pGameObject)
		return;
	const NavObstacleShape s = WorldShape();
	const ImU32 c = IM_COL32(255, 120, 60, 230);
	auto line = [&](const Vec3& a, const Vec3& b) { SceneViewOverlay::DrawLine(XMFLOAT3(a.x, a.y, a.z), XMFLOAT3(b.x, b.y, b.z), c, 1.5f); };
	if (s.Box)
	{
		const float cs = cosf(s.YRadians), sn = sinf(s.YRadians);
		Vec3 p[8];
		for (int i = 0; i < 8; ++i)
		{
			const float lx = (i & 1 ? 1.0f : -1.0f) * s.HalfExtents.x, ly = (i & 2 ? 1.0f : -1.0f) * s.HalfExtents.y, lz = (i & 4 ? 1.0f : -1.0f) * s.HalfExtents.z;
			p[i] = s.Center + Vec3(lx * cs + lz * sn, ly, -lx * sn + lz * cs);
		}
		const int e[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
		for (auto& k : e)
			line(p[k[0]], p[k[1]]);
		return;
	}
	for (int ring = 0; ring < 2; ++ring)
	{
		const float y = s.Center.y + (ring ? s.Height : 0.0f);
		for (int i = 0; i < 24; ++i)
		{
			const float a0 = XM_2PI * i / 24.0f, a1 = XM_2PI * (i + 1) / 24.0f;
			line(Vec3(s.Center.x + cosf(a0) * s.Radius, y, s.Center.z + sinf(a0) * s.Radius), Vec3(s.Center.x + cosf(a1) * s.Radius, y, s.Center.z + sinf(a1) * s.Radius));
		}
	}
	for (int i = 0; i < 4; ++i)
	{
		const float a = XM_PIDIV2 * i;
		const Vec3 b(s.Center.x + cosf(a) * s.Radius, s.Center.y, s.Center.z + sinf(a) * s.Radius);
		line(b, b + Vec3(0.0f, s.Height, 0.0f));
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(NavMeshObstacle)
{
	json j;
	j["type"] = "NavMeshObstacle";
	j["enabled"] = m_Enabled;
	j["shape"] = Shape;
	j["center"] = { Center.x, Center.y, Center.z };
	j["size"] = { Size.x, Size.y, Size.z };
	j["radius"] = Radius;
	j["height"] = Height;
	j["carve"] = Carve;
	j["moveThreshold"] = MoveThreshold;
	j["timeToStationary"] = TimeToStationary;
	j["carveOnlyStationary"] = CarveOnlyStationary;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(NavMeshObstacle)
{
	auto vec = [&](const char* key, Vec3& v) {
		if (j.contains(key) && j[key].is_array() && j[key].size() == 3)
			v = Vec3(j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>());
	};
	m_Enabled = j.value("enabled", true);
	Shape = j.value("shape", 1);
	vec("center", Center);
	vec("size", Size);
	Radius = j.value("radius", 0.5f);
	Height = j.value("height", 2.0f);
	Carve = j.value("carve", false);
	MoveThreshold = j.value("moveThreshold", 0.1f);
	TimeToStationary = j.value("timeToStationary", 0.5f);
	CarveOnlyStationary = j.value("carveOnlyStationary", true);
}
