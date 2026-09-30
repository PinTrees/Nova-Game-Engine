#include "pch.h"
#include "SceneHierachyEditorWindow.h"
#include <filesystem>
#include <algorithm>
#include "EditorGUI.h"
#include "EditorTheme.h"
#include "GameObjectFactory.h"

// Unity Hierarchy 창: 상단 [+ ▾] / 검색 행, 씬 이름 헤더 행, 그 아래 GameObject 트리
namespace
{
	constexpr float kRowHeight = 20.0f;
	constexpr float kIndent = 14.0f;

	std::string ToLowerCopy(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return s;
	}
}

SceneHierachyEditorWindow::SceneHierachyEditorWindow()
	: EditorWindow("Hierarchy", ICON_FA_LIST)
{
}

SceneHierachyEditorWindow::~SceneHierachyEditorWindow()
{
}

void SceneHierachyEditorWindow::DrawCreateMenu(Scene* scene, GameObject* parent)
{
	auto AddObj = [&](GameObject* obj)
	{
		if (parent != nullptr)
			obj->SetParent(parent);
		else
			scene->AddRootGameObject(obj);
		SelectionManager::SetSelectedGameObject(obj);
	};

	if (ImGui::MenuItem("Create Empty"))
		AddObj(GameObjectFactory::CreateEmpty());

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
		AddObj(GameObjectFactory::CreateCamera());
}

void SceneHierachyEditorWindow::DrawToolbar(Scene* scene)
{
	const float h = 26.0f;
	ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorTheme::Panel());
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 3.0f));
	ImGui::BeginChild("##HierToolbar", ImVec2(0, h), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
	ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::Button());
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::ButtonHover());
	ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Text());

	if (ImGui::Button(ICON_FA_PLUS "  " ICON_FA_CHEVRON_DOWN, ImVec2(44.0f, 20.0f)))
		ImGui::OpenPopup("HierarchyAddMenu");
	if (ImGui::BeginPopup("HierarchyAddMenu"))
	{
		DrawCreateMenu(scene, nullptr);
		ImGui::EndPopup();
	}

	ImGui::SameLine(0, 6.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, EditorTheme::Rgb(42, 42, 42));
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##HierSearch", ICON_FA_MAGNIFYING_GLASS "  All", m_Search, sizeof(m_Search));
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();

	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(2);

	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

void SceneHierachyEditorWindow::DrawSceneHeader(Scene* scene)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 p = ImGui::GetCursorScreenPos();
	const float w = ImGui::GetContentRegionAvail().x;
	const float h = kRowHeight + 2.0f;

	ImGui::PushID("##SceneHeader");
	if (ImGui::InvisibleButton("##header", ImVec2(w, h)))
		m_SceneOpen = !m_SceneOpen;
	ImGui::PopID();

	dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(EditorTheme::Rgb(66, 66, 66)));

	const float ty = p.y + (h - ImGui::GetFontSize()) * 0.5f;
	dl->AddText(ImVec2(p.x + 6.0f, ty), ImGui::GetColorU32(EditorTheme::TextDim()), m_SceneOpen ? ICON_FA_CHEVRON_DOWN : ICON_FA_CHEVRON_RIGHT);
	dl->AddText(ImVec2(p.x + 24.0f, ty), ImGui::GetColorU32(EditorTheme::Text()), ICON_FA_CUBES);

	std::string name = wstring_to_string(scene->GetName());
	if (name.empty()) name = "Untitled";
	dl->AddText(ImVec2(p.x + 44.0f, ty), ImGui::GetColorU32(EditorTheme::Rgb(230, 230, 230)), name.c_str());
}

void SceneHierachyEditorWindow::OnRender()
{
	Scene* currentScene = SceneManager::GetI()->GetCurrentScene();

	if (currentScene == nullptr)
		return;

	m_WindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	m_PendingDelete = nullptr;

	DrawToolbar(currentScene);
	DrawSceneHeader(currentScene);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	const std::string query = ToLowerCopy(m_Search);

	// 리스트 영역 (빈 곳 클릭 = 선택 해제, 빈 곳 우클릭 = 생성 메뉴)
	ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorTheme::Panel());
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::BeginChild("##HierList", ImVec2(0, 0), false);

	if (m_SceneOpen || !query.empty())
	{
		if (query.empty())
		{
			for (GameObject* g : currentScene->GetRootGameObjects())
				DrawGameObject(g, 1);
		}
		else
		{
			for (GameObject* g : currentScene->GetAllGameObjects())
			{
				if (ToLowerCopy(g->GetName()).find(query) != std::string::npos)
					DrawGameObject(g, 0);
			}
		}
	}

	// 남은 빈 영역: 드롭 대상 + 선택 해제 + 컨텍스트 메뉴
	ImVec2 rest = ImGui::GetContentRegionAvail();
	if (rest.y < 20.0f) rest.y = 20.0f;
	ImGui::InvisibleButton("##HierEmpty", ImVec2(rest.x, rest.y));
	if (ImGui::IsItemClicked())
		SelectionManager::SetSelectedGameObject(nullptr);

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
	if (ImGui::BeginPopupContextItem("##HierEmptyContext", ImGuiPopupFlags_MouseButtonRight))
	{
		DrawCreateMenu(currentScene, nullptr);
		ImGui::EndPopup();
	}

	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();

	if (m_PendingDelete != nullptr)
	{
		GameObject::Destroy(m_PendingDelete);
		SelectionManager::SetSelectedGameObject(nullptr);
		m_PendingDelete = nullptr;
	}
}

