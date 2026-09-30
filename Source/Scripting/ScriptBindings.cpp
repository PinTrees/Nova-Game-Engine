#include "pch.h"
#include "Debug.h"
#include "ScriptBindings.h"
#include "ScriptEngine.h"
#include "CSharpScript.h"
#include "GameViewEditorWindow.h"
#include "GameObjectFactory.h"
#include "ComponentFactory.h"
#include "RigidBody.h"
#include "Collider.h"
#include "Animator.h"
#include "AudioSource.h"
#include "AudioClip.h"
#include "AudioManager.h"
#include "PhysicsManager.h"

// C# 쪽 NativeApiTable(ScriptCore/Interop/NativeApi.cs)과 같은 순서·형식. 하나라도 어긋나면 Initialize 가 크기 불일치로 거부한다.
namespace
{
	using u8 = const char;   // C# 의 byte* (UTF-8, 널 끝)

	struct TimeData
	{
		float deltaTime, unscaledDeltaTime, time, unscaledTime, fixedDeltaTime, timeScale, realtimeSinceStartup;
		int frameCount;
	};
	struct RaycastData
	{
		Vec3 point;
		Vec3 normal;
		float distance;
		uint64 gameObject;
	};

	struct NativeApiTable
	{
		int Size;
		void(*Log)(int, u8*, u8*, u8*, int);

		int(*GO_IsValid)(uint64);
		u8* (*GO_GetName)(uint64);
		void(*GO_SetName)(uint64, u8*);
		int(*GO_GetActive)(uint64, int);
		void(*GO_SetActive)(uint64, int);
		u8* (*GO_GetTag)(uint64);
		void(*GO_SetTag)(uint64, u8*);
		uint64(*GO_Find)(u8*, int);
		uint64(*GO_Create)(u8*);
		uint64(*GO_CreatePrimitive)(int);
		void(*GO_Destroy)(uint64, float);
		uint64(*GO_Instantiate)(uint64, int, Vec3*, Quaternion*, uint64);
		int(*GO_HasComponent)(uint64, u8*);
		void* (*GO_AddComponent)(uint64, u8*);
		void(*GO_RemoveComponent)(uint64, u8*);

		void(*TR_GetVector)(uint64, int, Vec3*);
		void(*TR_SetVector)(uint64, int, Vec3*);
		void(*TR_GetQuat)(uint64, int, Quaternion*);
		void(*TR_SetQuat)(uint64, int, Quaternion*);
		uint64(*TR_GetParent)(uint64);
		void(*TR_SetParent)(uint64, uint64, int);
		int(*TR_GetChildCount)(uint64);
		uint64(*TR_GetChild)(uint64, int);

		void(*Time_Get)(TimeData*);
		int(*Input_GetKey)(int, int);
		int(*Input_GetMouseButton)(int, int);
		void(*Input_GetMouse)(Vec4*);
		void(*Screen_Get)(int*, int*);

		void(*RB_GetVector)(uint64, int, Vec3*);
		void(*RB_SetVector)(uint64, int, Vec3*);
		void(*RB_AddForce)(uint64, int, Vec3*, int);
		float(*RB_GetFloat)(uint64, int);
		void(*RB_SetFloat)(uint64, int, float);
		int(*RB_GetBool)(uint64, int);
		void(*RB_SetBool)(uint64, int, int);
		void(*RB_Move)(uint64, int, Vec4*);

		void(*AS_Call)(uint64, int);
		float(*AS_GetFloat)(uint64, int);
		void(*AS_SetFloat)(uint64, int, float);
		int(*AS_GetBool)(uint64, int);
		void(*AS_SetBool)(uint64, int, int);
		void(*AS_PlayOneShot)(uint64, u8*, float);
		u8* (*AS_GetClip)(uint64);
		void(*AS_SetClip)(uint64, u8*);
		float(*Audio_ClipLength)(u8*);

		void(*AN_SetParam)(uint64, u8*, int, float);
		float(*AN_GetParam)(uint64, u8*, int);

		int(*PH_Raycast)(Vec3*, Vec3*, float, RaycastData*);
		uint64(*Camera_Main)();

		void(*Script_SetEnabled)(void*, int);
	};

