#include "pch.h"
#include "Component.h"
#include "EditorGUI.h"

int Component::nextInstanceId = 0; 

Component::Component()  
	: m_InstanceId(nextInstanceId++) 
	, m_pGameObject(nullptr)
	, m_InspectorOpened(true)
{ 
}

Component::~Component()
{
}

void Component::RenderInspectorGUI()
{
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

	EditorGUI::ComponentDivider();

	string componentHeader_id = "ComponentHeader" + to_string(GetInstanceID());
	ImGui::PushID(componentHeader_id.c_str());

	bool isOpened = false;
	if(m_InspectorIconPath != L"")
		isOpened = EditorGUI::ComponentHeader(m_InspectorTitleName, L"ProjectSetting\\icons\\component\\" + m_InspectorIconPath, m_InspectorOpened);
	else isOpened = EditorGUI::ComponentHeader(m_InspectorTitleName, L"ProjectSetting\\icons\\icon_camera.png", m_InspectorOpened);

	// ComponentHeader context menu
	if (ImGui::BeginPopupContextItem("ComponentContextMenu", ImGuiMouseButton_Right))
	{
		if (ImGui::MenuItem("Reset Component")) Reset();
		if (ImGui::MenuItem("Remove Component")) GameObject::Destroy(this);
		ImGui::EndPopup();
	}

	ImGui::PopID(); 

	// Drag this component
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		// 컴포넌트의 주소를 페이로드에 넣음
		Component* component = this; 
		ImGui::SetDragDropPayload("COMPONENT_DRAG", &component, sizeof(Component*)); 
		ImGui::Text("Dragging: %s", m_InspectorTitleName.c_str());  
		ImGui::EndDragDropSource();
	}

	EditorGUI::Divider(Color(0.18f, 0.18f, 0.18f, 1.0f), 2.0f);

	EditorGUI::Label("Instance ID: " + to_string(m_InstanceId)); 

	if (isOpened)
	{
		ImGui::PushID(m_InstanceId + GetInstanceID());
		EditorGUI::ComponentBlockStylePush();
		EditorGUI::Spacing(Vec2(0, 4));

		OnInspectorGUI();

		EditorGUI::Spacing(Vec2(0, 4));
		EditorGUI::ComponentBlockStylePop();
		ImGui::PopID();
	}

	ImGui::PopStyleVar();
}
