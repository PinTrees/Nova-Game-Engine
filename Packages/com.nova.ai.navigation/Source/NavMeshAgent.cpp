#include "pch.h"
#include "NavMeshAgent.h"
#include "NavMeshSurface.h"
#include "NavMeshObstacle.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include "NavData.h"

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

const NavData* NavMeshAgent::Locate(Vec3& feet) const
{
	const Vec3 pos = m_pGameObject->GetTransform()->GetPosition();
	const NavData* nav = NavMeshSurface::FindData(pos);
	m_2D = nav && nav->Plane2D;
	if (m_2D)
	{
		m_WorldZ = pos.z;
		feet = nav->ToNav(pos);   // 2D 는 Base Offset 없이 (회전 · 높이가 없다)
	}
	else
		feet = pos - Vec3(0.0f, BaseOffset, 0.0f);
	return nav;
}

void NavMeshAgent::Place(const Vec3& feet)
{
	m_pGameObject->GetTransform()->SetPosition(m_2D ? Vec3(feet.x, feet.z, m_WorldZ) : feet + Vec3(0.0f, BaseOffset, 0.0f));
}

bool NavMeshAgent::SetDestination(const Vec3& target)
{
	if (m_pGameObject == nullptr)
		return false;
	Vec3 feet;
	const NavData* nav = Locate(feet);
	std::vector<Vec3> path;
	std::vector<unsigned char> flags;
	Destination = target;
	if (nav == nullptr || !nav->FindPath(feet, nav->ToNav(target), path, nullptr, &flags) || path.size() < 2)
	{
		HasPath = false;
		Corners.clear();
		CornerFlags.clear();
		m_PathData = nav;
		m_PathRevision = nav ? nav->Revision : 0;
		return false;
	}
	Corners = std::move(path);
	CornerFlags = std::move(flags);
	// 링크 시작 위에 서 있으면 처음 점이 링크 시작이다
	Next = (!CornerFlags.empty() && (CornerFlags[0] & NavData::CornerLinkStart)) ? 0 : 1;
	HasPath = true;
	m_PathData = nav;
	m_PathRevision = nav->Revision;
	return true;
}

void NavMeshAgent::ResetPath()
{
	HasPath = false;
	Corners.clear();
	CornerFlags.clear();
	Next = 0;
	OnLink = false;
}

bool NavMeshAgent::Warp(const Vec3& position)
{
	if (m_pGameObject == nullptr)
		return false;
	const NavData* nav = NavMeshSurface::FindData(position);
	m_2D = nav && nav->Plane2D;
	m_WorldZ = position.z;
	Vec3 p = nav ? nav->ToNav(position) : position;
	Vec3 on;
	if (nav && nav->Sample(p, 2.0f, on))
		p = on;
	Place(p);
	Velocity = Vec3::Zero;
	ResetPath();
	return true;
}

bool NavMeshAgent::IsOnNavMesh() const
{
	if (m_pGameObject == nullptr)
		return false;
	if (OnLink)
		return true;
	Vec3 feet;
	const NavData* nav = Locate(feet);
	Vec3 on;
	return nav && nav->Sample(feet, (std::max)(0.3f, Radius), on);
}

float NavMeshAgent::RemainingDistance() const
{
	if (!HasPath || m_pGameObject == nullptr || Next >= Corners.size())
		return HasPath ? 0.0f : std::numeric_limits<float>::infinity();
	const Vec3 feet = Feet();
	float d = OnLink ? Vec3::Distance(feet, LinkEnd) : Horizontal(feet, Corners[Next]);
	for (size_t i = Next + (OnLink ? 2 : 1); i < Corners.size(); ++i)
		d += Vec3::Distance(Corners[i - 1], Corners[i]);
	return d;
}

void NavMeshAgent::CompleteOffMeshLink()
{
	if (!OnLink || m_pGameObject == nullptr)
		return;
	OnLink = false;
	Place(LinkEnd);
	// [Next] = 링크 시작, [Next+1] = 링크 끝 → 그 다음 점으로
	if (!Corners.empty())
		Next = (std::min)(Next + 2, Corners.size() - 1);
}

