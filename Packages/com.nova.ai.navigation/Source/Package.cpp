// com.nova.ai.navigation 진입점 + C# API (Runtime/Navigation.cs 가 DllImport("NovaNavigation") 로 부른다)
#include "pch.h"
#include "NavMeshAgent.h"
#include "NavMeshSurface.h"
#include "ScriptBindings.h"

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

// ---- NavMeshAgent — float: 0 speed, 1 angularSpeed, 2 acceleration, 3 stoppingDistance, 4 radius, 5 height, 6 baseOffset, 7 remainingDistance(읽기)
NOVA_PACKAGE_EXPORT float NavAgent_GetFloat(uint64 go, int prop)
{
	NavMeshAgent* a = Find<NavMeshAgent>(go);
	if (a == nullptr) return 0.0f;
	switch (prop)
	{
	case 0: return a->Speed; case 1: return a->AngularSpeed; case 2: return a->Acceleration; case 3: return a->StoppingDistance;
	case 4: return a->Radius; case 5: return a->Height; case 6: return a->BaseOffset; default: return a->RemainingDistance();
	}
}

NOVA_PACKAGE_EXPORT void NavAgent_SetFloat(uint64 go, int prop, float v)
{
	NavMeshAgent* a = Find<NavMeshAgent>(go);
	if (a == nullptr) return;
	switch (prop)
	{
	case 0: a->Speed = v; break; case 1: a->AngularSpeed = v; break; case 2: a->Acceleration = v; break; case 3: a->StoppingDistance = v; break;
	case 4: a->Radius = v; break; case 5: a->Height = v; break; case 6: a->BaseOffset = v; break; default: break;
	}
}

// bool: 0 isStopped, 1 hasPath(읽기), 2 autoBraking, 3 isOnNavMesh(읽기), 4 pathPending(항상 false)
NOVA_PACKAGE_EXPORT int NavAgent_GetBool(uint64 go, int prop)
{
	NavMeshAgent* a = Find<NavMeshAgent>(go);
	if (a == nullptr) return 0;
	switch (prop) { case 0: return a->IsStopped; case 1: return a->HasPath; case 2: return a->AutoBraking; case 3: return a->IsOnNavMesh(); default: return 0; }
}

NOVA_PACKAGE_EXPORT void NavAgent_SetBool(uint64 go, int prop, int v)
{
	NavMeshAgent* a = Find<NavMeshAgent>(go);
	if (a == nullptr) return;
	if (prop == 0) a->IsStopped = v != 0;
	else if (prop == 2) a->AutoBraking = v != 0;
}

// vector: 0 destination, 1 velocity, 2 steeringTarget
NOVA_PACKAGE_EXPORT void NavAgent_GetVector(uint64 go, int prop, Vec3* out)
{
	NavMeshAgent* a = Find<NavMeshAgent>(go);
	if (out == nullptr) return;
	*out = a == nullptr ? Vec3::Zero : (prop == 0 ? a->Destination : (prop == 1 ? a->Velocity : a->SteeringTarget()));
}

NOVA_PACKAGE_EXPORT void NavAgent_SetVelocity(uint64 go, Vec3* v)
{
	if (NavMeshAgent* a = Find<NavMeshAgent>(go))
		if (v) a->Velocity = *v;
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
		out[i] = a->Corners[i];
	return n;
}

// ---- NavMesh (정적 질의)
NOVA_PACKAGE_EXPORT int NavMesh_CalculatePath(Vec3* start, Vec3* end, Vec3* out, int max)
{
	if (start == nullptr || end == nullptr) return -1;
	const NavGrid* grid = NavMeshSurface::FindGrid(*start);
	std::vector<Vec3> path;
	if (grid == nullptr || !grid->FindPath(*start, *end, path)) return -1;
	for (int i = 0; i < (int)path.size() && i < max && out; ++i)
		out[i] = path[i];
	return (int)path.size();
}

NOVA_PACKAGE_EXPORT int NavMesh_SamplePosition(Vec3* p, float maxDistance, Vec3* out)
{
	if (p == nullptr) return 0;
	const NavGrid* grid = NavMeshSurface::FindGrid(*p);
	int x, z, l;
	if (grid == nullptr || !grid->Sample(*p, maxDistance, x, z, l)) return 0;
	if (out) *out = grid->CellCenter(x, z, l);
	return 1;
}

// ---- NavMeshSurface.BuildNavMesh() (Play 중에도)
NOVA_PACKAGE_EXPORT int NavSurface_Build(uint64 go)
{
	NavMeshSurface* s = Find<NavMeshSurface>(go);
	std::string log;
	return s && s->Bake(log) ? 1 : 0;
}
