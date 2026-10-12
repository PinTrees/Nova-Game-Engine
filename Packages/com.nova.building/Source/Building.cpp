#include "pch.h"
#include "Building.h"
#include "BuildingGenerator.h"
#include "BuildingTextures.h"
#include "GameObject.h"
#include "MeshFilter.h"
#include "MeshRenderer.h"
#include "Mesh.h"
#include "UMaterial.h"
#include "UnityGUI.h"
#include "EditorLog.h"
#include "ComponentFactory.h"
#include <chrono>

std::function<void(Building*)> Building::s_OpenGenerator;

Building::Building()
{
	m_InspectorTitleName = "Building";
}

Building::~Building() {}

GameObject* Building::CreateObject(const BuildingDesc& d, const std::string& name)
{
	GameObject* obj = new GameObject(name);
	obj->AddComponent<MeshFilter>();
	obj->AddComponent<MeshRenderer>();
	Building* b = obj->AddComponent<Building>();
	b->SetDesc(d);
	b->Rebuild();
	obj->AddComponent(ComponentFactory::Instance().CreateComponent("MeshCollider"));
	return obj;
}

void Building::SetDesc(const BuildingDesc& d)
{
	Desc = d;
	++Revision;
	MarkPhysicsDirty();
}

void Building::EnsureBuilt()
{
	if (m_BuiltRevision == Revision || m_pGameObject == nullptr)
		return;
	Rebuild();
}

std::shared_ptr<Mesh> Building::MakeMesh(const BuildingGenerator::Output& out, const std::string& name)
{
	auto mesh = std::make_shared<Mesh>();
	mesh->Name = name;
	std::vector<uint32_t> remap;
	for (int slot = 0; slot < BuildingGenerator::SlotCount; ++slot)
	{
		const BuildingGenerator::Part& part = out.Parts[slot];
		if (part.I.empty())
			continue;
		remap.assign(part.V.size(), UINT32_MAX);
		std::vector<uint32_t> touched;
		size_t tri = 0;
		const size_t triCount = part.I.size() / 3;
		// 서브셋 하나 = 정점 65535 개까지 (16 비트 인덱스 — VertexStart 가 기준 정점)
		while (tri < triCount)
		{
			MeshGeometry::Subset s;
			s.Id = (uint32)mesh->Subsets.size();
			s.VertexStart = (uint32)mesh->Vertices.size();
			s.FaceStart = (uint32)(mesh->Indices.size() / 3);
			s.MaterialIndex = (uint32)slot;
			uint32_t local = 0;
			for (; tri < triCount; ++tri)
			{
				const uint32_t* t = &part.I[tri * 3];
				int fresh = 0;
				for (int k = 0; k < 3; ++k)
					fresh += remap[t[k]] == UINT32_MAX;
				if (local + fresh > 65535)
					break;
				for (int k = 0; k < 3; ++k)
				{
					uint32_t& r = remap[t[k]];
					if (r == UINT32_MAX)
					{
						r = local++;
						touched.push_back(t[k]);
						mesh->Vertices.push_back(part.V[t[k]]);
					}
					mesh->Indices.push_back((USHORT)r);
				}
			}
			s.VertexCount = local;
			s.FaceCount = (uint32)(mesh->Indices.size() / 3) - s.FaceStart;
			mesh->Subsets.push_back(s);
			for (uint32_t v : touched)
				remap[v] = UINT32_MAX;
			touched.clear();
		}
	}
	if (mesh->Vertices.empty())
		return nullptr;
	for (size_t i = 0; i < mesh->Subsets.size(); ++i)
	{
		Material m;
		m.Ambient = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
		m.Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
		m.Specular = XMFLOAT4(0.2f, 0.2f, 0.2f, 16.0f);
		mesh->Mat.push_back(m);
	}
	mesh->Setup();
	return mesh;
}

std::vector<std::shared_ptr<UMaterial>> Building::MakeMaterials(const BuildingDesc& d)
{
	std::vector<std::shared_ptr<UMaterial>> mats(BuildingGenerator::SlotCount);
	for (int slot = 0; slot < BuildingGenerator::SlotCount; ++slot)
		mats[slot] = BuildingTextures::SlotMaterial(d, slot);   // 텍스처 팩 사진 재질 (없으면 식으로 굽는 재질)
	return mats;
}

