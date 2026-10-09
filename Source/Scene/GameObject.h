#pragma once
#include "MemoryHeaps.h"
#include <atomic>
#include <nlohmann/json.hpp>
#include "Component.h"
#include "LightManager.h"
using json = nlohmann::json;
using std::make_shared;
using std::static_pointer_cast;
	
#include "PrefabUtility.h"

class MonoBehaviour;
class Component;
class Transform;

class NOVA_API GameObject
{
private:
	PrefabLink m_Prefab;	// 프리팹 인스턴스 연결 (없으면 일반 오브젝트)
	uint64 m_FileID;		// 저장되는 고유 ID (씬 파일/Undo/프리팹이 오브젝트를 다시 찾을 때). 실행마다 바뀌는 InstanceID 와 다르다
	uint64 m_InstanceID;	// ���� �ν��Ͻ� ID
	static atomic<uint64> g_NextInstanceID;

	string m_Name;

	uint8 m_LayerIndex;
	vector<shared_ptr<Component>> m_Components;
	vector<shared_ptr<Component>> m_ComponentsToAdd;	// �ӽ� �����

	vector<MonoBehaviour*> m_Scripts;

	GameObject* m_pParentGameObject;
	vector<GameObject*> m_pChildGameObjects;
	Transform* m_pTransform;

	// Runtime Value
	bool m_IsActive;
	bool m_Staged = false;   // 씬 스트리밍: 미리 짓는 중 (SetStaged)
	string m_Tag = "Untagged";
	bool m_IsStatic = false;

	// Editor
	vector<function<void()>>		m_Editor_LastUpdateActions; 
public:
	bool							m_Editor_HierachOpened;

public:
	GameObject();
	// 씬 힙에서 받는다 (MemoryHeaps.h — 활성 힙, 없으면 시스템). 놓을 때는 주소로 힙을 찾는다
	static void* operator new(size_t size);
	static void operator delete(void* p, size_t size);
	GameObject(const string& name);
	~GameObject();

public:
	static void Destroy(Component* component_ptr); 
	static void Destroy(GameObject* gameobject_ptr);
	// 아직 delete 되지 않은 오브젝트인가 (프레임을 넘겨 들고 있던 포인터를 쓰기 전에 확인)
	static bool IsAlive(const GameObject* gameobject_ptr);
	static size_t LiveCount();   // 지금 메모리에 있는 GameObject 수 (모든 씬 + 지우기 대기, CLI info)
	// Editor Only
	static void DestroyImmediatly(GameObject* gameobject_ptr);

public:
	uint64 GetInstanceID() const { return m_InstanceID; }
	uint64 GetFileID() const { return m_FileID; }
	const PrefabLink& GetPrefabLink() const { return m_Prefab; }
	void SetPrefabLink(const PrefabLink& link) { m_Prefab = link; }
	void SetFileID(uint64 id) { m_FileID = id; }
	// 복제/붙여넣기/프리팹 배치 뒤: 자기와 자식 모두 새 ID. 복사한 묶음 안을 가리키던 참조(Component::RemapFileIDs)도 새 ID 로
	void RegenerateFileIDs();
	// 여러 루트를 한 묶음으로 (씬을 더해 읽을 때): 루트 사이의 참조도 새 ID 로
	static void RegenerateFileIDs(const std::vector<GameObject*>& roots);
private:
	void RegenerateFileIDs(std::unordered_map<uint64, uint64>& map);
	void RemapFileIDs(const std::unordered_map<uint64, uint64>& map);
public:
	static uint64 NewFileID();
	const string& GetName() { return m_Name; }
	bool IsActive() const { return m_IsActive && !m_Staged; }   // Unity 의 activeSelf (Inspector 체크박스 · 저장은 m_IsActive)
	// 씬 스트리밍: LoadSceneAsync 가 미리 짓는 중인 씬의 루트 — 바꿔 끼울 때까지 꺼진 것으로 보인다
	//  (전역 목록 — 나무 · 카메라 · 볼륨 · 파티클 … — 이 지금 씬에 끼워 그리지 않게. 저장 · 인스펙터는 그대로)
	void SetStaged(bool staged) { m_Staged = staged; }
	bool IsStaged() const { return m_Staged; }
	void SetActive(bool active) { m_IsActive = active; }
	// Unity 의 activeInHierarchy: 자기와 모든 부모가 켜졌다 — 그리기 · 업데이트 · 물리는 이것을 본다 (부모를 끄면 자식도 꺼진다)
	bool IsActiveInHierarchy() const
	{
		for (const GameObject* g = this; g != nullptr; g = g->m_pParentGameObject)
			if (!g->m_IsActive || g->m_Staged)
				return false;
		return true;
	}
	// 장면 업데이트가 지난 프레임에 본 activeInHierarchy (바뀌면 컴포넌트의 OnHierarchyActiveChanged)
	bool m_HierarchyActiveSeen = true;
	void SetName(const string& name) { m_Name = name; }
	vector<GameObject*> GetChildren() { return m_pChildGameObjects; }
	const vector<GameObject*>& Children() const { return m_pChildGameObjects; }   // 복사 없이 (자주 훑는 곳용)
	int GetChildCount() { return m_pChildGameObjects.size(); }
	Transform* GetTransform() { return m_pTransform; }
	uint32_t CullSceneStamp = 0;   // SceneCulling: 이번 프레임에 그리는 씬의 오브젝트이면 그 프레임 번호
	
