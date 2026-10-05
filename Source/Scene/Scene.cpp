#include "pch.h"
#include "TagsAndLayers.h"
#include "RenderLayers.h"
#include "Physics2DManager.h"
#include "Scene.h"
#include <functional>
#include "InstancingBuffer.h"
#include "MathHelper.h"
#include "Tree.h"
#include "TreeRenderer.h"
#include "RockRenderer.h"
#include "DetailRenderer.h"
#include "SceneCulling.h"
#include "MeshBatcher.h"
#include "SelectionManager.h"
#include "Debug.h"
#include <memory>
#include <stdexcept>

Scene::Scene()
	: m_VecRootGameObjects(),
    m_ArrGameObjects{},
    m_ScenePath(L"")
{
    static std::atomic<uint64> s_NextSerial = 1;
    m_Serial = s_NextSerial++;
}

Scene::~Scene()
{
    // 루트뿐 아니라 자손까지 모두 OnDestroy + delete.
    // 예전에는 루트만 지워 자식 오브젝트가 남았다 — 자식의 컴포넌트(입자·빛 등)가 전역 목록에 그대로 남아
    // Play→Stop 이나 씬 전환 뒤에도 그려졌다 (모닥불 연기가 다른 씬에 남던 문제).
    // 루트에서 내려가며 모으므로 이 씬이 가진 오브젝트만, 한 번씩만 지운다.
    std::vector<GameObject*> all;
    std::function<void(GameObject*)> collect = [&](GameObject* g)
    {
        if (g == nullptr || std::find(all.begin(), all.end(), g) != all.end())
            return;
        all.push_back(g);
        for (GameObject* child : g->GetChildren())
            collect(child);
    };
    for (GameObject* g : m_VecRootGameObjects)
        collect(g);
    // DestroyGameObject 로 이미 OnDestroy 된 채 delete 를 기다리던 것 (트리에서 떨어져 있어 위에서 모이지 않는다)
    std::vector<GameObject*> pending;
    for (GameObject* g : m_PendingDelete)
        if (std::find(all.begin(), all.end(), g) == all.end() && std::find(pending.begin(), pending.end(), g) == pending.end())
            pending.push_back(g);

    // 먼저 모두 OnDestroy (컴포넌트가 다른 오브젝트를 볼 수 있으므로 지우기 전에), 그다음 delete
    for (GameObject* g : all)
        g->OnDestroy();
    for (GameObject* g : all)
        delete g;
    for (GameObject* g : pending)
        delete g;
    m_PendingDelete.clear();
    m_VecRootGameObjects.clear();
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
    Physics2DManager::Start(this);   // 2D 물리 (Box2D) — 3D 와 따로

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
    Physics2DManager::Exit();
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
            if (SceneCulling::IsVisible(component.get()) && RenderLayers::Visible(gameObject))   // 절두체 밖 · Culling Mask 밖은 건너뜀
                component->Render();
        }
    }

    // 나무 (Tree 컴포넌트 + 지형 나무): 인스턴싱 + LOD
    TreeRenderer::DrawAll(TreeRenderer::Pass::Main, false);
    RockRenderer::DrawAll(RockRenderer::Pass::Main, false);   // 바위: 인스턴싱 + LOD
    DetailRenderer::DrawAll(DetailRenderer::Pass::Main, false);
}

void Scene::RenderSceneShadow()
{
    MeshBatcher::Draw(this, MeshBatcher::Pass::Shadow, RenderManager::GetI()->RenderingEditorView);
    const ViewRenderers& view = CollectViewRenderers(this, m_ArrGameObjects[0]);
    for (SkinnedMeshRenderer* skinned : view.Skinned)
        if (SceneCulling::IsVisible(skinned) && RenderLayers::Visible(skinned->GetGameObject())) skinned->RenderShadow();
    for (Terrain* terrain : view.Terrains)
        if (RenderLayers::Visible(terrain->GetGameObject())) terrain->RenderShadow();

    TreeRenderer::DrawAll(TreeRenderer::Pass::Shadow, RenderManager::GetI()->RenderingEditorView);
    RockRenderer::DrawAll(RockRenderer::Pass::Shadow, RenderManager::GetI()->RenderingEditorView);
    DetailRenderer::DrawAll(DetailRenderer::Pass::Shadow, RenderManager::GetI()->RenderingEditorView);
}

