#pragma once
#include "Component.h"

// Unity Collider 공통 부분 (Is Trigger, Provides Contacts, Material, Center).
// 형상 파라미터는 파생 클래스(Box/Sphere/Capsule/Mesh)가 가지며, PhysicsManager 가 Jolt 형상으로 변환한다.
class Collider
	: public Component
{
protected:
	Vec3 m_Center;
	bool m_IsTrigger = false;
	bool m_ProvidesContacts = false;

	// Layer Overrides (비트 i = UnityGUI::LayerNames() 의 i 번째 레이어)
	int m_LayerOverridePriority = 0;
	uint32 m_IncludeLayers = 0;
	uint32 m_ExcludeLayers = 0;

	// 공통 직렬화 / 공통 Inspector 항목 (Is Trigger, Provides Contacts, Material, Center)
	void SerializeCommon(json& j) const;
	void DeserializeCommon(const json& j);
	void DrawCommonInspector(bool withCenter = true);
	void DrawLayerOverrides();   // "Layer Overrides" 폴드아웃 (Priority, Include, Exclude)

	// ---- Scene 뷰 와이어 기즈모 (Unity 콜라이더 색) ----
	bool ShouldDrawGizmo() const;   // 선택된 GameObject 의 콜라이더만 표시 (Unity 와 동일)
	static ImU32 GizmoColor();
	static void GizmoCircle(const Vec3& center, const Vec3& u, const Vec3& v, float radius, float a0 = 0.0f, float a1 = XM_2PI, int segments = 48);

public:
	Collider();
	~Collider();

public:
	void SetCenter(Vec3 center) { m_Center = center; }
	Vec3 GetCenter() const { return m_Center; }
	bool IsTrigger() const { return m_IsTrigger; }
	void SetIsTrigger(bool trigger) { m_IsTrigger = trigger; }
	int GetLayerOverridePriority() const { return m_LayerOverridePriority; }
	uint32 GetIncludeLayers() const { return m_IncludeLayers; }
	uint32 GetExcludeLayers() const { return m_ExcludeLayers; }
	void SetIncludeLayers(uint32 mask) { m_IncludeLayers = mask; }
	void SetExcludeLayers(uint32 mask) { m_ExcludeLayers = mask; }

	// 로컬 Center 를 월드 좌표로 변환 (콜라이더의 중심)
	Vec3 GetWorldCenter();

	virtual bool UsesUnityInspector() const override { return true; }

public:
	GENERATE_COMPONENT_BODY(Collider)
};
