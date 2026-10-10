#include "pch.h"
#include "MemoryHeaps.h"
#include "MissingComponent.h"
#include "PackageManager.h"
#include "GameObject.h"
#include "AddComponentMenu.h"
#include "Transform.h"
#include "RectTransform.h"
#include "EditorGUI.h"
#include "UnityGUI.h"
#include "MeshFilter.h"
#include "MeshRenderer.h"
#include "Physics2DComponents.h"
#include "Physics2DJoints.h"
#include <random>
#include <memory>
#include <stdexcept>

atomic<uint64> GameObject::g_NextInstanceID = 0;

void GameObject::EnsureRequiredComponents(Component* component, bool deferred)
{
    if (!dynamic_cast<Joint2D*>(component) || GetComponentIncludingPending<Rigidbody2D>()) return;
    auto body = std::make_shared<Rigidbody2D>();
    if (deferred) QueueComponent(body); else AddComponent(body);
}

namespace
{
	// 살아 있는 오브젝트 목록 (IsAlive). 씬 불러오기가 작업 스레드에서 만들 수 있어 잠근다
	std::mutex& LiveMutex() { static std::mutex m; return m; }
	std::unordered_set<const GameObject*>& LiveSet() { static std::unordered_set<const GameObject*> s; return s; }
	std::atomic<uint32_t> s_DestroyedSerial{ 0 };
	void MarkLive(const GameObject* g, bool live)
	{
		std::lock_guard<std::mutex> lock(LiveMutex());
		if (live) LiveSet().insert(g); else LiveSet().erase(g);
		if (!live)
			s_DestroyedSerial.fetch_add(1, std::memory_order_relaxed);
	}
}

uint32_t GameObject::DestroyedSerial()
{
	return s_DestroyedSerial.load(std::memory_order_relaxed);
}

bool GameObject::IsAlive(const GameObject* gameobject_ptr)
{
	if (gameobject_ptr == nullptr)
		return false;
	std::lock_guard<std::mutex> lock(LiveMutex());
	return LiveSet().count(gameobject_ptr) != 0;
}

size_t GameObject::LiveCount()
{
	std::lock_guard<std::mutex> lock(LiveMutex());
	return LiveSet().size();
}

uint64 GameObject::NewFileID()
{
	static std::mt19937_64 rng(std::random_device{}() ^ (uint64)::GetTickCount64());
	uint64 id = 0;
	while (id == 0)
		id = rng();
	return id;
}

void GameObject::RegenerateFileIDs()
{
	std::unordered_map<uint64, uint64> map;
	RegenerateFileIDs(map);
	RemapFileIDs(map);
}

void GameObject::RegenerateFileIDs(const std::vector<GameObject*>& roots)
{
	std::unordered_map<uint64, uint64> map;
	for (GameObject* r : roots)
		r->RegenerateFileIDs(map);
	for (GameObject* r : roots)
		r->RemapFileIDs(map);
}

void GameObject::RegenerateFileIDs(std::unordered_map<uint64, uint64>& map)
{
	const uint64 old = m_FileID;
	m_FileID = NewFileID();
	map[old] = m_FileID;
	for (GameObject* child : m_pChildGameObjects)
		child->RegenerateFileIDs(map);
}

void GameObject::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	for (auto& c : m_Components)
		c->RemapFileIDs(map);
	for (auto& c : m_ComponentsToAdd)
		c->RemapFileIDs(map);
	for (GameObject* child : m_pChildGameObjects)
		child->RemapFileIDs(map);
}


void* GameObject::operator new(size_t size) { return Memory::Heaps::Allocate(size, alignof(GameObject) < 16 ? 16 : alignof(GameObject), "GameObject"); }
void GameObject::operator delete(void* p, size_t size) { Memory::Heaps::Free(p, size); }

GameObject::GameObject()
	: m_FileID(NewFileID())
	, m_InstanceID(g_NextInstanceID++)
	, m_LayerIndex(0)
	, m_pParentGameObject(nullptr)
    , m_IsActive(true)
    , m_Editor_HierachOpened(false)
{
    MarkLive(this, true);
    m_pTransform = AddComponent<Transform>();
}

