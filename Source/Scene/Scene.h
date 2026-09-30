#pragma once
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class GameObject;
class Component;

class Scene
{
private:
	wstring m_ScenePath;

	vector<GameObject*> m_ArrGameObjects[(UINT)32];		// 태그 기반 배열 - 충돌 처리 최적화
	
	vector<GameObject*> m_VecAllGameObjects;			// 미 구현
	vector<GameObject*> m_VecRootGameObjects;

	vector<GameObject*> m_CullingGameObjects;
	vector<GameObject*> m_CullingEditorGameObjects;
public:
	Scene();
	~Scene();

public:
	wstring GetScenePath() const { return m_ScenePath; }
	void SetScenePath(const wstring& path) { m_ScenePath = path; }
	// 파일 이름(확장자 제외). 아직 저장되지 않은 씬은 "Untitled"
	wstring GetName() const;

public:
	void Enter();
	void Exit();

	void UpdateScene();
	void RenderScene();
	void RenderSceneShadow();
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
	 
	void AddRootGameObject(GameObject* gameObject);
	vector<GameObject*> GetRootGameObjects() { return m_VecRootGameObjects; }
	vector<GameObject*> GetAllGameObjects() const { return m_ArrGameObjects[0]; }

	// 오브젝트와 모든 자손을 전체 목록(렌더/업데이트/물리 대상)에 등록 (이미 있으면 건너뜀)
	void RegisterGameObjectTree(GameObject* gameObject);
	
	void SetCullingGameObjects(vector<GameObject*> culling) { m_CullingGameObjects = culling; }
	void SetCullingEditorGameObjects(vector<GameObject*> culling) { m_CullingEditorGameObjects = culling; }
	
	vector<GameObject*> GetCullingGameObjects() { return m_CullingGameObjects; }
	vector<GameObject*> GetCullingEditorGameObjects() { return m_CullingEditorGameObjects; }
	void RemoveRootGameObjects(GameObject* gameObject);

public:
	friend void from_json(const json& j, Scene& scene);
	friend void to_json(json& j, const Scene& scene);
};

