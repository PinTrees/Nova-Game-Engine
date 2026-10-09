#include "pch.h"
#include "Component.h"
#include "EditorGUI.h"
#include "UnityGUI.h"

int Component::nextInstanceId = 0; 

uint32_t Component::s_BindingSerial = 1;
uint32_t Component::s_PhysicsSerial = 1;

void Component::MarkPhysicsDirty()
{
	++s_PhysicsSerial;
}

Component::Component()  
	: m_InstanceId(nextInstanceId++) 
	, m_pGameObject(nullptr)
	, m_InspectorOpened(true)
{ 
}

Component::~Component()
{
	MarkPhysicsDirty();   // 지운 컴포넌트 — 물리 동기화 · FixedUpdate 호출 목록이 다시 모은다 (지운 포인터를 들고 있지 않게)
}

void Component::RenderInspectorGUI()
{
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

	const string type = GetType();
	const char* icon = "component";
	if (type == "Transform") icon = "transform";
	else if (type == "Camera") icon = "camera";
	if (const char* custom = InspectorIconName()) icon = custom;

	string headerId = "ComponentHeader" + to_string(GetInstanceID());
	const std::string title = InspectorTitle();
	const bool enabledBefore = m_Enabled;
	UnityGUI::HeaderResult header = UnityGUI::ComponentHeader(headerId.c_str(), title.c_str(), icon,
		&m_InspectorOpened, HasEnabledToggle() ? &m_Enabled : nullptr, type != "Transform");
	if (m_Enabled != enabledBefore)
		MarkPhysicsDirty();   // 체크박스가 m_Enabled 를 바로 고친다

	if (header.action == UnityGUI::HeaderAction::Reset) { Reset(); MarkPhysicsDirty(); }
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
		// 물리 컴포넌트: 그리기 앞뒤 값이 다르면 (끌기 · 체크 · 입력) 물리 번호 — 그려지기만 할 때는 올리지 않는다 (선택해 둔 채 Play 해도 바뀐 것만 동기화)
		const bool physics = AffectsPhysics();
		const json before = physics ? toJson() : json();
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
		if (physics && toJson() != before)
			MarkPhysicsDirty();
		ImGui::PopID();
	}

	ImGui::PopStyleVar();
}
