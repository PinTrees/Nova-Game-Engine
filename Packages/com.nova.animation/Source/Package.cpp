// com.nova.animation 진입점: Animator 창 · .controller 에셋 등록 + C# API (Runtime/Animator.cs 가 DllImport("NovaAnimation") 로 부른다)
#include "pch.h"
#include "Animator.h"
#include "AnimatorController.h"
#include "AnimatorEditorWindow.h"
#include "AnimatorInspector.h"
#include "EditorExtensions.h"
#include "EditorGUIManager.h"
#include "ScriptBindings.h"
#include "LegsAnimator.h"
#include "LookAnimator.h"
#include "HandsAnimator.h"

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

// 사람 본 (Unity HumanBodyBones 번호) → 월드 위치·회전. out[0..2] 위치, out[3..6] 회전. 없으면 0
NOVA_PACKAGE_EXPORT int AN_GetBone(uint64 id, int unityBone, float* out)
{
	// HumanBodyBones 0..20 → Humanoid::Bone, 54 = UpperChest
	static const int map[21] = { Humanoid::Hips, Humanoid::LeftUpperLeg, Humanoid::RightUpperLeg, Humanoid::LeftLowerLeg, Humanoid::RightLowerLeg,
		Humanoid::LeftFoot, Humanoid::RightFoot, Humanoid::Spine, Humanoid::Chest, Humanoid::Neck, Humanoid::Head,
		Humanoid::LeftShoulder, Humanoid::RightShoulder, Humanoid::LeftUpperArm, Humanoid::RightUpperArm,
		Humanoid::LeftLowerArm, Humanoid::RightLowerArm, Humanoid::LeftHand, Humanoid::RightHand, Humanoid::LeftToes, Humanoid::RightToes };
	const int bone = unityBone == 54 ? (int)Humanoid::UpperChest : (unityBone >= 0 && unityBone < 21 ? map[unityBone] : -1);
	Animator* an = Find(id);
	XMFLOAT3 p; XMFLOAT4 q;
	if (an == nullptr || out == nullptr || bone < 0 || !an->GetHumanBoneWorld(bone, p, q))
		return 0;
	out[0] = p.x; out[1] = p.y; out[2] = p.z;
	out[3] = q.x; out[4] = q.y; out[5] = q.z; out[6] = q.w;
	return 1;
}

// ---- LegsAnimator: float 0 weight, 1 maxStepDown, 2 maxStepUp, 3 hipsMaxDown / bool 0 adjustHips, 1 alignFeet
NOVA_PACKAGE_EXPORT float LEGS_GetFloat(uint64 id, int prop)
{
	GameObject* go = ScriptBindings::FindObject(id);
	LegsAnimator* l = go ? go->GetComponentIncludingPending<LegsAnimator>() : nullptr;
	if (l == nullptr) return 0.0f;
	switch (prop) { case 0: return l->Weight; case 1: return l->MaxStepDown; case 2: return l->MaxStepUp; case 3: return l->HipsMaxDown; case 4: return l->BodyLean; case 5: return l->MaxLean; case 6: return l->GetLean(); default: return 0.0f; }
}

NOVA_PACKAGE_EXPORT void LEGS_SetFloat(uint64 id, int prop, float v)
{
	GameObject* go = ScriptBindings::FindObject(id);
	LegsAnimator* l = go ? go->GetComponentIncludingPending<LegsAnimator>() : nullptr;
	if (l == nullptr) return;
	switch (prop)
	{
	case 0: l->Weight = std::clamp(v, 0.0f, 1.0f); break;
	case 1: l->MaxStepDown = (std::max)(0.0f, v); break;
	case 2: l->MaxStepUp = (std::max)(0.0f, v); break;
	case 3: l->HipsMaxDown = (std::max)(0.0f, v); break;
	case 4: l->BodyLean = std::clamp(v, 0.0f, 1.0f); break;
	case 5: l->MaxLean = std::clamp(v, 0.0f, 45.0f); break;
	default: break;
	}
}

// bool 0 adjustHips, 1 alignFeet, 2 footLocking, 10/11 왼발·오른발 고정 중 (읽기만)
NOVA_PACKAGE_EXPORT int LEGS_GetBool(uint64 id, int prop)
{
	GameObject* go = ScriptBindings::FindObject(id);
	LegsAnimator* l = go ? go->GetComponentIncludingPending<LegsAnimator>() : nullptr;
	if (l == nullptr) return 0;
	switch (prop) { case 0: return l->AdjustHips; case 1: return l->AlignFeet; case 2: return l->FootLocking; case 10: return l->IsFootLocked(0); case 11: return l->IsFootLocked(1); default: return 0; }
}

NOVA_PACKAGE_EXPORT void LEGS_SetBool(uint64 id, int prop, int v)
{
	GameObject* go = ScriptBindings::FindObject(id);
	if (LegsAnimator* l = go ? go->GetComponentIncludingPending<LegsAnimator>() : nullptr)
	{
		if (prop == 0) l->AdjustHips = v != 0;
		else if (prop == 1) l->AlignFeet = v != 0;
		else if (prop == 2) l->FootLocking = v != 0;
	}
}

