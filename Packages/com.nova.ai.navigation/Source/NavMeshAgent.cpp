#include "pch.h"
#include "NavMeshAgent.h"
#include "NavMeshSurface.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"

namespace
{
	std::vector<NavMeshAgent*> s_Agents;

	float Horizontal(const Vec3& a, const Vec3& b)
	{
		const float dx = b.x - a.x, dz = b.z - a.z;
		return std::sqrt(dx * dx + dz * dz);
	}
}

NavMeshAgent::NavMeshAgent()
{
	m_InspectorTitleName = "Nav Mesh Agent";
	s_Agents.push_back(this);
}

NavMeshAgent::~NavMeshAgent()
{
	s_Agents.erase(std::remove(s_Agents.begin(), s_Agents.end(), this), s_Agents.end());
}

const std::vector<NavMeshAgent*>& NavMeshAgent::All() { return s_Agents; }

bool NavMeshAgent::SetDestination(const Vec3& target)
{
	if (m_pGameObject == nullptr)
		return false;
	const Vec3 feet = m_pGameObject->GetTransform()->GetPosition() - Vec3(0.0f, BaseOffset, 0.0f);
	const NavGrid* grid = NavMeshSurface::FindGrid(feet);
	std::vector<Vec3> path;
	if (grid == nullptr || !grid->FindPath(feet, target, path) || path.size() < 2)
	{
		HasPath = false;
		Corners.clear();
		return false;
	}
	Corners = std::move(path);
	Next = 1;
	Destination = target;
	HasPath = true;
	return true;
}

void NavMeshAgent::ResetPath()
{
	HasPath = false;
	Corners.clear();
	Next = 0;
}

bool NavMeshAgent::Warp(const Vec3& position)
{
	if (m_pGameObject == nullptr)
		return false;
	Vec3 p = position;
	if (const NavGrid* grid = NavMeshSurface::FindGrid(p))
	{
		float y;
		if (grid->GroundHeight(p, y))
			p.y = y;
	}
	m_pGameObject->GetTransform()->SetPosition(p + Vec3(0.0f, BaseOffset, 0.0f));
	Velocity = Vec3::Zero;
	ResetPath();
	return true;
}

bool NavMeshAgent::IsOnNavMesh() const
{
	if (m_pGameObject == nullptr)
		return false;
	const Vec3 feet = m_pGameObject->GetTransform()->GetPosition() - Vec3(0.0f, BaseOffset, 0.0f);
	const NavGrid* grid = NavMeshSurface::FindGrid(feet);
	int x, z, l;
	return grid && grid->Sample(feet, grid->Settings.CellSize * 1.5f, x, z, l);
}

float NavMeshAgent::RemainingDistance() const
{
	if (!HasPath || m_pGameObject == nullptr || Next >= Corners.size())
		return HasPath ? 0.0f : std::numeric_limits<float>::infinity();
	const Vec3 feet = m_pGameObject->GetTransform()->GetPosition() - Vec3(0.0f, BaseOffset, 0.0f);
	float d = Horizontal(feet, Corners[Next]);
	for (size_t i = Next + 1; i < Corners.size(); ++i)
		d += Vec3::Distance(Corners[i - 1], Corners[i]);
	return d;
}

