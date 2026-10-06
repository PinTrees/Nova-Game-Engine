// com.nova.ai.navigation 진입점 + C# API (Runtime/Navigation.cs 가 DllImport("NovaNavigation") 로 부른다)
#include "pch.h"
#include "NavMeshAgent.h"
#include "NavMeshSurface.h"
#include "NavMeshLink.h"
#include "NavMeshObstacle.h"
#include "ScriptBindings.h"
#include "NavData.h"

NOVA_PACKAGE_EXPORT const char* NovaPackage_Abi() { return NOVA_PACKAGE_ABI_VERSION; }

namespace
{
	template <typename T>
	T* Find(uint64 gameObject)
	{
		GameObject* go = ScriptBindings::FindObject(gameObject);
		return go ? go->GetComponentIncludingPending<T>() : nullptr;
	}
}

// ---- NavMeshAgent — float: 0 speed, 1 angularSpeed, 2 acceleration, 3 stoppingDistance, 4 radius, 5 height, 6 baseOffset, 7 remainingDistance(읽기), 8 avoidancePriority
NOVA_PACKAGE_EXPORT float NavAgent_GetFloat(uint64 go, int prop)
{
	NavMeshAgent* a = Find<NavMeshAgent>(go);
	if (a == nullptr) return 0.0f;
	switch (prop)
	{
	case 0: return a->Speed; case 1: return a->AngularSpeed; case 2: return a->Acceleration; case 3: return a->StoppingDistance;
	case 4: return a->Radius; case 5: return a->Height; case 6: return a->BaseOffset; case 8: return (float)a->AvoidancePriority; default: return a->RemainingDistance();
	}
}

NOVA_PACKAGE_EXPORT void NavAgent_SetFloat(uint64 go, int prop, float v)
{
	NavMeshAgent* a = Find<NavMeshAgent>(go);
	if (a == nullptr) return;
	switch (prop)
	{
	case 0: a->Speed = v; break; case 1: a->AngularSpeed = v; break; case 2: a->Acceleration = v; break; case 3: a->StoppingDistance = v; break;
	case 4: a->Radius = v; break; case 5: a->Height = v; break; case 6: a->BaseOffset = v; break;
	case 8: a->AvoidancePriority = std::clamp((int)v, 0, 99); break; default: break;
	}
}

// bool: 0 isStopped, 1 hasPath(읽기), 2 autoBraking, 3 isOnNavMesh(읽기), 4 pathPending(항상 false), 5 isOnOffMeshLink(읽기), 6 autoTraverseOffMeshLink
NOVA_PACKAGE_EXPORT int NavAgent_GetBool(uint64 go, int prop)
{
	NavMeshAgent* a = Find<NavMeshAgent>(go);
	if (a == nullptr) return 0;
	switch (prop) { case 0: return a->IsStopped; case 1: return a->HasPath; case 2: return a->AutoBraking; case 3: return a->IsOnNavMesh(); case 5: return a->OnLink; case 6: return a->AutoTraverseOffMeshLink; default: return 0; }
}

NOVA_PACKAGE_EXPORT void NavAgent_SetBool(uint64 go, int prop, int v)
{
	NavMeshAgent* a = Find<NavMeshAgent>(go);
	if (a == nullptr) return;
	if (prop == 0) a->IsStopped = v != 0;
	else if (prop == 2) a->AutoBraking = v != 0;
	else if (prop == 6) a->AutoTraverseOffMeshLink = v != 0;
}

// vector: 0 destination, 1 velocity, 2 steeringTarget
NOVA_PACKAGE_EXPORT void NavAgent_GetVector(uint64 go, int prop, Vec3* out)
{
	NavMeshAgent* a = Find<NavMeshAgent>(go);
	if (out == nullptr) return;
	// 월드 좌표로 (2D 표면 위면 내비 공간 (x, 0, y) → (x, y))
	*out = a == nullptr ? Vec3::Zero : (prop == 0 ? a->Destination : (prop == 1 ? a->NavDirToWorld(a->Velocity) : a->NavToWorld(a->SteeringTarget())));
}

