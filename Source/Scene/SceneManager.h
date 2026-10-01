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
	std::wstring m_PlayOriginalPath;   // Play 를 시작한 씬 (Play 중 다른 씬을 읽어도 Stop 하면 이 씬으로)

	// 저장 이후 변경 여부 (Unity 의 "SampleScene*" 표시). 씬 JSON 해시를 저장 시점과 비교한다.
	size_t m_SavedHash = 0;
	size_t m_CheckedHash = 0;
	double m_LastDirtyCheck = -1.0;
	bool m_Dirty = false;
	bool m_CloseWithoutPrompt = false;
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
	
	// Editor: 단축키 Ctrl+S / Ctrl+Shift+S / Ctrl+N / Ctrl+O / Ctrl+P (프레임 시작, 한글 입력 중에도 동작)
	void HandleSceneShortcuts();
	// File > New Scene / Ctrl+N: 저장 안 한 변경이 있으면 먼저 묻는다
	void NewSceneFromEditor();
	// Edit > Play / Stop, Ctrl+P: Play 이면 멈추고 아니면 시작 (스크립트 컴파일 오류가 있으면 시작하지 않는다)
	void TogglePlayFromEditor();
	// File > Open Scene / Ctrl+O / Project 창 더블클릭: 빈 경로면 파일 대화상자. 저장 안 한 변경이 있으면 먼저 묻는다
	void OpenSceneFromEditor(const std::wstring& relPath = L"");
	// 저장 안 한 변경이 있으면 Unity 처럼 "Save / Don't Save / Cancel" 을 물은 뒤 action (없으면 바로).
	// 같은 씬을 다시 여는 경우처럼 Don't Save 일 때 할 일이 다르면 onDiscard 로
	void RequestSceneChange(std::function<void()> action, std::function<void()> onDiscard = nullptr);
	void DrawScenePrompt();   // 에디터 UI 프레임 안에서 (확인 창)
	bool IsScenePromptOpen() const { return (bool)m_PromptAction; }
	void CancelScenePrompt() { m_PromptAction = nullptr; m_PromptDiscard = nullptr; }   // nova quit --force
	// nova quit --force: 저장 확인 없이 닫는다 (한 번도 저장하지 않은 Untitled 씬은 DiscardChanges 로 되돌릴 파일이 없다)
	void SetCloseWithoutPrompt() { m_CloseWithoutPrompt = true; }
	bool CloseWithoutPrompt() const { return m_CloseWithoutPrompt; }

	// 현재 씬 저장 (Ctrl+S / File > Save / Hierarchy 메뉴 공통). Play 모드에서는 저장하지 않는다 (Unity 와 동일).
	bool SaveCurrentScene(bool saveAs = false);
	// 마지막 저장 이후 바뀐 내용이 있는지 (0.25 초마다 다시 계산)
	bool IsCurrentSceneDirty();
	// 현재 상태를 "저장됨" 으로 표시 (씬을 열거나 만든 직후)
	void MarkCurrentSceneSaved();
	// 저장하지 않은 변경을 버리고 파일에서 다시 읽는다
	void DiscardChanges();
	// Undo/Redo: 현재 씬을 JSON 상태로 다시 만든다 (경로 유지, 선택/Hierarchy 펼침 상태는 fileID 로 이어 간다)
	void RestoreSceneState(const std::string& sceneJson);
	void HandlePlay();
	// Play 중 씬 바꾸기 (C# SceneManager.LoadScene): 이번 프레임 끝에 이전 씬을 내리고 새 씬을 읽어 Awake/Start
	void LoadSceneDuringPlay(const std::wstring& scenePath);
	void HandleStop();
	void CreateScene();

	// 에디터 시작 시 열 씬을 결정한다.
	//  1) 마지막으로 연 씬 파일이 있으면 로드
	//  2) 프로젝트(--project)인데 씬이 하나도 없으면 Assets/Scenes/SampleScene.scene 을 기본 씬으로 만들어 저장
	//  3) 그 외(엔진 샘플 프로젝트)는 기존처럼 저장하지 않는 기본 씬
	void LoadStartupScene();

private:
	std::function<void()> m_PromptAction, m_PromptDiscard;   // RequestSceneChange 가 묻는 중
	bool m_PromptOpened = false;
};