void Scene::RenderSceneCover()
{
    // 위에서 본 덮개: 지붕 · 처마 · 나무 · 바위 · 지형 (풀은 비를 막지 않고, 캐릭터 밑은 눈이 쌓인 채 밟힌다)
    MeshBatcher::Draw(this, MeshBatcher::Pass::Shadow, RenderManager::GetI()->RenderingEditorView);
    const ViewRenderers& view = CollectViewRenderers(this, m_ArrGameObjects[0]);
    for (Terrain* terrain : view.Terrains)
        if (RenderLayers::Visible(terrain->GetGameObject())) terrain->RenderShadow();
    TreeRenderer::DrawAll(TreeRenderer::Pass::Shadow, RenderManager::GetI()->RenderingEditorView);
    RockRenderer::DrawAll(RockRenderer::Pass::Shadow, RenderManager::GetI()->RenderingEditorView);
}

void Scene::RenderSceneDeformers()
{
    // 아래에서 본 깊이로 눈을 누른다: 움직이는 것만 (스킨 메시 · 물리로 움직이는 메시 · 캐릭터)
    auto moves = [](GameObject* go) {
        for (GameObject* g = go; g != nullptr; g = g->GetParent())
            for (const auto& c : g->GetComponents())
            {
                const std::string t = c->GetType();
                if (t == "RigidBody" || t == "CharacterController")
                    return true;
            }
        return false;
    };
    auto active = [](GameObject* go) {
        for (GameObject* g = go; g != nullptr; g = g->GetParent())
            if (!g->IsActive()) return false;
        return true;
    };
    for (GameObject* go : m_ArrGameObjects[0])
    {
        if (!active(go) || !RenderLayers::Visible(go))
            continue;
        for (const auto& c : go->GetComponents())
        {
            if (!c->IsEnabled())
                continue;
            if (auto* skinned = dynamic_cast<SkinnedMeshRenderer*>(c.get()))
                skinned->RenderShadow();
            else if (auto* mesh = dynamic_cast<MeshRenderer*>(c.get()); mesh && moves(go))
                mesh->RenderShadow();
        }
    }
}

void Scene::RenderSceneShadowNormal()
{
    MeshBatcher::Draw(this, MeshBatcher::Pass::NormalDepth, false);
    const ViewRenderers& view = CollectViewRenderers(this, m_ArrGameObjects[0]);
    for (SkinnedMeshRenderer* skinned : view.Skinned)
        if (SceneCulling::IsVisible(skinned) && RenderLayers::Visible(skinned->GetGameObject())) skinned->RenderShadowNormal();
    for (Terrain* terrain : view.Terrains)
        if (RenderLayers::Visible(terrain->GetGameObject())) terrain->RenderShadowNormal();

    TreeRenderer::DrawAll(TreeRenderer::Pass::NormalDepth, false);
    RockRenderer::DrawAll(RockRenderer::Pass::NormalDepth, false);
    DetailRenderer::DrawAll(DetailRenderer::Pass::NormalDepth, false);
}

void Scene::_Editor_RenderScene()
{
    MeshBatcher::Draw(this, MeshBatcher::Pass::Main, true);
    const ViewRenderers& view = CollectViewRenderers(this, m_ArrGameObjects[0]);
    for (SkinnedMeshRenderer* skinned : view.Skinned)
        if (SceneCulling::IsVisible(skinned) && RenderLayers::Visible(skinned->GetGameObject())) skinned->_Editor_Render();
    for (Terrain* terrain : view.Terrains)
        if (RenderLayers::Visible(terrain->GetGameObject())) terrain->_Editor_Render();

    TreeRenderer::DrawAll(TreeRenderer::Pass::Main, true);
    RockRenderer::DrawAll(RockRenderer::Pass::Main, true);
    DetailRenderer::DrawAll(DetailRenderer::Pass::Main, true);
}

