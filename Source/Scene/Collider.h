#pragma once
#include "Component.h"

class Collider
	: public Component
{
private:
	Vec3 m_Center;

public:
	Collider();
	~Collider();

public:
	void SetCenter(Vec3 center) { m_Center = center; }
	Vec3 GetCenter() const { return m_Center; }

public:
	GENERATE_COMPONENT_BODY(Collider)
};

