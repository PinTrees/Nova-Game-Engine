#include "pch.h"
#include "Component.h"
#include "EditorGUI.h"
#include "UnityGUI.h"

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

	const string type = GetType();
	const char* icon = "component";
	if (type == "Transform") icon = "transform";
	else if (type == "Camera") icon = "camera";

	string headerId = "ComponentHeader" + to_string(GetInstanceID());
	UnityGUI::HeaderResult header = UnityGUI::ComponentHeader(headerId.c_str(), m_InspectorTitleName.c_str(), icon,
		&m_InspectorOpened, HasEnabledToggle() ? &m_Enabled : nullptr, type != "Transform");

	if (header.action == UnityGUI::HeaderAction::Reset) Reset();
	if (header.action == UnityGUI::HeaderAction::Remove) GameObject::Destroy(this);

	// Drag this component (순서 변경)
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		Component* component = this;
		ImGui::SetDragDropPayload("COMPONENT_DRAG", &component, sizeof(Component*));
		ImGui::Text("%s", m_InspectorTitleName.c_str());
		ImGui::EndDragDropSource();
	}

	if (header.open)
	{
		ImGui::PushID(m_InstanceId + GetInstanceID());
		if (UsesUnityInspector())
		{
			UnityGUI::Spacing(4);
			OnInspectorGUI();
			UnityGUI::Spacing(4);
		}
		else
		{
			EditorGUI::ComponentBlockStylePush();
			EditorGUI::Spacing(Vec2(0, 4));
			OnInspectorGUI();
			EditorGUI::Spacing(Vec2(0, 4));
			EditorGUI::ComponentBlockStylePop();
		}
		ImGui::PopID();
	}

	ImGui::PopStyleVar();
}
