#pragma once
#include "Component.h"

// 씬에 저장된 컴포넌트 타입을 이 엔진/프로젝트가 모를 때 (패키지를 넣지 않았거나 뺐을 때) 자리를 지키는 컴포넌트.
// 저장된 JSON 을 그대로 들고 있다가 다시 저장한다 → 패키지를 넣으면 실제 컴포넌트로 바뀐다 (Unity 의 Missing Script 와 같은 역할).
class MissingComponent
	: public Component
{
private:
	std::string m_MissingType;
	json m_Data;

public:
	MissingComponent(const std::string& type, const json& data) : m_MissingType(type), m_Data(data)
	{
		m_InspectorTitleName = type + " (Missing)";
	}

	const std::string& GetMissingType() const { return m_MissingType; }
	const json& GetData() const { return m_Data; }

	json toJson() const override { return m_Data; }
	void fromJson(const json& j) override { m_Data = j; }
	std::string GetType() const override { return "MissingComponent"; }

	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	bool HasEnabledToggle() const override { return false; }
};
