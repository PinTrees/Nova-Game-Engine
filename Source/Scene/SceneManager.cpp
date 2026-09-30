#include "pch.h"
#include "SceneManager.h"
#include "Scene.h"
#include "LightManager.h"
#include "GameObjectFactory.h"


SINGLE_BODY(SceneManager)

SceneManager::SceneManager()
	: m_pCurrScene(nullptr)
{

}

SceneManager::~SceneManager()
{
	Safe_Delete_Map(m_Scenes); 
}

void SceneManager::Init()
{
}

void SceneManager::LoadScene(wstring scenePath)
{
	Scene* scene = nullptr;

	if (m_Scenes.find(scenePath) != m_Scenes.end())
	{
		scene = m_Scenes[scenePath];
		m_pCurrScene = scene;
	}
	else
	{
		scene = Scene::Load(scenePath);
		if (scene == nullptr)
		{
			CreateScene();
		}
		else
		{
			m_Scenes[scenePath] = scene;
			m_pCurrScene = scene;
		}
	}

	m_pCurrScene->Enter();
	DisplayManager::GetI()->Init(); 
}

void SceneManager::UpdateScene()
{
	if (m_pCurrScene == nullptr)
		return;

	m_pCurrScene->UpdateScene();
}

void SceneManager::RenderScene()
{
	if (m_pCurrScene == nullptr)
		return;

	m_pCurrScene->RenderScene();
}

void SceneManager::LastUpdate()
{
	for (auto& action : m_Editor_LastUpdateActions)
		action();
	m_Editor_LastUpdateActions.clear();
}

void SceneManager::HandleSaveScene()
{
	if (INPUT_KEY_HOLD(KEY::CTRL) && INPUT_KEY_HOLD(KEY::LSHIFT) && INPUT_KEY_DOWN(KEY::S))
	{
		if (m_pCurrScene == nullptr)
			return;

		Scene::SaveNewScene(m_pCurrScene);
	}
	else if (INPUT_KEY_HOLD(KEY::CTRL) && INPUT_KEY_DOWN(KEY::S))
	{
		if (m_pCurrScene == nullptr)
			return;

		if (m_pCurrScene->GetScenePath().empty())
			Scene::SaveNewScene(m_pCurrScene);
		else
			Scene::Save(m_pCurrScene);
	}
}

void SceneManager::HandlePlay()
{
	if (m_pCurrScene == nullptr) 
		return;

	json j = *m_pCurrScene;
	m_PlayModeSceneSnapshot = j.dump();
	m_pCurrScene->Enter();
}

void SceneManager::HandleStop()
{
	if (m_pCurrScene == nullptr)
		return;

	m_pCurrScene->Exit();

	if (!m_PlayModeSceneSnapshot.empty())
	{
		wstring scenePath = m_pCurrScene->GetScenePath();
		delete m_pCurrScene;

		m_pCurrScene = new Scene();
		m_pCurrScene->SetScenePath(scenePath);
		json j = json::parse(m_PlayModeSceneSnapshot);
		from_json(j, *m_pCurrScene);
		m_PlayModeSceneSnapshot.clear();

		if (!scenePath.empty())
			m_Scenes[scenePath] = m_pCurrScene;
	}
	else
	{
		wstring scenePath = m_pCurrScene->GetScenePath();
		delete m_pCurrScene;
		m_pCurrScene = nullptr;
		m_Scenes.erase(scenePath);

		if (!scenePath.empty())
			SceneManager::LoadScene(scenePath);
		else
			CreateScene();
	}

	DisplayManager::GetI()->Init();
}

void SceneManager::CreateScene()
{
	if (m_pCurrScene != nullptr)
	{
		m_pCurrScene->Exit();
		delete m_pCurrScene;
		m_pCurrScene = nullptr;
	}

	m_pCurrScene = new Scene();

	GameObject* camera = GameObjectFactory::CreateCamera("Main Camera");
	camera->GetTransform()->SetPosition(Vec3(0.0f, 2.0f, -10.0f));
	m_pCurrScene->AddRootGameObject(camera);

	GameObject* light = GameObjectFactory::CreateDirectionalLight("Directional Light");
	m_pCurrScene->AddRootGameObject(light);

	DisplayManager::GetI()->Init();
}
