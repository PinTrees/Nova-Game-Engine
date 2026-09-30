#include "pch.h"
#include "GameObject.h"
#include "Transform.h"
#include "EditorGUI.h"

atomic<uint64> GameObject::g_NextInstanceID = 0;

GameObject::GameObject()
	: m_InstanceID(g_NextInstanceID++)
	, m_LayerIndex(0)
	, m_pParentGameObject(nullptr)
    , m_IsActive(true)
    , m_Editor_HierachOpened(false)
{
    m_pTransform = AddComponent<Transform>();
}

GameObject::GameObject(const string& name)
	: m_Name(name)
	, m_InstanceID(g_NextInstanceID++)
	, m_LayerIndex(0)
	, m_pParentGameObject(nullptr)
    , m_IsActive(true)
    , m_Editor_HierachOpened(false)
{
    m_pTransform = AddComponent<Transform>();
}

GameObject::~GameObject()
{
    m_Components.clear();
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
        const auto& scene = SceneManager::GetI()->GetCurrentScene(); 
        scene->DestroyGameObject(gameobject_ptr); 
    }); 
}

void GameObject::SetParent(GameObject* parent)
{
    if (nullptr == parent) // rootGameObject로
    {
        if (nullptr == m_pParentGameObject)
        {
            // 이미 rootGameObject이므로 처리X
        }
        else
        {
            // 부모오브젝트에서 RemoveChild(this) 후 자기 자신 rootGameObject로 추가
            m_pParentGameObject->RemoveChild(this);
            m_pParentGameObject->GetTransform()->RemoveChild(this->GetComponent_SP<Transform>());

            m_pParentGameObject = nullptr;
            GetTransform()->SetParent(nullptr);

            SceneManager::GetI()->GetCurrentScene()->AddRootGameObject(this);
        }
    }
    else // 다른 GameObject의 자식으로
    {
        if (nullptr == m_pParentGameObject)
        {
            // rootGameObject였던 오브젝트에서 제거
            SceneManager::GetI()->GetCurrentScene()->RemoveRootGameObjects(this);
        }
        else
        {
            // 원래 parent에서 자식 제거
            m_pParentGameObject->RemoveChild(this);
            m_pParentGameObject->GetTransform()->RemoveChild(this->GetComponent_SP<Transform>());
        }
        // parent 변경 후 parent의 자식으로 추가
        m_pParentGameObject = parent;
        GetTransform()->SetParent(parent->GetComponent_SP<Transform>());

        parent->SetChild(this);
        parent->GetTransform()->AddChild(this->GetComponent_SP<Transform>());
    }

    GetTransform()->UpdateTransform();
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
    for (const auto& component : m_ComponentsToAdd)
    {
        AddComponent(component);
    }
    m_ComponentsToAdd.clear();
}

void GameObject::OnInspectorGUI()
{
    // 게임 오브젝트 이름 변경 인풋 필드
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Dummy(ImVec2(2, 0));
    ImGui::SameLine();
    EditorGUI::Checkbox(m_IsActive);
    ImGui::SameLine();
    if (EditorGUI::InputField(m_Name))
    {
        // 이름 변경 시 필요한 추가 작업이 있으면 여기에 추가
    }

    ImGui::Dummy(ImVec2(0, 4));
    for (auto it = m_Components.begin(); it != m_Components.end(); ++it)
    {
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 6));
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

        (*it)->RenderInspectorGUI();
    }

    EditorGUI::ComponentDivider(); 
    ImGui::Dummy(ImVec2(0, 18)); 
    // Unity 처럼 가운데 정렬된 넓은 Add Component 버튼
    {
        const float availX = ImGui::GetContentRegionAvail().x;
        const float btnW = availX * 0.7f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availX - btnW) * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::Button("Add Component", ImVec2(btnW, 26.0f)))
        {
            ImGui::OpenPopup("add_component_popup");
        }
        ImGui::PopStyleVar();
    }

    if (ImGui::BeginPopup("add_component_popup"))
    {
        std::vector<std::string> componentTypes = ComponentFactory::Instance().GetComponentTypes();
        for (const auto& type : componentTypes)
        {
            if (ImGui::MenuItem(type.c_str()))
            {
                auto newComponent = ComponentFactory::Instance().CreateComponent(type);
                if (newComponent)
                {
                    m_ComponentsToAdd.push_back(newComponent);
                }
            }
        }
        ImGui::EndPopup();
    }
}

void to_json(json& j, const GameObject& obj)
{
    j = json
    {
        { "name", obj.m_Name },
        { "components", json::array() },
        { "children", json::array() } 
    };

    for (const shared_ptr<Component>& component : obj.m_Components)
    {
        j["components"].push_back(component->toJson());
    }

    for (const GameObject* child : obj.m_pChildGameObjects)
    {
        json childJson;
        to_json(childJson, *child);  
        j["children"].push_back(childJson);  
    }
}

void from_json(const json& j, GameObject& obj)
{
    obj.m_Name = j.at("name").get<std::string>();

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
        else
        {
            // ComponentFactory를 사용하여 컴포넌트 생성
            component = ComponentFactory::Instance().CreateComponent(type);
            if (component != nullptr)
            {
                component->fromJson(compJson);
                obj.AddComponent(component);
            }
            // 알 수 없거나 등록되지 않은 컴포넌트는 건너뛰어 크래시 방지
        }
    }

    if (j.contains("children"))
    {
        for (const auto& childJson : j.at("children"))
        {
            GameObject* child = new GameObject;
            from_json(childJson, *child); 
            child->SetParentImmediate(&obj); 
            child->GetComponent<Transform>()->SetParent(obj.GetComponent_SP<Transform>()); 
            child->GetComponent<Transform>()->UpdateTransform(); 
            obj.SetChild(child); 
        }
    }
}