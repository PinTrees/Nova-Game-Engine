#pragma once
#include "Collider.h"

class BoxCollider
	: public Collider
{
private:
	Vec3 m_Size;

public:
	BoxCollider();
	~BoxCollider();

public:
	void SetSize(Vec3 size) { m_Size = size; }
	Vec3 GetSize();

public:
	virtual void OnDrawGizmos() override;
	virtual void OnInspectorGUI() override;
	virtual const char* InspectorIconName() const override { return "box_collider"; }

public:
	GENERATE_COMPONENT_BODY(BoxCollider)
};

REGISTER_COMPONENT(BoxCollider) 