GameObject::GameObject(const string& name)
	: m_Name(name)
	, m_FileID(NewFileID())
	, m_InstanceID(g_NextInstanceID++)
	, m_LayerIndex(0)
	, m_pParentGameObject(nullptr)
    , m_IsActive(true)
    , m_Editor_HierachOpened(false)
{
    MarkLive(this, true);
    m_pTransform = AddComponent<Transform>();
}

bool GameObject::s_PendingWork = false;
int GameObject::s_PendingComponents = 0;

GameObject::~GameObject()
{
    MarkLive(this, false);
    m_Components.clear();
    s_PendingComponents = (std::max)(0, s_PendingComponents - (int)m_ComponentsToAdd.size());
    m_ComponentsToAdd.clear();
    m_Scripts.clear();
}

void GameObject::Destroy(Component* component_ptr) 
{
    SceneManager::GetI()->AddLastUpdate([component_ptr]() { 
        const auto& scene = SceneManager::GetI()->GetCurrentScene(); 
        scene->DestroyComponent(component_ptr); 
    });  
}

void GameObject::Destroy(GameObject* gameobject_ptr)
{
    SceneManager::GetI()->AddLastUpdate([gameobject_ptr]() { 
        if (!IsAlive(gameobject_ptr))   // 같은 오브젝트를 두 번 Destroy 했거나 그사이 씬이 바뀌어 이미 지워졌다
            return;
        const auto& scene = SceneManager::GetI()->GetCurrentScene(); 
        scene->DestroyGameObject(gameobject_ptr); 
    }); 
}

void GameObject::SetParent(GameObject* parent, bool worldPositionStays)
{
    if (parent == m_pParentGameObject)
        return;
    // 자기 자신이나 자손 밑으로는 옮길 수 없다
    for (GameObject* p = parent; p != nullptr; p = p->GetParent())
        if (p == this)
            return;
    Component::MarkPhysicsDirty();   // 바디 소유자 (Rigidbody 가 있는 조상) 가 바뀔 수 있다

    Transform* transform = GetTransform();
    const Vec3 worldPos = transform->GetPosition();
    const Quaternion worldRot = transform->GetRotation();
    const Vec3 worldScale = transform->GetScale();
    Scene* scene = SceneManager::GetI()->GetCurrentScene();

    // 이전 부모(또는 루트 목록)에서 떼어낸다
    if (m_pParentGameObject != nullptr)
    {
        m_pParentGameObject->RemoveChild(this);
        m_pParentGameObject->GetTransform()->RemoveChild(GetComponent_SP<Transform>());
    }
    else if (scene != nullptr)
    {
        scene->RemoveRootGameObjects(this);
    }

    m_pParentGameObject = parent;
    if (parent != nullptr)
    {
        transform->SetParent(parent->GetComponent_SP<Transform>());
        parent->SetChild(this);   // Transform 자식 목록에도 추가된다
        if (scene != nullptr)
            scene->RegisterGameObjectTree(this);   // 씬에 처음 들어오는 오브젝트도 렌더/업데이트 목록에 등록
    }
    else
    {
        transform->SetParent(nullptr);
        if (scene != nullptr)
            scene->AddRootGameObject(this);
    }

    if (worldPositionStays)
        transform->SetWorldPose(worldPos, worldRot, worldScale);
    else
        transform->UpdateTransform();
}

void GameObject::SetChild(GameObject* child)
{
	m_pChildGameObjects.push_back(child);
    GetTransform()->AddChild(child->GetComponent_SP<Transform>());
}

void GameObject::RemoveChild(GameObject* child)
{
    if (child == nullptr)
    {
        return;
    }
    auto it = std::find(m_pChildGameObjects.begin(), m_pChildGameObjects.end(), child);
    if (it != m_pChildGameObjects.end())
    {
        m_pChildGameObjects.erase(it);  
    }
}

void GameObject::Awake()
{
}

void GameObject::Start()
{
}

void GameObject::Update()
{
}

void GameObject::LateUpdate()
{
}

void GameObject::FixedUpdate()
{
}