void NavMeshAgent::Update()
{
	if (!Application::IsPlaying() || m_pGameObject == nullptr)
		return;
	const float dt = (std::min)((float)DT, 0.1f);
	if (dt <= 0.0f)
		return;
	Transform* tr = m_pGameObject->GetTransform();
	Vec3 feet = tr->GetPosition() - Vec3(0.0f, BaseOffset, 0.0f);

	Vec3 desired = Vec3::Zero;
	if (HasPath && !IsStopped && Next < Corners.size())
	{
		// 지나간 꺾이는 점 넘기기 (마지막 점 제외)
		const float pass = (std::max)(0.05f, Velocity.Length() * dt * 1.5f);
		while (Next + 1 < Corners.size() && Horizontal(feet, Corners[Next]) <= pass)
			++Next;
		const float remaining = RemainingDistance();
		if (remaining <= (std::max)(StoppingDistance, 0.02f))
		{
			Velocity = Vec3::Zero;   // 도착 (Unity 처럼 경로는 남겨 둔다: remainingDistance ≈ 0)
		}
		else
		{
			Vec3 dir = Corners[Next] - feet;
			dir.y = 0.0f;
			const float len = dir.Length();
			if (len > 1e-5f)
				dir /= len;
			float speed = Speed;
			if (AutoBraking)   // 끝에서 멈출 수 있는 속도: v = √(2·a·d)
				speed = (std::min)(speed, std::sqrt(2.0f * (std::max)(0.01f, Acceleration) * (std::max)(0.0f, remaining - StoppingDistance)));
			desired = dir * speed;
		}
	}
	// 가속 (목표 속도 쪽으로 Acceleration 만큼)
	Vec3 dv = desired - Velocity;
	const float maxDv = (std::max)(0.0f, Acceleration) * dt;
	if (dv.Length() > maxDv && dv.Length() > 1e-6f)
		dv = dv / dv.Length() * maxDv;
	Velocity += dv;
	Velocity.y = 0.0f;

	// 다른 에이전트와 겹치지 않게 밀어내기 (반지름 합 안쪽 — Unity 의 회피를 단순하게: 우선순위가 높은(숫자 작은) 쪽은 덜 밀린다)
	Vec3 push = Vec3::Zero;
	for (NavMeshAgent* other : s_Agents)
	{
		if (other == this || other->m_pGameObject == nullptr || !other->IsEnabled() || !other->m_pGameObject->IsActive())
			continue;
		const Vec3 op = other->m_pGameObject->GetTransform()->GetPosition() - Vec3(0.0f, other->BaseOffset, 0.0f);
		if (fabsf(op.y - feet.y) > (std::max)(Height, other->Height))
			continue;   // 다른 층
		Vec3 d = feet - op;
		d.y = 0.0f;
		const float dist = d.Length();
		const float minDist = Radius + other->Radius;
		if (dist >= minDist)
			continue;
		const Vec3 dir = dist > 1e-4f ? d / dist : Vec3((float)((size_t)this % 7) - 3.0f, 0.0f, 1.0f) / 3.2f;   // 정확히 겹치면 아무 쪽으로
		const float share = AvoidancePriority == other->AvoidancePriority ? 0.5f : (AvoidancePriority > other->AvoidancePriority ? 0.8f : 0.2f);
		push += dir * ((minDist - dist) * share);
	}
	if (push.LengthSquared() > 1e-10f)
	{
		const float maxPush = (std::max)(Speed, 1.0f) * dt;   // 한 프레임에 너무 튀지 않게
		if (push.Length() > maxPush)
			push = push / push.Length() * maxPush;
		const Vec3 pushed = feet + push;
		float y;
		if (const NavGrid* grid = NavMeshSurface::FindGrid(pushed))
			if (grid->GroundHeight(pushed, y) && fabsf(y - feet.y) <= grid->Settings.StepHeight + 0.05f)
				feet = pushed;   // 걸을 수 있는 곳으로만 민다
	}
	if (Velocity.LengthSquared() < 1e-8f)
	{
		tr->SetPosition(feet + Vec3(0.0f, BaseOffset, 0.0f));
		return;
	}

	feet += Velocity * dt;
	if (const NavGrid* grid = NavMeshSurface::FindGrid(feet))
	{
		float y;
		if (grid->GroundHeight(feet, y))
			feet.y = y;
	}
	tr->SetPosition(feet + Vec3(0.0f, BaseOffset, 0.0f));

	// 움직이는 쪽으로 돌기 (Angular Speed 도/초)
	if (Velocity.LengthSquared() > 0.01f)
	{
		const float targetYaw = XMConvertToDegrees(atan2f(Velocity.x, Velocity.z));
		Vec3 e = tr->GetEulerAngle();
		float delta = fmodf(targetYaw - e.y + 540.0f, 360.0f) - 180.0f;
		const float maxTurn = AngularSpeed * dt;
		delta = std::clamp(delta, -maxTurn, maxTurn);
		tr->SetEulerAngle(Vec3(e.x, e.y + delta, e.z));
	}
}