	// ---------------------------------------------------------------- 공용
	std::unordered_map<uint64, GameObject*> s_Cache;   // 프레임마다 비움
	struct PendingAdd { GameObject* Object; uint64 Parent; bool WorldStays; };
	std::vector<GameObject*> s_Created;                 // 이번 프레임에 만든 (아직 씬 목록에 없는) 오브젝트
	struct DelayedDestroy { uint64 Id; float Time; };
	std::vector<DelayedDestroy> s_DelayedDestroys;
	double s_StartTime = 0.0;

	const char* Ret(const std::string& s)
	{
		// 관리 코드가 바로 복사해 가므로 몇 개를 돌려 쓴다
		static std::string ring[8];
		static int i = 0;
		i = (i + 1) & 7;
		ring[i] = s;
		return ring[i].c_str();
	}

	Scene* CurrentScene() { return SceneManager::GetI()->GetCurrentScene(); }

	GameObject* Find(uint64 id)
	{
		if (id == 0)
			return nullptr;
		auto it = s_Cache.find(id);
		if (it != s_Cache.end())
			return it->second;
		Scene* scene = CurrentScene();
		GameObject* g = scene ? scene->FindByFileID(id) : nullptr;
		if (g == nullptr)
			for (GameObject* c : s_Created)
				if (c->GetFileID() == id) { g = c; break; }
		if (g)
			s_Cache[id] = g;
		return g;
	}

	void CacheTree(GameObject* g)
	{
		s_Cache[g->GetFileID()] = g;
		for (GameObject* c : g->GetChildren())
			CacheTree(c);
	}

	void ForgetTree(GameObject* g)
	{
		s_Cache.erase(g->GetFileID());
		for (GameObject* c : g->GetChildren())
			ForgetTree(c);
	}

	bool ActiveInHierarchy(GameObject* go)
	{
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
			if (!g->IsActive())
				return false;
		return true;
	}

	// 새로 만든 오브젝트: 캐시에 넣고 씬 목록에는 프레임 끝에 (업데이트 중 목록을 바꾸면 반복이 깨진다)
	void AddToSceneLater(GameObject* g, uint64 parent, bool worldStays)
	{
		s_Created.push_back(g);
		CacheTree(g);
		SceneManager::GetI()->AddLastUpdate([g, parent, worldStays]() {
			s_Created.erase(std::remove(s_Created.begin(), s_Created.end(), g), s_Created.end());
			Scene* scene = CurrentScene();
			if (scene == nullptr)
				return;
			scene->AddRootGameObject(g);
			if (parent)
				if (GameObject* p = scene->FindByFileID(parent))
					g->SetParent(p, worldStays);
		});
	}

	// Play 중 새 오브젝트: Awake (스크립트는 인스턴스 생성) — Start 는 첫 Update 직전
	void AwakeTree(GameObject* g)
	{
		if (!Application::IsPlaying())
			return;
		for (const auto& c : g->GetComponents())
		{
			if (auto* s = dynamic_cast<CSharpScript*>(c.get()))
				s->AwakeNow();
			else
			{
				c->Awake();
				c->Start();
			}
		}
		for (GameObject* child : g->GetChildren())
			AwakeTree(child);
	}

	void DestroyNow(uint64 id)
	{
		SceneManager::GetI()->AddLastUpdate([id]() {
			Scene* scene = CurrentScene();
			GameObject* g = scene ? scene->FindByFileID(id) : nullptr;
			if (g == nullptr)
				return;
			ForgetTree(g);
			scene->DestroyGameObject(g);
		});
	}

	Component* FindComponent(GameObject* g, const std::string& type)
	{
		if (g == nullptr)
			return nullptr;
		auto match = [&](Component* c) -> bool {
			if (type.rfind("script:", 0) == 0)
			{
				auto* s = dynamic_cast<CSharpScript*>(c);
				const std::string cls = type.substr(7);
				return s && (s->GetClassName() == cls || s->GetClassName().size() > cls.size() && s->GetClassName().compare(s->GetClassName().size() - cls.size() - 1, std::string::npos, "." + cls) == 0);
			}
			if (type == "Collider")
				return dynamic_cast<Collider*>(c) != nullptr;
			return c->GetType() == type;
		};
		for (const auto& c : g->GetComponents())
			if (c && match(c.get()))
				return c.get();
		return nullptr;
	}

