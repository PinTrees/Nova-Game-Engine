#pragma once
#include "Component.h"
#include "ComponentFactory.h"
#include "ScriptField.h"
#include "Input.h"
#include "EngineTime.h"
#include <string>
#include <vector>
#include <memory>

class Collider;
class Transform;
class GameObject;

class MonoBehaviour : public Component
{
private:
	std::string m_ScriptName = "MonoBehaviour";
	bool m_IsEnabled = true;

	std::vector<IScriptField*> m_Fields;
	std::vector<std::unique_ptr<IScriptField>> m_OwnedRawFields;

public:
	MonoBehaviour();
	virtual ~MonoBehaviour();

	// Script identification
	const std::string& GetScriptName() const { return m_ScriptName; }
	void SetScriptName(const std::string& name) { m_ScriptName = name; }

	// Enable / Disable
	bool IsEnabled() const { return m_IsEnabled; }
	void SetEnabled(bool enabled);

	// Unity-style shortcuts
	Transform* GetTransform();
	Transform* transform();
	GameObject* gameObject();

	// Field Registration (called automatically by Field<T> or REGISTER_PROPERTY)
	void RegisterField(IScriptField* field);

	template <typename T>
	void RegisterProperty(const std::string& name, T& variable)
	{
		auto rawField = std::make_unique<RawScriptField<T>>(name, &variable);
		m_Fields.push_back(rawField.get());
		m_OwnedRawFields.push_back(std::move(rawField));
	}

public:
	// Lifecycle Events
	virtual void Awake() override {}
	virtual void Start() override {}
	virtual void Update() override {}
	virtual void LateUpdate() override {}
	virtual void FixedUpdate() override {}
	virtual void OnEnable() {}
	virtual void OnDisable() {}
	virtual void OnDestroy() override {}

	// Physics callbacks
	virtual void OnCollisionEnter(Collider* other) {}
	virtual void OnTriggerEnter(Collider* other) {}

	// 100% Automated Inspector GUI
	virtual void OnInspectorGUI() override;

	// 100% Automated Serialization & Deserialization
	virtual json toJson() const override;
	virtual void fromJson(const json& j) override;
	virtual std::string GetType() const override { return m_ScriptName; }
};

// Global hook for Field<T> auto registration
inline void RegisterMonoBehaviourField(MonoBehaviour* owner, IScriptField* field)
{
	if (owner && field)
	{
		owner->RegisterField(field);
	}
}

// Macro helper for registering raw member variables
#define REGISTER_PROPERTY(var) RegisterProperty(#var, var)

// Automatic Script Registration macro for ComponentFactory
#define REGISTER_SCRIPT(CLASS) \
    namespace { \
        struct CLASS##_AutoRegister { \
            CLASS##_AutoRegister() { \
                ComponentFactory::Instance().RegisterComponent(#CLASS, []() -> std::shared_ptr<Component> { \
                    auto instance = std::make_shared<CLASS>(); \
                    instance->SetScriptName(#CLASS); \
                    return instance; \
                }); \
            } \
        }; \
        static CLASS##_AutoRegister s_##CLASS##_auto_register; \
    }
