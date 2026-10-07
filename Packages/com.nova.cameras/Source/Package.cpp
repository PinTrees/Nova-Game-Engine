// com.nova.cameras 진입점: Follow Camera + Cinemachine (Brain · 가상 카메라 · 단계 컴포넌트 · 충격),
// GameObject > Cinemachine 메뉴, CLI "cinemachine", C# API (Runtime/FollowCamera.cs · Runtime/Cinemachine.cs 가 DllImport("NovaCameras") 로 부른다)
#include "pch.h"
#include "FollowCamera.h"
#include "CinemachineCamera.h"
#include "CinemachineBrain.h"
#include "CinemachinePosition.h"
#include "CinemachineRotation.h"
#include "CinemachineNoise.h"
#include "ScriptBindings.h"
#include "CliServer.h"
#include "EditorExtensions.h"
#include "UndoSystem.h"

NOVA_PACKAGE_EXPORT const char* NovaPackage_Abi() { return NOVA_PACKAGE_ABI_VERSION; }

namespace
{
	constexpr const char* kPackage = "com.nova.cameras";

	// C# 가 넘긴 GameObject id → 컴포넌트 (이번 프레임에 만든 오브젝트 · 방금 AddComponent 한 것도)
	FollowCamera* Find(uint64 gameObject)
	{
		GameObject* go = ScriptBindings::FindObject(gameObject);
		return go ? go->GetComponentIncludingPending<FollowCamera>() : nullptr;
	}

	// ---------------------------------------------------------------- Cinemachine: 종류 번호 (Runtime/Cinemachine.cs 의 Kind 와 같다)
	enum Kind { KCamera = 0, KBrain, KFollow, KOrbital, KThirdPerson, KComposer, KHardLookAt, KRotateWithTarget, KPerlin, KImpulseSource, KImpulseListener };

	Component* FindComponent(GameObject* go, int kind)
	{
		if (go == nullptr)
			return nullptr;
		switch (kind)
		{
		case KCamera: return go->GetComponentIncludingPending<CinemachineCamera>();
		case KBrain: return go->GetComponentIncludingPending<CinemachineBrain>();
		case KFollow: return go->GetComponentIncludingPending<CinemachineFollow>();
		case KOrbital: return go->GetComponentIncludingPending<CinemachineOrbitalFollow>();
		case KThirdPerson: return go->GetComponentIncludingPending<CinemachineThirdPersonFollow>();
		case KComposer: return go->GetComponentIncludingPending<CinemachineRotationComposer>();
		case KHardLookAt: return go->GetComponentIncludingPending<CinemachineHardLookAt>();
		case KRotateWithTarget: return go->GetComponentIncludingPending<CinemachineRotateWithFollowTarget>();
		case KPerlin: return go->GetComponentIncludingPending<CinemachineBasicMultiChannelPerlin>();
		case KImpulseSource: return go->GetComponentIncludingPending<CinemachineImpulseSource>();
		case KImpulseListener: return go->GetComponentIncludingPending<CinemachineImpulseListener>();
		default: return nullptr;
		}
	}

	ICmProperties* Props(uint64 gameObject, int kind)
	{
		return dynamic_cast<ICmProperties*>(FindComponent(ScriptBindings::FindObject(gameObject), kind));
	}

	// ---------------------------------------------------------------- 편집기: GameObject > Cinemachine
	Camera* FindMainCamera(Scene* scene)
	{
		Camera* first = nullptr;
		for (GameObject* g : scene->GetAllGameObjects())
			if (Camera* c = g ? g->GetComponent<Camera>() : nullptr)
			{
				if (g->GetTag() == "MainCamera")
					return c;
				if (first == nullptr)
					first = c;
			}
		return first;
	}

