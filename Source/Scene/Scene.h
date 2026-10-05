#pragma once
#include <nlohmann/json.hpp>

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
	vector<GameObject*> m_PendingDelete;   // DestroyGameObject 로 뺀 오브젝트 — 프레임 끝 FlushDestroyed 에서 delete
public:
	Scene();
	~Scene();
	// 만들 때마다 새 번호 — 지운 씬 자리에 새 씬이 같은 주소로 만들어져도 다른 씬으로 알아본다 (Undo 의 씬 감시)
	uint64 GetSerial() const { return m_Serial; }

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

public:
	friend void from_json(const json& j, Scene& scene);
	friend void to_json(json& j, const Scene& scene);
};

