#include "pch.h"
#include "SceneHierachyEditorWindow.h"
#include <filesystem>
#include "EditorGUI.h"
#include "GameObjectFactory.h"

SceneHierachyEditorWindow::SceneHierachyEditorWindow()
	: EditorWindow("Hierachy", ICON_FA_LIST)
{
}

SceneHierachyEditorWindow::~SceneHierachyEditorWindow()
{
}

void SceneHierachyEditorWindow::OnRender()
{
	Scene* currentScene = SceneManager::GetI()->GetCurrentScene();

	if (currentScene == nullptr)
		return;

	// Top Tab Bar
	ImGui::BeginChild("##FixedTopBar", ImVec2(0, 30), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (EditorGUI::IconButton_FA(ICON_FA_PLUS, EditorGUI::DefaultTextStyle, ImVec2(30, 30), 30))
	{
		ImGui::OpenPopup("HierarchyAddMenu");
	}
	EditorGUI::Divider(Color(0.2f, 0.2f, 0.2f, 1.0f), 4.0f);

	auto RenderCreateMenu = [&](GameObject* parent = nullptr) {
		auto AddObj = [&](GameObject* obj) {
			if (parent != nullptr) {
				obj->SetParent(parent);
			} else {
				currentScene->AddRootGameObject(obj);
			}
			SelectionManager::SetSelectedGameObject(obj);
		};

		if (ImGui::MenuItem("Create Empty"))
		{
			AddObj(GameObjectFactory::CreateEmpty());
		}

		if (ImGui::BeginMenu("3D Object"))
		{
			if (ImGui::MenuItem("Cube")) AddObj(GameObjectFactory::CreateCube());
			if (ImGui::MenuItem("Sphere")) AddObj(GameObjectFactory::CreateSphere());
			if (ImGui::MenuItem("Cylinder")) AddObj(GameObjectFactory::CreateCylinder());
			if (ImGui::MenuItem("Plane")) AddObj(GameObjectFactory::CreatePlane());
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Light"))
		{
			if (ImGui::MenuItem("Directional Light")) AddObj(GameObjectFactory::CreateDirectionalLight());
			if (ImGui::MenuItem("Point Light")) AddObj(GameObjectFactory::CreatePointLight());
			if (ImGui::MenuItem("Spot Light")) AddObj(GameObjectFactory::CreateSpotLight());
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Camera"))
		{
			AddObj(GameObjectFactory::CreateCamera());
		}
	};

	if (ImGui::BeginPopup("HierarchyAddMenu"))
	{
		RenderCreateMenu(nullptr);
		ImGui::EndPopup();
	}

	ImGui::EndChild();

	// Top tab bar - Context menu (right click)
	if (ImGui::BeginPopupContextItem("##TopBarContextMenu", ImGuiMouseButton_Right))
	{
		RenderCreateMenu(nullptr);
		ImGui::EndPopup();
	}

	// Root GameObject UI
	ImGui::BeginChild("##RootGameObjectRegion", ImVec2(0, 0), true);
	for (const auto& g : currentScene->GetRootGameObjects()) {
		DrawGameObject(g); 
	}
	ImGui::EndChild();

	// 남은 영역을 드래그 앤 드롭 및 우클릭 컨텍스트 메뉴로 설정
	ImGui::BeginChild("##SceneHierachyDropContainer", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse); 
 
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FBX_FILE"))
		{
			const char* filePath = static_cast<const char*>(payload->Data);
			HandleFbxFileDrop(std::string(filePath), nullptr);
		}

		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAME_OBJECT"))
		{
			GameObject* droppedObject = *(GameObject**)payload->Data;
			droppedObject->SetParent(nullptr);
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::EndChild();
}


void SceneHierachyEditorWindow::DrawGameObject(GameObject* gameObject)
{
	ImGui::PushID(gameObject->GetInstanceID()); 
	bool isSelected = (SelectionManager::GetSelectedGameObject() == gameObject);

	ImGui::PushStyleColor(ImGuiCol_Button, isSelected ? ImVec4(0.172f, 0.36f, 0.529f, 1.0f) : EDITOR_GUI_COLOR_TRANSPARENT);     // 기본 버튼 색상 
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isSelected ? ImVec4(0.172f, 0.36f, 0.529f, 1.0f) : EDITOR_GUI_COLOR_BUTTON_HOBER);			// 호버 시 색상 
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.172f, 0.36f, 0.529f, 1.0f));				// 클릭 시 색상 
	ImGui::PushStyleColor(ImGuiCol_Text, EDITOR_GUI_COLOR_LABEL);									// 텍스트 색상
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

	ImVec2 availableSize = ImGui::GetContentRegionAvail();
	string gamobject_entry_id = "##" + gameObject->GetName() + to_string(gameObject->GetInstanceID());
	ImGui::SetCursorPosX(26);
	if (ImGui::Button(gamobject_entry_id.c_str(), ImVec2(availableSize.x, 26))) {
		SelectionManager::SetSelectedGameObject(gameObject);
	}

	// Context menu
	if (ImGui::BeginPopupContextItem(gamobject_entry_id.c_str()))
	{
		auto AddChildObj = [&](GameObject* child) {
			child->SetParent(gameObject);
			SelectionManager::SetSelectedGameObject(child);
		};

		if (ImGui::MenuItem("Create Empty (Child)"))
		{
			AddChildObj(GameObjectFactory::CreateEmpty());
		}

		if (ImGui::BeginMenu("3D Object"))
		{
			if (ImGui::MenuItem("Cube")) AddChildObj(GameObjectFactory::CreateCube());
			if (ImGui::MenuItem("Sphere")) AddChildObj(GameObjectFactory::CreateSphere());
			if (ImGui::MenuItem("Cylinder")) AddChildObj(GameObjectFactory::CreateCylinder());
			if (ImGui::MenuItem("Plane")) AddChildObj(GameObjectFactory::CreatePlane());
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Light"))
		{
			if (ImGui::MenuItem("Directional Light")) AddChildObj(GameObjectFactory::CreateDirectionalLight());
			if (ImGui::MenuItem("Point Light")) AddChildObj(GameObjectFactory::CreatePointLight());
			if (ImGui::MenuItem("Spot Light")) AddChildObj(GameObjectFactory::CreateSpotLight());
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Camera"))
		{
			AddChildObj(GameObjectFactory::CreateCamera());
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Delete GameObject"))
		{
			GameObject::Destroy(gameObject);
			SelectionManager::SetSelectedGameObject(nullptr);
		}

		ImGui::EndPopup();
	}

	// Drag GameObject
	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("GAME_OBJECT", &gameObject, sizeof(GameObject*));
		ImGui::Text("Dragging %s", gameObject->GetName().c_str());
		ImGui::EndDragDropSource();
	}

	// Drop In This GameObject
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FBX_FILE"))
		{
			const char* filePath = static_cast<const char*>(payload->Data);
			HandleFbxFileDrop(std::string(filePath), gameObject);
		}

		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAME_OBJECT"))
		{
			GameObject* droppedObject = *(GameObject**)payload->Data;
			droppedObject->SetParent(gameObject);
		}
		ImGui::EndDragDropTarget();
	}

	bool hasChild = gameObject->GetChildCount() > 0; 
	if (hasChild)
	{
		// Open Hierachy -----
		bool hierachy_opened = gameObject->m_Editor_HierachOpened;
		ImGui::SameLine();
		ImGui::SetCursorPosX(0);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY());
		if (ImGui::Button("##HierachyOpenButton", ImVec2(26, 26))) {
			gameObject->m_Editor_HierachOpened = !gameObject->m_Editor_HierachOpened;
		}
		ImGui::SameLine();
		ImGui::SetCursorPosX(8);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (hierachy_opened ? 6 : 0));
		EditorGUI::Label(hierachy_opened ? ICON_FA_SORT_DOWN : ICON_FA_CARET_RIGHT, EditorGUI::DefaultTextStyle, EDITOR_GUI_COLOR_LABEL_D);
		// --------------------
	}

	ImGui::SameLine();
	ImGui::SetCursorPosX(32);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
	EditorGUI::Image(L"\\ProjectSetting\\icons\\icon_gameobject_1.png", ImVec2(22, 26));

	ImGui::SameLine();
	ImGui::SetCursorPosX(60);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
	EditorGUI::Label(gameObject->GetName(), EditorGUI::DefaultTextStyle, EDITOR_GUI_COLOR_LABEL); 

	if (gameObject->m_Editor_HierachOpened) {
		std::vector<GameObject*> children = gameObject->GetChildren();
		ImGui::Indent(18.0f);
		ImGui::BeginChild("##ChildRegion", ImVec2(0, 0), false); 
		for (const auto& g : children) { 
			DrawGameObject(g);   
		}
		ImGui::EndChild();
	}
	
	ImGui::PopStyleVar(1);  
	ImGui::PopStyleColor(4);
	ImGui::PopID(); // ImGui ID 스택에서 팝
}