	template <typename T> T* Get(uint64 id) { GameObject* g = Find(id); return g ? g->GetComponent<T>() : nullptr; }

	ForceMode ToForceMode(int m)
	{
		switch (m)
		{
		case 1: return ForceMode::Impulse;
		case 2: return ForceMode::VelocityChange;
		case 3: return ForceMode::Acceleration;
		default: return ForceMode::Force;
		}
	}

	// ---------------------------------------------------------------- 함수들
	void Log(int level, u8* msg, u8* stack, u8* file, int line)
	{
		Debug::Write((LogType)std::clamp(level, 0, 2), msg ? msg : "", stack ? stack : "", file ? file : "", line);
	}

	int GO_IsValid(uint64 id) { return Find(id) != nullptr; }
	u8* GO_GetName(uint64 id) { GameObject* g = Find(id); return Ret(g ? g->GetName() : std::string()); }
	void GO_SetName(uint64 id, u8* n) { if (GameObject* g = Find(id)) g->SetName(n ? n : ""); }
	int GO_GetActive(uint64 id, int hierarchy) { GameObject* g = Find(id); return g ? (hierarchy ? ActiveInHierarchy(g) : g->IsActive()) : 0; }
	void GO_SetActive(uint64 id, int v) { if (GameObject* g = Find(id)) g->SetActive(v != 0); }
	u8* GO_GetTag(uint64 id) { GameObject* g = Find(id); return Ret(g ? g->GetTag() : std::string("Untagged")); }
	void GO_SetTag(uint64 id, u8* t) { if (GameObject* g = Find(id)) g->SetTag(t ? t : "Untagged"); }

	uint64 GO_Find(u8* key, int byTag)
	{
		Scene* scene = CurrentScene();
		if (scene == nullptr || key == nullptr)
			return 0;
		const std::string k = key;
		auto test = [&](GameObject* g) { return ActiveInHierarchy(g) && (byTag ? g->GetTag() == k : g->GetName() == k); };
		for (GameObject* g : scene->GetAllGameObjects())
			if (g && test(g))
				return g->GetFileID();
		for (GameObject* g : s_Created)
			if (test(g))
				return g->GetFileID();
		return 0;
	}

	uint64 GO_Create(u8* name)
	{
		GameObject* g = GameObjectFactory::CreateEmpty(name ? name : "New Game Object");
		AddToSceneLater(g, 0, true);
		return g->GetFileID();
	}

	uint64 GO_CreatePrimitive(int type)
	{
		GameObject* g = nullptr;
		switch (type)   // Unity PrimitiveType: Sphere, Capsule, Cylinder, Cube, Plane, Quad
		{
		case 0: g = GameObjectFactory::CreateSphere(); break;
		case 1: g = GameObjectFactory::CreateCapsule(); break;
		case 2: g = GameObjectFactory::CreateCylinder(); break;
		case 3: g = GameObjectFactory::CreateCube(); break;
		case 4: g = GameObjectFactory::CreatePlane(); break;
		default: g = GameObjectFactory::CreateQuad(); break;
		}
		AddToSceneLater(g, 0, true);
		AwakeTree(g);
		return g->GetFileID();
	}

	void GO_Destroy(uint64 id, float delay)
	{
		if (delay > 0.0f)
			s_DelayedDestroys.push_back({ id, ScriptEngine::PlayTime() + delay });
		else
			DestroyNow(id);
	}

	uint64 GO_Instantiate(uint64 src, int hasPose, Vec3* pos, Quaternion* rot, uint64 parent)
	{
		GameObject* s = Find(src);
		if (s == nullptr)
			return 0;
		json j = *s;
		GameObject* g = new GameObject();
		from_json(j, *g);
		g->RegenerateFileIDs();
		g->SetName(s->GetName() + "(Clone)");
		if (hasPose && pos && rot)
		{
			g->GetTransform()->SetPosition(*pos);
			g->GetTransform()->SetRotation(*rot);
		}
		// 위치를 주면 월드 기준 유지, 부모만 주면 원본의 로컬 값을 부모 기준으로 (Unity 의 instantiateInWorldSpace = false)
		AddToSceneLater(g, parent, hasPose != 0);
		AwakeTree(g);
		return g->GetFileID();
	}

