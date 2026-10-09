#include "pch.h"
#include "PackageManager.h"
#include "CharacterController.h"
#include "CSharpScript.h"
#include "GameObjectFactory.h"
#include "PrefabUtility.h"
#include "WheelCollider.h"
#include "Rigidbody.h"
#include "Ragdoll.h"
#include "UMaterial.h"
#include "GameObject.h"
#include "Transform.h"
#include "MeshRenderer.h"
#include "MeshFilter.h"
#include "SpriteRenderer.h"
#include "Camera.h"
#include "Light.h"
#include "BoxCollider.h"
#include "SphereCollider.h"
#include "CapsuleCollider.h"
#include "MeshCollider.h"
#include "Volume.h"
#include "AudioSource.h"
#include "AudioListener.h"
#include "ParticleSystem.h"
#include "VisualEffect.h"
#include "LineRenderer.h"
#include "Tree.h"
#include "TerrainStamp.h"
#include "TerrainBiome.h"
#include "WaterBody.h"
#include "TerrainSpline.h"
#include "Spline.h"
#include "PrototypeShape.h"
#include "MeshCollider.h"
#include "Rock.h"
#include "RockScatter.h"
#include "Terrain.h"
#include "TerrainData.h"
#include "AnimationPlayer.h"
#include "SkinnedMeshRenderer.h"
#include "VrmImport.h"
#include "FBXLoader.h"
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

