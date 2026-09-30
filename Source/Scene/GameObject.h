#pragma once
#include <atomic>
#include <nlohmann/json.hpp>
#include "Component.h"
#include "LightManager.h"
using json = nlohmann::json;
using std::make_shared;
using std::static_pointer_cast;
	
class MonoBehaviour;
class Component;
class Transform;

class GameObject
{
private:
	uint64 m_InstanceID;	// 고유 인스턴스 ID
	static atomic<uint64> g_NextInstanceID;

	string m_Name;

	uint8 m_LayerIndex;
	vector<shared_ptr<Component>> m_Components;
	vector<shared_ptr<Component>> m_ComponentsToAdd;	// 임시 저장소

	vector<MonoBehaviour*> m_Scripts;

	GameObject* m_pParentGameObject;
	vector<GameObject*> m_pChildGameObjects;
	Transform* m_pTransform;

	// Runtime Value
	bool m_IsActive;  

	// Editor
	vector<function<void()>>		m_Editor_LastUpdateActions; 
public:
	bool							m_Editor_HierachOpened;

public:
	GameObject();
	GameObject(const string& name);
	~GameObject();

public:
	static void Destroy(Component* component_ptr); 
	static void Destroy(GameObject* gameobject_ptr);
	// Editor Only
	static void DestroyImmediatly(GameObject* gameobject_ptr);

public:
	uint64 GetInstanceID() const { return m_InstanceID; }
	const string& GetName() { return m_Name; }
	vector<GameObject*> GetChildren() { return m_pChildGameObjects; }
	int GetChildCount() { return m_pChildGameObjects.size(); }
	Transform* GetTransform() { return m_pTransform; }
	
	void SetParent(GameObject* parent);
	GameObject* GetParent() { return m_pParentGameObject; }

	void Awake();
	void Start();
	void Update();
	void LateUpdate();
	void FixedUpdate();
	void LastUpdate(); 

	void OnDestroy(); 

	void SetLayerIndex(uint8 layer) { m_LayerIndex = layer; } 
	uint8 GetLayerIndex() { return m_LayerIndex; } 

	void ApplyPendingComponents();
	template <class T>
	T* AddComponent()
	{
		std::shared_ptr<T> component = std::make_shared<T>();
		Component* baseComponent = static_cast<Component*>(component.get());

		baseComponent->SetGameObject(this);
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
		m_Components.push_back(component);
	}
	template <class T>
	T* GetComponent() 
	{
		for (auto& component : m_Components)
		{
			std::shared_ptr<T> castedComponent = std::dynamic_pointer_cast<T>(component);
			if (castedComponent)
			{
				return castedComponent.get();
			}
		}
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

private:
	friend class GameObject;
	void SetChild(GameObject* child);
	void RemoveChild(GameObject* child);
	void SetParentImmediate(GameObject* g) { m_pParentGameObject = g; }

public:
	void OnInspectorGUI();

public:
	SERIALIZE(GameObject)
	DESERIALIZE(GameObject)
};