	int GO_HasComponent(uint64 id, u8* type) { return FindComponent(Find(id), type ? type : "") != nullptr; }

	void* GO_AddComponent(uint64 id, u8* type)
	{
		GameObject* g = Find(id);
		if (g == nullptr || type == nullptr)
			return nullptr;
		const std::string t = type;
		if (t.rfind("script:", 0) == 0)
		{
			auto s = CSharpScript::Create(t.substr(7));
			g->QueueComponent(s);
			s->AwakeNow();
			return s->GetHandle();
		}
		auto c = ComponentFactory::Instance().CreateComponent(t);
		if (c == nullptr)
			return nullptr;
		g->QueueComponent(c);
		if (Application::IsPlaying())
		{
			c->Awake();
			c->Start();
		}
		return (void*)1;
	}

	void GO_RemoveComponent(uint64 id, u8* type)
	{
		if (Component* c = FindComponent(Find(id), type ? type : ""))
			GameObject::Destroy(c);
	}

	void TR_GetVector(uint64 id, int prop, Vec3* out)
	{
		GameObject* g = Find(id);
		if (g == nullptr || out == nullptr) { if (out) *out = Vec3::Zero; return; }
		Transform* t = g->GetTransform();
		switch (prop)
		{
		case 0: *out = t->GetPosition(); break;
		case 1: *out = t->GetLocalPosition(); break;
		case 2: *out = t->GetLocalScale(); break;
		case 3: *out = t->GetEulerAngle(); break;
		case 4: *out = t->GetLocalEulerAngles(); break;
		case 5: *out = t->GetScale(); break;
		case 6: *out = t->GetForward(); break;
		case 7: *out = t->GetRight(); break;
		default: *out = t->GetUp(); break;
		}
	}

	void TR_SetVector(uint64 id, int prop, Vec3* v)
	{
		GameObject* g = Find(id);
		if (g == nullptr || v == nullptr)
			return;
		Transform* t = g->GetTransform();
		switch (prop)
		{
		case 0: t->SetPosition(*v); break;
		case 1: t->SetLocalPosition(*v); break;
		case 2: t->SetLocalScale(*v); break;
		case 3: t->SetEulerAngle(*v); break;
		case 4: t->SetLocalEulerAngles(*v); break;
		default: break;
		}
	}

	void TR_GetQuat(uint64 id, int prop, Quaternion* out)
	{
		GameObject* g = Find(id);
		if (out == nullptr) return;
		*out = g ? (prop == 0 ? g->GetTransform()->GetRotation() : g->GetTransform()->GetLocalRotation()) : Quaternion::Identity;
	}

	void TR_SetQuat(uint64 id, int prop, Quaternion* q)
	{
		GameObject* g = Find(id);
		if (g == nullptr || q == nullptr) return;
		if (prop == 0) g->GetTransform()->SetRotation(*q);
		else g->GetTransform()->SetLocalRotation(*q);
	}

	uint64 TR_GetParent(uint64 id) { GameObject* g = Find(id); return g && g->GetParent() ? g->GetParent()->GetFileID() : 0; }
	void TR_SetParent(uint64 id, uint64 parent, int worldStays)
	{
		GameObject* g = Find(id);
		if (g == nullptr) return;
		g->SetParent(parent ? Find(parent) : nullptr, worldStays != 0);
	}
	int TR_GetChildCount(uint64 id) { GameObject* g = Find(id); return g ? g->GetChildCount() : 0; }
	uint64 TR_GetChild(uint64 id, int index)
	{
		GameObject* g = Find(id);
		if (g == nullptr || index < 0 || index >= g->GetChildCount()) return 0;
		GameObject* c = g->GetChildren()[index];
		s_Cache[c->GetFileID()] = c;
		return c->GetFileID();
	}