void GameObject::LastUpdate()
{
    for (auto& action : m_Editor_LastUpdateActions)
    {
        action();
    }
    m_Editor_LastUpdateActions.clear(); 

    for (const auto& c : m_Components)
    {
        c->LastUpdate();
    }
}

void GameObject::OnDestroy()
{
    for (const auto& c : m_Components) 
    {
        c->OnDestroy();
    }
    m_Components.clear(); 
}

void GameObject::ApplyPendingComponents()
{
    if (m_ComponentsToAdd.empty())
        return;
    // 붙이는 동안 새로 대기열에 들어오는 것 (필요한 컴포넌트) 은 다음 번에
    vector<shared_ptr<Component>> list;
    list.swap(m_ComponentsToAdd);
    s_PendingComponents = (std::max)(0, s_PendingComponents - (int)list.size());
    for (const auto& component : list)
    {
        AddComponent(component);
    }
}

void GameObject::OnInspectorGUI()
{
    // Unity 스타일 GameObject 헤더: 아이콘 / 활성 / 이름 / Static / Tag / Layer
    {
        int layer = m_LayerIndex;
        const bool activeBefore = m_IsActive;
        UnityGUI::GameObjectHeader(&m_IsActive, &m_Name, &m_IsStatic, &m_Tag, &layer);
        if (m_IsActive != activeBefore || (uint8)layer != m_LayerIndex)
            Component::MarkPhysicsDirty();   // 체크박스 · 레이어가 값을 바로 고친다
        m_LayerIndex = (uint8)layer;
    }

    // Unity 의 프리팹 행: Prefab  [Open] [Select] [Overrides ▾]  (인스턴스 루트만)
    std::set<std::string> overriddenKeys;
    if (m_Prefab.IsValid())
        overriddenKeys = PrefabUtility::GetOverriddenComponentKeys(this);
    if (PrefabUtility::GetInstanceRoot(this) == this)
        UnityGUI::PrefabInstanceRow(this);

    // UI 오브젝트는 Unity 처럼 Transform 대신 Rect Transform 만 보인다 (Transform 은 RectTransform 이 매 프레임 계산)
    const bool hasRect = GetComponent<RectTransform>() != nullptr;
    for (auto it = m_Components.begin(); it != m_Components.end(); ++it)
    {
        if (hasRect && (*it).get() == static_cast<Component*>(m_pTransform))
            continue;
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 1));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("COMPONENT_DRAG")) 
            {
                Component* component = *(Component**)payload->Data; 
                int componentInstanceId = component->GetInstanceID();

                // 드래그된 컴포넌트를 현재 인덱스 위치로 이동
                if (componentInstanceId != (*it)->GetInstanceID())
                {
                    auto draggedIt = std::find_if(m_Components.begin(), m_Components.end(),
                        [componentInstanceId](const std::shared_ptr<Component>& comp)
                        {
                            return comp->GetInstanceID() == componentInstanceId;
                        });

                    if (draggedIt != m_Components.end())
                    {
                        int draggedIndex = std::distance(m_Components.begin(), draggedIt);
                        int targetIndex = std::distance(m_Components.begin(), it);

                        s_PendingWork = true;
                        m_Editor_LastUpdateActions.push_back([this, draggedIndex, targetIndex]()
                        {
                            auto draggedIt = m_Components.begin() + draggedIndex; 
                            auto targetIt = m_Components.begin() + targetIndex; 

                            // 컴포넌트를 타겟 위치에 삽입하고 기존 위치에서 제거
                            m_Components.insert(targetIt, *draggedIt);
                            // 삽입 후, 원래 위치의 요소를 제거해야 하므로, draggedIt 재계산 필요
                            m_Components.erase(m_Components.begin() + (draggedIndex > targetIndex ? draggedIndex + 1 : draggedIndex));
                        });
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        const float headerTop = ImGui::GetCursorScreenPos().y;
        (*it)->RenderInspectorGUI();

        // 이 컴포넌트에 프리팹 오버라이드가 있으면 헤더 왼쪽에 파란 막대 (Unity 와 같음)
        if (!overriddenKeys.empty())
        {
            std::string type = (*it)->toJson().value("type", std::string());
            int index = 0;
            for (auto p = m_Components.begin(); p != it; ++p)
                if ((*p)->toJson().value("type", std::string()) == type) ++index;
            if (overriddenKeys.count(type + "#" + std::to_string(index)))
            {
                const float x = ImGui::GetWindowPos().x + 1.0f;
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, headerTop), ImVec2(x + 2.0f, headerTop + 22.0f), IM_COL32(64, 150, 255, 255));
            }
        }
    }

    // 재질 Inspector (Unity 처럼 컴포넌트들 아래에 표시)
    if (MeshRenderer* meshRenderer = GetComponent<MeshRenderer>())
        meshRenderer->DrawMaterialInspectors();
    if (SkinnedMeshRenderer* skinned = GetComponent<SkinnedMeshRenderer>())
        skinned->DrawMaterialInspectors();

    EditorGUI::ComponentDivider();
    ImGui::Dummy(ImVec2(0, 18));
    // Unity 처럼 가운데 정렬된 넓은 Add Component 버튼
    {
        const float availX = ImGui::GetContentRegionAvail().x;
        const float btnW = availX * 0.7f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availX - btnW) * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::Button("Add Component", ImVec2(btnW, 26.0f)))
            AddComponentMenu::Open(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        // (개발/검증용) NOVA_DEV_ADDCOMP=1 | <카테고리> | ?<검색어> 이면 처음 한 번 팝업을 자동으로 연다
        static bool s_DevOpened = false;
        char preset[64] = {};
        if (!s_DevOpened && ImGui::GetFrameCount() > 120 && ::GetEnvironmentVariableA("NOVA_DEV_ADDCOMP", preset, sizeof(preset)) > 0)   // 시작 직후 포커스 변경으로 닫히지 않게 잠시 뒤에 연다
        {
            s_DevOpened = true;
            AddComponentMenu::Open(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            if (strcmp(preset, "1") != 0)
                AddComponentMenu::DevPreset(preset);
        }
        ImGui::PopStyleVar();
    }

    // Unity 의 Add Component 팝업 (검색 + 카테고리)
    AddComponentMenu::Draw(this, [this](std::shared_ptr<Component> c) { m_ComponentsToAdd.push_back(c); ++s_PendingComponents; });
}

