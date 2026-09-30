#include "pch.h"
#include "MeshUtility.h"

float MeshUtility::ComputeBoundingRadius(const Vec3& center, const std::vector<Vertex::PosNormalTexTan2>& vertices)
{
	float maxDistance = 0.0f;

	for (const auto& vertex : vertices)
	{
		float distance = std::sqrt((center.x - vertex.pos.x) * (center.x - vertex.pos.x) +
			(center.y - vertex.pos.y) * (center.y - vertex.pos.y) +
			(center.z - vertex.pos.z) * (center.z - vertex.pos.z));
		if (distance > maxDistance)
		{
			maxDistance = distance;
		}
	}

	return maxDistance;
}

float MeshUtility::ComputeBoundingRadius(const Vec3& center, const std::vector<Vertex::PosNormalTexTanSkinned>& vertices)
{
	float maxDistance = 0.0f;

	for (const auto& vertex : vertices)
	{
		float distance = std::sqrt((center.x - vertex.pos.x) * (center.x - vertex.pos.x) +
			(center.y - vertex.pos.y) * (center.y - vertex.pos.y) +
			(center.z - vertex.pos.z) * (center.z - vertex.pos.z));
		if (distance > maxDistance)
		{
			maxDistance = distance;
		}
	}

	return maxDistance;
}