GameObject* GameObjectFactory::CreateSprite(const std::string& name, const std::string& sprite)
{
	GameObject* obj = new GameObject(name);
	auto sr = obj->AddComponent<SpriteRenderer>();
	sr->SetSprite(sprite);
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

GameObject* GameObjectFactory::CreateParticleSystem(const std::string& name)
{
	GameObject* obj = new GameObject(name);
	obj->GetTransform()->SetLocalEulerAngles(Vec3(-90.0f, 0.0f, 0.0f));
	obj->AddComponent<ParticleSystem>();
	return obj;
}

// GameObject > Effects > Visual Effect (Unity 와 같은 이름 · 회전 없음)
GameObject* GameObjectFactory::CreateVisualEffect(const std::string& assetPath)
{
	GameObject* obj = new GameObject("Visual Effect");
	VisualEffect* vfx = obj->AddComponent<VisualEffect>();
	vfx->AssetPath = assetPath;
	return obj;
}

// GameObject > Effects > Line · Trail (Unity: 이름 "Line" · "Trail", 렌더러 하나)
GameObject* GameObjectFactory::CreateLineEffect(bool trail)
{
	GameObject* obj = new GameObject(trail ? "Trail" : "Line");
	if (trail)
		obj->AddComponent<TrailRenderer>();
	else
		obj->AddComponent<LineRenderer>();
	return obj;
}

GameObject* GameObjectFactory::CreateTree(const std::string& name)
{
	GameObject* obj = new GameObject(name);
	obj->AddComponent<Tree>()->ApplyPreset(0);
	return obj;
}

GameObject* GameObjectFactory::CreateTerrainStamp(int shape)
{
	using S = TerrainStamp::Shape;
	const S s = (S)std::clamp(shape, 0, (int)S::Count - 1);
	GameObject* obj = new GameObject(std::string(TerrainStamp::ShapeName(s)) + " Stamp");
	TerrainStamp* stamp = obj->AddComponent<TerrainStamp>();
	stamp->StampShape = s;
	// 모양마다 알맞은 기본 높이
	switch (s)
	{
	case S::Mountain: stamp->Height = 240.0f; break;
	case S::Hill: stamp->Height = 60.0f; stamp->Detail = 0.15f; break;
	case S::Crater: stamp->Height = 90.0f; break;
	case S::Volcano: stamp->Height = 260.0f; stamp->Detail = 0.25f; break;
	case S::Mesa: stamp->Height = 110.0f; stamp->BlendSize = 0.2f; break;
	case S::Ridge: stamp->Height = 140.0f; stamp->Roundness = 0.0f; break;
	case S::Canyon: stamp->Height = 90.0f; stamp->Roundness = 0.0f; stamp->BlendSize = 0.15f; break;
	case S::Dunes: stamp->Height = 30.0f; stamp->Detail = 0.1f; break;
	case S::Island: stamp->Height = 80.0f; break;
	default: stamp->Height = 150.0f; stamp->Detail = 0.0f; break;
	}
	Vec3 pos(0, 0, 0), scale(300, 1, 300);
	if (!Terrain::GetActiveTerrains().empty())
	{
		Terrain* t = Terrain::GetActiveTerrains()[0];
		if (auto data = t->GetTerrainData())
		{
			const Vec3 tp = t->GetPosition();
			pos = Vec3(tp.x + data->Size.x * 0.5f, tp.y, tp.z + data->Size.z * 0.5f);
			const float w = (std::min)(data->Size.x, data->Size.z) * 0.3f;
			scale = Vec3(s == S::Ridge || s == S::Canyon || s == S::Dunes ? w * 1.6f : w, 1.0f, w);
		}
	}
	obj->GetTransform()->SetPosition(pos);
	obj->GetTransform()->SetLocalScale(scale);
	return obj;
}

GameObject* GameObjectFactory::CreateTerrainBiome(const std::string& preset)
{
	GameObject* obj = new GameObject(preset.empty() ? std::string("Terrain Biome") : preset + " Biome");
	TerrainBiome* biome = obj->AddComponent<TerrainBiome>();
	if (!preset.empty())
		biome->Preset = preset;
	// 첫 지형의 가운데, 지형 너비의 45 %
	Vec3 pos(0, 0, 0), scale(400, 1, 400);
	if (!Terrain::GetActiveTerrains().empty())
	{
		Terrain* t = Terrain::GetActiveTerrains()[0];
		if (auto data = t->GetTerrainData())
		{
			const Vec3 tp = t->GetPosition();
			pos = Vec3(tp.x + data->Size.x * 0.5f, tp.y, tp.z + data->Size.z * 0.5f);
			const float w = (std::min)(data->Size.x, data->Size.z) * 0.45f;
			scale = Vec3(w, 1.0f, w);
		}
	}
	obj->GetTransform()->SetPosition(pos);
	obj->GetTransform()->SetLocalScale(scale);
	return obj;
}

GameObject* GameObjectFactory::CreateWaterBody(int type)
{
	using T = WaterBody::Type;
	const T t = (T)std::clamp(type, 0, (int)T::Count - 1);
	GameObject* obj = new GameObject(t == T::Ocean ? "Ocean" : t == T::Lake ? "Lake" : "River");
	WaterBody* water = obj->AddComponent<WaterBody>();
	water->BodyType = t;
	water->Profile = t == T::Ocean ? "Tropical Ocean" : t == T::Lake ? "Calm Lake" : "Mountain River";
	water->ResetShape();
	// 첫 지형 가운데 (바다는 지형 아래쪽 높이)
	Vec3 pos(0, 0, 0);
	if (!Terrain::GetActiveTerrains().empty())
	{
		Terrain* tr = Terrain::GetActiveTerrains()[0];
		if (auto data = tr->GetTerrainData())
		{
			const Vec3 tp = tr->GetPosition();
			pos = Vec3(tp.x + data->Size.x * 0.5f, tp.y, tp.z + data->Size.z * 0.5f);
			const float ground = tp.y + tr->SampleHeight(pos);
			if (t == T::Ocean)
			{
				// 지형 높이의 아래 20 % 쯤 (해안이 생기게)
				float lo = FLT_MAX, hi = -FLT_MAX;
				for (int z = 0; z <= 8; ++z)
					for (int x = 0; x <= 8; ++x)
					{
						const float h = tr->SampleHeight(Vec3(tp.x + data->Size.x * x / 8.0f, 0, tp.z + data->Size.z * z / 8.0f));
						lo = (std::min)(lo, h); hi = (std::max)(hi, h);
					}
				pos.y = tp.y + lo + (hi - lo) * 0.2f;
			}
			else if (t == T::Lake)
				pos.y = ground + 1.0f;
			else
				pos.y = ground + 12.0f;   // 강 점은 높이를 가진다 (Snap Points To Ground 로 맞춘다)
		}
	}
	obj->GetTransform()->SetPosition(pos);
	if (t == T::River)
		water->SnapToGround();
	return obj;
}

GameObject* GameObjectFactory::CreateRock(int preset)
{
	const int p = std::clamp(preset, 0, (int)RockDesc::PresetCount - 1);
	GameObject* obj = new GameObject(RockDesc::PresetName(p));
	Rock* rock = obj->AddComponent<Rock>();
	rock->Desc.Params.Seed = 1 + (int)(GetTickCount64() % 9973);   // 만들 때마다 다른 모양
	rock->ApplyPreset(p);
	return obj;
}

GameObject* GameObjectFactory::CreateRockScatter(int preset)
{
	const int p = std::clamp(preset, 0, (int)RockDesc::PresetCount - 1);
	GameObject* obj = new GameObject(std::string(RockDesc::PresetName(p)) + " Scatter");
	RockScatter* scatter = obj->AddComponent<RockScatter>();
	scatter->Desc.ApplyPreset(p);
	scatter->Seed = 1 + (int)(GetTickCount64() % 9973);
	// 첫 지형 가운데, 지형 너비의 30 %
	Vec3 pos(0, 0, 0), scale(100, 1, 100);
	if (!Terrain::GetActiveTerrains().empty())
	{
		Terrain* t = Terrain::GetActiveTerrains()[0];
		if (auto data = t->GetTerrainData())
		{
			const Vec3 tp = t->GetPosition();
			pos = Vec3(tp.x + data->Size.x * 0.5f, tp.y, tp.z + data->Size.z * 0.5f);
			const float w = (std::min)(data->Size.x, data->Size.z) * 0.3f;
			scale = Vec3(w, 1.0f, w);
		}
	}
	obj->GetTransform()->SetPosition(pos);
	obj->GetTransform()->SetLocalScale(scale);
	return obj;
}

GameObject* GameObjectFactory::CreatePrototypeShape(int shape)
{
	shape = std::clamp(shape, 0, (int)PrototypeShape::Shape::Count - 1);
	using S = PrototypeShape::Shape;
	GameObject* obj = new GameObject(std::string("Prototype ") + PrototypeShape::ShapeName((S)shape));
	obj->AddComponent<MeshFilter>();
	MeshRenderer* mr = obj->AddComponent<MeshRenderer>();
	// 갈래마다 Prototype 재질 (World Space UV — 늘려도 격자 1 m): 바닥 · 경사는 Gray, 벽은 Light, 원기둥은 LightGray
	const char* tone = (S)shape == S::Stairs || (S)shape == S::Ramp ? "Gray" : (S)shape == S::Cylinder || (S)shape == S::Cone ? "LightGray" : "Light";
	mr->SetMaterialPath(0, string_to_wstring(std::string("Resources\\Packages\\Prototype\\Materials\\Prototype_") + tone + ".mat"));
	PrototypeShape* ps = obj->AddComponent<PrototypeShape>();
	ps->Kind = (S)shape;
	switch ((S)shape)
	{
	case S::Stairs: ps->Size = Vec3(2, 2, 3); break;
	case S::Ramp: ps->Size = Vec3(2, 1, 4); break;
	case S::Cylinder: ps->Size = Vec3(2, 4, 2); break;
	case S::Cone: ps->Size = Vec3(3, 4, 3); break;
	case S::ArchWall: ps->Size = Vec3(4, 3.5f, 0.4f); break;
	case S::CurvedWall: ps->Size = Vec3(1, 6, 1); ps->Radius = 12.0f; ps->Angle = 90.0f; ps->Segments = 32; break;
	default: ps->Size = Vec3(4, 3, 4); break;
	}
	ps->Rebuild();
	obj->AddComponent<MeshCollider>();   // 캐릭터로 걸어 본다
	return obj;
}

GameObject* GameObjectFactory::CreateSpline(int preset)
{
	// 프로토타입 패키지 (Resources/Packages/Prototype) 의 프리팹을 곡선에 — 성벽 · 길은 휘고, 울타리 · 판자는 반복
	static const char* kNames[] = { "Spline", "Wall Spline", "Fence Spline", "Plank Path Spline", "Road Spline" };
	preset = std::clamp(preset, 0, 4);
	GameObject* obj = new GameObject(kNames[preset]);
	SplineContainer* spline = obj->AddComponent<SplineContainer>();
	if (preset == 0)
		return obj;
	SplineInstantiate* si = obj->AddComponent<SplineInstantiate>();
	const char* base = "Resources/Packages/Prototype/Prefabs/";
	SplineInstantiate::Item item;
	switch (preset)
	{
	case 1:   // 성벽: 총안 벽을 휘어 잇는다 (경사에서도 세로)
		item.Prefab = std::string(base) + "Castle/Castle_Wall_4m.prefab";
		si->Placement = SplineInstantiate::Method::Deform;
		si->KeepUpright = true;
		break;
	case 2:   // 울타리: 2 m 울타리를 그대로 반복
		item.Prefab = std::string(base) + "Structure/Fence_2m.prefab";
		si->Placement = SplineInstantiate::Method::Repeat;
		break;
	case 3:   // 판자 길: 판자를 가로로 (앞 축 Z), 경사를 따라 눕힌다
		item.Prefab = std::string(base) + "Props/Plank_2m.prefab";
		si->Placement = SplineInstantiate::Method::Repeat;
		si->ForwardAxis = SplineInstantiate::Axis::Z;
		si->Gap = 0.08f;
		si->KeepUpright = false;
		si->RandomYaw = 3.0f;
		break;
	default:  // 길: 길 판을 휘어 잇는다, 경사를 따라
		item.Prefab = std::string(base) + "Floor/Road_4m.prefab";
		si->Placement = SplineInstantiate::Method::Deform;
		si->KeepUpright = false;
		break;
	}
	si->Items.push_back(item);
	(void)spline;
	return obj;
}

GameObject* GameObjectFactory::CreateTerrainSpline(int mode)
{
	using M = TerrainSpline::Mode;
	const M m = (M)std::clamp(mode, 0, (int)M::Count - 1);
	GameObject* obj = new GameObject(std::string(TerrainSpline::ModeName(m)) + " Spline");
	TerrainSpline* spline = obj->AddComponent<TerrainSpline>();
	spline->SplineMode = m;
	spline->ResetShape();
	Vec3 pos(0, 0, 0);
	if (!Terrain::GetActiveTerrains().empty())
	{
		Terrain* t = Terrain::GetActiveTerrains()[0];
		if (auto data = t->GetTerrainData())
		{
			const Vec3 tp = t->GetPosition();
			pos = Vec3(tp.x + data->Size.x * 0.5f, tp.y, tp.z + data->Size.z * 0.5f);
			pos.y = tp.y + t->SampleHeight(pos);
		}
	}
	obj->GetTransform()->SetPosition(pos);
	spline->SnapToGround();
	return obj;
}

void GameObjectFactory::AddSkinnedChildren(GameObject* root, const std::string& modelPath)
{
	auto file = ResourceManager::GetI()->LoadMeshFile(modelPath);
	if (file != nullptr)
	{
		// VRM: 묻힌 그림 · 재질을 꺼내 (<파일>.Textures · <파일>.Materials) 재질 칸에 붙인다 (Unity 의 Extract Materials)
		std::vector<std::wstring> vrmMaterials;
		if (VrmImport::IsVrm(string_to_wstring(modelPath)))
		{
			vrmMaterials = VrmImport::ExtractMaterials(string_to_wstring(modelPath));
			// MToon 재질은 lilToon (Toon Shader 패키지) 으로 — 없으면 넣는다 (없어도 Unlit 으로 그려진다)
			if (!vrmMaterials.empty() && !PackageManager::IsInProject("com.nova.toon"))
			{
				std::string error;
				if (!PackageManager::Add("com.nova.toon", error))
					EditorLog::Write("Character", "could not add com.nova.toon: %s", error.c_str());
			}
		}
		else if (_wcsicmp(std::filesystem::path(string_to_wstring(modelPath)).extension().c_str(), L".fbx") == 0)
			vrmMaterials = FBXLoader::ExtractMaterials(string_to_wstring(modelPath));   // FBX 재질 색 · 그림 (프로젝트 Assets 의 모델만)
		for (int i = 0; i < (int)file->SkinnedMeshs.size(); ++i)
		{
			const std::string childName = file->SkinnedMeshs[i]->Name.empty() ? "Mesh" + std::to_string(i) : file->SkinnedMeshs[i]->Name;
			GameObject* child = new GameObject(childName);
			SkinnedMeshRenderer* smr = child->AddComponent<SkinnedMeshRenderer>();
			smr->SetSkinnedMesh(string_to_wstring(modelPath), i);
			for (int m = 0; m < (int)vrmMaterials.size(); ++m)
				for (const auto& subset : file->SkinnedMeshs[i]->Subsets)
					if ((int)subset.MaterialIndex == m) { smr->SetMaterialPath(m, vrmMaterials[m]); break; }
			child->SetParentImmediate(root);
			child->GetTransform()->SetParent(root->GetComponent_SP<Transform>());
			root->SetChild(child);
			child->GetTransform()->UpdateTransform();
		}
	}
}

void GameObjectFactory::AttachChild(GameObject* child, GameObject* parent)
{
	child->SetParentImmediate(parent);
	child->GetTransform()->SetParent(parent->GetComponent_SP<Transform>());
	parent->SetChild(child);
	child->GetTransform()->UpdateTransform();
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
	// Animator 는 Animation 패키지 (com.nova.animation) — 없으면 넣고, 이름으로 만든다.
	// 컨트롤러는 JSON 으로 (패키지가 다음 프레임에 첫 포즈를 계산해 편집 중에도 보인다)
	if (!PackageManager::IsInProject("com.nova.animation"))
	{
		std::string error;
		if (!PackageManager::Add("com.nova.animation", error))
			EditorLog::Write("Character", "could not add com.nova.animation: %s", error.c_str());
	}
	if (auto animator = ComponentFactory::Instance().CreateComponent("Animator"))
	{
		animator->fromJson({ { "type", "Animator" }, { "enabled", true }, { "controller", controllerPath } });
		root->AddComponent(animator);
	}
	// VRM 의 Spring Bone → Dynamic Bone (머리카락 · 옷 · 끈이 흔들린다)
	if (VrmImport::IsVrm(string_to_wstring(modelPath)))
	{
		// 표정 (BlendShape 묶음, 자동 깜빡임)
		const json expressions = VrmImport::ExpressionsJson(string_to_wstring(modelPath));
		if (!expressions.is_null())
			if (auto expr = ComponentFactory::Instance().CreateComponent("Expressions"))
			{
				expr->fromJson(expressions);
				root->AddComponent(expr);
			}
		const json springs = VrmImport::DynamicBoneJson(string_to_wstring(modelPath));
		if (!springs.is_null())
			if (auto dyn = ComponentFactory::Instance().CreateComponent("DynamicBone"))
			{
				dyn->fromJson(springs);
				root->AddComponent(dyn);
			}
	}
	return root;
}

GameObject* GameObjectFactory::CreateThirdPersonCharacter(const std::string& name, std::string* note)
{
	std::string msg;
	const char* kStarter = "com.nova.starter-assets";
	if (!PackageManager::IsInProject(kStarter))
	{
		std::string error;
		if (PackageManager::Add(kStarter, error))
			msg = "Added the packages Starter Assets and Cameras to the project. ";
		else
			msg = "Could not add " + std::string(kStarter) + ": " + error + ". ";
	}
	GameObject* root = CreateAnimatedCharacter(name);
	auto cc = std::make_shared<CharacterController>();
	cc->SetCenter(Vec3(0.0f, 0.9f, 0.0f));   // 기본 캐릭터: 원점 = 발, 키 약 1.8 m
	cc->SetRadius(0.3f);
	cc->SetHeight(1.8f);
	root->AddComponent(cc);
	root->AddComponent(CSharpScript::Create("StarterAssets.ThirdPersonController"));
	root->AddComponent(CSharpScript::Create("StarterAssets.VehicleEnterExit"));   // 장면에 차가 있으면 E 로 타고 내린다

	// Main Camera 에 Follow Camera (패키지 컴포넌트라 이름으로 만들고 JSON 으로 값을 넣는다)
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	GameObject* cam = nullptr;
	if (scene)
		for (GameObject* g : scene->GetAllGameObjects())
			if (g->GetComponent<Camera>() && (g->GetTag() == "MainCamera" || cam == nullptr))
				cam = g;
	if (cam)
	{
		std::shared_ptr<Component> follow;
		for (const auto& c : cam->GetComponents())
			if (c && c->GetType() == "FollowCamera")
				follow = c;
		if (!follow)
		{
			follow = ComponentFactory::Instance().CreateComponent("FollowCamera");
			if (follow)
				cam->AddComponent(follow);
		}
		if (follow)
		{
			json j = follow->toJson();
			j["target"] = root->GetFileID();
			j["distance"] = 4.0f;
			j["height"] = 1.8f;
			j["lookAtHeight"] = 1.3f;
			follow->fromJson(j);
			msg += "Main Camera follows '" + name + "'. ";
		}
		else
			msg += "Follow Camera is not available (Cameras package not loaded). ";
	}
	else
		msg += "No camera in the scene to follow the character. ";
	msg += "Press Play: WASD / arrows move, Shift sprints, Space jumps, right mouse drag turns the camera.";
	if (note)
		*note = msg;
	return root;
}

// ---------------------------------------------------------------- Starter Assets: 차 · 래그돌 표적
namespace
{
	// Starter Assets 패키지 (C# 스크립트) 가 프로젝트에 없으면 넣는다 — 안내 문구
	std::string EnsureStarterAssets()
	{
		const char* kStarter = "com.nova.starter-assets";
		if (PackageManager::IsInProject(kStarter))
			return "";
		std::string error;
		if (PackageManager::Add(kStarter, error))
			return "Added the packages Starter Assets and Cameras to the project. ";
		return "Could not add " + std::string(kStarter) + ": " + error + ". ";
	}

	// 장면의 Main Camera (없으면 아무 카메라)
	GameObject* MainCameraObject()
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		GameObject* cam = nullptr;
		if (scene)
			for (GameObject* g : scene->GetAllGameObjects())
				if (g->GetComponent<Camera>() && (g->GetTag() == "MainCamera" || cam == nullptr))
					cam = g;
		return cam;
	}

	// Main Camera 의 Follow Camera 가 target 을 따라가게 (패키지 컴포넌트라 이름으로 만들고 JSON 으로 값을 넣는다)
	void FollowWithMainCamera(GameObject* target, float distance, float height, float lookAtHeight, bool followRotation, std::string& msg)
	{
		GameObject* cam = MainCameraObject();
		if (cam == nullptr)
		{
			msg += "No camera in the scene to follow '" + target->GetName() + "'. ";
			return;
		}
		std::shared_ptr<Component> follow;
		for (const auto& c : cam->GetComponents())
			if (c && c->GetType() == "FollowCamera")
				follow = c;
		if (!follow)
		{
			follow = ComponentFactory::Instance().CreateComponent("FollowCamera");
			if (follow)
				cam->AddComponent(follow);
		}
		if (!follow)
		{
			msg += "Follow Camera is not available (Cameras package not loaded). ";
			return;
		}
		json j = follow->toJson();
		j["target"] = target->GetFileID();
		j["distance"] = distance;
		j["height"] = height;
		j["lookAtHeight"] = lookAtHeight;
		j["followTargetRotation"] = followRotation;
		follow->fromJson(j);
		msg += "Main Camera follows '" + target->GetName() + "'. ";
	}

	// 프로젝트의 Assets/StarterAssets/Materials 에 색 재질 (없으면 만든다 — Unity 가 Starter Assets 를 Assets 로 가져오는 것처럼). 프로젝트 기준 경로
	std::wstring StarterMaterial(const char* name, XMFLOAT4 color, float smoothness, float metallic, XMFLOAT4 emission = XMFLOAT4(0, 0, 0, 0))
	{
		const std::wstring rel = L"Assets\\StarterAssets\\Materials\\" + string_to_wstring(name) + L".mat";
		const std::filesystem::path full = PathManager::GetI()->GetMovePathW(rel);
		std::error_code ec;
		if (!std::filesystem::exists(full, ec))
		{
			std::filesystem::create_directories(full.parent_path(), ec);
			const std::string made = UMaterial::Create(wstring_to_string(full.parent_path().wstring()));   // "New Material.mat" → 이름 바꾸기
			std::filesystem::rename(PathManager::GetI()->GetMovePathW(string_to_wstring(made)), full, ec);
			std::unique_ptr<UMaterial> m(UMaterial::Load(wstring_to_string(rel)));
			if (m)
			{
				m->SetColorProperty("_BaseColor", color);
				m->SetFloatProperty("_Smoothness", smoothness);
				m->SetFloatProperty("_Metallic", metallic);
				if (emission.x > 0.0f || emission.y > 0.0f || emission.z > 0.0f)
					m->SetColorProperty("_EmissionColor", emission);
				UMaterial::Save(m.get());
			}
		}
		return rel;
	}

	GameObject* Part(PrimitiveType type, const char* name, GameObject* parent, Vec3 position, Vec3 scale, const std::wstring& material,
		bool keepCollider, Vec3 euler = Vec3::Zero)
	{
		GameObject* g = nullptr;
		switch (type)
		{
		case PrimitiveType::Cube: g = keepCollider ? GameObjectFactory::CreateCube(name) : GameObjectFactory::CreatePrimitive(type, name); break;
		default: g = GameObjectFactory::CreatePrimitive(type, name); break;
		}
		GameObjectFactory::AttachChild(g, parent);
		g->GetTransform()->SetLocalPosition(position);
		g->GetTransform()->SetLocalEulerAngles(euler);
		g->GetTransform()->SetLocalScale(scale);
		if (MeshRenderer* mr = g->GetComponent<MeshRenderer>())
			mr->SetMaterialPath(0, material);
		return g;
	}

	// 차 (1200 kg): 차체 · 지붕 (상자 콜라이더 — 합쳐서 하나의 Rigidbody), 범퍼 · 전조등, 바퀴 자리마다 Wheel Collider + 그림 바퀴 ("<이름> Mesh", 콜라이더 없음)
	GameObject* BuildCar(const std::string& name)
	{
		const std::wstring paint = StarterMaterial("CarPaint", XMFLOAT4(0.80f, 0.10f, 0.08f, 1), 0.85f, 0.35f);
		const std::wstring trim = StarterMaterial("CarTrim", XMFLOAT4(0.10f, 0.10f, 0.11f, 1), 0.45f, 0.0f);
		const std::wstring glass = StarterMaterial("CarGlass", XMFLOAT4(0.08f, 0.11f, 0.15f, 1), 0.95f, 0.1f);
		const std::wstring tire = StarterMaterial("CarTire", XMFLOAT4(0.05f, 0.05f, 0.05f, 1), 0.2f, 0.0f);
		const std::wstring rim = StarterMaterial("CarRim", XMFLOAT4(0.75f, 0.76f, 0.78f, 1), 0.8f, 0.9f);
		const std::wstring lamp = StarterMaterial("CarLamp", XMFLOAT4(1.0f, 0.95f, 0.8f, 1), 0.9f, 0.0f, XMFLOAT4(2.0f, 1.9f, 1.6f, 1));
		const std::wstring tail = StarterMaterial("CarTailLamp", XMFLOAT4(0.6f, 0.02f, 0.02f, 1), 0.9f, 0.0f, XMFLOAT4(1.2f, 0.05f, 0.05f, 1));

		using PT = PrimitiveType;
		GameObject* car = new GameObject(name);
		auto rb = car->AddComponent<RigidBody>();
		rb->SetMass(1200.0f);
		rb->SetLinearDamping(0.02f);
		rb->SetAngularDamping(0.3f);
		rb->SetInterpolation(RigidBody::Interpolation::Interpolate);
		Part(PT::Cube, "Body", car, Vec3(0, 0.55f, 0), Vec3(1.8f, 0.5f, 4.2f), paint, true);
		Part(PT::Cube, "Cabin", car, Vec3(0, 1.02f, -0.35f), Vec3(1.6f, 0.46f, 2.0f), glass, true);
		Part(PT::Cube, "Roof", car, Vec3(0, 1.26f, -0.4f), Vec3(1.5f, 0.04f, 1.7f), paint, false);
		Part(PT::Cube, "Front Bumper", car, Vec3(0, 0.38f, 2.12f), Vec3(1.94f, 0.22f, 0.14f), trim, false);
		Part(PT::Cube, "Rear Bumper", car, Vec3(0, 0.38f, -2.12f), Vec3(1.94f, 0.22f, 0.14f), trim, false);
		Part(PT::Cube, "Headlight L", car, Vec3(-0.62f, 0.62f, 2.1f), Vec3(0.38f, 0.14f, 0.04f), lamp, false);
		Part(PT::Cube, "Headlight R", car, Vec3(0.62f, 0.62f, 2.1f), Vec3(0.38f, 0.14f, 0.04f), lamp, false);
		Part(PT::Cube, "Taillight L", car, Vec3(-0.66f, 0.64f, -2.1f), Vec3(0.34f, 0.12f, 0.04f), tail, false);
		Part(PT::Cube, "Taillight R", car, Vec3(0.66f, 0.64f, -2.1f), Vec3(0.34f, 0.12f, 0.04f), tail, false);

		struct W { const char* name; float x, z; };
		const W wheels[] = { { "Wheel FL", -0.9f, 1.32f }, { "Wheel FR", 0.9f, 1.32f }, { "Wheel RL", -0.9f, -1.32f }, { "Wheel RR", 0.9f, -1.32f } };
		const float radius = 0.36f;
		for (const W& w : wheels)
		{
			GameObject* wheel = new GameObject(w.name);
			GameObjectFactory::AttachChild(wheel, car);
			wheel->GetTransform()->SetLocalPosition(Vec3(w.x, 0.42f, w.z));
			auto wc = wheel->AddComponent<WheelCollider>();
			wc->Radius = radius;
			wc->SuspensionDistance = 0.22f;
			wc->SuspensionSpring = { 40000.0f, 4500.0f, 0.5f };
			wc->Mass = 20.0f;
			// 그림 바퀴: 차의 자식 (CarController 가 GetWorldPose 로 맞춘다) — 원기둥 (Y 축) 을 눕혀 X 축으로
			GameObject* pivot = new GameObject(std::string(w.name) + " Mesh");
			GameObjectFactory::AttachChild(pivot, car);
			pivot->GetTransform()->SetLocalPosition(Vec3(w.x, 0.42f - 0.22f * 0.5f, w.z));
			const float side = w.x < 0.0f ? -1.0f : 1.0f;
			Part(PT::Cylinder, "Tire", pivot, Vec3::Zero, Vec3(radius * 2.0f, 0.13f, radius * 2.0f), tire, false, Vec3(0, 0, 90));   // 폭 0.26 (원기둥 높이 2 × 0.13)
			Part(PT::Cylinder, "Rim", pivot, Vec3(side * 0.03f, 0, 0), Vec3(radius * 1.25f, 0.12f, radius * 1.25f), rim, false, Vec3(0, 0, 90));
		}
		car->AddComponent(CSharpScript::Create("StarterAssets.CarController"));
		return car;
	}
}