void to_json(json& j, const GameObject& obj)
{
    j = json
    {
        { "name", obj.m_Name },
        { "fileID", obj.m_FileID },
        { "active", obj.m_IsActive },
        { "tag", obj.m_Tag },
        { "layer", (int)obj.m_LayerIndex },
        { "static", obj.m_IsStatic },
        { "components", json::array() },
        { "children", json::array() } 
    };

    for (const shared_ptr<Component>& component : obj.m_Components)
    {
        j["components"].push_back(component->toJson());
    }

    for (const GameObject* child : obj.m_pChildGameObjects)
    {
        if (child->m_HideAndDontSave)
            continue;   // 컴포넌트가 다시 만드는 오브젝트 (저장하지 않는다)
        json childJson;
        to_json(childJson, *child);
        j["children"].push_back(childJson);
    }

    // 프리팹 인스턴스: 연결 정보 + 에셋과 다른 속성 목록 (씬을 다시 읽을 때 에셋 값에 이것만 덮어쓴다)
    if (obj.m_Prefab.IsValid())
    {
        json overrides = json::array();
        for (const auto& o : PrefabUtility::ComputeOverrides(j, obj.m_Prefab))
            overrides.push_back(o);
        j["prefab"] = { { "asset", obj.m_Prefab.Asset }, { "source", obj.m_Prefab.Source }, { "root", obj.m_Prefab.Root }, { "overrides", overrides } };
    }
}

