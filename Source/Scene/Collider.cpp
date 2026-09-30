#include "pch.h"
#include "Collider.h"

Collider::Collider()
	: m_Center(Vec3::Zero)
{
}

Collider::~Collider()
{
}

GENERATE_COMPONENT_FUNC_TOJSON(Collider)
{
	json j;
	j["type"] = "Collider";
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Collider)
{

}