NOVA_PACKAGE_EXPORT void NavAgent_SetVelocity(uint64 go, Vec3* v)
{
	if (NavMeshAgent* a = Find<NavMeshAgent>(go))
		if (v) a->Velocity = a->Is2D() ? Vec3(v->x, 0.0f, v->y) : *v;
}

NOVA_PACKAGE_EXPORT int NavAgent_SetDestination(uint64 go, Vec3* target)
{
	NavMeshAgent* a = Find<NavMeshAgent>(go);
	return a && target && a->SetDestination(*target) ? 1 : 0;
}

NOVA_PACKAGE_EXPORT void NavAgent_ResetPath(uint64 go)
{
	if (NavMeshAgent* a = Find<NavMeshAgent>(go))
		a->ResetPath();
}

NOVA_PACKAGE_EXPORT int NavAgent_Warp(uint64 go, Vec3* p)
{
	NavMeshAgent* a = Find<NavMeshAgent>(go);
	return a && p && a->Warp(*p) ? 1 : 0;
}

NOVA_PACKAGE_EXPORT int NavAgent_GetCorners(uint64 go, Vec3* out, int max)
{
	NavMeshAgent* a = Find<NavMeshAgent>(go);
	if (a == nullptr || !a->HasPath) return 0;
	const int n = (int)a->Corners.size();
	for (int i = 0; i < n && i < max && out; ++i)
		out[i] = a->NavToWorld(a->Corners[i]);
	return n;
}

// ---- NavMesh (정적 질의)
NOVA_PACKAGE_EXPORT int NavMesh_CalculatePath(Vec3* start, Vec3* end, Vec3* out, int max)
{
	if (start == nullptr || end == nullptr) return -1;
	const NavData* nav = NavMeshSurface::FindData(*start);
	std::vector<Vec3> path;
	if (nav == nullptr || !nav->FindPath(nav->ToNav(*start), nav->ToNav(*end), path)) return -1;
	for (int i = 0; i < (int)path.size() && i < max && out; ++i)
		out[i] = nav->ToWorld(path[i], start->z);
	return (int)path.size();
}

NOVA_PACKAGE_EXPORT int NavMesh_SamplePosition(Vec3* p, float maxDistance, Vec3* out)
{
	if (p == nullptr) return 0;
	const NavData* nav = NavMeshSurface::FindData(*p);
	Vec3 on;
	if (nav == nullptr || !nav->Sample(nav->ToNav(*p), maxDistance, on)) return 0;
	if (out) *out = nav->ToWorld(on, p->z);
	return 1;
}

// ---- NavMeshSurface.BuildNavMesh() (Play 중에도)
NOVA_PACKAGE_EXPORT int NavSurface_Build(uint64 go)
{
	NavMeshSurface* s = Find<NavMeshSurface>(go);
	std::string log;
	return s && s->Bake(log) ? 1 : 0;
}

// Off-Mesh Link: 지나는 중인 링크의 시작·끝 (지나는 중이 아니면 0)
NOVA_PACKAGE_EXPORT int NavAgent_GetLinkData(uint64 go, Vec3* start, Vec3* end)
{
	NavMeshAgent* a = Find<NavMeshAgent>(go);
	if (a == nullptr || !a->OnLink) return 0;
	if (start) *start = a->NavToWorld(a->LinkStart);
	if (end) *end = a->NavToWorld(a->LinkEnd);
	return 1;
}

NOVA_PACKAGE_EXPORT void NavAgent_CompleteOffMeshLink(uint64 go)
{
	if (NavMeshAgent* a = Find<NavMeshAgent>(go))
		a->CompleteOffMeshLink();
}

// ---- NavMeshObstacle — float: 0 radius, 1 height, 2 carvingMoveThreshold, 3 carvingTimeToStationary / bool: 0 carving, 1 carveOnlyStationary / int shape
NOVA_PACKAGE_EXPORT float NavObstacle_GetFloat(uint64 go, int prop)
{
	NavMeshObstacle* o = Find<NavMeshObstacle>(go);
	if (o == nullptr) return 0.0f;
	switch (prop) { case 0: return o->Radius; case 1: return o->Height; case 2: return o->MoveThreshold; default: return o->TimeToStationary; }
}