void from_json(const json& j, GameObject& obj)
{
    if (!j.is_object())
        throw std::runtime_error("scene game object must be an object");
    // 프리팹 인스턴스 루트: 현재 에셋 값 + 저장된 오버라이드로 다시 조립한 JSON 으로 만든다 (에셋 변경이 반영됨)
    if (PrefabUtility::NeedsMerge(j))
    {
        if (const json* premerged = PrefabUtility::Premerged(&j))   // 씬 불러오기가 병렬로 미리 합친 것
            from_json(*premerged, obj);
        else
            from_json(PrefabUtility::MergeWithAsset(j), obj);
        return;
    }
    if (j.contains("prefab") && j["prefab"].is_object())
    {
        obj.m_Prefab.Asset = j["prefab"].value("asset", std::string());
        obj.m_Prefab.Source = j["prefab"].value("source", (uint64)0);
        obj.m_Prefab.Root = j["prefab"].value("root", false);
        obj.m_Prefab.Revision = PrefabUtility::CurrentRevision(obj.m_Prefab.Asset);   // 방금 현재 에셋으로 조립됨
    }
    obj.m_Name = j.at("name").get<std::string>();
    if (j.contains("fileID") && j["fileID"].is_number_unsigned())
        obj.m_FileID = j["fileID"].get<uint64>();
    obj.m_IsActive = j.value("active", true);
    Component::MarkPhysicsDirty();
    obj.m_Tag = j.value("tag", std::string("Untagged"));
    obj.m_LayerIndex = (uint8)j.value("layer", 0);
    obj.m_IsStatic = j.value("static", false);

    if (!j.at("components").is_array())
        throw std::runtime_error("game object components must be an array");
    if (j.contains("children") && !j.at("children").is_array())
        throw std::runtime_error("game object children must be an array");

    // 컴포넌트 복원
    for (const auto& compJson : j.at("components"))
    {
        if (compJson.is_null())
        {
            continue;
        }

        std::string type = compJson.at("type").get<std::string>();
        shared_ptr<Component> component = nullptr;
        if (type == "Transform")
        {
            Component* pComponent = component.get();
            pComponent = obj.GetComponent<Transform>();
            pComponent->fromJson(compJson);
        }
        else if (type == "Rigidbody2D" && obj.GetComponent<Rigidbody2D>())
        {
            // 예전 JSON 에서 앞에 나온 Joint 가 이미 Rigidbody2D 를 붙였을 수 있다
            obj.GetComponent<Rigidbody2D>()->fromJson(compJson);
        }
        else
        {
            // ComponentFactory를 사용하여 컴포넌트 생성
            component = ComponentFactory::Instance().CreateComponent(type);
            if (component == nullptr && !type.empty() && type != "MissingComponent" && PackageManager::AddForComponent(type))
                component = ComponentFactory::Instance().CreateComponent(type);
            if (component != nullptr)
            {
                component->fromJson(compJson);
                obj.AddComponent(component);
            }
            else if (!type.empty() && type != "MissingComponent")
            {
                // 모르는 타입 (패키지를 넣지 않음 등): 데이터를 그대로 들고 있다가 다시 저장한다
                obj.AddComponent(std::make_shared<MissingComponent>(type, compJson));
            }
        }
    }

    // 이전 버전에서 저장된 씬: MeshRenderer 가 직접 가진 메시를 MeshFilter 로 옮긴다 (Unity 구조)
    if (MeshRenderer* mr = obj.GetComponent<MeshRenderer>())
    {
        if (mr->HasOwnMesh() && obj.GetComponent<MeshFilter>() == nullptr)
        {
            wstring path; int subset = 0;
            shared_ptr<Mesh> mesh = mr->TakeOwnMesh(path, subset);
            auto filter = std::make_shared<MeshFilter>();
            filter->SetMesh(mesh, path, subset);
            obj.AddComponent(filter);
            // Unity 순서: Transform, Mesh Filter, Mesh Renderer ...
            if (obj.m_Components.size() > 2)
                std::rotate(obj.m_Components.begin() + 1, obj.m_Components.end() - 1, obj.m_Components.end());
        }
    }

    if (j.contains("children"))
    {
        for (const auto& childJson : j.at("children"))
        {
            auto owned = std::make_unique<GameObject>();
            GameObject* child = owned.get();
            // 자식 복원 전에 트리에 넣어 씬의 실패 정리가 부분 복원된 자손까지 찾게 한다.
            child->SetParentImmediate(&obj); 
            obj.SetChild(child);
            owned.release();
            from_json(childJson, *child);
            child->GetComponent<Transform>()->SetParent(obj.GetComponent_SP<Transform>()); 
            child->GetComponent<Transform>()->UpdateTransform(); 
        }
    }
}
