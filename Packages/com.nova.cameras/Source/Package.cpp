// com.nova.cameras 진입점 + C# API (Runtime/FollowCamera.cs 가 DllImport("NovaCameras") 로 부른다)
#include "pch.h"
#include "FollowCamera.h"
#include "ScriptBindings.h"

NOVA_PACKAGE_EXPORT const char* NovaPackage_Abi() { return NOVA_PACKAGE_ABI_VERSION; }

namespace
{
	// C# 가 넘긴 GameObject id → 컴포넌트 (이번 프레임에 만든 오브젝트 · 방금 AddComponent 한 것도)
	FollowCamera* Find(uint64 gameObject)
	{
		GameObject* go = ScriptBindings::FindObject(gameObject);
		return go ? go->GetComponentIncludingPending<FollowCamera>() : nullptr;
	}
}

// float: 0 distance, 1 height, 2 lookAtHeight, 3 damping, 4 yaw, 5 obstaclePadding
NOVA_PACKAGE_EXPORT float NovaCameras_GetFloat(uint64 gameObject, int prop)
{
	FollowCamera* c = Find(gameObject);
	if (c == nullptr) return 0.0f;
	switch (prop) { case 0: return c->Distance; case 1: return c->Height; case 2: return c->LookAtHeight; case 3: return c->Damping; case 4: return c->Yaw; default: return c->ObstaclePadding; }
}

NOVA_PACKAGE_EXPORT void NovaCameras_SetFloat(uint64 gameObject, int prop, float v)
{
	FollowCamera* c = Find(gameObject);
	if (c == nullptr) return;
	switch (prop) { case 0: c->Distance = (std::max)(0.0f, v); break; case 1: c->Height = v; break; case 2: c->LookAtHeight = v; break;
		case 3: c->Damping = (std::max)(0.0f, v); break; case 4: c->Yaw = v; break; default: c->ObstaclePadding = v; break; }
}

// bool: 0 followTargetRotation, 1 avoidObstacles
NOVA_PACKAGE_EXPORT int NovaCameras_GetBool(uint64 gameObject, int prop)
{
	FollowCamera* c = Find(gameObject);
	if (c == nullptr) return 0;
	return prop == 0 ? c->FollowTargetRotation : c->AvoidObstacles;
}

NOVA_PACKAGE_EXPORT void NovaCameras_SetBool(uint64 gameObject, int prop, int v)
{
	if (FollowCamera* c = Find(gameObject))
		(prop == 0 ? c->FollowTargetRotation : c->AvoidObstacles) = v != 0;
}

NOVA_PACKAGE_EXPORT uint64 NovaCameras_GetTarget(uint64 gameObject)
{
	FollowCamera* c = Find(gameObject);
	return c ? c->Target : 0;
}

NOVA_PACKAGE_EXPORT void NovaCameras_SetTarget(uint64 gameObject, uint64 target)
{
	if (FollowCamera* c = Find(gameObject))
	{
		c->Target = target;
		c->SnapNow();
	}
}

NOVA_PACKAGE_EXPORT void NovaCameras_Snap(uint64 gameObject)
{
	if (FollowCamera* c = Find(gameObject))
		c->SnapNow();
}
