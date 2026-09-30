#include "pch.h"
#include "Mesh.h"
#include "FBXLoader.h"
#include "LoadM3d.h"
#include "MathHelper.h"
#include "File.h"
#include "MeshUtility.h"


Mesh::Mesh()
{
}

Mesh::~Mesh()
{
}

void Mesh::Setup()
{
	Vec3 minVertex = { +MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity };
	Vec3 maxVertex = { -MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity };
	for (int i = 0; i < Vertices.size(); i++)
	{
		if (Vertices[i].pos.x < minVertex.x) minVertex.x = Vertices[i].pos.x;
		if (Vertices[i].pos.y < minVertex.y) minVertex.y = Vertices[i].pos.y;
		if (Vertices[i].pos.z < minVertex.z) minVertex.z = Vertices[i].pos.z;

		if (Vertices[i].pos.x > maxVertex.x) maxVertex.x = Vertices[i].pos.x;
		if (Vertices[i].pos.y > maxVertex.y) maxVertex.y = Vertices[i].pos.y;
		if (Vertices[i].pos.z > maxVertex.z) maxVertex.z = Vertices[i].pos.z;
	}

	Ball.center.x = (minVertex.x + maxVertex.x) / 2;
	Ball.center.y = (minVertex.y + maxVertex.y) / 2;
	Ball.center.z = (minVertex.z + maxVertex.z) / 2;

	Ball.radius = MeshUtility::ComputeBoundingRadius(Ball.center, Vertices);

	ModelMesh.SetVertices(Application::GetI()->GetDevice(), &Vertices[0], Vertices.size()); 
	ModelMesh.SetIndices(Application::GetI()->GetDevice(), &Indices[0], Indices.size()); 
	ModelMesh.SetSubsetTable(Subsets);

	SubsetCount = Subsets.size();
}

void Mesh::from_byte(ifstream& inStream)
{
	if (!inStream.is_open())
	{
		std::cerr << "File stream is not open!" << std::endl;
		return;
	}

	// 1. 이름 읽기 (문자열)
	size_t nameLength = 0;
	inStream.read(reinterpret_cast<char*>(&nameLength), sizeof(size_t));

	Name.resize(nameLength);
	inStream.read(&Name[0], nameLength);

	// 2. Vertices 읽기 (PosNormalTexTan2 배열)
	size_t vertexCount = 0;
	inStream.read(reinterpret_cast<char*>(&vertexCount), sizeof(size_t));

	Vertices.resize(vertexCount);
	inStream.read(reinterpret_cast<char*>(Vertices.data()), vertexCount * sizeof(Vertex::PosNormalTexTan2));

	// 3. Indices 읽기 (USHORT 배열)
	size_t indexCount = 0;
	inStream.read(reinterpret_cast<char*>(&indexCount), sizeof(size_t));

	Indices.resize(indexCount);
	inStream.read(reinterpret_cast<char*>(Indices.data()), indexCount * sizeof(USHORT));

	// 4. Subsets 읽기
	size_t subsetCount = 0;
	inStream.read(reinterpret_cast<char*>(&subsetCount), sizeof(size_t));

	Subsets.resize(subsetCount);
	for (auto& subset : Subsets)
	{
		// 이름 읽기 (문자열)
		size_t subsetNameLength = 0;
		inStream.read(reinterpret_cast<char*>(&subsetNameLength), sizeof(size_t));

		subset.Name.resize(subsetNameLength);
		inStream.read(&subset.Name[0], subsetNameLength);

		// 다른 필드들 읽기 (Id, MaterialIndex, VertexStart, VertexCount, FaceStart, FaceCount)
		inStream.read(reinterpret_cast<char*>(&subset.Id), sizeof(subset.Id));
		inStream.read(reinterpret_cast<char*>(&subset.MaterialIndex), sizeof(subset.MaterialIndex));
		inStream.read(reinterpret_cast<char*>(&subset.VertexStart), sizeof(subset.VertexStart));
		inStream.read(reinterpret_cast<char*>(&subset.VertexCount), sizeof(subset.VertexCount));
		inStream.read(reinterpret_cast<char*>(&subset.FaceStart), sizeof(subset.FaceStart));
		inStream.read(reinterpret_cast<char*>(&subset.FaceCount), sizeof(subset.FaceCount));
	}

	Setup();
}

void Mesh::to_byte(ofstream& outStream)
{
	if (!outStream.is_open())
	{
		std::cerr << "File stream is not open!" << std::endl;
		return;
	}

	// 1. 이름 저장 (문자열)
	size_t nameLength = Name.size();
	outStream.write(reinterpret_cast<const char*>(&nameLength), sizeof(size_t));
	outStream.write(Name.c_str(), nameLength);

	// 2. Vertices 저장 (PosNormalTexTan2 배열)
	size_t vertexCount = Vertices.size();
	outStream.write(reinterpret_cast<const char*>(&vertexCount), sizeof(size_t));
	outStream.write(reinterpret_cast<const char*>(Vertices.data()), vertexCount * sizeof(Vertex::PosNormalTexTan2));

	// 3. Indices 저장 (USHORT 배열)
	size_t indexCount = Indices.size();
	outStream.write(reinterpret_cast<const char*>(&indexCount), sizeof(size_t));
	outStream.write(reinterpret_cast<const char*>(Indices.data()), indexCount * sizeof(USHORT));

	// 4. Subsets 저장
	size_t subsetCount = Subsets.size();
	outStream.write(reinterpret_cast<const char*>(&subsetCount), sizeof(size_t));

	for (const auto& subset : Subsets)
	{
		// 이름 저장 (문자열)
		size_t subsetNameLength = subset.Name.size();
		outStream.write(reinterpret_cast<const char*>(&subsetNameLength), sizeof(size_t));
		outStream.write(subset.Name.c_str(), subsetNameLength);

		// 다른 필드들 저장 (Id, MaterialIndex, VertexStart, VertexCount, FaceStart, FaceCount)
		outStream.write(reinterpret_cast<const char*>(&subset.Id), sizeof(subset.Id));
		outStream.write(reinterpret_cast<const char*>(&subset.MaterialIndex), sizeof(subset.MaterialIndex));
		outStream.write(reinterpret_cast<const char*>(&subset.VertexStart), sizeof(subset.VertexStart));
		outStream.write(reinterpret_cast<const char*>(&subset.VertexCount), sizeof(subset.VertexCount));
		outStream.write(reinterpret_cast<const char*>(&subset.FaceStart), sizeof(subset.FaceStart));
		outStream.write(reinterpret_cast<const char*>(&subset.FaceCount), sizeof(subset.FaceCount));
	}
}

void from_json(const json& j, Mesh& m)
{
}

void to_json(json& j, const Mesh& m)
{
	SERIALIZE_TYPE(j, Mesh);
	SERIALIZE_STRING(j, m.Name, "name");
	SERIALIZE_PosNormalTexTan2_ARRAY(j, m.Vertices, "vertices");
	SERIALIZE_USHORT_ARRAY(j, m.Indices, "indices");

	json array_json = json::array();
	for (MeshGeometry::Subset subset : m.Subsets)
	{
		json subset_json;
		subset_json["name"] = subset.Name;
		subset_json["id"] = subset.Id;
		subset_json["materialIndex"] = subset.MaterialIndex;
		subset_json["vertexStart"] = subset.VertexStart;
		subset_json["vertexCount"] = subset.VertexCount;
		subset_json["faceStart"] = subset.FaceStart;
		subset_json["faceCount"] = subset.FaceCount;
		array_json.push_back(subset_json);
	}
	j["subsets"] = array_json;
}