void SceneHierachyEditorWindow::DrawGameObject(GameObject* gameObject, int depth)
{
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	ImGui::PushID(gameObject->GetInstanceID());

	const bool isSelected = (SelectionManager::GetSelectedGameObject() == gameObject);
	const bool hasChild = gameObject->GetChildCount() > 0;
	ImDrawList* dl = ImGui::GetWindowDrawList();

	// 선택 행 색: 포커스 있으면 Unity 파랑, 없으면 회색
	const ImVec4 selColor = m_WindowFocused ? EditorTheme::Selection() : EditorTheme::SelectionIdle();
	ImGui::PushStyleColor(ImGuiCol_Header, selColor);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, isSelected ? selColor : EditorTheme::Rgb(70, 70, 70));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, selColor);

	ImVec2 p = ImGui::GetCursorScreenPos();
	const float w = ImGui::GetContentRegionAvail().x;

	ImGui::SetNextItemAllowOverlap();
	if (ImGui::Selectable("##row", isSelected, ImGuiSelectableFlags_AllowOverlap, ImVec2(w, kRowHeight)))
		SelectionManager::SetSelectedGameObject(gameObject);
	ImGui::PopStyleColor(3);

	// 컨텍스트 메뉴
	if (ImGui::BeginPopupContextItem("##ctx", ImGuiPopupFlags_MouseButtonRight))
	{
		DrawCreateMenu(scene, gameObject);
		ImGui::Separator();
		if (ImGui::MenuItem("Delete"))
			m_PendingDelete = gameObject;
		ImGui::EndPopup();
	}

	// 드래그 & 드롭
	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("GAME_OBJECT", &gameObject, sizeof(GameObject*));
		ImGui::Text("%s", gameObject->GetName().c_str());
		ImGui::EndDragDropSource();
	}
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
			if (droppedObject != gameObject)
				droppedObject->SetParent(gameObject);
		}
		ImGui::EndDragDropTarget();
	}

	// 폴드 화살표
	const float x0 = p.x + depth * kIndent;
	const float ty = p.y + (kRowHeight - ImGui::GetFontSize()) * 0.5f;
	bool open = gameObject->m_Editor_HierachOpened;
	if (hasChild)
	{
		ImGui::SetCursorScreenPos(ImVec2(x0 + 2.0f, p.y));
		ImGui::SetNextItemAllowOverlap();
		if (ImGui::InvisibleButton("##fold", ImVec2(16.0f, kRowHeight)))
			gameObject->m_Editor_HierachOpened = !gameObject->m_Editor_HierachOpened;
		dl->AddText(ImVec2(x0 + 5.0f, ty), ImGui::GetColorU32(EditorTheme::TextDim()), open ? ICON_FA_CHEVRON_DOWN : ICON_FA_CHEVRON_RIGHT);
	}

	// 아이콘 + 이름
	dl->AddText(ImVec2(x0 + 20.0f, ty), ImGui::GetColorU32(EditorTheme::Rgb(150, 190, 230)), ICON_FA_CUBE);
	dl->AddText(ImVec2(x0 + 40.0f, ty), ImGui::GetColorU32(EditorTheme::Rgb(225, 225, 225)), gameObject->GetName().c_str());

	// 다음 행 위치 복원
	ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + kRowHeight));

	if (hasChild && open)
	{
		for (GameObject* child : gameObject->GetChildren())
			DrawGameObject(child, depth + 1);
	}

	ImGui::PopID();
}

void SceneHierachyEditorWindow::PopupContextMenu()
{
	Scene* currentScene = SceneManager::GetI()->GetCurrentScene();

	if (currentScene == nullptr)
		return;

	if (ImGui::BeginPopupContextWindow("##SceneHierarchyContextMenu"))
	{
		DrawCreateMenu(currentScene, nullptr);
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
