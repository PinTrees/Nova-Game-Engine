#include "pch.h"
#include "Debug.h"
#include "ScriptEngine.h"
#include "CSharpScript.h"
#include "UndoSystem.h"
#include "SceneHierachyEditorWindow.h"
#include <filesystem>
#include <algorithm>
#include "EditorGUI.h"
#include "EditorTheme.h"
#include "GameObjectFactory.h"
#include "GameObjectMenu.h"
#include "UnityGUI.h"

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

namespace
{
	// 클립보드: Copy/Cut 한 GameObject(하위 포함)를 JSON 으로 보관한다.
	std::string s_ClipJson;
	uint64_t s_CutSourceId = 0;

	void CollectAll(GameObject* g, std::vector<GameObject*>& out)
	{
		out.push_back(g);
		for (GameObject* c : g->GetChildren())
			CollectAll(c, out);
	}

	GameObject* FindById(Scene* scene, uint64_t id)
	{
		std::vector<GameObject*> all;
		for (GameObject* root : scene->GetRootGameObjects())
			CollectAll(root, all);
		for (GameObject* g : all)
			if (g->GetInstanceID() == id) return g;
		return nullptr;
	}

	// "Cube" -> "Cube (1)", "Cube (1)" -> "Cube (2)" (같은 부모 아래에서 겹치지 않게)
	std::string UniqueCopyName(Scene* scene, GameObject* source)
	{
		std::string base = source->GetName();
		size_t p = base.rfind(" (");
		if (p != std::string::npos && !base.empty() && base.back() == ')')
			base = base.substr(0, p);

		std::vector<GameObject*> siblings;
		if (source->GetParent())
			siblings = source->GetParent()->GetChildren();
		else
			siblings = scene->GetRootGameObjects();

		for (int n = 1;; ++n)
		{
			std::string candidate = base + " (" + std::to_string(n) + ")";
			bool used = false;
			for (GameObject* s : siblings)
				if (s->GetName() == candidate) { used = true; break; }
			if (!used) return candidate;
		}
	}
}

void SceneHierachyEditorWindow::CopyObject(GameObject* target, bool cut)
{
	if (target == nullptr)
		return;
	json j = *target;
	s_ClipJson = j.dump();
	s_CutSourceId = cut ? target->GetInstanceID() : 0;
}

GameObject* SceneHierachyEditorWindow::PasteObject(Scene* scene, GameObject* parent)
{
	if (s_ClipJson.empty() || scene == nullptr)
		return nullptr;

	json j = json::parse(s_ClipJson, nullptr, false);
	if (j.is_discarded())
		return nullptr;

	GameObject* g = new GameObject();
	from_json(j, *g);
	if (s_CutSourceId == 0)
		g->RegenerateFileIDs();   // 복사 붙여넣기는 새 오브젝트 (잘라내기는 같은 오브젝트가 옮겨 간다)
	scene->AddRootGameObject(g);
	if (parent != nullptr)
		g->SetParent(parent, false);

	// Cut 은 붙여넣는 순간 원본을 제거한다
	if (s_CutSourceId != 0)
	{
		GameObject* source = FindById(scene, s_CutSourceId);
		if (source != nullptr && source != g)
			GameObject::Destroy(source);
		s_CutSourceId = 0;
		s_ClipJson.clear();
	}
	SelectionManager::SetSelectedGameObject(g);
	return g;
}

GameObject* SceneHierachyEditorWindow::DuplicateObject(Scene* scene, GameObject* target)
{
	if (target == nullptr || scene == nullptr)
		return nullptr;

	json j = *target;
	GameObject* g = new GameObject();
	from_json(j, *g);
	g->RegenerateFileIDs();
	g->SetName(UniqueCopyName(scene, target));
	scene->AddRootGameObject(g);
	if (target->GetParent() != nullptr)
		g->SetParent(target->GetParent(), false);   // 원본과 같은 로컬 값 → 같은 자리
	SelectionManager::SetSelectedGameObject(g);
	return g;
}

