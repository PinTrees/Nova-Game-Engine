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

	// 에디터 시작 시 열 씬을 결정한다.
	//  1) 마지막으로 연 씬 파일이 있으면 로드
	//  2) 프로젝트(--project)인데 씬이 하나도 없으면 Assets/Scenes/SampleScene.scene 을 기본 씬으로 만들어 저장
	//  3) 그 외(엔진 샘플 프로젝트)는 기존처럼 저장하지 않는 기본 씬
	void LoadStartupScene();
};
