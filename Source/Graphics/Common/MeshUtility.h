#pragma once

class MeshUtility
{
public:
	static float ComputeBoundingRadius(const Vec3& center, const std::vector<Vertex::PosNormalTexTan2>& vertices);
	static float ComputeBoundingRadius(const Vec3& center, const std::vector<Vertex::PosNormalTexTanSkinned>& vertices); 
};

