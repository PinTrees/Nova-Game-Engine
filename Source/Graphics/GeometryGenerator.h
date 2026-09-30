#pragma once

class GeometryGenerator
{
public:
	struct Vertex
	{
		Vertex() : position(0, 0, 0), normal(0, 0, 0), tangentU(0, 0, 0), texC(0, 0) {}
		Vertex(const XMFLOAT3& p, const XMFLOAT3& n, const XMFLOAT3& t, const XMFLOAT2& uv)
			: position(p), normal(n), tangentU(t), texC(uv){}
		Vertex(
			float px, float py, float pz, 
			float nx, float ny, float nz,
			float tx, float ty, float tz,
			float u, float v)
			: position(px,py,pz), normal(nx,ny,nz),
			  tangentU(tx, ty, tz), texC(u,v){}

		XMFLOAT3 position;
		XMFLOAT3 normal;
		XMFLOAT3 tangentU;
		XMFLOAT2 texC;
	};

	struct MeshData
	{
		std::vector<Vertex> vertices;
		std::vector<uint32> indices;
	};

	void CreateBox(float width, float height, float depth, MeshData& meshData);
	void CreateSphere(float radius, uint32 sliceCount, uint32 stackCount, MeshData& meshData);
	void CreateGeosphere(float radius, uint32 numSubdivisions, MeshData& meshData);
	void CreateCylinder(float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount, MeshData& meshData);
	void CreateGrid(float width, float depth, uint32 m, uint32 n, MeshData& meshData);
	void CreateFullscreenQuad(MeshData& meshData);

private:
	void Subdivide(MeshData& meshData);
	void BuildCylinderTopCap(float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount, MeshData& meshData);
	void BuildCylinderBottomCap(float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount, MeshData& meshData);
};