void Scene::_Editor_RenderSceneShadowNormal()
{
    MeshBatcher::Draw(this, MeshBatcher::Pass::NormalDepth, true);
    const ViewRenderers& view = CollectViewRenderers(this, m_ArrGameObjects[0]);
    for (SkinnedMeshRenderer* skinned : view.Skinned)
        if (SceneCulling::IsVisible(skinned) && RenderLayers::Visible(skinned->GetGameObject())) skinned->_Editor_RenderShadowNormal();
    for (Terrain* terrain : view.Terrains)
        if (RenderLayers::Visible(terrain->GetGameObject())) terrain->_Editor_RenderShadowNormal();

    TreeRenderer::DrawAll(TreeRenderer::Pass::NormalDepth, true);
    RockRenderer::DrawAll(RockRenderer::Pass::NormalDepth, true);
    DetailRenderer::DrawAll(DetailRenderer::Pass::NormalDepth, true);
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

// 씬 경로 → 실제 파일 경로 (프로젝트 기준 상대 경로 또는 절대 경로)
static std::wstring ResolveScenePath(const std::wstring& path)
{
    std::filesystem::path fp(path);
    return fp.is_absolute() ? path : PathManager::GetI()->GetMovePathW(path);
}

Scene* Scene::Load(wstring scenePath)
{
    if (scenePath.empty())
        return nullptr;   // 저장하지 않은 기본 씬으로 시작하는 정상 경로
    try
    {
        std::ifstream is{ std::filesystem::path(ResolveScenePath(scenePath)) };
        if (!is)
            throw std::runtime_error("could not read the scene file");

        // parse()는 파일 뒤의 잘못된 내용도 거절한다. 복원이 끝나기 전까지 임시 씬이 소유한다.
        const json j = json::parse(is);
        auto scene = std::make_unique<Scene>();
        scene->m_ScenePath = scenePath;
        from_json(j, *scene);
        return scene.release();
    }
    catch (const std::exception& error)
    {
        Debug::LogError("Could not open scene '" + wstring_to_string(scenePath) + "': " + error.what());
        return nullptr;
    }
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
    // 이미 지웠거나 delete 를 기다리는 오브젝트면 무시 (같은 프레임에 두 번 지우기)
    if (gameobject == nullptr || !GameObject::IsAlive(gameobject)
        || std::find(m_PendingDelete.begin(), m_PendingDelete.end(), gameobject) != m_PendingDelete.end())
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
        if (SelectionManager::GetSelectedGameObject() == g)
            SelectionManager::ClearSelection();
        if (std::find(m_PendingDelete.begin(), m_PendingDelete.end(), g) == m_PendingDelete.end())
            m_PendingDelete.push_back(g);
    };
    removeTree(gameobject);
}

void Scene::FlushDestroyed()
{
    if (m_PendingDelete.empty())
        return;
    // 예전에는 delete 하지 않아 지운 오브젝트가 계속 메모리에 남았다 (스크립트 Destroy·Hierarchy 삭제·Undo 마다).
    // 컴포넌트는 OnDestroy 에서 이미 풀렸고, 여기서는 GameObject 자체를 지운다. 컬링 목록의 옛 포인터도 뺀다.
    std::vector<GameObject*> list;
    list.swap(m_PendingDelete);
    auto drop = [&](vector<GameObject*>& v)
    {
        v.erase(std::remove_if(v.begin(), v.end(), [&](GameObject* g) { return std::find(list.begin(), list.end(), g) != list.end(); }), v.end());
    };
    drop(m_CullingGameObjects);
    drop(m_CullingEditorGameObjects);
    for (GameObject* g : list)
    {
        if (SelectionManager::GetSelectedGameObject() == g)
            SelectionManager::ClearSelection();
        delete g;
    }
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
        { "layerFormat", TagsAndLayers::kLayerFormat },   // 레이어 번호 = Unity 번호 (없으면 예전 파일 → 불러올 때 옮김)
        { "rootGameObjects", json::array() }
    };
    for (const GameObject* gameObject : scene.m_VecRootGameObjects)
    {
        j["rootGameObjects"].push_back(*gameObject);
    }
}

void from_json(const json& j, Scene& scene)
{
    if (!j.is_object() || !j.contains("rootGameObjects") || !j.at("rootGameObjects").is_array())
        throw std::runtime_error("scene rootGameObjects must be an array");

    // 예전 레이어 번호 (3 Water, 4 UI) 로 저장한 씬 → Unity 번호
    const bool legacyLayers = j.value("layerFormat", 1) < TagsAndLayers::kLayerFormat;
    for (const auto& gameObjectJson : j.at("rootGameObjects"))
    {
        auto owned = std::make_unique<GameObject>();
        GameObject* gameObject = owned.get();
        // 복원 중 예외가 나도 Scene 소멸자가 이 루트와 이미 붙인 자손을 정리한다.
        scene.AddRootGameObject(gameObject);
        owned.release();
        if (legacyLayers)
        {
            json migrated = gameObjectJson;
            TagsAndLayers::MigrateLegacyObjectJson(migrated);
            from_json(migrated, *gameObject);
        }
        else
            from_json(gameObjectJson, *gameObject);
        scene.RegisterGameObjectTree(gameObject);   // 복원이 끝난 자손도 렌더/업데이트 목록에 등록
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