void NavMeshAgent::OnInspectorGUI()
{
	UnityGUI::Label("Steering", 0, true);
	UnityGUI::Float("Speed", &Speed, 1);
	UnityGUI::Float("Angular Speed", &AngularSpeed, 1);
	UnityGUI::Float("Acceleration", &Acceleration, 1);
	UnityGUI::Float("Stopping Distance", &StoppingDistance, 1);
	UnityGUI::Toggle("Auto Braking", &AutoBraking, 1);
	UnityGUI::Label("Obstacle Avoidance", 0, true);
	UnityGUI::Float("Radius", &Radius, 1);
	UnityGUI::Float("Height", &Height, 1);
	UnityGUI::Float("Base Offset", &BaseOffset, 1);
	if (UnityGUI::Int("Priority", &AvoidancePriority, 1)) AvoidancePriority = std::clamp(AvoidancePriority, 0, 99);
	if (Application::IsPlaying())
	{
		char buf[96];
		snprintf(buf, sizeof(buf), "%s, %.2f m left, %.2f m/s", HasPath ? "has path" : "no path", HasPath ? RemainingDistance() : 0.0f, Velocity.Length());
		UnityGUI::ValueLabel("State", buf);
	}
	if (NavMeshSurface::All().empty())
		UnityGUI::HelpBox("Add a NavMesh Surface to the scene and press Bake.", false);
}

void NavMeshAgent::OnDrawGizmos()
{
	if (m_pGameObject == nullptr || !SceneViewOverlay::IsActive() || SelectionManager::GetSelectedObjectType() != SelectionType::GAMEOBJECT ||
		SelectionManager::GetSelectedGameObject() != m_pGameObject)
		return;
	if (!HasPath || Corners.size() < 2)
		return;
	const ImU32 c = IM_COL32(255, 220, 60, 255);
	for (size_t i = (std::max)((size_t)1, Next); i < Corners.size(); ++i)
	{
		const Vec3 a = (i == Next) ? m_pGameObject->GetTransform()->GetPosition() - Vec3(0.0f, BaseOffset, 0.0f) : Corners[i - 1];
		const Vec3 b = Corners[i];
		SceneViewOverlay::DrawLine(XMFLOAT3(a.x, a.y + 0.05f, a.z), XMFLOAT3(b.x, b.y + 0.05f, b.z), c, 2.0f);
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(NavMeshAgent)
{
	json j;
	j["type"] = "NavMeshAgent";
	j["enabled"] = m_Enabled;
	j["speed"] = Speed;
	j["angularSpeed"] = AngularSpeed;
	j["acceleration"] = Acceleration;
	j["stoppingDistance"] = StoppingDistance;
	j["autoBraking"] = AutoBraking;
	j["radius"] = Radius;
	j["height"] = Height;
	j["baseOffset"] = BaseOffset;
	j["avoidancePriority"] = AvoidancePriority;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(NavMeshAgent)
{
	m_Enabled = j.value("enabled", true);
	Speed = j.value("speed", 3.5f);
	AngularSpeed = j.value("angularSpeed", 120.0f);
	Acceleration = j.value("acceleration", 8.0f);
	StoppingDistance = j.value("stoppingDistance", 0.0f);
	AutoBraking = j.value("autoBraking", true);
	Radius = j.value("radius", 0.5f);
	Height = j.value("height", 2.0f);
	BaseOffset = j.value("baseOffset", 0.0f);
	AvoidancePriority = j.value("avoidancePriority", 50);
}