	void Time_Get(TimeData* t)
	{
		if (t == nullptr) return;
		LARGE_INTEGER f, c;
		::QueryPerformanceFrequency(&f);
		::QueryPerformanceCounter(&c);
		const double now = (double)c.QuadPart / (double)f.QuadPart;
		const float dt = Application::IsPaused() ? 0.0f : (float)DT;
		t->deltaTime = dt;
		t->unscaledDeltaTime = (float)DT;
		t->time = ScriptEngine::PlayTime();
		t->unscaledTime = ScriptEngine::PlayTime();
		t->fixedDeltaTime = PhysicsManager::GetI()->GetFixedTimestep();
		t->timeScale = 1.0f;
		t->realtimeSinceStartup = (float)(now - s_StartTime);
		t->frameCount = ScriptEngine::FrameCount();
	}

	int Input_GetKey(int vk, int mode) { return ScriptEngine::KeyState(vk, mode) ? 1 : 0; }
	int Input_GetMouseButton(int b, int mode) { return ScriptEngine::MouseButtonState(b, mode) ? 1 : 0; }
	void Input_GetMouse(Vec4* out)
	{
		if (out == nullptr) return;
		float x = 0, y = 0;
		GameViewEditorWindow::MouseToGame(x, y);
		*out = Vec4(x, y, 0.0f, GameViewEditorWindow::HasInputFocus() ? GameViewEditorWindow::ScrollDelta() : 0.0f);
	}
	void Screen_Get(int* w, int* h)
	{
		int ww = 0, hh = 0;
		GameViewEditorWindow::GameSize(ww, hh);
		if (w) *w = ww;
		if (h) *h = hh;
	}

	void RB_GetVector(uint64 id, int prop, Vec3* out)
	{
		RigidBody* rb = Get<RigidBody>(id);
		if (out == nullptr) return;
		if (rb == nullptr) { *out = Vec3::Zero; return; }
		*out = prop == 0 ? rb->GetVelocity() : (prop == 1 ? rb->GetAngularVelocity() : rb->GetWorldCenterOfMass());
	}
	void RB_SetVector(uint64 id, int prop, Vec3* v)
	{
		RigidBody* rb = Get<RigidBody>(id);
		if (rb == nullptr || v == nullptr) return;
		if (prop == 0) rb->SetVelocity(*v);
		else if (prop == 1) rb->SetAngularVelocity(*v);
	}
	void RB_AddForce(uint64 id, int kind, Vec3* v, int mode)
	{
		RigidBody* rb = Get<RigidBody>(id);
		if (rb == nullptr || v == nullptr) return;
		if (kind == 0) rb->AddForce(*v, ToForceMode(mode));
		else rb->AddTorque(*v, ToForceMode(mode));
	}
	float RB_GetFloat(uint64 id, int prop)
	{
		RigidBody* rb = Get<RigidBody>(id);
		if (rb == nullptr) return 0.0f;
		return prop == 0 ? rb->GetMass() : (prop == 1 ? rb->GetLinearDamping() : rb->GetAngularDamping());
	}
	void RB_SetFloat(uint64 id, int prop, float v)
	{
		RigidBody* rb = Get<RigidBody>(id);
		if (rb == nullptr) return;
		if (prop == 0) rb->SetMass(v);
		else if (prop == 1) rb->SetLinearDamping(v);
		else rb->SetAngularDamping(v);
	}
	int RB_GetBool(uint64 id, int prop)
	{
		RigidBody* rb = Get<RigidBody>(id);
		if (rb == nullptr) return 0;
		return prop == 0 ? rb->GetUseGravity() : (prop == 1 ? rb->IsKinematic() : rb->IsSleeping());
	}
	void RB_SetBool(uint64 id, int prop, int v)
	{
		RigidBody* rb = Get<RigidBody>(id);
		if (rb == nullptr) return;
		if (prop == 0) rb->SetUseGravity(v != 0);
		else if (prop == 1) rb->SetKinematic(v != 0);
	}
	void RB_Move(uint64 id, int kind, Vec4* v)
	{
		RigidBody* rb = Get<RigidBody>(id);
		if (rb == nullptr || v == nullptr) return;
		if (kind == 0) rb->MovePosition(Vec3(v->x, v->y, v->z));
		else rb->MoveRotation(Quaternion(v->x, v->y, v->z, v->w));
	}

