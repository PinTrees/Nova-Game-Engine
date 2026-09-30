#include "pch.h"
#include "Scene.h"
#include <functional>
#include "InstancingBuffer.h"
#include "MathHelper.h"
#include "Tree.h"

Scene::Scene()
	: m_VecRootGameObjects(),
    m_ArrGameObjects{},
    m_ScenePath(L"")
{
}

Scene::~Scene()
{
    for (const auto& g : m_VecRootGameObjects)
        g->OnDestroy(); 

	Safe_Delete_Vec(m_VecRootGameObjects);
    m_ArrGameObjects[0].clear();
}

void Scene::Enter()
{
    // 스크립트(Awake/Start)가 오브젝트를 만들거나 부모를 바꾸면 목록이 늘어나므로 복사본을 돈다
    const std::vector<GameObject*> objects = m_ArrGameObjects[0];
    for (auto& gameObject : objects)
    {
        for (auto& component : gameObject->GetComponents())
        {
            component->Awake();
        }
    }

    // Unity 와 같이 Start 에서 Rigidbody 를 바로 쓸 수 있도록 물리 바디를 먼저 만든다
    PhysicsManager::GetI()->Start();

    for (auto& gameObject : objects)
    {
        for (auto& component : gameObject->GetComponents())
        {
            component->Start();
        }
    }
}

void Scene::Exit()
{
    PhysicsManager::GetI()->Exit(); 
}

void Scene::RenderScene()
{
    if (RenderManager::GetI()->InstancingMode) // if문에 Editor에서 인스턴싱을 사용할 것인지에 대한 bool형 변수로 인스턴싱 사용여부 판단
    {
        map<InstanceID, vector<GameObject*>> cache;

        for (const auto& gameObject : m_ArrGameObjects[0]) // cullingObejcts로 변경예정, play아닐때도 업데이트할 함수 추가해야함 Light, camera
        {
            for (auto& component : gameObject->GetComponents())
            {

                // 컴포넌트 마다 MeshRenderer인지 체크
                shared_ptr<MeshRenderer> meshRenderer = dynamic_pointer_cast<MeshRenderer>(component);
                //MeshRenderer* meshRenderer = gameObject->GetComponent<MeshRenderer>();
                
                if (meshRenderer)
                {
                    // map<인스턴싱 ID,gameObject> 변수에 추가
                    const InstanceID instanceId = meshRenderer->GetInstanceID();
                    cache[instanceId].emplace_back(gameObject);
                }
                else 
                {
                    component->Render();
                }
            }
        }

        // 같은 오브젝트들 끼리 world배열에 저장 후 world배열을 인자값으로 넘기면서 인스턴싱 렌더
        for (auto& pair : cache)
        {
            const vector<GameObject*>& vec = pair.second;
            shared_ptr<InstancingBuffer> buffer = make_shared<InstancingBuffer>(); // worldMatrix 값을 가지고 있는 인스턴싱 버퍼, 렌더로 넘겨야함
            {
                //const InstanceID instanceId = pair.first;

                for (int32 i = 0; i < vec.size(); i++)
                {
                    GameObject* gameObject = vec[i];
                    InstancingData data;
                    data.world = gameObject->GetTransform()->GetWorldMatrix();

                    // Mesh와 Material의 조합 아이디 배열의 data 벡터에 data(월드좌표들)저장
                    buffer->AddData(data);
                    //AddData(instanceId, data);
                }

                vec[0]->GetComponent<MeshRenderer>()->RenderInstancing(buffer);
            }
        }
    }
    else
    {
        for (auto& gameObject : m_ArrGameObjects[0])
        {
            for (auto& component : gameObject->GetComponents())
            {
                component->Render(); 
            }
        }
    }
}