void Building::Rebuild()
{
	m_BuiltRevision = Revision;
	if (m_pGameObject == nullptr)
		return;
	const auto t0 = std::chrono::steady_clock::now();
	MeshFilter* mf = m_pGameObject->GetComponent<MeshFilter>();
	if (mf == nullptr)
		mf = m_pGameObject->AddComponent<MeshFilter>();
	BuildingGenerator::Output out;
	BuildingGenerator::Generate(Desc, out);
	int count = 0;
	const char* const* names = BuildingDesc::StyleNames(count);
	const std::string style = Desc.Style >= 0 && Desc.Style < count ? names[Desc.Style] : "Custom";
	std::shared_ptr<Mesh> mesh = MakeMesh(out, "Building " + style);
	if (!mesh)
	{
		EditorLog::Write("Building", "'%s': nothing to build", m_pGameObject->GetName().c_str());
		return;
	}
	mf->SetMesh(mesh, L"", 0);
	MeshRenderer* mr = m_pGameObject->GetComponent<MeshRenderer>();
	if (mr == nullptr)
		mr = m_pGameObject->AddComponent<MeshRenderer>();
	const auto mats = MakeMaterials(Desc);
	for (int i = 0; i < (int)mats.size(); ++i)
		mr->SetMaterialAt(i, mats[i], L"");
	MarkPhysicsDirty();
	m_Stats = Stats();
	m_Stats.Vertices = (int)mesh->Vertices.size();
	m_Stats.Triangles = (int)(mesh->Indices.size() / 3);
	m_Stats.Subsets = (int)mesh->Subsets.size();
	m_Stats.Windows = out.Windows;
	m_Stats.Doors = out.Doors;
	m_Stats.Wings = out.Wings;
	m_Stats.Towers = out.Towers;
	m_Stats.Size = XMFLOAT3(out.Max.x - out.Min.x, out.Max.y - out.Min.y, out.Max.z - out.Min.z);
	m_Stats.Ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
	EditorLog::Write("Building", "'%s' (%s): %d vertices, %d triangles, %d windows, %.1f x %.1f x %.1f m, %.1f ms", m_pGameObject->GetName().c_str(), style.c_str(),
		m_Stats.Vertices, m_Stats.Triangles, m_Stats.Windows, m_Stats.Size.x, m_Stats.Size.y, m_Stats.Size.z, m_Stats.Ms);
}

void Building::OnInspectorGUI()
{
	using namespace UnityGUI;
	bool changed = false;
	int count = 0;
	const char* const* names = BuildingDesc::StyleNames(count);
	int style = Desc.Style;
	if (Dropdown("Style", &style, names, count))
	{
		Desc.ApplyStyle(style);
		changed = true;
	}
	HelpBox(BuildingDesc::StyleDescription(Desc.Style), false);
	if (Int("Seed", &Desc.Seed))
		changed = true;
	if (CenterButton("Randomize (same style)"))
	{
		Desc.Randomize(Desc.Seed + 1);
		changed = true;
	}
	if (Foldout("Size"))
	{
		changed |= Slider("Width", &Desc.Width, 3.0f, 60.0f, 1);
		changed |= Slider("Depth", &Desc.Depth, 3.0f, 60.0f, 1);
		changed |= Int("Floors", &Desc.Floors, 1);
		changed |= Slider("Floor Height", &Desc.FloorHeight, 2.2f, 8.0f, 1);
		static const char* kRoofs[] = { "Gable", "Hip", "Mansard", "Gambrel", "Flat", "Shed", "Pyramid", "Cone", "Dome", "Onion", "Tiered" };
		int roof = (int)Desc.RoofStyle;
		if (Dropdown("Roof", &roof, kRoofs, (int)BuildingDesc::Roof::Count, 1)) { Desc.RoofStyle = (BuildingDesc::Roof)roof; changed = true; }
		changed |= Slider("Roof Pitch", &Desc.RoofPitch, 5.0f, 75.0f, 1);
	}
	if (CenterButton("Open Building Generator") && s_OpenGenerator)
		s_OpenGenerator(this);
	char text[128];
	snprintf(text, sizeof(text), "%d vertices, %d triangles, %d windows, %d doors", m_Stats.Vertices, m_Stats.Triangles, m_Stats.Windows, m_Stats.Doors);
	ValueLabel("Mesh", text);
	snprintf(text, sizeof(text), "%.1f x %.1f x %.1f m", m_Stats.Size.x, m_Stats.Size.y, m_Stats.Size.z);
	ValueLabel("Bounds", text);
	if (changed)
	{
		Desc.Floors = std::clamp(Desc.Floors, 1, 12);
		++Revision;
		MarkPhysicsDirty();
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(Building)
{
	json j;
	SERIALIZE_TYPE(j, Building);
	j["enabled"] = m_Enabled;
	j["desc"] = Desc.ToJson();
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Building)
{
	MarkPhysicsDirty();
	m_Enabled = j.value("enabled", true);
	if (j.contains("desc") && j["desc"].is_object())
		Desc.FromJson(j["desc"]);
	++Revision;
}
