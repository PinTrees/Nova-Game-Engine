#pragma once
#include "Collider.h"

class SphereCollider
	: public Collider
{
private:
	float m_Radius;

public:
	SphereCollider();
	~SphereCollider();

public:
	void SetRadius(float radius) { MarkPhysicsDirty(); m_Radius = radius; }
	float GetRadius();

public:
	virtual void OnDrawGizmos() override;
	virtual void OnInspectorGUI() override;
	virtual const char* InspectorIconName() const override { return "sphere_collider"; }

	GENERATE_COMPONENT_BODY(SphereCollider)
};

REGISTER_COMPONENT(SphereCollider)