void Scene::RenderSceneShadow()
{
    if (RenderManager::GetI()->InstancingMode)
    {
        map<InstanceID, vector<GameObject*>> cache;

        for (const auto& gameObject : m_ArrGameObjects[0])
        {
            auto meshRenderer = gameObject->GetComponent<MeshRenderer>();
            if (meshRenderer == nullptr)
                continue;

            // map<인스턴싱 ID,gameObject> 변수에 추가
            const InstanceID instanceId = meshRenderer->GetInstanceID();
            cache[instanceId].emplace_back(gameObject);
        }

        // 같은 오브젝트들 끼리 world배열에 저장 후 world배열을 인자값으로 넘기면서 인스턴싱 렌더
        for (auto& pair : cache)
        {
            const vector<GameObject*>& vec = pair.second;
            shared_ptr<InstancingBuffer> buffer = make_shared<InstancingBuffer>(); // worldMatrix 값을 가지고 있는 인스턴싱 버퍼, 렌더로 넘겨야함
            {
                //const InstanceID instanceId = pair.first;

                for (int32 i = 0; i < vec.size(); i++)
                {
                    GameObject* gameObject = vec[i];
                    InstancingData data;
                    data.world = gameObject->GetTransform()->GetWorldMatrix();

                    // Mesh와 Material의 조합 아이디 배열의 data 벡터에 data(월드좌표들)저장
                    buffer->AddData(data);
                    //AddData(instanceId, data);
                }

                vec[0]->GetComponent<MeshRenderer>()->RenderShadowInstancing(buffer);
            }
        }

        for (const auto& gameObject : m_ArrGameObjects[0])
        {
            if (Terrain* terrain = gameObject->GetComponent<Terrain>()) terrain->RenderShadow();
            if (Tree* tree = gameObject->GetComponent<Tree>()) tree->RenderShadow();
        }

    }
    else
    {
        for (auto& gameObject : m_ArrGameObjects[0])
        {
            MeshRenderer* meshRenderer = gameObject->GetComponent<MeshRenderer>();
            if (meshRenderer) meshRenderer->RenderShadow();

            SkinnedMeshRenderer* skinnedMeshRenderer = gameObject->GetComponent<SkinnedMeshRenderer>();
            if (skinnedMeshRenderer) skinnedMeshRenderer->RenderShadow();

            if (Terrain* terrain = gameObject->GetComponent<Terrain>()) terrain->RenderShadow();
            if (Tree* tree = gameObject->GetComponent<Tree>()) tree->RenderShadow();
        }
    }
}

void Scene::RenderSceneShadowNormal()
{
    if (RenderManager::GetI()->InstancingMode)
    {
        map<InstanceID, vector<GameObject*>> cache;

        for (const auto& gameObject : m_ArrGameObjects[0])
        {
            auto meshRenderer = gameObject->GetComponent<MeshRenderer>();
            if (meshRenderer == nullptr)
                continue;

            // map<인스턴싱 ID,gameObject> 변수에 추가
            const InstanceID instanceId = meshRenderer->GetInstanceID();
            cache[instanceId].emplace_back(gameObject);
        }

        // 같은 오브젝트들 끼리 world배열에 저장 후 world배열을 인자값으로 넘기면서 인스턴싱 렌더
        for (auto& pair : cache)
        {
            const vector<GameObject*>& vec = pair.second;
            shared_ptr<InstancingBuffer> buffer = make_shared<InstancingBuffer>(); // worldMatrix 값을 가지고 있는 인스턴싱 버퍼, 렌더로 넘겨야함
            {
                //const InstanceID instanceId = pair.first;

                for (int32 i = 0; i < vec.size(); i++)
                {
                    GameObject* gameObject = vec[i];
                    InstancingData data;
                    data.world = gameObject->GetTransform()->GetWorldMatrix();

                    // Mesh와 Material의 조합 아이디 배열의 data 벡터에 data(월드좌표들)저장
                    buffer->AddData(data);
                    //AddData(instanceId, data);
                }

                vec[0]->GetComponent<MeshRenderer>()->RenderShadowNormalInstancing(buffer);
            }
        }

        for (const auto& gameObject : m_ArrGameObjects[0])
        {
            if (Terrain* terrain = gameObject->GetComponent<Terrain>()) terrain->RenderShadowNormal();
            if (Tree* tree = gameObject->GetComponent<Tree>()) tree->RenderShadowNormal();
        }
    }
    else
    {
        for (const auto& gameObject : m_ArrGameObjects[0])
        {
            MeshRenderer* meshRenderer = gameObject->GetComponent<MeshRenderer>();
            if (meshRenderer) meshRenderer->RenderShadowNormal();

            SkinnedMeshRenderer* skinnedMeshRenderer = gameObject->GetComponent<SkinnedMeshRenderer>();
            if (skinnedMeshRenderer) skinnedMeshRenderer->RenderShadowNormal();

            if (Terrain* terrain = gameObject->GetComponent<Terrain>()) terrain->RenderShadowNormal();
            if (Tree* tree = gameObject->GetComponent<Tree>()) tree->RenderShadowNormal();
        }
    }
}