NOVA_PACKAGE_EXPORT void NavObstacle_SetFloat(uint64 go, int prop, float v)
{
	NavMeshObstacle* o = Find<NavMeshObstacle>(go);
	if (o == nullptr) return;
	v = (std::max)(0.0f, v);
	switch (prop) { case 0: o->Radius = v; break; case 1: o->Height = v; break; case 2: o->MoveThreshold = v; break; default: o->TimeToStationary = v; break; }
}

NOVA_PACKAGE_EXPORT int NavObstacle_GetBool(uint64 go, int prop)
{
	NavMeshObstacle* o = Find<NavMeshObstacle>(go);
	if (o == nullptr) return 0;
	return prop == 0 ? o->Carve : (prop == 1 ? o->CarveOnlyStationary : o->IsCarving());
}

NOVA_PACKAGE_EXPORT void NavObstacle_SetBool(uint64 go, int prop, int v)
{
	if (NavMeshObstacle* o = Find<NavMeshObstacle>(go))
	{
		if (prop == 0) o->Carve = v != 0;
		else if (prop == 1) o->CarveOnlyStationary = v != 0;
	}
}

NOVA_PACKAGE_EXPORT int NavObstacle_GetShape(uint64 go)
{
	NavMeshObstacle* o = Find<NavMeshObstacle>(go);
	return o ? o->Shape : 1;
}

NOVA_PACKAGE_EXPORT void NavObstacle_SetShape(uint64 go, int shape)
{
	if (NavMeshObstacle* o = Find<NavMeshObstacle>(go))
		o->Shape = shape == 0 ? 0 : 1;
}

// vector: 0 center, 1 size
NOVA_PACKAGE_EXPORT void NavObstacle_GetVector(uint64 go, int prop, Vec3* out)
{
	NavMeshObstacle* o = Find<NavMeshObstacle>(go);
	if (out) *out = o == nullptr ? Vec3::Zero : (prop == 0 ? o->Center : o->Size);
}

NOVA_PACKAGE_EXPORT void NavObstacle_SetVector(uint64 go, int prop, Vec3* v)
{
	NavMeshObstacle* o = Find<NavMeshObstacle>(go);
	if (o == nullptr || v == nullptr) return;
	(prop == 0 ? o->Center : o->Size) = *v;
}

// ---- NavMeshLink — vector: 0 startPoint, 1 endPoint / float width / bool bidirectional
NOVA_PACKAGE_EXPORT void NavLink_GetVector(uint64 go, int prop, Vec3* out)
{
	NavMeshLink* l = Find<NavMeshLink>(go);
	if (out) *out = l == nullptr ? Vec3::Zero : (prop == 0 ? l->StartPoint : l->EndPoint);
}

NOVA_PACKAGE_EXPORT void NavLink_SetVector(uint64 go, int prop, Vec3* v)
{
	NavMeshLink* l = Find<NavMeshLink>(go);
	if (l == nullptr || v == nullptr) return;
	(prop == 0 ? l->StartPoint : l->EndPoint) = *v;
}

NOVA_PACKAGE_EXPORT float NavLink_GetWidth(uint64 go)
{
	NavMeshLink* l = Find<NavMeshLink>(go);
	return l ? l->Width : 0.0f;
}

NOVA_PACKAGE_EXPORT void NavLink_SetWidth(uint64 go, float v)
{
	if (NavMeshLink* l = Find<NavMeshLink>(go))
		l->Width = (std::max)(0.0f, v);
}

NOVA_PACKAGE_EXPORT int NavLink_GetBidirectional(uint64 go)
{
	NavMeshLink* l = Find<NavMeshLink>(go);
	return l && l->Bidirectional ? 1 : 0;
}

NOVA_PACKAGE_EXPORT void NavLink_SetBidirectional(uint64 go, int v)
{
	if (NavMeshLink* l = Find<NavMeshLink>(go))
		l->Bidirectional = v != 0;
}