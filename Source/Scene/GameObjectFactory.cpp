#include "pch.h"
#include "GameObjectFactory.h"
#include "GameObject.h"
#include "Transform.h"
#include "MeshRenderer.h"
#include "MeshFilter.h"
#include "Camera.h"
#include "Light.h"
#include "BoxCollider.h"
#include "SphereCollider.h"
#include "CapsuleCollider.h"
#include "MeshCollider.h"
#include "Volume.h"
#include "AudioSource.h"
#include "AudioListener.h"
#include "AnimationPlayer.h"
#include "SkinnedMeshRenderer.h"
#include "SkinnedMesh.h"
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

// Unity 캡슐: 반지름 0.5, 전체 높이 2 (원통 구간 1 + 양쪽 반구)
static void CreateCapsuleData(GeometryGenerator::MeshData& data, float radius, float cylinderHeight, uint32 slices, uint32 ringsPerHemisphere)
{
	const float pi = 3.14159265358979f;
	const uint32 ringCount = ringsPerHemisphere * 2 + 2;      // 위/아래 반구 (극점 포함)
	std::vector<std::pair<float, float>> rings;              // (phi, y 오프셋)
	for (uint32 i = 0; i <= ringsPerHemisphere; ++i)
		rings.push_back({ (pi * 0.5f) * i / ringsPerHemisphere, +cylinderHeight * 0.5f });
	for (uint32 i = 0; i <= ringsPerHemisphere; ++i)
		rings.push_back({ pi * 0.5f + (pi * 0.5f) * i / ringsPerHemisphere, -cylinderHeight * 0.5f });
	(void)ringCount;

	for (size_t i = 0; i < rings.size(); ++i)
	{
		float phi = rings[i].first;
		float v = (float)i / (float)(rings.size() - 1);
		for (uint32 j = 0; j <= slices; ++j)
		{
			float theta = 2.0f * pi * j / slices;
			XMFLOAT3 n(sinf(phi) * cosf(theta), cosf(phi), sinf(phi) * sinf(theta));
			XMFLOAT3 p(radius * n.x, radius * n.y + rings[i].second, radius * n.z);
			XMFLOAT3 t(-sinf(theta), 0.0f, cosf(theta));
			data.vertices.push_back(GeometryGenerator::Vertex(p, n, t, XMFLOAT2((float)j / slices, v)));
		}
	}

	const uint32 ringVertexCount = slices + 1;
	for (uint32 i = 0; i + 1 < rings.size(); ++i)
	{
		for (uint32 j = 0; j < slices; ++j)
		{
			data.indices.push_back(i * ringVertexCount + j);
			data.indices.push_back(i * ringVertexCount + j + 1);
			data.indices.push_back((i + 1) * ringVertexCount + j);
			data.indices.push_back((i + 1) * ringVertexCount + j);
			data.indices.push_back(i * ringVertexCount + j + 1);
			data.indices.push_back((i + 1) * ringVertexCount + j + 1);
		}
	}
}

// Unity 쿼드: XY 평면의 1x1 사각형
static void CreateQuadData(GeometryGenerator::MeshData& data)
{
	data.vertices.push_back(GeometryGenerator::Vertex(-0.5f, -0.5f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f));
	data.vertices.push_back(GeometryGenerator::Vertex(-0.5f, +0.5f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f));
	data.vertices.push_back(GeometryGenerator::Vertex(+0.5f, +0.5f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f));
	data.vertices.push_back(GeometryGenerator::Vertex(+0.5f, -0.5f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f));
	const uint32 idx[6] = { 0, 1, 2, 0, 2, 3 };
	data.indices.assign(idx, idx + 6);
}

static const wchar_t* BuiltinName(PrimitiveType type)
{
	switch (type)
	{
	case PrimitiveType::Cube: return L"Cube";
	case PrimitiveType::Sphere: return L"Sphere";
	case PrimitiveType::Capsule: return L"Capsule";
	case PrimitiveType::Cylinder: return L"Cylinder";
	case PrimitiveType::Plane: return L"Plane";
	case PrimitiveType::Quad: return L"Quad";
	}
	return L"";
}

