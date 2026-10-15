// com.nova.animation 진입점: Animator 창 · .controller 에셋 등록 + C# API (Runtime/Animator.cs 가 DllImport("NovaAnimation") 로 부른다)
#include "pch.h"
#include "Animator.h"
#include "AnimatorController.h"
#include "AnimatorEditorWindow.h"
#include "AnimatorInspector.h"
#include "AvatarMask.h"
#include "AnimationEventStore.h"
#include "CliServer.h"
#include "EditorExtensions.h"
#include "EditorGUIManager.h"
#include "ScriptBindings.h"
#include "LegsAnimator.h"
#include "LookAnimator.h"
#include "HandsAnimator.h"
#include "TwoBoneIKConstraint.h"
#include "DynamicBone.h"
#include "Expressions.h"

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

	// Create > Avatar Mask (.mask) — 레이어가 움직일 몸 부위
	EditorExtensions::AssetType m;
	m.Owner = kPackage;
	m.Extension = ".mask";
	m.Icon = "avatar_mask";
	m.CreateMenu = "Avatar Mask";
	m.DefaultName = "New Avatar Mask";
	m.Create = [](const std::string& path) { AvatarMask::Create(path); };
	m.Inspector = [](const std::string& path) { AnimatorInspector::DrawAvatarMask(path); };
	EditorExtensions::RegisterAssetType(m);

	// nova animevent list|add|clear --model <fbx> --clip N [--time s --function F --string --float --int]
	CliServer::Register("animevent", "animation events {op: list | add | clear, model, clip, time, function, string, float, int} (stored in <model>.animevents)",
		[](const nlohmann::json& a, nlohmann::json& r, std::string& e) -> bool {
			const std::string op = a.value("op", std::string("list"));
			const std::string model = a.value("model", std::string());
			if (model.empty()) { e = "model is required (--model Assets/x.fbx)"; return false; }
			auto num = [&](const char* k, double d) -> double {
				if (!a.contains(k)) return d;
				const auto& v = a[k];
				return v.is_number() ? v.get<double>() : (v.is_string() ? atof(v.get<std::string>().c_str()) : d);
			};
			auto str = [&](const char* k) -> std::string {
				if (!a.contains(k)) return std::string();
				const auto& v = a[k];
				return v.is_string() ? v.get<std::string>() : v.dump();
			};
			const int clip = (int)num("clip", 0);
			auto current = AnimationEventStore::Get(model, clip);
			AnimationEventStore::EventList list = current ? *current : AnimationEventStore::EventList();
			if (op == "add")
			{
				AnimationEventStore::Event ev;
				ev.Time = (float)num("time", 0.0);
				ev.Function = str("function");
				ev.String = str("string");
				ev.Float = (float)num("float", 0.0);
				ev.Int = (int)num("int", 0);
				if (ev.Function.empty()) { e = "function is required (--function OnHit)"; return false; }
				list.push_back(ev);
				if (!AnimationEventStore::Set(model, clip, list)) { e = "could not write " + AnimationEventStore::FilePath(model); return false; }
			}
			else if (op == "clear")
			{
				list.clear();
				if (!AnimationEventStore::Set(model, clip, list)) { e = "could not write " + AnimationEventStore::FilePath(model); return false; }
			}
			else if (op != "list") { e = "unknown op '" + op + "' (list | add | clear)"; return false; }
			std::sort(list.begin(), list.end(), [](const auto& x, const auto& y) { return x.Time < y.Time; });
			nlohmann::json events = nlohmann::json::array();
			for (const auto& ev : list)
				events.push_back({ { "time", ev.Time }, { "function", ev.Function }, { "string", ev.String }, { "float", ev.Float }, { "int", ev.Int } });
			r = { { "model", model }, { "clip", clip }, { "file", AnimationEventStore::FilePath(model) }, { "events", events } };
			return true;
		});

	// Animator 자세: 모든 Update 뒤 · LateUpdate 앞에 모아서 (작업 스레드)
	Scene::SetAfterUpdateHook(kPackage, []() { Animator::FlushPendingPoses(); });
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnUnload()
{
	CliServer::Unregister("animevent");
	Scene::SetAfterUpdateHook(kPackage, nullptr);
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
	if (layer < 0) layer = an->LayerOfState(state);   // Unity: -1 = 그 이름의 상태가 있는 첫 레이어
	if (fade < 0.0f) an->Play(state, layer, std::isfinite(normalizedTime) ? normalizedTime : 0.0f);
	else an->CrossFade(state, fade, layer);
}

