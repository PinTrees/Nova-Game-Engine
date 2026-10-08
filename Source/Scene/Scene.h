#pragma once
#include "MemoryHeaps.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <unordered_set>

using json = nlohmann::json;

class GameObject;
class Component;

class NOVA_API Scene
{
private:
	wstring m_ScenePath;

	vector<GameObject*> m_ArrGameObjects[(UINT)32];		// 태그 기반 배열 - 충돌 처리 최적화
	
	vector<GameObject*> m_VecAllGameObjects;			// 미 구현
	vector<GameObject*> m_VecRootGameObjects;

	vector<GameObject*> m_CullingGameObjects;
	vector<GameObject*> m_CullingEditorGameObjects;
	uint64 m_Serial = 0;
	Memory::Heaps::SceneHeap* m_Heap = nullptr;
	vector<GameObject*> m_PendingDelete;   // DestroyGameObject 로 뺀 오브젝트 — 프레임 끝 FlushDestroyed 에서 delete
public:
	Scene();
	~Scene();
	// 만들 때마다 새 번호 — 지운 씬 자리에 새 씬이 같은 주소로 만들어져도 다른 씬으로 알아본다 (Undo 의 씬 감시)
	uint64 GetSerial() const { return m_Serial; }
	// 이 씬의 힙 (GameObject · 컴포넌트가 받는 곳 — 씬을 지우면 영역째 돌려준다, MemoryHeaps.h)
	Memory::Heaps::SceneHeap* Heap() const { return m_Heap; }

public:
	wstring GetScenePath() const { return m_ScenePath; }
	void SetScenePath(const wstring& path) { m_ScenePath = path; }
	// 파일 이름(확장자 제외). 아직 저장되지 않은 씬은 "Untitled"
	wstring GetName() const;

public:
	// Play 시작 · 씬 읽기: 모든 컴포넌트 Awake → 물리 바디 → afterAwake (C# sceneLoaded — Unity 처럼 Awake 뒤 Start 전) → Start
	//  started = 이미 Awake · Start 한 오브젝트 (이전 씬에서 넘어온 DontDestroyOnLoad) — 건너뛴다
	void Enter(const std::function<void()>& afterAwake = nullptr, const std::unordered_set<GameObject*>* started = nullptr);
	void Exit();

	void UpdateScene();
	void EditorUpdateScene();   // 편집 중 (Play 아님): 켜진 오브젝트 컴포넌트의 _Editor_Update
	void RenderScene();
	void RenderSceneShadow();
	void RenderSceneCover();   // 날씨 덮개 맵 (WeatherCover): 지붕 · 나무 · 바위 · 지형 (풀 · 캐릭터는 뺀다)
	void RenderSceneDeformers();   // 눈을 밟는 것 (WeatherCover 의 발자국): 스킨 메시 + Rigidbody · Character Controller 의 메시
	void RenderSceneShadowNormal();

	// Editor Only
	void _Editor_RenderScene();
	void _Editor_RenderSceneShadowNormal();

	void RenderSceneGizmos();
	void LastFramUpdate();

public:
	static Scene* Load(wstring scenePath);
	static bool Save(Scene* scene);          // 저장 성공 시 true
	static bool SaveNewScene(Scene* scene);  // 파일 대화상자에서 취소하면 false

public:
	void DestroyComponent(Component* component);  
	void DestroyGameObject(GameObject* gameobject);
	// DestroyGameObject 로 뺀 오브젝트를 실제로 delete (프레임 끝, 그 프레임의 렌더·UI·LastUpdate 가 끝난 뒤)
	void FlushDestroyed();
	 
	void AddRootGameObject(GameObject* gameObject);
	vector<GameObject*> GetRootGameObjects() { return m_VecRootGameObjects; }
	const vector<GameObject*>& RootGameObjects() const { return m_VecRootGameObjects; }   // 복사 없이
	// 루트 순서를 바꾼다 (Undo 부분 복원: 같은 루트 집합의 새 순서)
	void SetRootOrder(const vector<GameObject*>& order) { m_VecRootGameObjects = order; }
	// 저장되는 고유 ID 로 찾기 (없으면 nullptr)
	GameObject* FindByFileID(uint64 fileID) const;
	vector<GameObject*> GetAllGameObjects() const { return m_ArrGameObjects[0]; }
	// 복사하지 않는 전체 목록 (훑는 동안 씬을 바꾸지 않는 곳만 — 오브젝트를 만들거나 지우면 목록이 바뀐다)
	const vector<GameObject*>& GameObjectsView() const { return m_ArrGameObjects[0]; }

	// 오브젝트와 모든 자손을 전체 목록(렌더/업데이트/물리 대상)에 등록 (이미 있으면 건너뜀)
	void RegisterGameObjectTree(GameObject* gameObject);
	
	void SetCullingGameObjects(vector<GameObject*> culling) { m_CullingGameObjects = culling; }
	void SetCullingEditorGameObjects(vector<GameObject*> culling) { m_CullingEditorGameObjects = culling; }
	
	vector<GameObject*> GetCullingGameObjects() { return m_CullingGameObjects; }
	vector<GameObject*> GetCullingEditorGameObjects() { return m_CullingEditorGameObjects; }
	void RemoveRootGameObjects(GameObject* gameObject);
	// 지우지 않고 목록에서만 뺀다 (DontDestroyOnLoad 오브젝트를 새 씬으로 옮길 때): 루트 + 자손
	void DetachTree(GameObject* root);
	// 모든 루트를 지우지 않고 넘겨준다 (더해 읽은 씬의 오브젝트를 지금 씬으로 옮길 때) — 이 씬은 빈 채로 지워도 된다
	vector<GameObject*> ReleaseAll();

public:
	friend void from_json(const json& j, Scene& scene);
	friend void to_json(json& j, const Scene& scene);
};

