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

	// 저장 이후 변경 여부 (Unity 의 "SampleScene*" 표시). 씬 JSON 해시를 저장 시점과 비교한다.
	size_t m_SavedHash = 0;
	size_t m_CheckedHash = 0;
	double m_LastDirtyCheck = -1.0;
	bool m_Dirty = false;
	size_t ComputeSceneHash() const;

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

	// 현재 씬 저장 (Ctrl+S / File > Save / Hierarchy 메뉴 공통). Play 모드에서는 저장하지 않는다 (Unity 와 동일).
	bool SaveCurrentScene(bool saveAs = false);
	// 마지막 저장 이후 바뀐 내용이 있는지 (0.25 초마다 다시 계산)
	bool IsCurrentSceneDirty();
	// 현재 상태를 "저장됨" 으로 표시 (씬을 열거나 만든 직후)
	void MarkCurrentSceneSaved();
	// 저장하지 않은 변경을 버리고 파일에서 다시 읽는다
	void DiscardChanges();
	void HandlePlay();
	void HandleStop();
	void CreateScene();

	// 에디터 시작 시 열 씬을 결정한다.
	//  1) 마지막으로 연 씬 파일이 있으면 로드
	//  2) 프로젝트(--project)인데 씬이 하나도 없으면 Assets/Scenes/SampleScene.scene 을 기본 씬으로 만들어 저장
	//  3) 그 외(엔진 샘플 프로젝트)는 기존처럼 저장하지 않는 기본 씬
	void LoadStartupScene();
};