	void AS_Call(uint64 id, int op)
	{
		AudioSource* a = Get<AudioSource>(id);
		if (a == nullptr) return;
		switch (op) { case 0: a->Play(); break; case 1: a->Stop(); break; case 2: a->Pause(); break; default: a->UnPause(); break; }
	}
	float AS_GetFloat(uint64 id, int prop)
	{
		AudioSource* a = Get<AudioSource>(id);
		if (a == nullptr) return 0.0f;
		switch (prop) { case 0: return a->GetVolume(); case 1: return a->GetPitch(); case 2: return a->GetStereoPan(); case 3: return a->GetSpatialBlend(); default: return a->GetTime(); }
	}
	void AS_SetFloat(uint64 id, int prop, float v)
	{
		AudioSource* a = Get<AudioSource>(id);
		if (a == nullptr) return;
		switch (prop) { case 0: a->SetVolume(v); break; case 1: a->SetPitch(v); break; case 2: a->SetStereoPan(v); break; case 3: a->SetSpatialBlend(v); break; default: break; }
	}
	int AS_GetBool(uint64 id, int prop)
	{
		AudioSource* a = Get<AudioSource>(id);
		if (a == nullptr) return 0;
		switch (prop) { case 0: return a->IsPlaying(); case 1: return a->GetLoop(); case 2: return a->GetMute(); default: return a->GetPlayOnAwake(); }
	}
	void AS_SetBool(uint64 id, int prop, int v)
	{
		AudioSource* a = Get<AudioSource>(id);
		if (a == nullptr) return;
		switch (prop) { case 1: a->SetLoop(v != 0); break; case 2: a->SetMute(v != 0); break; case 3: a->SetPlayOnAwake(v != 0); break; default: break; }
	}
	void AS_PlayOneShot(uint64 id, u8* clipPath, float volume)
	{
		auto clip = clipPath ? AudioClip::Load(clipPath) : nullptr;
		if (clip == nullptr) return;
		if (id == 0) { AudioManager::PlayOneShot(clip, volume); return; }
		if (AudioSource* a = Get<AudioSource>(id)) a->PlayOneShot(clip, volume);
	}
	u8* AS_GetClip(uint64 id) { AudioSource* a = Get<AudioSource>(id); return Ret(a ? a->GetClipPath() : std::string()); }
	void AS_SetClip(uint64 id, u8* path) { if (AudioSource* a = Get<AudioSource>(id)) a->SetClip(path ? path : ""); }
	float Audio_ClipLength(u8* path) { auto clip = path ? AudioClip::Load(path) : nullptr; return clip ? clip->Length : 0.0f; }

	void AN_SetParam(uint64 id, u8* name, int kind, float v)
	{
		Animator* an = Get<Animator>(id);
		if (an == nullptr || name == nullptr) return;
		switch (kind)
		{
		case 0: an->SetFloat(name, v); break;
		case 1: an->SetInteger(name, (int)v); break;
		case 2: an->SetBool(name, v != 0.0f); break;
		default: if (v != 0.0f) an->SetTrigger(name); else an->ResetTrigger(name); break;
		}
	}
	float AN_GetParam(uint64 id, u8* name, int kind)
	{
		Animator* an = Get<Animator>(id);
		if (an == nullptr || name == nullptr) return 0.0f;
		switch (kind) { case 0: return an->GetFloat(name); case 1: return (float)an->GetInteger(name); case 2: return an->GetBool(name) ? 1.0f : 0.0f; default: return 0.0f; }
	}

	int PH_Raycast(Vec3* origin, Vec3* dir, float maxDistance, RaycastData* out)
	{
		if (origin == nullptr || dir == nullptr || out == nullptr) return 0;
		*out = RaycastData{};
		Vec3 d = *dir;
		if (d.LengthSquared() < 1e-12f) return 0;
		d.Normalize();
		RaycastHit hit;
		if (!PhysicsManager::GetI()->Raycast(*origin, d, hit, maxDistance, false))
			return 0;
		out->point = hit.point;
		out->normal = hit.normal;
		out->distance = hit.distance;
		out->gameObject = hit.gameObject ? hit.gameObject->GetFileID() : 0;
		return 1;
	}