	GameObject* FindByName(const std::string& name)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr || name.empty())
			return nullptr;
		for (GameObject* g : scene->GetAllGameObjects())
			if (g && g->GetName() == name)
				return g;
		return nullptr;
	}

	enum class Preset { Camera, Follow, FreeLook, ThirdPerson };

	// 가상 카메라 만들기: Main Camera 의 자세 · 렌즈로, Main Camera 에 Brain 이 없으면 붙인다. target = 고른 오브젝트 (Tracking Target)
	GameObject* CreateVirtualCamera(Scene* scene, GameObject* target, Preset preset, const std::string& name)
	{
		if (scene == nullptr)
			return nullptr;
		Camera* main = FindMainCamera(scene);
		if (main && main->GetGameObject()->GetComponent<CinemachineBrain>() == nullptr)
			main->GetGameObject()->AddComponent<CinemachineBrain>();

		GameObject* go = new GameObject(name);
		CinemachineCamera* vcam = go->AddComponent<CinemachineCamera>();
		if (main)
		{
			Transform* mt = main->GetGameObject()->GetTransform();
			go->GetTransform()->SetPosition(mt->GetPosition());
			go->GetTransform()->SetRotation(mt->GetRotation());
			vcam->Lens.FieldOfView = XMConvertToDegrees(main->GetFovY());
			vcam->Lens.NearClipPlane = main->GetNearZ();
			vcam->Lens.FarClipPlane = main->GetFarZ();
			vcam->Lens.OrthographicSize = main->GetOrthoSize();
			vcam->Validate();
		}
		if (target && target != go)
			vcam->TrackingTarget = target->GetFileID();

		switch (preset)
		{
		case Preset::Follow:
			go->AddComponent<CinemachineFollow>();
			go->AddComponent<CinemachineRotationComposer>();
			break;
		case Preset::FreeLook:
		{
			CinemachineOrbitalFollow* orbit = go->AddComponent<CinemachineOrbitalFollow>();
			orbit->OrbitStyle = 1;
			orbit->TrackerSettings.BindingMode = CmTracker::WorldSpace;
			go->AddComponent<CinemachineRotationComposer>();
			break;
		}
		case Preset::ThirdPerson:
		{
			CinemachineThirdPersonFollow* tp = go->AddComponent<CinemachineThirdPersonFollow>();
			tp->AvoidObstacles = true;
			break;
		}
		default:
			break;
		}
		scene->AddRootGameObject(go);
		scene->RegisterGameObjectTree(go);
		Undo::SetActionName("Create " + name);
		Undo::RequestCheck();
		return go;
	}

	void RegisterMenu(const char* path, Preset preset, const char* name)
	{
		EditorExtensions::CreateMenuItem item;
		item.Owner = kPackage;
		item.Path = path;
		item.Create = [preset, name](Scene* scene, GameObject* parent) { return CreateVirtualCamera(scene, parent, preset, name); };
		EditorExtensions::RegisterCreateMenu(item);
	}

	// ---------------------------------------------------------------- CLI: nova cinemachine <op> [--인자 …]
	json Help()
	{
		return {
			{ "info", "brains (live camera, blend from/to/t, output pose), cameras (priority, live, pose, pipeline), impulses" },
			{ "create", "--kind camera|follow|freelook|thirdperson [--name N] [--target T]   like GameObject > Cinemachine (adds a Brain to Main Camera)" },
			{ "prioritize", "<camera>   make it the newest of its priority (Unity Prioritize)" },
			{ "priority", "<camera> --value 20" },
			{ "enable", "<camera> --value true|false   component enabled" },
			{ "axis", "<camera> [--horizontal deg] [--vertical v] [--radial r]   Orbital Follow axes" },
			{ "impulse", "<source> [--force 1] [--velocity x,y,z]   Cinemachine Impulse Source" },
			{ "snap", "<camera>   next update without damping" },
			{ "blend", "--style Cut|EaseInOut|EaseIn|EaseOut|HardIn|HardOut|Linear [--time 2] [--from A --to B]   default blend of the Brain, or a custom blend" },
		};
	}

	CinemachineCamera* CliCamera(const json& a, std::string& e)
	{
		const std::string name = a.value("camera", a.value("path", std::string()));
		GameObject* go = FindByName(name);
		CinemachineCamera* vcam = go ? go->GetComponent<CinemachineCamera>() : nullptr;
		if (vcam == nullptr)
			e = "no Cinemachine Camera named '" + name + "'";
		return vcam;
	}

	bool RunCli(const json& a, json& r, std::string& e)
	{
		const std::string op = a.value("op", std::string("help"));
		if (op == "help")
		{
			r = Help();
			return true;
		}
		if (op == "info")
		{
			json brains = json::array(), cameras = json::array();
			for (CinemachineBrain* b : CmCore::Brains())
				if (b->GetGameObject() && CmCore::FindObject(b->GetGameObject()->GetFileID()) == b->GetGameObject())
					brains.push_back(b->Info());
			for (CinemachineCamera* c : CmCore::Cameras())
				if (c->GetGameObject() && CmCore::FindObject(c->GetGameObject()->GetFileID()) == c->GetGameObject())
					cameras.push_back(c->Info());
			r = { { "brains", brains }, { "cameras", cameras }, { "impulses", CmCore::ActiveImpulseCount() }, { "playing", Application::IsPlaying() } };
			return true;
		}
		if (op == "create")
		{
			const std::string kind = a.value("kind", std::string("camera"));
			Preset preset = Preset::Camera;
			const char* name = "CinemachineCamera";
			if (kind == "follow") { preset = Preset::Follow; name = "Follow Camera"; }
			else if (kind == "freelook") { preset = Preset::FreeLook; name = "FreeLook Camera"; }
			else if (kind == "thirdperson") { preset = Preset::ThirdPerson; name = "Third Person Aim Camera"; }
			else if (kind != "camera") { e = "kind: camera | follow | freelook | thirdperson"; return false; }
			GameObject* target = nullptr;
			if (a.contains("target"))
			{
				target = FindByName(a["target"].get<std::string>());
				if (target == nullptr) { e = "no target '" + a["target"].get<std::string>() + "'"; return false; }
			}
			GameObject* go = CreateVirtualCamera(SceneManager::GetI()->GetCurrentScene(), target, preset, a.value("name", std::string(name)));
			if (go == nullptr) { e = "no scene"; return false; }
			r = go->GetComponent<CinemachineCamera>()->Info();
			return true;
		}
		if (op == "impulse")
		{
			const std::string name = a.value("source", a.value("path", std::string()));
			GameObject* go = FindByName(name);
			CinemachineImpulseSource* src = go ? go->GetComponent<CinemachineImpulseSource>() : nullptr;
			if (src == nullptr) { e = "no Cinemachine Impulse Source named '" + name + "'"; return false; }
			if (a.contains("velocity") && a["velocity"].is_array() && a["velocity"].size() == 3)
				src->GenerateImpulseWithVelocity(Vec3(a["velocity"][0].get<float>(), a["velocity"][1].get<float>(), a["velocity"][2].get<float>()));
			else
				src->GenerateImpulseWithForce(a.value("force", 1.0f));
			r = { { "impulses", CmCore::ActiveImpulseCount() } };
			return true;
		}
		if (op == "blend")
		{
			CinemachineBrain* brain = nullptr;
			for (CinemachineBrain* b : CmCore::Brains())
				if (b->GetGameObject() && CmCore::FindObject(b->GetGameObject()->GetFileID()) == b->GetGameObject())
				{
					brain = b;
					break;
				}
			if (brain == nullptr) { e = "no Cinemachine Brain in the scene"; return false; }
			int style = brain->DefaultBlendStyle;
			if (a.contains("style"))
			{
				const json& st = a["style"];
				if (st.is_number())
					style = st.get<int>();
				else
				{
					// 이름 (공백 · 대소문자 무시)
					auto norm = [](std::string x) {
						x.erase(std::remove(x.begin(), x.end(), ' '), x.end());
						std::transform(x.begin(), x.end(), x.begin(), [](unsigned char c) { return (char)tolower(c); });
						return x;
					};
					style = -1;
					for (int i = 0; i < CmCore::BlendStyleCount; ++i)
						if (norm(CmCore::BlendStyleNames()[i]) == norm(st.get<std::string>()))
							style = i;
					if (style < 0) { e = "style: Cut | EaseInOut | EaseIn | EaseOut | HardIn | HardOut | Linear"; return false; }
				}
			}
			const float time = a.value("time", brain->DefaultBlendTime);
			if (a.contains("from") || a.contains("to"))
			{
				CinemachineBrain::CustomBlend c;
				c.From = a.value("from", c.From);
				c.To = a.value("to", c.To);
				c.Style = std::clamp(style, 0, (int)CmCore::BlendStyleCount - 1);
				c.Time = (std::max)(0.0f, time);
				auto& list = brain->CustomBlends;
				list.erase(std::remove_if(list.begin(), list.end(), [&c](const CinemachineBrain::CustomBlend& x) { return x.From == c.From && x.To == c.To; }), list.end());
				list.push_back(c);
			}
			else
			{
				brain->DefaultBlendStyle = style;
				brain->DefaultBlendTime = time;
				brain->Validate();
			}
			r = brain->Info();
			r["customBlends"] = brain->toJson()["customBlends"];
			return true;
		}
		CinemachineCamera* vcam = CliCamera(a, e);
		if (vcam == nullptr)
			return false;
		if (op == "prioritize")
			vcam->Prioritize();
		else if (op == "priority")
		{
			if (!a.contains("value")) { e = "--value <int>"; return false; }
			vcam->Priority = a["value"].get<int>();
		}
		else if (op == "enable")
			vcam->SetEnabled(a.value("value", true));
		else if (op == "snap")
			vcam->SnapNext();
		else if (op == "axis")
		{
			CinemachineOrbitalFollow* orbit = vcam->GetGameObject()->GetComponent<CinemachineOrbitalFollow>();
			if (orbit == nullptr) { e = "no Cinemachine Orbital Follow on " + vcam->GetGameObject()->GetName(); return false; }
			if (a.contains("horizontal")) orbit->HorizontalAxis.Value = a["horizontal"].get<float>();
			if (a.contains("vertical")) orbit->VerticalAxis.Value = a["vertical"].get<float>();
			if (a.contains("radial")) orbit->RadialAxis.Value = a["radial"].get<float>();
			orbit->Validate();
		}
		else
		{
			e = "unknown op '" + op + "' (nova cinemachine help)";
			return false;
		}
		r = vcam->Info();
		return true;
	}
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnLoad()
{
	if (Application::IsPlayer())
		return;   // 게임 빌드에는 편집기가 없다
	RegisterMenu("Cinemachine/Cinemachine Camera", Preset::Camera, "CinemachineCamera");
	RegisterMenu("Cinemachine/Targeted Cameras/Follow Camera", Preset::Follow, "Follow Camera");
	RegisterMenu("Cinemachine/Targeted Cameras/FreeLook Camera", Preset::FreeLook, "FreeLook Camera");
	RegisterMenu("Cinemachine/Targeted Cameras/Third Person Aim Camera", Preset::ThirdPerson, "Third Person Aim Camera");
	CliServer::Register("cinemachine", "Cinemachine: {op: info|create|prioritize|priority|enable|axis|impulse|snap, ...} (nova cinemachine help)",
		[](const json& args, json& result, std::string& error) { return RunCli(args, result, error); });
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnUnload()
{
	CliServer::Unregister("cinemachine");
	EditorExtensions::UnregisterOwner(kPackage);
}

// ================================================================ Follow Camera (C#: Runtime/FollowCamera.cs)
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

// ================================================================ Cinemachine (C#: Runtime/Cinemachine.cs)
//  (gameObject, kind, prop) — kind = 위 Kind, prop 번호는 각 컴포넌트의 FloatProp · IntProp · BoolProp · VecProp · ObjectProp
NOVA_PACKAGE_EXPORT float NovaCM_GetFloat(uint64 gameObject, int kind, int prop)
{
	ICmProperties* p = Props(gameObject, kind);
	float* v = p ? p->FloatProp(prop) : nullptr;
	return v ? *v : 0.0f;
}

NOVA_PACKAGE_EXPORT void NovaCM_SetFloat(uint64 gameObject, int kind, int prop, float value)
{
	ICmProperties* p = Props(gameObject, kind);
	if (float* v = p ? p->FloatProp(prop) : nullptr)
	{
		*v = value;
		p->Validate();
	}
}

NOVA_PACKAGE_EXPORT int NovaCM_GetInt(uint64 gameObject, int kind, int prop)
{
	ICmProperties* p = Props(gameObject, kind);
	int* v = p ? p->IntProp(prop) : nullptr;
	return v ? *v : 0;
}

NOVA_PACKAGE_EXPORT void NovaCM_SetInt(uint64 gameObject, int kind, int prop, int value)
{
	ICmProperties* p = Props(gameObject, kind);
	if (int* v = p ? p->IntProp(prop) : nullptr)
	{
		*v = value;
		p->Validate();
	}
}

NOVA_PACKAGE_EXPORT int NovaCM_GetBool(uint64 gameObject, int kind, int prop)
{
	ICmProperties* p = Props(gameObject, kind);
	bool* v = p ? p->BoolProp(prop) : nullptr;
	return v && *v ? 1 : 0;
}

NOVA_PACKAGE_EXPORT void NovaCM_SetBool(uint64 gameObject, int kind, int prop, int value)
{
	ICmProperties* p = Props(gameObject, kind);
	if (bool* v = p ? p->BoolProp(prop) : nullptr)
	{
		*v = value != 0;
		p->Validate();
	}
}

NOVA_PACKAGE_EXPORT void NovaCM_GetVector(uint64 gameObject, int kind, int prop, Vec3* out)
{
	if (out == nullptr)
		return;
	ICmProperties* p = Props(gameObject, kind);
	Vec3* v = p ? p->VecProp(prop) : nullptr;
	*out = v ? *v : Vec3();
}

NOVA_PACKAGE_EXPORT void NovaCM_SetVector(uint64 gameObject, int kind, int prop, const Vec3* value)
{
	ICmProperties* p = Props(gameObject, kind);
	if (Vec3* v = p && value ? p->VecProp(prop) : nullptr)
	{
		*v = *value;
		p->Validate();
	}
}

NOVA_PACKAGE_EXPORT uint64 NovaCM_GetObject(uint64 gameObject, int kind, int prop)
{
	ICmProperties* p = Props(gameObject, kind);
	uint64* v = p ? p->ObjectProp(prop) : nullptr;
	return v ? *v : 0;
}

NOVA_PACKAGE_EXPORT void NovaCM_SetObject(uint64 gameObject, int kind, int prop, uint64 value)
{
	ICmProperties* p = Props(gameObject, kind);
	if (uint64* v = p ? p->ObjectProp(prop) : nullptr)
		*v = value;
}

// 동작: Camera 0 Prioritize · 1 Snap (PreviousStateIsValid = false) · 2 IsLive, Brain 0 IsBlending, Perlin 0 ReSeed,
//       Impulse Source 0 At (a 위치, b 속도) · 1 WithVelocity (b) · 2 WithForce (f)
NOVA_PACKAGE_EXPORT float NovaCM_Call(uint64 gameObject, int kind, int fn, const Vec3* a, const Vec3* b, float f)
{
	Component* c = FindComponent(ScriptBindings::FindObject(gameObject), kind);
	if (c == nullptr)
		return 0.0f;
	switch (kind)
	{
	case KCamera:
	{
		auto* vcam = static_cast<CinemachineCamera*>(c);
		if (fn == 0) vcam->Prioritize();
		else if (fn == 1) vcam->SnapNext();
		else if (fn == 2) return vcam->IsLive() ? 1.0f : 0.0f;
		return 0.0f;
	}
	case KBrain:
		return fn == 0 && static_cast<CinemachineBrain*>(c)->IsBlending() ? 1.0f : 0.0f;
	case KPerlin:
		if (fn == 0) static_cast<CinemachineBasicMultiChannelPerlin*>(c)->ReSeed();
		return 0.0f;
	case KImpulseSource:
	{
		auto* src = static_cast<CinemachineImpulseSource*>(c);
		if (fn == 0 && a && b) src->GenerateImpulseAtPositionWithVelocity(*a, *b);
		else if (fn == 1 && b) src->GenerateImpulseWithVelocity(*b);
		else if (fn == 2) src->GenerateImpulseWithForce(f);
		return 0.0f;
	}
	default:
		return 0.0f;
	}
}

// Brain 의 Live 가상 카메라 GameObject (없으면 0)
NOVA_PACKAGE_EXPORT uint64 NovaCM_BrainLive(uint64 gameObject)
{
	auto* brain = static_cast<CinemachineBrain*>(FindComponent(ScriptBindings::FindObject(gameObject), KBrain));
	CinemachineCamera* live = brain ? brain->ActiveVirtualCamera() : nullptr;
	return live && live->GetGameObject() ? live->GetGameObject()->GetFileID() : 0;
}
