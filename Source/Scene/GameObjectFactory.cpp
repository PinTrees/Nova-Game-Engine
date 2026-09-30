#include "pch.h"
#include "GameObjectFactory.h"
#include "GameObject.h"
#include "Transform.h"
#include "MeshRenderer.h"
#include "Camera.h"
#include "Light.h"
#include "BoxCollider.h"
#include "SphereCollider.h"
#include "GeometryGenerator.h"
#include "Mesh.h"

std::map<PrimitiveType, std::shared_ptr<Mesh>> GameObjectFactory::s_PrimitiveMeshes;

static std::shared_ptr<Mesh> BuildMeshFromGeometry(const GeometryGenerator::MeshData& meshData, const std::string& name)
{
	std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>();
	mesh->Name = name;

	mesh->Vertices.resize(meshData.vertices.size());
	for (size_t i = 0; i < meshData.vertices.size(); ++i)
	{
		mesh->Vertices[i].pos = meshData.vertices[i].position;
		mesh->Vertices[i].normal = meshData.vertices[i].normal;
		mesh->Vertices[i].tex = meshData.vertices[i].texC;
		mesh->Vertices[i].tangentU = XMFLOAT4(
			meshData.vertices[i].tangentU.x,
			meshData.vertices[i].tangentU.y,
			meshData.vertices[i].tangentU.z,
			1.0f
		);
	}

	mesh->Indices.resize(meshData.indices.size());
	for (size_t i = 0; i < meshData.indices.size(); ++i)
	{
		mesh->Indices[i] = static_cast<USHORT>(meshData.indices[i]);
	}

	MeshGeometry::Subset subset;
	subset.Id = 0;
	subset.VertexStart = 0;
	subset.VertexCount = static_cast<uint32>(mesh->Vertices.size());
	subset.FaceStart = 0;
	subset.FaceCount = static_cast<uint32>(mesh->Indices.size() / 3);
	subset.MaterialIndex = 0;
	mesh->Subsets.push_back(subset);

	Material defaultMat;
	defaultMat.Ambient = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
	defaultMat.Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
	defaultMat.Specular = XMFLOAT4(0.4f, 0.4f, 0.4f, 16.0f);
	mesh->Mat.push_back(defaultMat);

	mesh->Setup();
	return mesh;
}

std::shared_ptr<Mesh> GameObjectFactory::GetPrimitiveMesh(PrimitiveType type)
{
	auto it = s_PrimitiveMeshes.find(type);
	if (it != s_PrimitiveMeshes.end())
		return it->second;

	GeometryGenerator geoGen;
	std::shared_ptr<Mesh> mesh = nullptr;

	switch (type)
	{
	case PrimitiveType::Cube:
	{
		GeometryGenerator::MeshData data;
		geoGen.CreateBox(1.0f, 1.0f, 1.0f, data);
		mesh = BuildMeshFromGeometry(data, "Cube");
		break;
	}
	case PrimitiveType::Sphere:
	{
		GeometryGenerator::MeshData data;
		geoGen.CreateSphere(0.5f, 24, 24, data);
		mesh = BuildMeshFromGeometry(data, "Sphere");
		break;
	}
	case PrimitiveType::Cylinder:
	{
		GeometryGenerator::MeshData data;
		geoGen.CreateCylinder(0.5f, 0.5f, 1.0f, 24, 24, data);
		mesh = BuildMeshFromGeometry(data, "Cylinder");
		break;
	}
	case PrimitiveType::Plane:
	{
		GeometryGenerator::MeshData data;
		geoGen.CreateGrid(10.0f, 10.0f, 10, 10, data);
		mesh = BuildMeshFromGeometry(data, "Plane");
		break;
	}
	}

	if (mesh != nullptr)
	{
		s_PrimitiveMeshes[type] = mesh;
	}

	return mesh;
}

GameObject* GameObjectFactory::CreateEmpty(const std::string& name)
{
	GameObject* obj = new GameObject(name);
	return obj;
}

GameObject* GameObjectFactory::CreateCube(const std::string& name)
{
	GameObject* obj = new GameObject(name);
	auto mr = obj->AddComponent<MeshRenderer>();
	mr->SetMesh(GetPrimitiveMesh(PrimitiveType::Cube));

	auto col = obj->AddComponent<BoxCollider>();
	col->SetSize(Vec3(1.0f, 1.0f, 1.0f));
	return obj;
}

GameObject* GameObjectFactory::CreateSphere(const std::string& name)
{
	GameObject* obj = new GameObject(name);
	auto mr = obj->AddComponent<MeshRenderer>();
	mr->SetMesh(GetPrimitiveMesh(PrimitiveType::Sphere));

	auto col = obj->AddComponent<SphereCollider>();
	col->SetRadius(0.5f);
	return obj;
}

GameObject* GameObjectFactory::CreateCylinder(const std::string& name)
{
	GameObject* obj = new GameObject(name);
	auto mr = obj->AddComponent<MeshRenderer>();
	mr->SetMesh(GetPrimitiveMesh(PrimitiveType::Cylinder));
	return obj;
}

GameObject* GameObjectFactory::CreatePlane(const std::string& name)
{
	GameObject* obj = new GameObject(name);
	auto mr = obj->AddComponent<MeshRenderer>();
	mr->SetMesh(GetPrimitiveMesh(PrimitiveType::Plane));

	auto col = obj->AddComponent<BoxCollider>();
	col->SetSize(Vec3(10.0f, 0.01f, 10.0f));
	return obj;
}

GameObject* GameObjectFactory::CreateDirectionalLight(const std::string& name)
{
	GameObject* obj = new GameObject(name);
	auto light = obj->AddComponent<Light>();
	DirectionalLight dir;
	dir.Ambient = XMFLOAT4(0.6f, 0.6f, 0.6f, 1.0f);
	dir.Diffuse = XMFLOAT4(0.8f, 0.7f, 0.7f, 1.0f);
	dir.Specular = XMFLOAT4(0.6f, 0.6f, 0.7f, 1.0f);
	dir.Direction = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);
	light->SetDirLight(dir);
	return obj;
}

GameObject* GameObjectFactory::CreatePointLight(const std::string& name)
{
	GameObject* obj = new GameObject(name);
	auto light = obj->AddComponent<Light>();
	PointLight pt;
	pt.Ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	pt.Diffuse = XMFLOAT4(1.0f, 0.9f, 0.7f, 1.0f);
	pt.Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	pt.Position = XMFLOAT3(0.0f, 3.0f, 0.0f);
	pt.Range = 15.0f;
	pt.Att = XMFLOAT3(0.0f, 0.1f, 0.0f);
	light->SetPointLight(pt);
	return obj;
}

GameObject* GameObjectFactory::CreateSpotLight(const std::string& name)
{
	GameObject* obj = new GameObject(name);
	auto light = obj->AddComponent<Light>();
	SpotLight sp;
	sp.Ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	sp.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sp.Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sp.Position = XMFLOAT3(0.0f, 5.0f, 0.0f);
	sp.Range = 25.0f;
	sp.Direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
	sp.Spot = 45.0f;
	sp.Att = XMFLOAT3(1.0f, 0.0f, 0.0f);
	light->SetSpotLight(sp);
	return obj;
}

GameObject* GameObjectFactory::CreateCamera(const std::string& name)
{
	GameObject* obj = new GameObject(name);
	obj->AddComponent<Camera>();
	if (name == "Main Camera")
		obj->SetTag("MainCamera");
	return obj;
}
