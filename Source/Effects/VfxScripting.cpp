#include "pch.h"
#include "VisualEffect.h"
#include "ScriptBindings.h"

// C# 의 VisualEffect (ScriptCore/Engine/VisualEffect.cs — Unity 의 UnityEngine.VFX.VisualEffect) 가 이름으로 부르는 함수
namespace
{
	VisualEffect* FindVfx(uint64 id)
	{
		GameObject* go = ScriptBindings::FindObject(id);
		return go ? go->GetComponentIncludingPending<VisualEffect>() : nullptr;   // AddComponent 직후에도
	}

	const char* Keep(std::string s)
	{
		static thread_local std::string t;
		t = std::move(s);
		return t.c_str();
	}

	std::string Str(const char* s) { return s ? std::string(s) : std::string(); }
}

// op: 0 Play, 1 Stop, 2 Reinit, 3 SendEvent(name), 4 ResetOverride(name). 컴포넌트가 없으면 0
NOVA_PACKAGE_EXPORT int NovaVfx_Call(uint64 id, int op, const char* name)
{
	VisualEffect* v = FindVfx(id);
	if (!v) return 0;
	switch (op)
	{
	case 0: v->Play(); break;
	case 1: v->Stop(); break;
	case 2: v->Reinit(); break;
	case 3: v->SendEvent(Str(name)); break;
	case 4: v->ResetOverride(Str(name)); break;
	default: return 0;
	}
	return 1;
}

// Exposed Property: 덮어쓴 값 (없으면 에셋 기본값). 반환 = 속성 종류 + 1 (0 = 없음 — Has* 용)
NOVA_PACKAGE_EXPORT int NovaVfx_GetProperty(uint64 id, const char* name, float* value)
{
	VisualEffect* v = FindVfx(id);
	if (!v || !name) return 0;
	std::array<float, 4> out = { 0, 0, 0, 0 };
	if (!v->GetProperty(name, out)) return 0;
	if (value) for (int i = 0; i < 4; ++i) value[i] = out[i];
	const auto asset = v->GetAsset();
	const Vfx::Property* p = asset ? asset->FindProperty(name) : nullptr;
	return p ? (int)p->Type + 1 : 1;
}

NOVA_PACKAGE_EXPORT int NovaVfx_SetProperty(uint64 id, const char* name, const float* value)
{
	VisualEffect* v = FindVfx(id);
	if (!v || !name || !value) return 0;
	return v->SetProperty(name, { value[0], value[1], value[2], value[3] }) ? 1 : 0;
}

// which: 0 aliveParticleCount, 1 playRate, 2 pause, 3 startSeed, 4 resetSeedOnPlay, 5 HasAnySystemAwake, 6 enabled
NOVA_PACKAGE_EXPORT float NovaVfx_GetFloat(uint64 id, int which)
{
	VisualEffect* v = FindVfx(id);
	if (!v) return 0.0f;
	switch (which)
	{
	case 0: return (float)v->AliveParticleCount();
	case 1: return v->PlayRate;
	case 2: return v->Paused ? 1.0f : 0.0f;
	case 3: return (float)v->StartSeed;
	case 4: return v->ResetSeedOnPlay ? 1.0f : 0.0f;
	case 5: return v->HasAnySystemAwake() ? 1.0f : 0.0f;
	case 6: return v->IsEnabled() ? 1.0f : 0.0f;
	default: return 0.0f;
	}
}

NOVA_PACKAGE_EXPORT void NovaVfx_SetFloat(uint64 id, int which, float value)
{
	VisualEffect* v = FindVfx(id);
	if (!v) return;
	switch (which)
	{
	case 1: v->PlayRate = (std::max)(0.0f, value); break;
	case 2: v->Paused = value != 0.0f; break;
	case 3: v->StartSeed = (uint32)(std::max)(0.0f, value); break;
	case 4: v->ResetSeedOnPlay = value != 0.0f; break;
	case 6: v->SetEnabled(value != 0.0f); break;
	default: break;
	}
}

// which: 0 에셋 경로 (visualEffectAsset), 1 initialEventName
NOVA_PACKAGE_EXPORT const char* NovaVfx_GetString(uint64 id, int which)
{
	VisualEffect* v = FindVfx(id);
	if (!v) return Keep(std::string());
	return Keep(which == 0 ? v->AssetPath : v->InitialEvent);
}

NOVA_PACKAGE_EXPORT void NovaVfx_SetString(uint64 id, int which, const char* value)
{
	VisualEffect* v = FindVfx(id);
	if (!v) return;
	if (which == 0) v->AssetPath = Str(value);   // 다음 Update 에서 새 에셋으로 다시 시작
	else v->InitialEvent = Str(value);
}