	uint64 Camera_Main()
	{
		auto cam = DisplayManager::GetI()->GetActiveCamera();
		return cam && cam->GetGameObject() ? cam->GetGameObject()->GetFileID() : 0;
	}

	void Script_SetEnabled(void* component, int enabled)
	{
		if (component)
			static_cast<CSharpScript*>(component)->SetEnabledFromScript(enabled != 0);
	}
}

namespace ScriptBindings
{
	int TableSize() { return (int)sizeof(NativeApiTable); }

	void Fill(void* p)
	{
		LARGE_INTEGER f, c;
		::QueryPerformanceFrequency(&f);
		::QueryPerformanceCounter(&c);
		s_StartTime = (double)c.QuadPart / (double)f.QuadPart;

		NativeApiTable& t = *static_cast<NativeApiTable*>(p);
		t = NativeApiTable{};
		t.Size = (int)sizeof(NativeApiTable);
		t.Log = Log;
		t.GO_IsValid = GO_IsValid; t.GO_GetName = GO_GetName; t.GO_SetName = GO_SetName;
		t.GO_GetActive = GO_GetActive; t.GO_SetActive = GO_SetActive; t.GO_GetTag = GO_GetTag; t.GO_SetTag = GO_SetTag;
		t.GO_Find = GO_Find; t.GO_Create = GO_Create; t.GO_CreatePrimitive = GO_CreatePrimitive; t.GO_Destroy = GO_Destroy;
		t.GO_Instantiate = GO_Instantiate; t.GO_HasComponent = GO_HasComponent; t.GO_AddComponent = GO_AddComponent;
		t.GO_RemoveComponent = GO_RemoveComponent;
		t.TR_GetVector = TR_GetVector; t.TR_SetVector = TR_SetVector; t.TR_GetQuat = TR_GetQuat; t.TR_SetQuat = TR_SetQuat;
		t.TR_GetParent = TR_GetParent; t.TR_SetParent = TR_SetParent; t.TR_GetChildCount = TR_GetChildCount; t.TR_GetChild = TR_GetChild;
		t.Time_Get = Time_Get; t.Input_GetKey = Input_GetKey; t.Input_GetMouseButton = Input_GetMouseButton;
		t.Input_GetMouse = Input_GetMouse; t.Screen_Get = Screen_Get;
		t.RB_GetVector = RB_GetVector; t.RB_SetVector = RB_SetVector; t.RB_AddForce = RB_AddForce;
		t.RB_GetFloat = RB_GetFloat; t.RB_SetFloat = RB_SetFloat; t.RB_GetBool = RB_GetBool; t.RB_SetBool = RB_SetBool; t.RB_Move = RB_Move;
		t.AS_Call = AS_Call; t.AS_GetFloat = AS_GetFloat; t.AS_SetFloat = AS_SetFloat; t.AS_GetBool = AS_GetBool; t.AS_SetBool = AS_SetBool;
		t.AS_PlayOneShot = AS_PlayOneShot; t.AS_GetClip = AS_GetClip; t.AS_SetClip = AS_SetClip; t.Audio_ClipLength = Audio_ClipLength;
		t.AN_SetParam = AN_SetParam; t.AN_GetParam = AN_GetParam;
		t.PH_Raycast = PH_Raycast; t.Camera_Main = Camera_Main;
		t.Script_SetEnabled = Script_SetEnabled;
	}

	void BeginFrame()
	{
		s_Cache.clear();
	}

	void Update()
	{
		if (!Application::IsPlaying() || s_DelayedDestroys.empty())
			return;
		const float now = ScriptEngine::PlayTime();
		for (size_t i = 0; i < s_DelayedDestroys.size();)
		{
			if (s_DelayedDestroys[i].Time <= now)
			{
				DestroyNow(s_DelayedDestroys[i].Id);
				s_DelayedDestroys.erase(s_DelayedDestroys.begin() + i);
			}
			else
				++i;
		}
	}

	void Reset()
	{
		s_Cache.clear();
		s_Created.clear();
		s_DelayedDestroys.clear();
	}
}
