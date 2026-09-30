#pragma once
#include <nlohmann/json.hpp>
#include "JsonUtility.h"

using json = nlohmann::json;

class GameObject;

class Component
{
private:
	static int nextInstanceId;

protected:
	GameObject* m_pGameObject;
	int m_InstanceId;

protected:
	// Editor
	string	m_InspectorTitleName = "";
	wstring	m_InspectorIconPath = L"";  
	bool	m_InspectorOpened;
	bool	m_Enabled = true;   // Inspector 헤더의 활성 체크박스

public:
	Component();
	virtual ~Component();

	int GetInstanceID() { return m_InstanceId; }

	virtual void Awake()  { }
	virtual void Start()  { }	
	virtual void Update() { }
	virtual void LateUpdate()  { }
	virtual void FixedUpdate() { }

	virtual void LastUpdate() { }

	virtual void Render() { }

	// Editor Only
	virtual void _Editor_Render() { }
	virtual void _Editor_Update() { }

	virtual void Reset() { }
	void RenderInspectorGUI(); 
	virtual void OnInspectorGUI() { }
	// Unity 스타일 Inspector(UnityGUI)로 본문을 그리는 컴포넌트는 true (기존 스타일 push 를 생략)
	virtual bool UsesUnityInspector() const { return false; }
	// 헤더에 활성 체크박스를 표시할지 (Transform 은 표시하지 않음)
	virtual bool HasEnabledToggle() const { return true; }
	bool IsEnabled() const { return m_Enabled; }
	void SetEnabled(bool enabled) { m_Enabled = enabled; }
	virtual void OnDrawGizmos() { }
	virtual void OnDestroy() { }
public:
	GameObject* GetGameObject() { return m_pGameObject; }

private:
	friend class GameObject;
	void SetGameObject(GameObject* gameObject) { m_pGameObject = gameObject; }

public:
	virtual json toJson() const = 0;
	virtual void fromJson(const json& j) = 0;
	virtual std::string GetType() const = 0;
};