// ---- LookAnimator: float 0 weight, 1 maxYaw, 2 speed / 대상 오브젝트 / 위치
NOVA_PACKAGE_EXPORT float LOOK_GetFloat(uint64 id, int prop)
{
	GameObject* go = ScriptBindings::FindObject(id);
	LookAnimator* l = go ? go->GetComponentIncludingPending<LookAnimator>() : nullptr;
	if (l == nullptr) return 0.0f;
	return prop == 0 ? l->Weight : (prop == 1 ? l->MaxYaw : l->Speed);
}

NOVA_PACKAGE_EXPORT void LOOK_SetFloat(uint64 id, int prop, float v)
{
	GameObject* go = ScriptBindings::FindObject(id);
	LookAnimator* l = go ? go->GetComponentIncludingPending<LookAnimator>() : nullptr;
	if (l == nullptr) return;
	if (prop == 0) l->Weight = std::clamp(v, 0.0f, 1.0f);
	else if (prop == 1) l->MaxYaw = std::clamp(v, 0.0f, 180.0f);
	else l->Speed = (std::max)(0.1f, v);
}

NOVA_PACKAGE_EXPORT uint64 LOOK_GetTarget(uint64 id)
{
	GameObject* go = ScriptBindings::FindObject(id);
	LookAnimator* l = go ? go->GetComponentIncludingPending<LookAnimator>() : nullptr;
	return l ? l->Target : 0;
}

NOVA_PACKAGE_EXPORT void LOOK_SetTarget(uint64 id, uint64 target)
{
	GameObject* go = ScriptBindings::FindObject(id);
	if (LookAnimator* l = go ? go->GetComponentIncludingPending<LookAnimator>() : nullptr)
		l->Target = target;
}

// use = 0 이면 위치를 풀고 Target 을 다시 본다
NOVA_PACKAGE_EXPORT void LOOK_SetPosition(uint64 id, Vec3* p, int use)
{
	GameObject* go = ScriptBindings::FindObject(id);
	LookAnimator* l = go ? go->GetComponentIncludingPending<LookAnimator>() : nullptr;
	if (l == nullptr) return;
	if (use && p) l->SetLookAtPosition(*p);
	else l->ClearLookAtPosition();
}

// ---- HandsAnimator: hand 0 왼손 1 오른손
namespace
{
	HandsAnimator* FindHands(uint64 id)
	{
		GameObject* go = ScriptBindings::FindObject(id);
		return go ? go->GetComponentIncludingPending<HandsAnimator>() : nullptr;
	}
}

// float: 0 weight, 1 blendSpeed, 10+hand 위치 가중치, 20+hand 회전 가중치
NOVA_PACKAGE_EXPORT float HANDS_GetFloat(uint64 id, int prop)
{
	HandsAnimator* h = FindHands(id);
	if (h == nullptr) return 0.0f;
	if (prop == 0) return h->Weight;
	if (prop == 1) return h->BlendSpeed;
	if (prop >= 10 && prop < 12) return h->Hands[prop - 10].PositionWeight;
	if (prop >= 20 && prop < 22) return h->Hands[prop - 20].RotationWeight;
	return 0.0f;
}

NOVA_PACKAGE_EXPORT void HANDS_SetFloat(uint64 id, int prop, float v)
{
	HandsAnimator* h = FindHands(id);
	if (h == nullptr) return;
	if (prop == 0) h->Weight = std::clamp(v, 0.0f, 1.0f);
	else if (prop == 1) h->BlendSpeed = (std::max)(0.1f, v);
	else if (prop >= 10 && prop < 12) h->Hands[prop - 10].PositionWeight = std::clamp(v, 0.0f, 1.0f);
	else if (prop >= 20 && prop < 22) h->Hands[prop - 20].RotationWeight = std::clamp(v, 0.0f, 1.0f);
}

// 대상: which 0 Target, 1 Hint
NOVA_PACKAGE_EXPORT uint64 HANDS_GetObject(uint64 id, int hand, int which)
{
	HandsAnimator* h = FindHands(id);
	if (h == nullptr || hand < 0 || hand > 1) return 0;
	return which == 0 ? h->Hands[hand].Target : h->Hands[hand].Hint;
}

NOVA_PACKAGE_EXPORT void HANDS_SetObject(uint64 id, int hand, int which, uint64 target)
{
	HandsAnimator* h = FindHands(id);
	if (h == nullptr || hand < 0 || hand > 1) return;
	(which == 0 ? h->Hands[hand].Target : h->Hands[hand].Hint) = target;
}

// mode 0 위치, 1 회전 (q = x y z w), 2 지우기
NOVA_PACKAGE_EXPORT void HANDS_SetIK(uint64 id, int hand, int mode, float* v)
{
	HandsAnimator* h = FindHands(id);
	if (h == nullptr) return;
	if (mode == 0 && v) h->SetIKPosition(hand, Vec3(v[0], v[1], v[2]));
	else if (mode == 1 && v) h->SetIKRotation(hand, Quaternion(v[0], v[1], v[2], v[3]));
	else if (mode == 2) h->ClearIK(hand);
}

NOVA_PACKAGE_EXPORT void HANDS_GetHand(uint64 id, int hand, Vec3* out)
{
	HandsAnimator* h = FindHands(id);
	if (out) *out = h ? h->GetHandPosition(hand) : Vec3::Zero;
}