GameObject* GameObjectFactory::CreateCar(const std::string& name, std::string* note)
{
	std::string msg = EnsureStarterAssets();
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (scene == nullptr)
		return nullptr;
	// 프리팹 (Assets/StarterAssets/Car.prefab): 처음이면 만들어 저장, 다음부터는 그 프리팹의 인스턴스 (고친 프리팹이 모든 차에)
	const std::string prefab = "Assets\\StarterAssets\\Car.prefab";
	std::error_code ec;
	GameObject* car = nullptr;
	if (std::filesystem::exists(PathManager::GetI()->GetMovePathW(string_to_wstring(prefab)), ec))
		car = PrefabUtility::InstantiatePrefab(prefab, scene);
	if (car == nullptr)
	{
		car = BuildCar("Car");   // 에셋의 이름 = Car (인스턴스 이름은 아래에서)
		scene->AddRootGameObject(car);
		if (PrefabUtility::SaveAsPrefabAssetAndConnect(car, prefab))
			msg += "Saved the prefab " + prefab + ". ";
	}
	car->SetName(name);
	FollowWithMainCamera(car, 7.0f, 2.6f, 0.9f, true, msg);
	msg += "Press Play: W / S drive and brake (S again reverses), A / D steer, Space handbrake, R flips the car back.";
	if (note)
		*note = msg;
	return car;
}