void NavMeshAgent::Update()
{
	if (!Application::IsPlaying() || m_pGameObject == nullptr)
		return;
	const float dt = (std::min)((float)DT, 0.1f);
	if (dt <= 0.0f)
		return;
	Transform* tr = m_pGameObject->GetTransform();
	auto turnTowards = [&](const Vec3& dir)
	{
		if (m_2D || dir.x * dir.x + dir.z * dir.z < 1e-6f)   // 2D 는 돌리지 않는다 (Unity 2D 의 updateRotation = false)
			return;
		const float targetYaw = XMConvertToDegrees(atan2f(dir.x, dir.z));
		Vec3 e = tr->GetEulerAngle();
		float delta = fmodf(targetYaw - e.y + 540.0f, 360.0f) - 180.0f;
		const float maxTurn = AngularSpeed * dt;
		delta = std::clamp(delta, -maxTurn, maxTurn);
		tr->SetEulerAngle(Vec3(e.x, e.y + delta, e.z));
	};

	// Off-Mesh Link 를 지나는 중: 시작 → 끝을 Speed 로 (Unity 의 자동 통과와 같이 곧게). 자동이 아니면 스크립트가 움직인다
	if (OnLink)
	{
		if (!AutoTraverseOffMeshLink)
			return;
		const float len = (std::max)(0.01f, Vec3::Distance(LinkStart, LinkEnd));
		LinkProgress = (std::min)(1.0f, LinkProgress + (std::max)(0.1f, Speed) * dt / len);
		Place(Vec3::Lerp(LinkStart, LinkEnd, LinkProgress));
		Velocity = (LinkEnd - LinkStart) / len * Speed;
		turnTowards(LinkEnd - LinkStart);
		if (LinkProgress >= 1.0f)
			CompleteOffMeshLink();
		return;
	}

	Vec3 feet;
	const NavData* nav = Locate(feet);
	// 내비 메시가 바뀌었다 (장애물 깎기·링크·다시 굽기) → 같은 목적지로 길을 다시 찾는다
	if (HasPath && (nav != m_PathData || (nav && nav->Revision != m_PathRevision)))
		SetDestination(Destination);

	Vec3 desired = Vec3::Zero;
	if (HasPath && !IsStopped && Next < Corners.size())
	{
		auto isLinkStart = [&](size_t i) { return i + 1 < Corners.size() && i < CornerFlags.size() && (CornerFlags[i] & NavData::CornerLinkStart); };
		// 지나간 꺾이는 점 넘기기 (마지막 점과 링크 시작은 넘기지 않는다)
		const float pass = (std::max)(0.05f, Velocity.Length() * dt * 1.5f);
		while (Next + 1 < Corners.size() && !isLinkStart(Next) && Horizontal(feet, Corners[Next]) <= pass)
			++Next;
		// 링크 시작에 닿았다 → 링크로
		if (isLinkStart(Next) && Horizontal(feet, Corners[Next]) <= (std::max)(pass, 0.15f) && fabsf(feet.y - Corners[Next].y) < Height)
		{
			OnLink = true;
			LinkStart = feet;
			LinkEnd = Corners[Next + 1];
			LinkProgress = 0.0f;
			if (!AutoTraverseOffMeshLink)
				Velocity = Vec3::Zero;
			return;
		}
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

	// 다른 에이전트·장애물과 겹치지 않게 밀어내기 (반지름 합 안쪽 — Unity 의 회피를 단순하게: 우선순위가 높은(숫자 작은) 쪽은 덜 밀린다)
	Vec3 push = Vec3::Zero;
	for (NavMeshAgent* other : s_Agents)
	{
		if (other == this || other->m_pGameObject == nullptr || !other->IsEnabled() || !other->m_pGameObject->IsActiveInHierarchy() || other->OnLink)
			continue;
		const Vec3 op = other->Feet();
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
	for (NavMeshObstacle* o : NavMeshObstacle::All())
	{
		if (m_2D)
			break;   // NavMesh Obstacle 은 3D 모양 (2D 장애물은 정적 2D 콜라이더로 굽는다)
		if (o->GetGameObject() == nullptr || !o->IsEnabled() || !o->GetGameObject()->IsActiveInHierarchy())
			continue;
		Vec3 p;
		if (o->PushOut(feet, Radius, Height, p))
			push += p;   // 장애물은 움직이지 않으니 에이전트가 다 비킨다
	}
	if (push.LengthSquared() > 1e-10f)
	{
		const float maxPush = (std::max)(Speed, 1.0f) * dt;   // 한 프레임에 너무 튀지 않게
		if (push.Length() > maxPush)
			push = push / push.Length() * maxPush;
		Vec3 pushed;
		if (nav && nav->MoveAlongSurface(feet, feet + push, pushed))
			feet = pushed;   // 걸을 수 있는 곳으로만 민다 (벽에서 멈춤)
	}
	if (Velocity.LengthSquared() >= 1e-8f)
	{
		// 메시 위로 미끄러지며 (벽·가장자리에서 멈춤) 바닥 높이까지
		const Vec3 to = feet + Velocity * dt;
		Vec3 moved;
		feet = nav && nav->MoveAlongSurface(feet, to, moved) ? moved : to;
	}
	// 다각형 높이는 꼭짓점 사이에서 언덕보다 뜨거나 묻힐 수 있다 → 정적 콜라이더 바닥에 붙인다 (Unity 의 Height Mesh 역할). 2D 는 평면
	if (!m_2D)
	{
		const float window = (nav ? nav->Settings.StepHeight : 0.4f) + 0.5f;
		RaycastHit hits[4];
		const int n = PhysicsManager::GetI()->RaycastAll(feet + Vec3(0.0f, window, 0.0f), Vec3(0.0f, -1.0f, 0.0f), window * 2.0f, hits, 4, true);
		for (int i = 0; i < n; ++i)
			if (hits[i].distance > 1e-3f && hits[i].normal.y > 0.3f)
			{
				feet.y = hits[i].point.y;
				break;
			}
	}
	Place(feet);
	if (Velocity.LengthSquared() < 1e-8f)
		return;

	// 움직이는 쪽으로 돌기 (Angular Speed 도/초)
	if (Velocity.LengthSquared() > 0.01f)
		turnTowards(Velocity);
}
void NavMeshAgent::OnInspectorGUI()
{
	UnityGUI::Label("Steering", 0, true);
	UnityGUI::Float("Speed", &Speed, 1);
	UnityGUI::Float("Angular Speed", &AngularSpeed, 1);
	UnityGUI::Float("Acceleration", &Acceleration, 1);
	UnityGUI::Float("Stopping Distance", &StoppingDistance, 1);
	UnityGUI::Toggle("Auto Braking", &AutoBraking, 1);
	UnityGUI::Label("Path Finding", 0, true);
	UnityGUI::Toggle("Auto Traverse Off Mesh Link", &AutoTraverseOffMeshLink, 1);
	UnityGUI::Label("Obstacle Avoidance", 0, true);
	UnityGUI::Float("Radius", &Radius, 1);
	UnityGUI::Float("Height", &Height, 1);
	UnityGUI::Float("Base Offset", &BaseOffset, 1);
	if (UnityGUI::Int("Priority", &AvoidancePriority, 1)) AvoidancePriority = std::clamp(AvoidancePriority, 0, 99);
	if (Application::IsPlaying())
	{
		char buf[96];
		snprintf(buf, sizeof(buf), "%s, %.2f m left, %.2f m/s", OnLink ? "on off-mesh link" : (HasPath ? "has path" : "no path"), HasPath ? RemainingDistance() : 0.0f, Velocity.Length());
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
		const Vec3 lift = m_2D ? Vec3(0.0f, 0.0f, -0.05f) : Vec3(0.0f, 0.05f, 0.0f);
		const Vec3 a = ((i == Next) ? NavToWorld(Feet()) : NavToWorld(Corners[i - 1])) + lift;
		const Vec3 b = NavToWorld(Corners[i]) + lift;
		SceneViewOverlay::DrawLine(XMFLOAT3(a.x, a.y, a.z), XMFLOAT3(b.x, b.y, b.z), c, 2.0f);
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
	j["autoTraverseOffMeshLink"] = AutoTraverseOffMeshLink;
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
	AutoTraverseOffMeshLink = j.value("autoTraverseOffMeshLink", true);
}
