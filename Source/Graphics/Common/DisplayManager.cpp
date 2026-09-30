#include "pch.h"
#include "DisplayManager.h"

SINGLE_BODY(DisplayManager)

DisplayManager::DisplayManager() 
{
}
DisplayManager::~DisplayManager()
{
}

void DisplayManager::Init()
{
	m_AllCameraComponents.clear(); 

	auto scene = SceneManager::GetI()->GetCurrentScene(); 

	for (auto gameobject : scene->GetAllGameObjects()) 
	{
		auto camera = gameobject->GetComponent_SP<Camera>(); 
		if (camera != nullptr) 
			m_AllCameraComponents.push_back(camera); 
	}


	// 기본 종횡비를 설정
	m_DefaultAspectRatios["1:1"] = 1.0f;
	m_DefaultAspectRatios["4:3"] = 4.0f / 3.0f;
	m_DefaultAspectRatios["16:9"] = 16.0f / 9.0f;
	m_DefaultAspectRatios["21:9"] = 21.0f / 9.0f;

	m_DefaultAspectRatios["3:4"] = 3.0f / 4.0f;
	m_DefaultAspectRatios["9:16"] = 9.0f / 16.0f;
	m_DefaultAspectRatios["9:21"] = 9.0f / 21.0f;
}

void DisplayManager::RegisterCameraComponent(const weak_ptr<Camera>& camera)
{
}

void DisplayManager::DeleteCameraComponent(const weak_ptr<Camera>& camera)
{

}

shared_ptr<Camera> DisplayManager::GetActiveCamera()
{
	if (m_AllCameraComponents.size() <= 0)
		return nullptr; 
	
	auto sharedCamera = m_AllCameraComponents[0].lock(); 

	if (!sharedCamera) {
		return nullptr;
	}

	return sharedCamera;
}