GameObject* GameObjectFactory::CreateRagdollTarget(const std::string& name, std::string* note)
{
	std::string msg = EnsureStarterAssets();
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (scene == nullptr)
		return nullptr;
	// 기본 캐릭터 (애니메이션) + Ragdoll Wizard (꺼짐 = 서서 애니메이션을 따라감) + RagdollTarget (맞으면 켠다)
	GameObject* root = CreateAnimatedCharacter(name);
	scene->AddRootGameObject(root);   // Wizard 는 장면 안의 자세로 바디를 만든다
	std::string error;
	if (Ragdoll* ragdoll = Ragdoll::Build(root, 20.0f, error))
		ragdoll->Active = false;
	else
		msg += "Ragdoll: " + error + ". ";
	root->AddComponent(CSharpScript::Create("StarterAssets.RagdollTarget"));
	// Main Camera 에 RagdollShooter (클릭 = 쏘기) — 이미 있으면 그대로
	if (GameObject* cam = MainCameraObject())
	{
		bool has = false;
		for (const auto& c : cam->GetComponents())
			if (auto s = std::dynamic_pointer_cast<CSharpScript>(c); s && s->GetClassName() == "StarterAssets.RagdollShooter")
				has = true;
		if (!has)
			cam->AddComponent(CSharpScript::Create("StarterAssets.RagdollShooter"));
		msg += "Press Play and click '" + name + "' in the Game view: it falls as a ragdoll where it was hit. ";
	}
	else
		msg += "No camera in the scene for the Ragdoll Shooter. ";
	if (note)
		*note = msg;
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
