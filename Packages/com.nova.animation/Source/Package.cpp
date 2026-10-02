// com.nova.animation 진입점: Animator 창 · .controller 에셋 등록 + C# API (Runtime/Animator.cs 가 DllImport("NovaAnimation") 로 부른다)
#include "pch.h"
#include "Animator.h"
#include "AnimatorController.h"
#include "AnimatorEditorWindow.h"
#include "AnimatorInspector.h"
#include "EditorExtensions.h"
#include "EditorGUIManager.h"
#include "ScriptBindings.h"

NOVA_PACKAGE_EXPORT const char* NovaPackage_Abi() { return NOVA_PACKAGE_ABI_VERSION; }

namespace
{
	constexpr const char* kPackage = "com.nova.animation";
	AnimatorEditorWindow* s_Window = nullptr;

	Animator* Find(uint64 gameObject)
	{
		GameObject* go = ScriptBindings::FindObject(gameObject);
		return go ? go->GetComponentIncludingPending<Animator>() : nullptr;
	}
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnLoad()
{
	// Window > Animator (게임 빌드에는 편집기가 없다)
	if (!Application::IsPlayer())
	{
		s_Window = new AnimatorEditorWindow();
		EditorGUIManager::GetI()->RegisterWindow(s_Window);
	}

	// Project 창: Create > Animator Controller, 더블클릭 = Animator 창, Inspector
	EditorExtensions::AssetType t;
	t.Owner = kPackage;
	t.Extension = ".controller";
	t.Icon = "animator_controller";
	t.CreateMenu = "Animator Controller";
	t.DefaultName = "New Animator Controller";
	t.DragPayload = "CONTROLLER_FILE";
	t.Create = [](const std::string& path) { AnimatorController::Create(path); };
	t.Open = [](const std::string&) { AnimatorEditorWindow::Focus(); };
	t.Inspector = [](const std::string& path) { AnimatorInspector::DrawController(AnimatorController::Load(path)); };
	EditorExtensions::RegisterAssetType(t);
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnUnload()
{
	EditorExtensions::UnregisterOwner(kPackage);
	if (SelectionManager::GetSelectedObjectType() == SelectionType::CUSTOM && SelectionManager::GetCustomSelection().Owner == "Animator")
		SelectionManager::ClearSelection();   // 이 DLL 의 함수를 가리키는 선택은 지운다
	if (s_Window)
	{
		EditorGUIManager::GetI()->UnregisterWindow(s_Window);
		delete s_Window;
		s_Window = nullptr;
	}
}

// ---- 파라미터: kind 0 Float, 1 Int, 2 Bool, 3 Trigger (v != 0 → Set, 0 → Reset)
NOVA_PACKAGE_EXPORT void AN_SetParam(uint64 id, const char* name, int kind, float v)
{
	Animator* an = Find(id);
	if (an == nullptr || name == nullptr) return;
	switch (kind)
	{
	case 0: an->SetFloat(name, v); break;
	case 1: an->SetInteger(name, (int)v); break;
	case 2: an->SetBool(name, v != 0.0f); break;
	default: if (v != 0.0f) an->SetTrigger(name); else an->ResetTrigger(name); break;
	}
}

NOVA_PACKAGE_EXPORT float AN_GetParam(uint64 id, const char* name, int kind)
{
	Animator* an = Find(id);
	if (an == nullptr || name == nullptr) return 0.0f;
	switch (kind) { case 0: return an->GetFloat(name); case 1: return (float)an->GetInteger(name); case 2: return an->GetBool(name) ? 1.0f : 0.0f; default: return 0.0f; }
}

// Play / CrossFade: fade < 0 이면 Play(normalizedTime), 아니면 CrossFade(fade 초). layer < 0 = 0
NOVA_PACKAGE_EXPORT void AN_Play(uint64 id, const char* state, int layer, float normalizedTime, float fade)
{
	Animator* an = Find(id);
	if (an == nullptr || state == nullptr) return;
	if (layer < 0) layer = 0;
	if (fade < 0.0f) an->Play(state, layer, std::isfinite(normalizedTime) ? normalizedTime : 0.0f);
	else an->CrossFade(state, fade, layer);
}

// float: 0 speed, 1 applyRootMotion (0/1)
NOVA_PACKAGE_EXPORT float AN_GetFloat(uint64 id, int prop)
{
	Animator* an = Find(id);
	if (an == nullptr) return 0.0f;
	return prop == 0 ? an->GetSpeed() : (an->GetApplyRootMotion() ? 1.0f : 0.0f);
}

NOVA_PACKAGE_EXPORT void AN_SetFloat(uint64 id, int prop, float v)
{
	Animator* an = Find(id);
	if (an == nullptr) return;
	if (prop == 0) an->SetSpeed(v);
	else an->SetApplyRootMotion(v != 0.0f);
}

// vector: 0 deltaPosition, 1 velocity
NOVA_PACKAGE_EXPORT void AN_GetVector(uint64 id, int prop, Vec3* out)
{
	Animator* an = Find(id);
	if (out) *out = an == nullptr ? Vec3::Zero : (prop == 0 ? an->GetDeltaPosition() : an->GetVelocity());
}

// 현재 상태 이름 + out[0] 정규화 시간, out[1] 길이(초), out[2] 전이 중(0/1)
NOVA_PACKAGE_EXPORT const char* AN_GetState(uint64 id, int layer, float* out)
{
	Animator* an = Find(id);
	if (out) out[0] = out[1] = out[2] = 0.0f;
	if (an == nullptr) return ScriptBindings::ReturnString(std::string());
	const Animator::LayerRuntime* r = an->GetLayerRuntime(layer);
	if (out && r)
	{
		out[0] = an->GetNormalizedTime(layer, r->Current, r->Time);
		out[1] = an->GetCurrentStateLength(layer);
		out[2] = r->Next >= 0 ? 1.0f : 0.0f;
	}
	return ScriptBindings::ReturnString(an->GetCurrentStateName(layer));
}