void SceneHierachyEditorWindow::BeginRename(GameObject* target)
{
	if (target == nullptr)
		return;
	m_RenameTarget = target;
	m_RenameFrames = 0;
	strncpy_s(m_RenameBuffer, target->GetName().c_str(), _TRUNCATE);
}

// Unity Hierarchy 컨텍스트 메뉴 (target: 우클릭한 오브젝트, 빈 곳이면 nullptr)
void SceneHierachyEditorWindow::DrawContextMenu(Scene* scene, GameObject* target)
{
	const bool hasTarget = (target != nullptr);
	const bool hasClipboard = !s_ClipJson.empty();

	if (ImGui::MenuItem("Cut", "Ctrl+X", false, hasTarget)) CopyObject(target, true);
	if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasTarget)) CopyObject(target, false);
	if (ImGui::MenuItem("Paste", "Ctrl+V", false, hasClipboard)) PasteObject(scene, target);
	ImGui::MenuItem("Paste Special", nullptr, false, false);
	if (ImGui::MenuItem("Rename", nullptr, false, hasTarget)) BeginRename(target);
	if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasTarget)) DuplicateObject(scene, target);
	if (ImGui::MenuItem("Delete", "Del", false, hasTarget)) m_PendingDelete = target;

	GameObject* prefabRoot = hasTarget ? PrefabUtility::GetInstanceRoot(target) : nullptr;
	ImGui::Separator();
	if (ImGui::BeginMenu("Prefab", prefabRoot != nullptr))
	{
		if (ImGui::MenuItem("Select Asset"))
			SelectionManager::SetSelectedFile(PathManager::GetI()->GetMovePathW(string_to_wstring(prefabRoot->GetPrefabLink().Asset)));
		if (ImGui::MenuItem("Unpack Completely"))
		{
			Undo::SetActionName("Unpack Prefab");
			PrefabUtility::UnpackCompletely(prefabRoot);
		}
		ImGui::EndMenu();
	}

	ImGui::Separator();
	ImGui::MenuItem("Select All", nullptr, false, false);
	if (ImGui::MenuItem("Deselect All", nullptr, false, SelectionManager::GetSelectedGameObject() != nullptr))
		SelectionManager::SetSelectedGameObject(nullptr);
	ImGui::MenuItem("Invert Selection", nullptr, false, false);
	ImGui::MenuItem("Select Children", nullptr, false, false);

	ImGui::Separator();
	ImGui::MenuItem("Find References in Scene", nullptr, false, false);

	ImGui::Separator();
	ImGui::MenuItem("Set as Default Parent", nullptr, false, false);

	ImGui::Separator();
	GameObjectMenu::DrawCreateItems(scene, target);
}

// Hierarchy 에 포커스가 있을 때의 단축키
void SceneHierachyEditorWindow::HandleShortcuts(Scene* scene)
{
	ImGuiIO& io = ImGui::GetIO();
	if (!m_WindowFocused || io.WantTextInput || m_RenameTarget != nullptr)
		return;

	GameObject* selected = SelectionManager::GetSelectedGameObject();
	if (io.KeyCtrl && !io.KeyShift)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_C, false)) CopyObject(selected, false);
		if (ImGui::IsKeyPressed(ImGuiKey_X, false)) CopyObject(selected, true);
		if (ImGui::IsKeyPressed(ImGuiKey_V, false)) PasteObject(scene, selected);
		if (ImGui::IsKeyPressed(ImGuiKey_D, false)) DuplicateObject(scene, selected);
	}
	if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N, false))
	{
		GameObject* g = GameObjectFactory::CreateEmpty();
		scene->AddRootGameObject(g);
		SelectionManager::SetSelectedGameObject(g);
	}
	if (selected != nullptr)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) m_PendingDelete = selected;
		if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) BeginRename(selected);
	}
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
	GameObjectMenu::PushContextStyle();
	GameObjectMenu::SetMenuWidth(190.0f);
	if (ImGui::BeginPopup("HierarchyAddMenu"))
	{
		GameObjectMenu::DrawCreateItems(scene, nullptr);
		ImGui::EndPopup();
	}
	GameObjectMenu::PopContextStyle();

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

