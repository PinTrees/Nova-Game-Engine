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
    if (nullptr == parent) // rootGameObject��
    {
        if (nullptr == m_pParentGameObject)
        {
            // �̹� rootGameObject�̹Ƿ� ó��X
        }
        else
        {
            // �θ������Ʈ���� RemoveChild(this) �� �ڱ� �ڽ� rootGameObject�� �߰�
            m_pParentGameObject->RemoveChild(this);
            m_pParentGameObject->GetTransform()->RemoveChild(this->GetComponent_SP<Transform>());

            m_pParentGameObject = nullptr;
            GetTransform()->SetParent(nullptr);

            SceneManager::GetI()->GetCurrentScene()->AddRootGameObject(this);
        }
    }
    else // �ٸ� GameObject�� �ڽ�����
    {
        if (nullptr == m_pParentGameObject)
        {
            // rootGameObject���� ������Ʈ���� ����
            SceneManager::GetI()->GetCurrentScene()->RemoveRootGameObjects(this);
        }
        else
        {
            // ���� parent���� �ڽ� ����
            m_pParentGameObject->RemoveChild(this);
            m_pParentGameObject->GetTransform()->RemoveChild(this->GetComponent_SP<Transform>());
        }
        // parent ���� �� parent�� �ڽ����� �߰�
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
    // ���� ������Ʈ �̸� ���� ��ǲ �ʵ�
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Dummy(ImVec2(2, 0));
    ImGui::SameLine();
    EditorGUI::Checkbox(m_IsActive);
    ImGui::SameLine();
    if (EditorGUI::InputField(m_Name))
    {
        // �̸� ���� �� �ʿ��� �߰� �۾��� ������ ���⿡ �߰�
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

                // �巡�׵� ������Ʈ�� ���� �ε��� ��ġ�� �̵�
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

                            // ������Ʈ�� Ÿ�� ��ġ�� �����ϰ� ���� ��ġ���� ����
                            m_Components.insert(targetIt, *draggedIt);
                            // ���� ��, ���� ��ġ�� ��Ҹ� �����ؾ� �ϹǷ�, draggedIt ���� �ʿ�
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
    if (EditorGUI::Button("Add Component")) 
    {
        ImGui::OpenPopup("add_component_popup");
    }
    if (ImGui::Button("Add Component"))
    {
        ImGui::OpenPopup("add_component_popup");
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

    // ������Ʈ ����
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
            // ComponentFactory�� ����Ͽ� ������Ʈ ����
            component = ComponentFactory::Instance().CreateComponent(type);
            component->fromJson(compJson);
            obj.AddComponent(component);
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