void Scene::_Editor_RenderScene()
{
    for (auto& gameObject : m_ArrGameObjects[0])
    {
        MeshRenderer* meshRenderer = gameObject->GetComponent<MeshRenderer>();
        if (meshRenderer) meshRenderer->_Editor_Render();

        SkinnedMeshRenderer* skinnedMeshRenderer = gameObject->GetComponent<SkinnedMeshRenderer>();
        if (skinnedMeshRenderer) skinnedMeshRenderer->_Editor_Render();

        if (Terrain* terrain = gameObject->GetComponent<Terrain>()) terrain->_Editor_Render();
        if (Tree* tree = gameObject->GetComponent<Tree>()) tree->_Editor_Render();
    }
}

void Scene::_Editor_RenderSceneShadowNormal()
{
    for (const auto& gameObject : m_ArrGameObjects[0])
    {
        MeshRenderer* meshRenderer = gameObject->GetComponent<MeshRenderer>(); 
        if (meshRenderer) meshRenderer->_Editor_RenderShadowNormal();  

        SkinnedMeshRenderer* skinnedMeshRenderer = gameObject->GetComponent<SkinnedMeshRenderer>();
        if (skinnedMeshRenderer) skinnedMeshRenderer->_Editor_RenderShadowNormal();

        if (Terrain* terrain = gameObject->GetComponent<Terrain>()) terrain->_Editor_RenderShadowNormal();
        if (Tree* tree = gameObject->GetComponent<Tree>()) tree->_Editor_RenderShadowNormal();
} 
}

void Scene::RenderSceneGizmos()
{
    // 선택 오브젝트의 이동/회전/크기 핸들은 SceneGizmoTools 가 그린다 (Unity 스타일)

    for (auto& gameObject : m_ArrGameObjects[0])
    {
        for (auto& component : gameObject->GetComponents())
        {
            component->OnDrawGizmos();
        }
    }

    PhysicsManager::GetI()->DebugRender();
}

void Scene::LastFramUpdate()
{
    const std::vector<GameObject*> objects = m_ArrGameObjects[0];   // 편집 동작이 목록을 바꿀 수 있어 복사본
    for (auto& gameObject : objects)
    {
        gameObject->LastUpdate();
        gameObject->ApplyPendingComponents();
    }
}

void Scene::UpdateScene()
{
    // 스크립트의 Update 가 오브젝트를 만들거나 부모를 바꾸면(transform.SetParent) 목록이 늘어나므로 복사본을 돈다.
    // 새 오브젝트는 다음 프레임부터 업데이트된다 (Unity 와 같음)
    const std::vector<GameObject*> objects = m_ArrGameObjects[0];
    for (auto& gameObject : objects)
    {
        for (auto& component : gameObject->GetComponents())
        {
            component->Update();
        }
    }

    for (auto& gameObject : objects)
    {
        for (auto& component : gameObject->GetComponents())
        {
            component->LateUpdate();
        }
    }
}

wstring Scene::GetName() const
{
    if (m_ScenePath.empty())
        return L"Untitled";
    return std::filesystem::path(m_ScenePath).stem().wstring();
}

Scene* Scene::Load(wstring scenePath)
{
    std::ifstream is(wstring_to_string(PathManager::GetI()->GetMovePathW(scenePath)));

    if (!is)
    {
        return nullptr;
    }

    json j;
    is >> j;

    Scene* scene = new Scene();
    scene->m_ScenePath = scenePath;
    from_json(j, *scene);

    return scene;
}

// 씬 경로 → 실제 파일 경로 (프로젝트 기준 상대 경로 또는 절대 경로)
static std::wstring ResolveScenePath(const std::wstring& path)
{
    std::filesystem::path fp(path);
    return fp.is_absolute() ? path : PathManager::GetI()->GetMovePathW(path);
}

// 씬을 JSON 으로 파일에 쓴다. 성공하면 마지막으로 연 씬으로 기록한다.
static bool WriteSceneFile(Scene* scene, const std::wstring& scenePath)
{
    const std::wstring filePath = ResolveScenePath(scenePath);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(filePath).parent_path(), ec);

    json j = *scene;
    std::ofstream os(filePath, std::ios::binary | std::ios::trunc);
    if (!os)
        return false;
    os << j.dump(4);
    os.close();
    if (os.fail())
        return false;

    EditorSettingManager::SetLastOpenedScenePath(scenePath);
    return true;
}

bool Scene::Save(Scene* scene)
{
    if (scene == nullptr || scene->m_ScenePath.empty())
        return false;
    return WriteSceneFile(scene, scene->m_ScenePath);
}