// Unity Hierarchy 의 씬 행: [▼] [씬 아이콘] 씬 파일 이름(굵게, 저장 안 된 변경이 있으면 *)  ...  [⋮]
// 우클릭 또는 ⋮ 버튼으로 씬 메뉴 (Save Scene, Save Scene As, Discard changes, GameObject 생성 ...)
void SceneHierachyEditorWindow::DrawSceneHeader(Scene* scene)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 p = ImGui::GetCursorScreenPos();
	const float w = ImGui::GetContentRegionAvail().x;
	const float h = kRowHeight + 2.0f;

	ImGui::PushID("##SceneHeader");
	ImGui::InvisibleButton("##header", ImVec2(w, h), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	const bool hovered = ImGui::IsItemHovered();
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	const bool onKebab = mouse.x >= p.x + w - 22.0f;
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		if (onKebab)
			ImGui::OpenPopup("##SceneMenu");
		else
			m_SceneOpen = !m_SceneOpen;
	}
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		ImGui::OpenPopup("##SceneMenu");

	dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(hovered ? EditorTheme::Rgb(70, 70, 70) : EditorTheme::Rgb(62, 62, 62)));
	dl->AddLine(ImVec2(p.x, p.y + h - 0.5f), ImVec2(p.x + w, p.y + h - 0.5f), ImGui::GetColorU32(EditorTheme::Rgb(36, 36, 36)));

	const float iconY = floorf(p.y + (h - 16.0f) * 0.5f);
	UnityGUI::DrawIcon(dl, m_SceneOpen ? "arrow_down" : "arrow_right", ImVec2(p.x + 4.0f, iconY), 16.0f);
	UnityGUI::DrawIcon(dl, "scene", ImVec2(p.x + 20.0f, iconY), 16.0f);

	// 씬 파일 이름 (예: SampleScene), 저장하지 않은 변경이 있으면 * (Unity 와 동일)
	std::string name = wstring_to_string(scene->GetName());
	if (name.empty()) name = "Untitled";
	if (SceneManager::GetI()->IsCurrentSceneDirty())
		name += "*";
	ImFont* bold = UnityGUI::BoldFont();
	const float fs = bold->FontSize;
	dl->AddText(bold, fs, ImVec2(p.x + 40.0f, floorf(p.y + (h - fs) * 0.5f + 0.5f)), ImGui::GetColorU32(EditorTheme::Rgb(230, 230, 230)), name.c_str());
	UnityGUI::DrawIcon(dl, "kebab", ImVec2(p.x + w - 20.0f, iconY), 16.0f, onKebab && hovered ? IM_COL32_WHITE : IM_COL32(200, 200, 200, 255));

	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_DelayNormal) && !scene->GetScenePath().empty())
		ImGui::SetTooltip("%s", wstring_to_string(scene->GetScenePath()).c_str());

	// 씬 메뉴 (Unity 의 씬 헤더 컨텍스트 메뉴)
	const bool playing = Application::IsPlaying();
	const bool dirty = SceneManager::GetI()->IsCurrentSceneDirty();
	GameObjectMenu::PushContextStyle();
	GameObjectMenu::SetMenuWidth(230.0f);
	if (ImGui::BeginPopup("##SceneMenu"))
	{
		ImGui::MenuItem("Set Active Scene", nullptr, false, false);
		ImGui::Separator();
		if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, !playing))
			SceneManager::GetI()->SaveCurrentScene(false);
		if (ImGui::MenuItem("Save Scene As", nullptr, false, !playing))
			SceneManager::GetI()->SaveCurrentScene(true);
		if (ImGui::MenuItem("Save All", nullptr, false, !playing))
			SceneManager::GetI()->SaveCurrentScene(false);
		ImGui::Separator();
		ImGui::MenuItem("Unload Scene", nullptr, false, false);
		ImGui::MenuItem("Remove Scene", nullptr, false, false);
		if (ImGui::MenuItem("Discard changes", nullptr, false, dirty && !playing && !scene->GetScenePath().empty()))
			m_PendingDiscard = true;
		ImGui::Separator();
		ImGui::MenuItem("Select Scene Asset", nullptr, false, false);
		ImGui::MenuItem("Add New Scene", nullptr, false, false);
		ImGui::Separator();
		if (ImGui::BeginMenu("GameObject"))
		{
			GameObjectMenu::DrawCreateItems(scene, nullptr);
			ImGui::EndMenu();
		}
		ImGui::EndPopup();
	}
	GameObjectMenu::PopContextStyle();
	ImGui::PopID();
}