// Animator IK (Unity): goal 0 왼발 · 1 오른발 · 2 왼손 · 3 오른손. kind 0 위치 (3), 1 회전 (4), 2 위치 무게, 3 회전 무게, 4 힌트 위치 (3), 5 힌트 무게
NOVA_PACKAGE_EXPORT void AN_SetIK(uint64 id, int goal, int kind, const float* v)
{
	Animator* an = Find(id);
	if (an == nullptr || v == nullptr || goal < 0 || goal > 3) return;
	Animator::IKGoal& g = an->IKGoalAt(goal);
	switch (kind)
	{
	case 0: g.Position = Vec3(v[0], v[1], v[2]); break;
	case 1: g.Rotation = Quaternion(v[0], v[1], v[2], v[3]); break;
	case 2: g.PositionWeight = std::clamp(v[0], 0.0f, 1.0f); break;
	case 3: g.RotationWeight = std::clamp(v[0], 0.0f, 1.0f); break;
	case 4: g.Hint = Vec3(v[0], v[1], v[2]); break;
	case 5: g.HintWeight = std::clamp(v[0], 0.0f, 1.0f); break;
	}
}
NOVA_PACKAGE_EXPORT void AN_GetIK(uint64 id, int goal, int kind, float* out)
{
	Animator* an = Find(id);
	if (out == nullptr) return;
	out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f;
	if (an == nullptr || goal < 0 || goal > 3) return;
	const Animator::IKGoal& g = an->IKGoalAt(goal);
	switch (kind)
	{
	case 0: out[0] = g.Position.x; out[1] = g.Position.y; out[2] = g.Position.z; break;
	case 1: out[0] = g.Rotation.x; out[1] = g.Rotation.y; out[2] = g.Rotation.z; out[3] = g.Rotation.w; break;
	case 2: out[0] = g.PositionWeight; break;
	case 3: out[0] = g.RotationWeight; break;
	case 4: out[0] = g.Hint.x; out[1] = g.Hint.y; out[2] = g.Hint.z; break;
	case 5: out[0] = g.HintWeight; break;
	}
}
// 시선: kind 0 위치 (3), 1 무게 (weight, body, head, eyes, clamp)
NOVA_PACKAGE_EXPORT void AN_SetLookAt(uint64 id, int kind, const float* v)
{
	Animator* an = Find(id);
	if (an == nullptr || v == nullptr) return;
	Animator::LookAtGoal& l = an->LookAtState();
	if (kind == 0) l.Position = Vec3(v[0], v[1], v[2]);
	else { l.Weight = std::clamp(v[0], 0.0f, 1.0f); l.Body = std::clamp(v[1], 0.0f, 1.0f); l.Head = std::clamp(v[2], 0.0f, 1.0f); l.Eyes = v[3]; l.Clamp = std::clamp(v[4], 0.0f, 1.0f); }
}

