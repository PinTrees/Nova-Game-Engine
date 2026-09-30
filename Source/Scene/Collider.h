#pragma once
#include "Component.h"

class Collider
	: public Component
{
protected:
	Vec3 m_Center;
	bool m_IsTrigger = false;
	bool m_ProvidesContacts = false;

	// 공통 직렬화 / 공통 Inspector 항목 (Is Trigger, Provides Contacts, Material, Center)
	void SerializeCommon(json& j) const;
	void DeserializeCommon(const json& j);
	void DrawCommonInspector();

public:
	Collider();
	~Collider();

public:
	void SetCenter(Vec3 center) { m_Center = center; }
	Vec3 GetCenter() const { return m_Center; }
	bool IsTrigger() const { return m_IsTrigger; }
	void SetIsTrigger(bool trigger) { m_IsTrigger = trigger; }

	// 로컬 Center 를 월드 좌표로 변환 (콜라이더의 중심)
	Vec3 GetWorldCenter();

	virtual bool UsesUnityInspector() const override { return true; }

public:
	GENERATE_COMPONENT_BODY(Collider)
};

