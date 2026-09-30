#include "pch.h"
#include "ComponentFactory.h"
#include "Camera.h"
#include "Light.h"
#include "MeshRenderer.h"
#include "MeshFilter.h"
#include "SkinnedMeshRenderer.h"
#include "RigidBody.h"
#include "BoxCollider.h"
#include "SphereCollider.h"
#include "CapsuleCollider.h"
#include "MeshCollider.h"
#include "AnimationPlayer.h"
#include "Animator.h"
#include "Terrain.h"
#include "TerrainCollider.h"
#include "Volume.h"

ComponentFactory::ComponentFactory()
{
	InitBuiltInComponents();
}

ComponentFactory& ComponentFactory::Instance()
{
	static ComponentFactory instance;
	return instance;
}

bool ComponentFactory::RegisterComponent(const std::string& type, CreateComponentFn fn)
{
	m_FactoryMap[type] = fn;
	return true;
}

std::shared_ptr<Component> ComponentFactory::CreateComponent(const std::string& type)
{
	auto it = m_FactoryMap.find(type);
	if (it != m_FactoryMap.end())
	{
		return it->second();
	}
	return nullptr;
}

std::vector<std::string> ComponentFactory::GetComponentTypes() const
{
	std::vector<std::string> types;
	for (const auto& pair : m_FactoryMap)
	{
		types.push_back(pair.first);
	}
	return types;
}

void ComponentFactory::InitBuiltInComponents()
{
	RegisterComponent("Camera", []() { return std::make_shared<Camera>(); });
	RegisterComponent("Light", []() { return std::make_shared<Light>(); });
	RegisterComponent("MeshFilter", []() { return std::make_shared<MeshFilter>(); });
	RegisterComponent("MeshRenderer", []() { return std::make_shared<MeshRenderer>(); });
	RegisterComponent("SkinnedMeshRenderer", []() { return std::make_shared<SkinnedMeshRenderer>(); });
	RegisterComponent("RigidBody", []() { return std::make_shared<RigidBody>(); });
	RegisterComponent("BoxCollider", []() { return std::make_shared<BoxCollider>(); });
	RegisterComponent("SphereCollider", []() { return std::make_shared<SphereCollider>(); });
	RegisterComponent("CapsuleCollider", []() { return std::make_shared<CapsuleCollider>(); });
	RegisterComponent("MeshCollider", []() { return std::make_shared<MeshCollider>(); });
	RegisterComponent("AnimationPlayer", []() { return std::make_shared<AnimationPlayer>(); });
	RegisterComponent("Animator", []() { return std::make_shared<Animator>(); });
	RegisterComponent("Terrain", []() { return std::make_shared<Terrain>(); });
	RegisterComponent("TerrainCollider", []() { return std::make_shared<TerrainCollider>(); });
	RegisterComponent("Volume", []() { return std::make_shared<Volume>(); });
}