bool Scene::SaveNewScene(Scene* scene)
{
    if (scene == nullptr)
        return false;
    std::wstring filePath = EditorUtility::SaveFileDialog(PathManager::GetI()->GetMovePathW(L"Assets\\"), L"Save Scene As", L"scene");
    if (filePath.empty())
        return false;   // 취소

    // 확장자 보정 후, 프로젝트 안이면 "Assets\\..." 상대 경로로 저장한다 (밖이면 절대 경로 그대로)
    if (std::filesystem::path(filePath).extension() != L".scene")
        filePath += L".scene";
    const std::wstring relative = PathManager::GetI()->GetCutSolutionPath(filePath);
    if (!WriteSceneFile(scene, relative))
        return false;
    scene->m_ScenePath = relative;
    return true;
}

void Scene::DestroyComponent(Component* component)
{
    if (component->GetType() == "Transform")
    {
        // 디버그
        return;
    }

    component->OnDestroy(); 
    auto& components = component->GetGameObject()->GetComponents();

    bool isDeleteComponent = false;
    for (int i = 0; i < components.size(); ++i) 
    {
        auto& c = components[i]; 
        if (c.get() == component)  
        {
            c.reset();
            components.erase(components.begin() + i);
            isDeleteComponent = true;
            break;
        }
    }
    
    if (!isDeleteComponent && component != nullptr)
        delete component; 
}

void Scene::DestroyGameObject(GameObject* gameobject)
{
    if (gameobject == nullptr)
        return;

    // 부모의 자식 목록에서 떼어낸다
    if (GameObject* parent = gameobject->GetParent())
    {
        parent->RemoveChild(gameobject);
        parent->GetTransform()->RemoveChild(gameobject->GetComponent_SP<Transform>());
    }
    auto it1 = std::find(m_VecRootGameObjects.begin(), m_VecRootGameObjects.end(), gameobject);
    if (it1 != m_VecRootGameObjects.end())
        m_VecRootGameObjects.erase(it1);

    // 자손까지 모두 제거 (Unity: 부모를 지우면 자식도 함께 지워진다)
    std::function<void(GameObject*)> removeTree = [&](GameObject* g)
    {
        for (GameObject* child : g->GetChildren())
            removeTree(child);
        g->OnDestroy();
        auto it2 = std::find(m_ArrGameObjects[0].begin(), m_ArrGameObjects[0].end(), g);
        if (it2 != m_ArrGameObjects[0].end())
            m_ArrGameObjects[0].erase(it2);
    };
    removeTree(gameobject);
}

void Scene::AddRootGameObject(GameObject* gameObject)
{
    if (gameObject == nullptr)
        return;

    if (std::find(m_VecRootGameObjects.begin(), m_VecRootGameObjects.end(), gameObject) == m_VecRootGameObjects.end())
        m_VecRootGameObjects.push_back(gameObject);
    RegisterGameObjectTree(gameObject);   // 자식이 있는 오브젝트(붙여넣기, 씬 로드)도 자손까지 등록
}

void Scene::RegisterGameObjectTree(GameObject* gameObject)
{
    if (gameObject == nullptr)
        return;
    if (std::find(m_ArrGameObjects[0].begin(), m_ArrGameObjects[0].end(), gameObject) == m_ArrGameObjects[0].end())
        m_ArrGameObjects[0].push_back(gameObject);
    for (GameObject* child : gameObject->GetChildren())
        RegisterGameObjectTree(child);
}

void Scene::RemoveRootGameObjects(GameObject* gameObject)
{
    auto it = std::find(m_VecRootGameObjects.begin(), m_VecRootGameObjects.end(), gameObject);
    if (it != m_VecRootGameObjects.end())
    { 
        m_VecRootGameObjects.erase(it);
    }
}

void to_json(json& j, const Scene& scene)
{
    j = json
    {
        { "rootGameObjects", json::array() }
    };
    for (const GameObject* gameObject : scene.m_VecRootGameObjects)
    {
        j["rootGameObjects"].push_back(*gameObject);
    }
}

void from_json(const json& j, Scene& scene)
{
    for (const auto& gameObjectJson : j.at("rootGameObjects"))
    {
        GameObject* gameObject = new GameObject();
        from_json(gameObjectJson, *gameObject);
        scene.AddRootGameObject(gameObject);   // 저장된 자식 오브젝트까지 렌더/업데이트 목록에 등록
    }
}

GameObject* Scene::FindByFileID(uint64 fileID) const
{
    if (fileID == 0)
        return nullptr;
    for (GameObject* go : m_ArrGameObjects[0])
        if (go && go->GetFileID() == fileID)
            return go;
    return nullptr;
}