void SceneHierachyEditorWindow::PopupContextMenu()
{
	Scene* currentScene = SceneManager::GetI()->GetCurrentScene();

	if (currentScene == nullptr)
		return;

	if (ImGui::BeginPopupContextWindow("##SceneHierarchyContextMenu"))
	{
		if (ImGui::MenuItem("Add Camera"))
		{
			// 카메라 추가 로직
			GameObject* newCamera = new GameObject("Camera");
			currentScene->AddRootGameObject(newCamera);
			//newCamera->AddComponent<CameraComponent>();
			//currentScene->AddGameObject(newCamera);
		}
		if (ImGui::MenuItem("Add Directinal Light"))
		{
			// 라이트 추가 로직
			GameObject* newLight = new GameObject("Directinal Light");
			currentScene->AddRootGameObject(newLight);
		}
		if (ImGui::MenuItem("Add Empty GameObject"))
		{
			// 빈 게임 오브젝트 추가 로직
			GameObject* newEmpty = new GameObject("GameObject");
			currentScene->AddRootGameObject(newEmpty);
		}
		ImGui::EndPopup();
	}
}

void SceneHierachyEditorWindow::HandleFbxFileDrop(const std::string& filePath, GameObject* parent)
{
	// 파일 경로 유효성 검사
	std::filesystem::path path(filePath);
	if (!std::filesystem::exists(path))
	{
		std::cerr << "File does not exist: " << filePath << std::endl;
		return;
	}

	// 파일 이름 추출 (확장자 제외)
	std::string fileName = path.stem().string();

	GameObject* newGameObject = new GameObject(fileName);

	Scene* currentScene = SceneManager::GetI()->GetCurrentScene();
	if (currentScene != nullptr)
	{
		if (parent == nullptr)
		{
			currentScene->AddRootGameObject(newGameObject);
		}
		else
		{
			newGameObject->SetParent(parent);
		}
	}
}
