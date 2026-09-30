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


	// �⺻ ��Ⱦ�� ����
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

shared_ptr<Camera> DisplayManager::GetCameraForDisplay(int display)
{
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (scene == nullptr)
		return nullptr;
	shared_ptr<Camera> best;
	for (GameObject* go : scene->GetAllGameObjects())
	{
		if (go == nullptr)
			continue;
		bool active = true;
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
			if (!g->IsActive()) { active = false; break; }
		if (!active)
			continue;
		auto camera = go->GetComponent_SP<Camera>();
		if (camera == nullptr || !camera->IsEnabled() || camera->GetTargetDisplay() != display)
			continue;
		if (best == nullptr || camera->GetPriority() > best->GetPriority())
			best = camera;
	}
	return best;
}

shared_ptr<Camera> DisplayManager::GetActiveCamera()
{
	// Game 뷰가 고른 디스플레이의 카메라 (그림자 범위 등도 이 카메라 기준)
	if (auto camera = GetCameraForDisplay(m_ActiveDisplay))
		return camera;

	if (m_AllCameraComponents.size() <= 0)
		return nullptr; 
	
	auto sharedCamera = m_AllCameraComponents[0].lock(); 

	if (!sharedCamera) {
		return nullptr;
	}

	return sharedCamera;
}