	// 부모 변경. worldPositionStays = true 면 월드 위치/회전/크기를 유지한다 (Unity 의 Transform.SetParent 와 동일).
	// 새로 만든 오브젝트를 자식으로 넣을 때는 false (로컬 값 그대로 부모 기준에 놓임).
	void SetParent(GameObject* parent, bool worldPositionStays = true);
	GameObject* GetParent() { return m_pParentGameObject; }

	void Awake();
	void Start();
	void Update();
	void LateUpdate();
	void FixedUpdate();
	void LastUpdate(); 

	void OnDestroy(); 

	const string& GetTag() const { return m_Tag; }
	void SetTag(const string& tag) { m_Tag = tag; }
	bool IsStatic() const { return m_IsStatic; }
	void SetStatic(bool isStatic) { m_IsStatic = isStatic; }

	void SetLayerIndex(uint8 layer) { m_LayerIndex = layer; } 
	uint8 GetLayerIndex() { return m_LayerIndex; } 

	void ApplyPendingComponents();
	// 업데이트 도중(스크립트의 AddComponent 등) 컴포넌트를 붙일 때: 바로 이 GameObject 소속으로 만들고, 목록에는 프레임 끝에 넣는다
	void QueueComponent(const std::shared_ptr<Component>& component)
	{
		if (component == nullptr)
			return;
		component->SetGameObject(this);
		EnsureRequiredComponents(component.get(), true);
		m_ComponentsToAdd.push_back(component);
	}
	template <class T>
	T* AddComponent()
	{
		std::shared_ptr<T> component = Memory::Heaps::MakeShared<T>();   // 씬 힙 (컴포넌트 + 제어 블록 한 덩어리)
		Component* baseComponent = static_cast<Component*>(component.get());

		baseComponent->SetGameObject(this);
		EnsureRequiredComponents(baseComponent, false);
		m_Components.push_back(component);
		
		if ("Light" == baseComponent->GetType())
		{
			shared_ptr<Light> ptr = dynamic_pointer_cast<Light>(component);
			LightManager::GetI()->SetLight(ptr);
		}

		return component.get();
	}
	void AddComponent(const std::shared_ptr<Component>& component)
	{
		if (component == nullptr)
			return;

		if ("Light" == component->GetType())
		{
			shared_ptr<Light> ptr = dynamic_pointer_cast<Light>(component);
			LightManager::GetI()->SetLight(ptr);
		}

		component->SetGameObject(this);
		EnsureRequiredComponents(component.get(), false);
		m_Components.push_back(component);
	}
	// 같은 자리의 컴포넌트를 바꾼다 (패키지를 넣은 뒤 MissingComponent → 실제 컴포넌트)
	void ReplaceComponent(Component* old, const std::shared_ptr<Component>& component)
	{
		for (auto& c : m_Components)
			if (c.get() == old)
			{
				component->SetGameObject(this);
				c = component;
				return;
			}
	}
	template <class T>
	T* GetComponent()
	{
		// dynamic_pointer_cast 는 검사하는 컴포넌트마다 shared_ptr 를 만들어(원자적 참조 수 증감) 느리다 → 포인터만 검사
		for (auto& component : m_Components)
			if (T* c = dynamic_cast<T*>(component.get()))
				return c;
		return nullptr;
	}
	template <class T> 
	shared_ptr<T> GetComponent_SP()
	{
		for (auto& component : m_Components)
		{
			std::shared_ptr<T> castedComponent = std::dynamic_pointer_cast<T>(component);
			if (castedComponent)
			{
				return castedComponent;
			}
		}
		return nullptr;
	}
	vector<shared_ptr<Component>>& GetComponents() { return m_Components; }
	// 이번 프레임에 붙였지만 아직 목록에 들어가지 않은 컴포넌트 (스크립트의 AddComponent 직후 GetComponent 용)
	const vector<shared_ptr<Component>>& GetPendingComponents() const { return m_ComponentsToAdd; }
	template <class T>
	T* GetComponentIncludingPending()
	{
		if (T* c = GetComponent<T>())
			return c;
		for (auto& component : m_ComponentsToAdd)
			if (T* c = dynamic_cast<T*>(component.get()))
				return c;
		return nullptr;
	}

private:
	friend class GameObject;
	void EnsureRequiredComponents(Component* component, bool deferred);
	friend class Scene;
	friend class GameObjectFactory;   // Scene::DestroyGameObject 가 부모의 자식 목록을 정리한다
	void SetChild(GameObject* child);
	void RemoveChild(GameObject* child);
	void SetParentImmediate(GameObject* g) { m_pParentGameObject = g; }

public:
	void OnInspectorGUI();

public:
	SERIALIZE(GameObject)
	DESERIALIZE(GameObject)
};

