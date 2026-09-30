#pragma once
#include <string>
#include <map>
#include <vector>
#include <functional>

class Scene;

class SceneManager
{
	SINGLE_HEADER(SceneManager)

private:
	Scene* m_pCurrScene;
	std::map<std::wstring, Scene*> m_Scenes;

	// Editor
	std::vector<std::function<void()>> m_Editor_LastUpdateActions;
	std::string m_PlayModeSceneSnapshot;

public:
	void Init();

	void LoadScene(std::wstring scenePath);

	void UpdateScene();
	void RenderScene();
	void LastUpdate(); 

	Scene* GetCurrentScene() { return m_pCurrScene; }

public:
	void AddLastUpdate(std::function<void()> action) { m_Editor_LastUpdateActions.push_back(action); } 
	
	// Editor
	void HandleSaveScene();
	void HandlePlay();
	void HandleStop();
	void CreateScene();
};
