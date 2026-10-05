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
	// 등록된 카메라만 본다 (예전: 부를 때마다 씬의 모든 오브젝트를 훑었다 — 렌더러 2000 개의 도시에서 프레임마다 여러 번, 안드로이드 약 1 ms).
	//  규칙은 그대로: 현재 씬 · 활성 계층 · 켜짐 · 그 디스플레이, 오브젝트의 첫 Camera, Priority 가 같으면 씬 순서가 앞인 것
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (scene == nullptr)
		return nullptr;
	const vector<GameObject*>& objects = scene->GameObjectsView();
	Camera* best = nullptr;
	size_t bestOrder = 0;
	for (Camera* camera : Camera::All())
	{
		GameObject* go = camera->GetGameObject();
		if (go == nullptr || !camera->IsEnabled() || camera->GetTargetDisplay() != display)
			continue;
		if (!GameObject::IsAlive(go))
			continue;   // 오브젝트는 지워졌는데 컴포넌트가 아직 살아 있다 (Play 를 멈추고 씬을 다시 읽는 중 등)
		if (best != nullptr && camera->GetPriority() < best->GetPriority())
			continue;
		bool active = true;
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
			if (!g->IsActive()) { active = false; break; }
		if (!active || go->GetComponent<Camera>() != camera)
			continue;   // 꺼진 계층 · 아직 붙지 않은 (AddComponent 대기) 또는 두 번째 Camera
		const size_t order = (size_t)(std::find(objects.begin(), objects.end(), go) - objects.begin());
		if (order == objects.size())
			continue;   // 다른 씬 (프리팹 편집 · 미리보기)
		if (best == nullptr || camera->GetPriority() > best->GetPriority() || order < bestOrder)
		{
			best = camera;
			bestOrder = order;
		}
	}
	return best ? best->GetGameObject()->GetComponent_SP<Camera>() : nullptr;
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