// Two Bone IK Constraint: float 0 weight, 1 target position weight, 2 target rotation weight, 3 hint weight
namespace
{
	TwoBoneIKConstraint* FindTwoBone(uint64 id)
	{
		GameObject* go = ScriptBindings::FindObject(id);
		return go ? go->GetComponentIncludingPending<TwoBoneIKConstraint>() : nullptr;
	}
}
NOVA_PACKAGE_EXPORT float TBIK_GetFloat(uint64 id, int prop)
{
	TwoBoneIKConstraint* c = FindTwoBone(id);
	if (c == nullptr) return 0.0f;
	const float v[4] = { c->Weight, c->TargetPositionWeight, c->TargetRotationWeight, c->HintWeight };
	return prop >= 0 && prop < 4 ? v[prop] : 0.0f;
}
NOVA_PACKAGE_EXPORT void TBIK_SetFloat(uint64 id, int prop, float value)
{
	TwoBoneIKConstraint* c = FindTwoBone(id);
	if (c == nullptr) return;
	float* v[4] = { &c->Weight, &c->TargetPositionWeight, &c->TargetRotationWeight, &c->HintWeight };
	if (prop >= 0 && prop < 4) *v[prop] = std::clamp(value, 0.0f, 1.0f);
}
NOVA_PACKAGE_EXPORT uint64 TBIK_GetObject(uint64 id, int which)
{
	TwoBoneIKConstraint* c = FindTwoBone(id);
	return c ? (which == 0 ? c->Target : c->Hint) : 0;
}
NOVA_PACKAGE_EXPORT void TBIK_SetObject(uint64 id, int which, uint64 target)
{
	if (TwoBoneIKConstraint* c = FindTwoBone(id)) (which == 0 ? c->Target : c->Hint) = target;
}
// 본: which 0 Root, 1 Mid, 2 Tip
NOVA_PACKAGE_EXPORT const char* TBIK_GetBone(uint64 id, int which)
{
	TwoBoneIKConstraint* c = FindTwoBone(id);
	return ScriptBindings::ReturnString(c == nullptr ? std::string() : (which == 0 ? c->Root : which == 1 ? c->Mid : c->Tip));
}
NOVA_PACKAGE_EXPORT void TBIK_SetBone(uint64 id, int which, const char* name)
{
	TwoBoneIKConstraint* c = FindTwoBone(id);
	if (c == nullptr || name == nullptr) return;
	(which == 0 ? c->Root : which == 1 ? c->Mid : c->Tip) = name;
}
NOVA_PACKAGE_EXPORT int TBIK_AutoSetup(uint64 id, int limb)
{
	TwoBoneIKConstraint* c = FindTwoBone(id);
	return c && c->AutoSetup(limb) ? 1 : 0;
}