void SceneHierachyEditorWindow::OnRender()
{
	Scene* currentScene = SceneManager::GetI()->GetCurrentScene();

	if (currentScene == nullptr)
		return;

	if (m_PendingDiscard)
	{
		m_PendingDiscard = false;
		SceneManager::GetI()->DiscardChanges();
		return;
	}

	m_WindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	m_PendingDelete = nullptr;

	HandleShortcuts(currentScene);
	DrawToolbar(currentScene);
	DrawSceneHeader(currentScene);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	const std::string query = ToLowerCopy(m_Search);

	// 리스트 영역 (빈 곳 클릭 = 선택 해제, 빈 곳 우클릭 = 생성 메뉴)
	ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorTheme::Panel());
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::BeginChild("##HierList", ImVec2(0, 0), false);

	// 행 목록: 펼친 트리를 한 줄로 (검색 중이면 이름이 맞는 오브젝트만 평평하게)
	m_Rows.clear();
	if (m_SceneOpen || !query.empty())
	{
		if (query.empty())
		{
			for (GameObject* g : currentScene->GetRootGameObjects())
				CollectRows(g, 1);
		}
		else
		{
			std::vector<GameObject*> all;
			for (GameObject* root : currentScene->GetRootGameObjects())
				CollectAll(root, all);
			for (GameObject* g : all)
				if (ToLowerCopy(g->GetName()).find(query) != std::string::npos)
					m_Rows.push_back({ g, 0 });
		}
	}
	// 가상 스크롤: 스크롤 영역에 보이는 행만 그린다 (오브젝트가 수천 개여도 한 화면 분량만)
	{
		ImGuiListClipper clipper;
		clipper.Begin((int)m_Rows.size(), kRowHeight);
		while (clipper.Step())
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
				DrawGameObject(m_Rows[i].Object, m_Rows[i].Depth);
		clipper.End();
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
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PREFAB_FILE"))
		{
			EditorLog::Write("DragDrop", "drop prefab %s on hierarchy", static_cast<const char*>(payload->Data));
			Undo::SetActionName("Instantiate Prefab");
			if (GameObject* g = PrefabUtility::InstantiatePrefab(static_cast<const char*>(payload->Data), currentScene, nullptr))
				SelectionManager::SetSelectedGameObject(g);
		}
		ImGui::EndDragDropTarget();
	}
	GameObjectMenu::PushContextStyle();
	GameObjectMenu::SetMenuWidth(330.0f);
	if (ImGui::BeginPopupContextItem("##HierEmptyContext", ImGuiPopupFlags_MouseButtonRight))
	{
		DrawContextMenu(currentScene, nullptr);
		ImGui::EndPopup();
	}
	GameObjectMenu::PopContextStyle();

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

	// 컨텍스트 메뉴 (우클릭하면 해당 오브젝트가 선택된다)
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		SelectionManager::SetSelectedGameObject(gameObject);
	GameObjectMenu::PushContextStyle();
	GameObjectMenu::SetMenuWidth(330.0f);
	if (ImGui::BeginPopupContextItem("##ctx", ImGuiPopupFlags_MouseButtonRight))
	{
		DrawContextMenu(scene, gameObject);
		ImGui::EndPopup();
	}
	GameObjectMenu::PopContextStyle();
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		BeginRename(gameObject);

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
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PREFAB_FILE"))
		{
			EditorLog::Write("DragDrop", "drop prefab %s on hierarchy", static_cast<const char*>(payload->Data));
			Undo::SetActionName("Instantiate Prefab");
			if (GameObject* g = PrefabUtility::InstantiatePrefab(static_cast<const char*>(payload->Data), scene, gameObject))
				SelectionManager::SetSelectedGameObject(g);
		}
		// C# 스크립트(.cs)를 오브젝트에 놓으면 그 클래스를 컴포넌트로 붙인다 (Unity)
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
		{
			const std::filesystem::path file(static_cast<const char*>(payload->Data));
			if (_stricmp(file.extension().string().c_str(), ".cs") == 0)
			{
				const std::string cls = file.stem().string();
				if (ScriptEngine::FindClass(cls))
				{
					Undo::SetActionName("Add Component");
					gameObject->AddComponent(CSharpScript::Create(cls));
					SelectionManager::SetSelectedGameObject(gameObject);
				}
				else
					Debug::Write(LogType::Error, "Can't add script behaviour '" + cls + "'. The script needs to derive from MonoBehaviour and its class name must match the file name (or fix compile errors).");
			}
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
	const bool isPrefab = PrefabUtility::IsPartOfPrefabInstance(gameObject);
	dl->AddText(ImVec2(x0 + 20.0f, ty), ImGui::GetColorU32(isPrefab ? EditorTheme::Rgb(92, 160, 255) : EditorTheme::Rgb(150, 190, 230)), ICON_FA_CUBE);
	if (m_RenameTarget == gameObject)
	{
		// 이름 바꾸기 입력창: Enter/포커스 해제 시 적용, Esc 취소
		ImGui::SetCursorScreenPos(ImVec2(x0 + 38.0f, p.y + 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 1.0f));
		ImGui::SetNextItemWidth((std::max)(60.0f, w - (x0 - p.x) - 44.0f));
		if (m_RenameFrames == 0)
			ImGui::SetKeyboardFocusHere();
		bool enter = ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		ImGui::PopStyleVar(2);
		bool lostFocus = (m_RenameFrames > 1) && !ImGui::IsItemActive();
		++m_RenameFrames;
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			m_RenameTarget = nullptr;
		else if (enter || lostFocus)
		{
			if (m_RenameBuffer[0] != 0)
				gameObject->SetName(m_RenameBuffer);
			m_RenameTarget = nullptr;
		}
	}
	else
		dl->AddText(ImVec2(x0 + 40.0f, ty), ImGui::GetColorU32(isPrefab ? EditorTheme::Rgb(126, 181, 255) : EditorTheme::Rgb(225, 225, 225)), gameObject->GetName().c_str());

	// 다음 행 위치 (한 행 높이만큼 차지했다고 알린다 → 목록 가상 스크롤의 높이 계산)
	ImGui::SetCursorScreenPos(ImVec2(p.x, p.y));
	ImGui::Dummy(ImVec2(w, kRowHeight));

	ImGui::PopID();
}

void SceneHierachyEditorWindow::CollectRows(GameObject* gameObject, int depth)
{
	m_Rows.push_back({ gameObject, depth });
	if (gameObject->m_Editor_HierachOpened && gameObject->GetChildCount() > 0)
		for (GameObject* child : gameObject->GetChildren())
			CollectRows(child, depth + 1);
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

	// 스킨 메시가 있는 모델 (FBX · VRM …) = Unity 처럼 캐릭터로 (스킨 메시 + Animator, VRM 은 재질 · Dynamic Bone 까지)
	const std::string rel = wstring_to_string(PathManager::GetI()->GetCutSolutionPath(path.wstring()));
	if (auto model = ResourceManager::GetI()->LoadMeshFile(rel); model && !model->SkinnedMeshs.empty())
	{
		Undo::SetActionName("Instantiate Model");
		GameObject* character = GameObjectFactory::CreateAnimatedCharacter(fileName, rel);
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene)
		{
			if (parent == nullptr) scene->AddRootGameObject(character);
			else { character->SetParent(parent, false); scene->RegisterGameObjectTree(character); }
		}
		SelectionManager::SetSelectedGameObject(character);
		return;
	}

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
			newGameObject->SetParent(parent, false);
		}
	}
}