std::wstring GameObjectFactory::GetBuiltinMeshPath(PrimitiveType type)
{
	return std::wstring(L"builtin:") + BuiltinName(type);
}

bool GameObjectFactory::IsBuiltinMeshPath(const std::wstring& path)
{
	return path.rfind(L"builtin:", 0) == 0;
}

std::shared_ptr<Mesh> GameObjectFactory::LoadBuiltinMesh(const std::wstring& path)
{
	if (!IsBuiltinMeshPath(path))
		return nullptr;
	std::wstring name = path.substr(8);
	for (int i = 0; i <= (int)PrimitiveType::Quad; ++i)
	{
		if (name == BuiltinName((PrimitiveType)i))
			return GetPrimitiveMesh((PrimitiveType)i);
	}
	return nullptr;
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
	case PrimitiveType::Capsule:
	{
		GeometryGenerator::MeshData data;
		CreateCapsuleData(data, 0.5f, 1.0f, 24, 8);
		mesh = BuildMeshFromGeometry(data, "Capsule");
		break;
	}
	case PrimitiveType::Cylinder:
	{
		GeometryGenerator::MeshData data;
		geoGen.CreateCylinder(0.5f, 0.5f, 2.0f, 24, 1, data);   // Unity 원통: 반지름 0.5, 높이 2
		mesh = BuildMeshFromGeometry(data, "Cylinder");
		break;
	}
	case PrimitiveType::Quad:
	{
		GeometryGenerator::MeshData data;
		CreateQuadData(data);
		mesh = BuildMeshFromGeometry(data, "Quad");
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

GameObject* GameObjectFactory::CreatePrimitive(PrimitiveType type, const std::string& name)
{
	GameObject* obj = new GameObject(name);
	// Unity 와 같은 구조: MeshFilter(메시) + MeshRenderer(재질). 내장 메시는 "builtin:" 경로로 씬에 저장된다.
	auto filter = obj->AddComponent<MeshFilter>();
	filter->SetMesh(GetPrimitiveMesh(type), GetBuiltinMeshPath(type), 0);
	auto mr = obj->AddComponent<MeshRenderer>();
	mr->SetBuiltinMesh(GetBuiltinMeshPath(type), GetPrimitiveMesh(type));
	return obj;
}

GameObject* GameObjectFactory::CreateCube(const std::string& name)
{
	GameObject* obj = CreatePrimitive(PrimitiveType::Cube, name);
	auto col = obj->AddComponent<BoxCollider>();
	col->SetSize(Vec3(1.0f, 1.0f, 1.0f));
	return obj;
}

GameObject* GameObjectFactory::CreateSphere(const std::string& name)
{
	GameObject* obj = CreatePrimitive(PrimitiveType::Sphere, name);
	auto col = obj->AddComponent<SphereCollider>();
	col->SetRadius(0.5f);
	return obj;
}

// Unity 와 같은 기본 콜라이더: Capsule/Cylinder = Capsule Collider, Plane/Quad = Mesh Collider
GameObject* GameObjectFactory::CreateCapsule(const std::string& name)
{
	GameObject* obj = CreatePrimitive(PrimitiveType::Capsule, name);
	obj->AddComponent<CapsuleCollider>();
	return obj;
}

GameObject* GameObjectFactory::CreateCylinder(const std::string& name)
{
	GameObject* obj = CreatePrimitive(PrimitiveType::Cylinder, name);
	obj->AddComponent<CapsuleCollider>();
	return obj;
}

GameObject* GameObjectFactory::CreatePlane(const std::string& name)
{
	GameObject* obj = CreatePrimitive(PrimitiveType::Plane, name);
	obj->AddComponent<MeshCollider>();
	return obj;
}

GameObject* GameObjectFactory::CreateQuad(const std::string& name)
{
	GameObject* obj = CreatePrimitive(PrimitiveType::Quad, name);
	obj->AddComponent<MeshCollider>();
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
	light->SetDirLight(dir);
	// Unity 새 Directional Light 와 같은 기본값: 회전 (50, -30, 0), Soft Shadows
	obj->GetTransform()->SetLocalEulerAngles(Vec3(50.0f, -30.0f, 0.0f));
	light->SetShadowType(2);
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
	obj->AddComponent<AudioListener>();   // Unity 처럼 카메라에 Audio Listener
	if (name == "Main Camera")
		obj->SetTag("MainCamera");
	return obj;
}

GameObject* GameObjectFactory::CreateAudioSource()
{
	GameObject* obj = new GameObject("Audio Source");
	obj->AddComponent<AudioSource>();
	return obj;
}

void GameObjectFactory::AddSkinnedChildren(GameObject* root, const std::string& modelPath)
{
	auto file = ResourceManager::GetI()->LoadMeshFile(modelPath);
	if (file != nullptr)
	{
		for (int i = 0; i < (int)file->SkinnedMeshs.size(); ++i)
		{
			const std::string childName = file->SkinnedMeshs[i]->Name.empty() ? "Mesh" + std::to_string(i) : file->SkinnedMeshs[i]->Name;
			GameObject* child = new GameObject(childName);
			child->AddComponent<SkinnedMeshRenderer>()->SetSkinnedMesh(string_to_wstring(modelPath), i);
			child->SetParentImmediate(root);
			child->GetTransform()->SetParent(root->GetComponent_SP<Transform>());
			root->SetChild(child);
			child->GetTransform()->UpdateTransform();
		}
	}
}

GameObject* GameObjectFactory::CreateCharacter(const std::string& name, const std::string& modelPath, const std::string& clipPath)
{
	GameObject* root = new GameObject(name);
	AddSkinnedChildren(root, modelPath);
	AnimationPlayer* animation = root->AddComponent<AnimationPlayer>();
	if (!clipPath.empty())
		animation->SetClip(clipPath, 0);
	// 에디터에서도 첫 프레임 포즈로 보이게 한다 (Play 전)
	animation->Sample();
	return root;
}

GameObject* GameObjectFactory::CreateAnimatedCharacter(const std::string& name, const std::string& modelPath, const std::string& controllerPath)
{
	GameObject* root = new GameObject(name);
	AddSkinnedChildren(root, modelPath);
	Animator* animator = root->AddComponent<Animator>();
	animator->SetController(controllerPath);
	// 에디터에서도 기본 상태의 첫 프레임 포즈로 보이게 한다 (Play 전)
	animator->Rebind();
	return root;
}

GameObject* GameObjectFactory::CreateVolume(VolumeShape shape)
{
	static const char* kNames[] = { "Global Volume", "Box Volume", "Sphere Volume" };
	GameObject* obj = new GameObject(kNames[(int)shape]);
	Volume* volume = obj->AddComponent<Volume>();
	if (shape == VolumeShape::Box)
	{
		volume->SetMode(Volume::Mode::Local);
		obj->AddComponent<BoxCollider>()->SetIsTrigger(true);
	}
	else if (shape == VolumeShape::Sphere)
	{
		volume->SetMode(Volume::Mode::Local);
		obj->AddComponent<SphereCollider>()->SetIsTrigger(true);
	}
	return obj;
}

GameObject* GameObjectFactory::CreateTerrain(const std::string& name)
{
	// Unity 기본값: 1000 x 600 x 1000, 높이맵 513
	std::string path = "Assets\\New Terrain.terraindata";
	for (int n = 1; std::filesystem::exists(PathManager::GetI()->GetMovePathW(string_to_wstring(path))); ++n)
		path = "Assets\\New Terrain " + std::to_string(n) + ".terraindata";
	auto data = TerrainData::Create(path);

	GameObject* obj = new GameObject(name);
	obj->AddComponent<Terrain>()->SetTerrainData(data);
	obj->AddComponent<TerrainCollider>();   // Terrain Data 가 비면 Terrain 의 데이터를 쓴다
	return obj;
}
