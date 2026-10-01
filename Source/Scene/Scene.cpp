#include "pch.h"
#include "Scene.h"
#include <functional>
#include "InstancingBuffer.h"
#include "MathHelper.h"
#include "Tree.h"
#include "TreeRenderer.h"
#include "RockRenderer.h"
#include "SceneCulling.h"
#include "MeshBatcher.h"

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

// 화면 하나 동안 쓰는 Skinned Mesh Renderer / 지형 목록: 그림자 조각마다 모든 오브젝트를 다시 훑지 않게 (화면 시작 뒤 첫 패스가 만든다)
namespace
{
    struct ViewRenderers
    {
        uint32 Serial = ~0u, Frame = ~0u;
        const Scene* Owner = nullptr;
        std::vector<SkinnedMeshRenderer*> Skinned;
        std::vector<Terrain*> Terrains;
    };
    ViewRenderers s_ViewRenderers;

    const ViewRenderers& CollectViewRenderers(const Scene* scene, const vector<GameObject*>& objects)
    {
        ViewRenderers& v = s_ViewRenderers;
        const uint32 serial = RenderManager::GetI()->ViewSerial;
        if (v.Owner == scene && v.Serial == serial && v.Frame == SceneCulling::FrameIndex())
            return v;
        v.Owner = scene;
        v.Serial = serial;
        v.Frame = SceneCulling::FrameIndex();
        v.Skinned.clear();
        v.Terrains.clear();
        for (GameObject* gameObject : objects)
        {
            if (SkinnedMeshRenderer* skinned = gameObject->GetComponent<SkinnedMeshRenderer>())
                v.Skinned.push_back(skinned);
            if (Terrain* terrain = gameObject->GetComponent<Terrain>())
                v.Terrains.push_back(terrain);
        }
        return v;
    }
}

// 그리기 패스. Mesh Renderer 는 MeshBatcher 가 (메시, 서브셋, 재질)로 묶어 인스턴싱으로 그리고,
// 나머지(Skinned Mesh Renderer, 지형, 파티클 등)는 컴포넌트마다. 모두 SceneCulling 의 절두체 결과로 거른다.
void Scene::RenderScene()
{
    MeshBatcher::Draw(this, MeshBatcher::Pass::Main, false);
    for (auto& gameObject : m_ArrGameObjects[0])
    {
        for (auto& component : gameObject->GetComponents())
        {
            if (dynamic_cast<MeshRenderer*>(component.get()) != nullptr)
                continue;   // MeshBatcher 가 그렸다
            if (SceneCulling::IsVisible(component.get()))   // 절두체 밖 렌더러는 건너뜀
                component->Render();
        }
    }

    // 나무 (Tree 컴포넌트 + 지형 나무): 인스턴싱 + LOD
    TreeRenderer::DrawAll(TreeRenderer::Pass::Main, false);
    RockRenderer::DrawAll(RockRenderer::Pass::Main, false);   // 바위: 인스턴싱 + LOD
}

void Scene::RenderSceneShadow()
{
    MeshBatcher::Draw(this, MeshBatcher::Pass::Shadow, RenderManager::GetI()->RenderingEditorView);
    const ViewRenderers& view = CollectViewRenderers(this, m_ArrGameObjects[0]);
    for (SkinnedMeshRenderer* skinned : view.Skinned)
        if (SceneCulling::IsVisible(skinned)) skinned->RenderShadow();
    for (Terrain* terrain : view.Terrains)
        terrain->RenderShadow();

    TreeRenderer::DrawAll(TreeRenderer::Pass::Shadow, RenderManager::GetI()->RenderingEditorView);
    RockRenderer::DrawAll(RockRenderer::Pass::Shadow, RenderManager::GetI()->RenderingEditorView);
}

void Scene::RenderSceneShadowNormal()
{
    MeshBatcher::Draw(this, MeshBatcher::Pass::NormalDepth, false);
    const ViewRenderers& view = CollectViewRenderers(this, m_ArrGameObjects[0]);
    for (SkinnedMeshRenderer* skinned : view.Skinned)
        if (SceneCulling::IsVisible(skinned)) skinned->RenderShadowNormal();
    for (Terrain* terrain : view.Terrains)
        terrain->RenderShadowNormal();

    TreeRenderer::DrawAll(TreeRenderer::Pass::NormalDepth, false);
    RockRenderer::DrawAll(RockRenderer::Pass::NormalDepth, false);
}

void Scene::_Editor_RenderScene()
{
    MeshBatcher::Draw(this, MeshBatcher::Pass::Main, true);
    const ViewRenderers& view = CollectViewRenderers(this, m_ArrGameObjects[0]);
    for (SkinnedMeshRenderer* skinned : view.Skinned)
        if (SceneCulling::IsVisible(skinned)) skinned->_Editor_Render();
    for (Terrain* terrain : view.Terrains)
        terrain->_Editor_Render();

    TreeRenderer::DrawAll(TreeRenderer::Pass::Main, true);
    RockRenderer::DrawAll(RockRenderer::Pass::Main, true);
}

void Scene::_Editor_RenderSceneShadowNormal()
{
    MeshBatcher::Draw(this, MeshBatcher::Pass::NormalDepth, true);
    const ViewRenderers& view = CollectViewRenderers(this, m_ArrGameObjects[0]);
    for (SkinnedMeshRenderer* skinned : view.Skinned)
        if (SceneCulling::IsVisible(skinned)) skinned->_Editor_RenderShadowNormal();
    for (Terrain* terrain : view.Terrains)
        terrain->_Editor_RenderShadowNormal();

    TreeRenderer::DrawAll(TreeRenderer::Pass::NormalDepth, true);
    RockRenderer::DrawAll(RockRenderer::Pass::NormalDepth, true);
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
