#pragma once
#include <string>
#include <map>
#include <vector>
#include <functional>

class Scene;
class GameObject;

class NOVA_API SceneManager
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
	// 충돌 복구: autosave JSON 을 원래 경로(빈 문자열 = 저장한 적 없는 씬)의 씬으로 연다 (변경됨 상태 — Ctrl+S 로 저장)
	bool OpenRecoveredScene(const std::string& sceneJson, const std::wstring& scenePath);
	// Undo/Redo: 현재 씬을 JSON 상태로 다시 만든다 (경로 유지, 선택/Hierarchy 펼침 상태는 fileID 로 이어 간다)
	void RestoreSceneState(const std::string& sceneJson);
	void HandlePlay();
	// Play 중 씬 바꾸기 (C# SceneManager.LoadScene): 이번 프레임 끝에 이전 씬을 내리고 새 씬을 읽어 Awake/Start
	void LoadSceneDuringPlay(const std::wstring& scenePath);
	void HandleStop();
	void CreateScene();

	// ---- 실행 중 여러 씬 (Unity 의 SceneManager — LoadSceneMode.Additive · LoadSceneAsync · UnloadSceneAsync · DontDestroyOnLoad)
	//  엔진의 씬 객체는 하나 (m_pCurrScene). 더해 읽은 씬의 오브젝트도 그 안에 넣고 루트마다 어느 씬 것인지 (핸들) 를 기억한다
	//  — 그리기 · 물리 · 스크립트는 그대로, C# 의 Scene (핸들 · 이름 · GetRootGameObjects) 과 내리기 (Unload) 만 핸들로 나눈다
	struct RuntimeScene { int Handle = 0; std::wstring Path; };
	struct SceneOp
	{
		float Progress = 0.0f; bool Done = false; bool AllowActivation = true; bool Failed = false; int Handle = 0;
		// 측정 (nova scenestream status): 바꿔 끼운 프레임의 단계별 ms, 기다리는 동안 가장 긴 프레임 · 바꾼 다음 프레임
		double ActivateMs = 0, BuildMs = 0, AssetMs = 0, TeardownMs = 0, EnterMs = 0, MaxWaitFrameMs = 0, FrameAfterMs = 0;
		uint32_t AssetLoads = 0, Objects = 0, WaitFrames = 0;
		// 미리 짓기 (비동기): 프레임마다 예산만큼 지은 시간 합 · 가장 긴 한 번 · 프레임 수 · 그동안 불러온 에셋
		double StageMs = 0, StageMaxMs = 0, StageAssetMs = 0;
		uint32_t StageFrames = 0, StageAssetLoads = 0, Roots = 0;
	};
	static constexpr int kDontDestroyOnLoadHandle = -1;
	// 씬 읽기 (Play 중, 프레임 끝에 바뀐다): additive = 지금 씬들에 더하기, async = 파일 읽기 · 해석을 작업 스레드에서
	//  (allowSceneActivation 이 false 면 0.9 에서 멈춘다 — Unity 와 같음). 반환 = 작업 번호 (C# AsyncOperation)
	int RequestSceneLoad(const std::wstring& scenePath, bool additive, bool async);
	int RequestSceneUnload(int handle);   // 0 = 그런 씬이 없거나 마지막 씬 (Unity 처럼 마지막 씬은 내리지 않는다)
	const SceneOp* GetSceneOp(int op) const;
	void SetSceneOpAllowActivation(int op, bool allow);
	std::vector<RuntimeScene> LoadedScenes() const;   // 읽은 순서 ([0] = Single 로 읽은 씬)
	std::wstring ScenePathOfHandle(int handle) const;   // 이번 Play 에서 쓴 핸들 (내린 씬도 — C# sceneUnloaded 의 이름)
	int ActiveSceneHandle() const;
	bool SetActiveSceneHandle(int handle);
	int SceneHandleOf(GameObject* go) const;            // 루트까지 올라가 그 루트의 씬 (DontDestroyOnLoad = -1)
	bool MoveRootToScene(GameObject* go, int handle);   // MoveGameObjectToScene · DontDestroyOnLoad (루트 오브젝트만)
	std::vector<GameObject*> RootsOfScene(int handle) const;
	void OnRuntimeRootCreated(GameObject* root);        // Instantiate · new GameObject 가 루트로 들어올 때: 활성 씬으로
	void UpdateSceneOps();                              // 프레임 끝 (LastUpdate 뒤): 작업 진행 · 바꾸기
	void BeginRuntimeScenes();                          // Play 시작: 지금 씬 = 첫 핸들
	void EndRuntimeScenes();                            // Play 끝: 작업 · 핸들 비우기
	void NotifyFirstSceneLoaded();                      // Play 시작 씬의 C# sceneLoaded (Awake 뒤 Start 전)
	friend struct SceneManagerRuntimeAccess;            // SceneManagerRuntime.cpp (씬 바꾸기)

	// 에디터 시작 시 열 씬을 결정한다.
	//  1) 마지막으로 연 씬 파일이 있으면 로드
	//  2) 프로젝트(--project)인데 씬이 하나도 없으면 Assets/Scenes/SampleScene.scene 을 기본 씬으로 만들어 저장
	//  3) 그 외(엔진 샘플 프로젝트)는 기존처럼 저장하지 않는 기본 씬
	void LoadStartupScene();

private:
	std::function<void()> m_PromptAction, m_PromptDiscard;   // RequestSceneChange 가 묻는 중
	bool m_PromptOpened = false;
};
