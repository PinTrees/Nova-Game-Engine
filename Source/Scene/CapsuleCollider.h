#pragma once
#include "Collider.h"

// Unity 의 Capsule Collider. Height 는 양 끝 반구를 포함한 전체 길이, Direction 은 캡슐 축 (0 X, 1 Y, 2 Z).
class CapsuleCollider
	: public Collider
{
private:
	float m_Radius = 0.5f;
	float m_Height = 2.0f;
	int m_Direction = 1;

public:
	CapsuleCollider();
	~CapsuleCollider();

public:
	float GetRadius() const { return m_Radius; }
	void SetRadius(float r) { MarkPhysicsDirty(); m_Radius = (std::max)(0.0f, r); }
	float GetHeight() const { return m_Height; }
	void SetHeight(float h) { MarkPhysicsDirty(); m_Height = (std::max)(0.0f, h); }
	int GetDirection() const { return m_Direction; }
	void SetDirection(int axis) { MarkPhysicsDirty(); m_Direction = std::clamp(axis, 0, 2); }

	// 월드 스케일을 적용한 반지름 / 원통부 절반 길이 (Unity 규칙: 반지름은 축에 수직인 두 스케일 중 큰 값)
	void GetScaledDimensions(const Vec3& lossyScale, float& radius, float& halfCylinder) const;

public:
	virtual void OnDrawGizmos() override;
	virtual void OnInspectorGUI() override;
	virtual const char* InspectorIconName() const override { return "capsule_collider"; }

	GENERATE_COMPONENT_BODY(CapsuleCollider)
};

REGISTER_COMPONENT(CapsuleCollider)