// 레이어 Weight (이 Animator 만) · 이름 → 번호
NOVA_PACKAGE_EXPORT void AN_SetLayerWeight(uint64 id, int layer, float weight)
{
	if (Animator* an = Find(id)) an->SetLayerWeight(layer, weight);
}
NOVA_PACKAGE_EXPORT float AN_GetLayerWeight(uint64 id, int layer)
{
	Animator* an = Find(id);
	return an ? an->GetLayerWeight(layer) : 0.0f;
}
NOVA_PACKAGE_EXPORT int AN_GetLayerIndex(uint64 id, const char* name)
{
	Animator* an = Find(id);
	return an && name ? an->GetLayerIndex(name) : -1;
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

// ---- Expressions: 이름으로 표정 값 (0..1), 0 autoBlink / float 0 blinkInterval, 1 blinkDuration, 2 자동 깜빡임 값
namespace
{
	Expressions* FindExpr(uint64 id)
	{
		GameObject* go = ScriptBindings::FindObject(id);
		return go ? go->GetComponentIncludingPending<Expressions>() : nullptr;
	}
}

NOVA_PACKAGE_EXPORT int EXP_SetWeight(uint64 id, const char* name, float v)
{
	Expressions* e = FindExpr(id);
	return e && name && e->SetWeight(name, v) ? 1 : 0;
}

NOVA_PACKAGE_EXPORT float EXP_GetWeight(uint64 id, const char* name)
{
	Expressions* e = FindExpr(id);
	return e && name ? e->GetWeight(name) : 0.0f;
}

NOVA_PACKAGE_EXPORT int EXP_Count(uint64 id)
{
	Expressions* e = FindExpr(id);
	return e ? (int)e->List.size() : 0;
}

NOVA_PACKAGE_EXPORT const char* EXP_Name(uint64 id, int index)
{
	Expressions* e = FindExpr(id);
	return e && index >= 0 && index < (int)e->List.size() ? ScriptBindings::ReturnString(e->List[index].Name) : nullptr;
}

NOVA_PACKAGE_EXPORT float EXP_GetFloat(uint64 id, int prop)
{
	Expressions* e = FindExpr(id);
	if (!e) return 0.0f;
	switch (prop) { case 0: return e->BlinkInterval; case 1: return e->BlinkDuration; case 2: return e->AutoBlinkValue(); case 3: return e->AutoBlink ? 1.0f : 0.0f; default: return 0.0f; }
}

NOVA_PACKAGE_EXPORT void EXP_SetFloat(uint64 id, int prop, float v)
{
	Expressions* e = FindExpr(id);
	if (!e) return;
	switch (prop) { case 0: e->BlinkInterval = (std::max)(0.5f, v); break; case 1: e->BlinkDuration = (std::max)(0.05f, v); break; case 3: e->AutoBlink = v != 0.0f; break; default: break; }
}

// ---- DynamicBone: float 0 weight, 1 stiffnessScale, 2 gravityScale, 3 dragScale, 4 windTurbulence / vector 0 wind
namespace
{
	DynamicBone* FindDyn(uint64 id)
	{
		GameObject* go = ScriptBindings::FindObject(id);
		return go ? go->GetComponentIncludingPending<DynamicBone>() : nullptr;
	}
}

NOVA_PACKAGE_EXPORT float DYN_GetFloat(uint64 id, int prop)
{
	DynamicBone* d = FindDyn(id);
	if (d == nullptr) return 0.0f;
	switch (prop) { case 0: return d->Weight; case 1: return d->StiffnessScale; case 2: return d->GravityScale; case 3: return d->DragScale; case 4: return d->WindTurbulence; default: return 0.0f; }
}

NOVA_PACKAGE_EXPORT void DYN_SetFloat(uint64 id, int prop, float v)
{
	DynamicBone* d = FindDyn(id);
	if (d == nullptr) return;
	switch (prop)
	{
	case 0: d->Weight = std::clamp(v, 0.0f, 1.0f); break;
	case 1: d->StiffnessScale = (std::max)(0.0f, v); break;
	case 2: d->GravityScale = (std::max)(0.0f, v); break;
	case 3: d->DragScale = (std::max)(0.0f, v); break;
	case 4: d->WindTurbulence = std::clamp(v, 0.0f, 1.0f); break;
	default: break;
	}
}

NOVA_PACKAGE_EXPORT void DYN_GetVector(uint64 id, int prop, Vec3* out)
{
	DynamicBone* d = FindDyn(id);
	if (out) *out = d && prop == 0 ? d->Wind : Vec3::Zero;
}

NOVA_PACKAGE_EXPORT void DYN_SetVector(uint64 id, int prop, Vec3* v)
{
	if (DynamicBone* d = FindDyn(id); d && v && prop == 0) d->Wind = *v;
}

// what: 0 사슬 수, 1 chain 의 본 수, 2 충돌체 수
NOVA_PACKAGE_EXPORT int DYN_Info(uint64 id, int what, int chain)
{
	DynamicBone* d = FindDyn(id);
	if (d == nullptr) return 0;
	if (what == 0) return (int)d->Chains.size();
	if (what == 1) return chain >= 0 && chain < (int)d->Chains.size() ? (int)d->Chains[chain].Joints.size() : 0;
	return (int)d->Colliders.size();
}

NOVA_PACKAGE_EXPORT int DYN_GetTail(uint64 id, int chain, int joint, Vec3* out)
{
	DynamicBone* d = FindDyn(id);
	Vec3 p;
	if (d == nullptr || out == nullptr || !d->TailPosition(chain, joint, p)) return 0;
	*out = p;
	return 1;
}

NOVA_PACKAGE_EXPORT void DYN_Reset(uint64 id)
{
	if (DynamicBone* d = FindDyn(id)) d->ResetSimulation();
}
