#include "pch.h"
#include "MemoryHeaps.h"
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
#include "Terrain.h"
#include "TerrainCollider.h"
#include "Volume.h"
#include "AudioSource.h"
#include "AudioListener.h"
#include "CSharpScript.h"

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

void ComponentFactory::UnregisterComponent(const std::string& type)
{
	m_FactoryMap.erase(type);
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
	RegisterComponent("Camera", []() { return Memory::Heaps::MakeShared<Camera>(); });
	RegisterComponent("Light", []() { return Memory::Heaps::MakeShared<Light>(); });
	RegisterComponent("MeshFilter", []() { return Memory::Heaps::MakeShared<MeshFilter>(); });
	RegisterComponent("MeshRenderer", []() { return Memory::Heaps::MakeShared<MeshRenderer>(); });
	RegisterComponent("SkinnedMeshRenderer", []() { return Memory::Heaps::MakeShared<SkinnedMeshRenderer>(); });
	RegisterComponent("RigidBody", []() { return Memory::Heaps::MakeShared<RigidBody>(); });
	RegisterComponent("BoxCollider", []() { return Memory::Heaps::MakeShared<BoxCollider>(); });
	RegisterComponent("SphereCollider", []() { return Memory::Heaps::MakeShared<SphereCollider>(); });
	RegisterComponent("CapsuleCollider", []() { return Memory::Heaps::MakeShared<CapsuleCollider>(); });
	RegisterComponent("MeshCollider", []() { return Memory::Heaps::MakeShared<MeshCollider>(); });
	RegisterComponent("AnimationPlayer", []() { return Memory::Heaps::MakeShared<AnimationPlayer>(); });
	RegisterComponent("Terrain", []() { return Memory::Heaps::MakeShared<Terrain>(); });
	RegisterComponent("TerrainCollider", []() { return Memory::Heaps::MakeShared<TerrainCollider>(); });
	RegisterComponent("Volume", []() { return Memory::Heaps::MakeShared<Volume>(); });
	RegisterComponent("AudioSource", []() { return Memory::Heaps::MakeShared<AudioSource>(); });
	RegisterComponent("AudioListener", []() { return Memory::Heaps::MakeShared<AudioListener>(); });
	RegisterComponent("CSharpScript", []() { return Memory::Heaps::MakeShared<CSharpScript>(); });   // C# 스크립트 (씬에서 읽을 때)
}