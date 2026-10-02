#include "pch.h"
#include "AutoSave.h"
#include "SceneManager.h"
#include "UndoSystem.h"
#include "Scene.h"
#include "LightManager.h"
#include "GameObjectFactory.h"
#include "EditorSettingManager.h"
#include "SelectionManager.h"
#include "Debug.h"
#include "NovaCodeWindow.h"
#include "PlayerRuntime.h"
#include "BuildSettings.h"
#include "UISystem.h"
#include "ScriptEngine.h"
#include "Volume.h"
#include "RenderPipelineSettings.h"
#include "EditorUtility.h"


SINGLE_BODY(SceneManager)

SceneManager::SceneManager()
	: m_pCurrScene(nullptr)
{

}

SceneManager::~SceneManager()
{
	Safe_Delete_Map(m_Scenes); 
}

void SceneManager::Init()
{
}

void SceneManager::LoadScene(wstring scenePath)
{
	Scene* scene = nullptr;

	// Unity 의 씬 열기와 같이 이전 씬을 내린다 (Play 중이 아닐 때). 예전에는 캐시에 남겨 두어
	// 그 씬의 나무·물·지형·입자가 전역 목록(Tree::All 등)에 남아 새 씬에 같이 그려졌고, 물리도 Exit 되지 않았다.
	// 저장하지 않은 변경은 여기 오기 전에 저장/버리기를 정한다 (DiscardChanges 와 같은 방식으로 지운다)
	if (m_pCurrScene != nullptr && m_pCurrScene->GetScenePath() != scenePath && !Application::IsPlaying())
	{
		Scene* old = m_pCurrScene;
		old->Exit();
		for (auto it = m_Scenes.begin(); it != m_Scenes.end();)
			it = it->second == old ? m_Scenes.erase(it) : std::next(it);
		m_pCurrScene = nullptr;
		SelectionManager::ClearSelection();
		delete old;
		TerrainData::DropUnsaved();   // 저장하지 않고 닫은 지형 편집은 버린다 (다시 열면 파일에서)
	}

	if (m_Scenes.find(scenePath) != m_Scenes.end())
	{
		scene = m_Scenes[scenePath];
		m_pCurrScene = scene;
	}
	else
	{
		scene = Scene::Load(scenePath);
		if (scene == nullptr)
		{
			CreateScene();
		}
		else
		{
			m_Scenes[scenePath] = scene;
			m_pCurrScene = scene;
		}
	}

	m_pCurrScene->Enter();
	DisplayManager::GetI()->Init();
	MarkCurrentSceneSaved();   // 방금 연 씬은 "저장됨" 상태
}

void SceneManager::UpdateScene()
{
	if (m_pCurrScene == nullptr)
		return;

	m_pCurrScene->UpdateScene();
}

void SceneManager::RenderScene()
{
	if (m_pCurrScene == nullptr)
		return;

	m_pCurrScene->RenderScene();
}

void SceneManager::LastUpdate()
{
	// 실행 중인 동작이 또 AddLastUpdate 를 부를 수 있다(씬 교체 → Exit → Destroy 등). 목록을 떼어 낸 뒤 돌려서
	// 반복 중 벡터가 다시 할당되어 실행 중인 람다가 사라지는 일을 막는다. 새로 들어온 동작은 다음 프레임에 실행.
	std::vector<std::function<void()>> actions;
	actions.swap(m_Editor_LastUpdateActions);
	for (auto& action : actions)
		action();
}

void SceneManager::HandleSceneShortcuts()
{
	// GetAsyncKeyState 기반이라 한글 입력(IME) 상태에서도 동작한다. Ctrl 과 키를 같은 프레임에 눌러도 되도록 TAP 도 허용.
	const bool ctrl = (INPUT_KEY_HOLD(KEY::CTRL)) || (INPUT_KEY_DOWN(KEY::CTRL));
	if (!ctrl || Application::IsPlayer())
		return;
	const bool s = INPUT_KEY_DOWN(KEY::S), n = INPUT_KEY_DOWN(KEY::N), o = INPUT_KEY_DOWN(KEY::O), p = INPUT_KEY_DOWN(KEY::P);
	if (!s && !n && !o && !p)
		return;
	// NOVA Code 에서의 Ctrl+S/N/O 는 스크립트 저장·새 파일 등이다. 확인 창이 떠 있으면 무시
	if (NovaCodeWindow::IsFocused() || IsScenePromptOpen())
		return;
	const bool shift = (INPUT_KEY_HOLD(KEY::LSHIFT)) || (INPUT_KEY_DOWN(KEY::LSHIFT)) || (::GetAsyncKeyState(VK_RSHIFT) & 0x8000);
	if (s)
		SaveCurrentScene(shift);
	else if (n && !shift)   // Ctrl+Shift+N = Hierarchy 의 빈 오브젝트 만들기
		NewSceneFromEditor();
	else if (o && !shift)
		OpenSceneFromEditor();
	else if (p && !shift)   // 예전에는 메뉴에 Ctrl+P 라고만 적혀 있고 키는 동작하지 않았다
		TogglePlayFromEditor();
}

void SceneManager::TogglePlayFromEditor()
{
	if (!Application::IsPlaying())
	{
		if (!ScriptEngine::CanEnterPlayMode())   // 컴파일 오류가 있으면 들어가지 않는다
			return;
		Application::SetPaused(false);
		Application::SetPlaying(true);
		HandlePlay();
	}
	else
	{
		Application::SetPlaying(false);
		Application::SetPaused(false);
		SelectionManager::ClearSelection();
		HandleStop();
	}
	EditorLog::Write("Scene", "play toggled from the editor: %s", Application::IsPlaying() ? "playing" : "stopped");
}

void SceneManager::NewSceneFromEditor()
{
	if (Application::IsPlaying())
	{
		Debug::Log("Play 모드에서는 새 씬을 만들 수 없습니다. Play 를 멈춘 뒤 다시 하세요.");
		return;
	}
	EditorLog::Write("Scene", "%s", "new scene requested");
	RequestSceneChange([this]() { CreateScene(); });
}

void SceneManager::OpenSceneFromEditor(const std::wstring& relPathIn)
{
	if (Application::IsPlaying())
	{
		Debug::Log("Play 모드에서는 다른 씬을 열 수 없습니다. Play 를 멈춘 뒤 다시 하세요.");
		return;
	}
	std::wstring relPath = relPathIn;
	if (relPath.empty())
	{
		// Unity 처럼 파일을 먼저 고르고, 그다음 저장 여부를 묻는다
		const std::wstring filePath = EditorUtility::OpenFileDialog(PathManager::GetI()->GetMovePathW(L"Assets\\"), L"Open Scene", std::vector<std::wstring>{ L"scene" });
		if (filePath.empty())
			return;
		relPath = PathManager::GetI()->GetCutSolutionPath(filePath);
	}
	EditorLog::Write("Scene", "open scene requested: %s", wstring_to_string(relPath).c_str());
	const bool same = m_pCurrScene && m_pCurrScene->GetScenePath() == relPath;
	if (same)   // 열려 있는 씬을 다시 열기: 저장 = 그대로, 저장 안 함 = 파일에서 다시 읽기
		RequestSceneChange([]() {}, [this]() { DiscardChanges(); });
	else
		RequestSceneChange([this, relPath]() { LoadScene(relPath); });
}

void SceneManager::RequestSceneChange(std::function<void()> action, std::function<void()> onDiscard)
{
	if (!action)
		return;
	if (!IsCurrentSceneDirty())
	{
		action();
		return;
	}
	m_PromptAction = std::move(action);
	m_PromptDiscard = std::move(onDiscard);
	m_PromptOpened = false;   // 다음 UI 프레임에 확인 창을 연다 (DrawScenePrompt)
}

void SceneManager::DrawScenePrompt()
{
	if (!m_PromptAction)
		return;
	const char* id = "Scene Has Been Modified##NovaScenePrompt";
	if (!m_PromptOpened)
	{
		ImGui::OpenPopup(id);
		m_PromptOpened = true;
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
		return;
	enum { None, Save, DontSave, Cancel } choice = None;
	std::string name = m_pCurrScene ? wstring_to_string(m_pCurrScene->GetName()) : std::string();
	if (name.empty()) name = "Untitled";
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
	ImGui::TextWrapped("Do you want to save the changes you made in the scene \"%s\"?", name.c_str());
	ImGui::TextDisabled("Your changes will be lost if you don't save them.");
	ImGui::Spacing();
	if (ImGui::Button("Save", ImVec2(120, 0))) choice = Save;
	ImGui::SameLine();
	if (ImGui::Button("Don't Save", ImVec2(120, 0))) choice = DontSave;
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0))) choice = Cancel;
	// 키보드: Enter = Save, D = Don't Save, Esc = Cancel
	if (choice == None && !ImGui::GetIO().WantTextInput)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) choice = Save;
		else if (ImGui::IsKeyPressed(ImGuiKey_D, false)) choice = DontSave;
		else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) choice = Cancel;
	}
	ImGui::PopStyleVar();
	if (choice != None)
	{
		ImGui::CloseCurrentPopup();
		std::function<void()> action = std::move(m_PromptAction), discard = std::move(m_PromptDiscard);
		m_PromptAction = nullptr;
		m_PromptDiscard = nullptr;
		EditorLog::Write("Scene", "unsaved changes prompt: %s", choice == Save ? "Save" : choice == DontSave ? "Don't Save" : "Cancel");
		// 씬 교체는 프레임 끝에 (이번 프레임의 창들이 지금 씬의 오브젝트를 쓰는 중)
		if (choice == Save)
			AddLastUpdate([this, action]() { if (SaveCurrentScene(false)) action(); });   // 저장을 취소하거나 실패하면 그대로 둔다
		else if (choice == DontSave)
			AddLastUpdate([action, discard]() { if (discard) discard(); else action(); });
	}
	ImGui::EndPopup();
}

bool SceneManager::SaveCurrentScene(bool saveAs)
{
	if (m_pCurrScene == nullptr)
		return false;
	if (Application::IsPlaying())
	{
		Debug::Log("Play 모드에서는 씬을 저장할 수 없습니다. Play 를 멈춘 뒤 저장하세요.");
		return false;
	}

	const bool ok = (saveAs || m_pCurrScene->GetScenePath().empty()) ? Scene::SaveNewScene(m_pCurrScene) : Scene::Save(m_pCurrScene);
	EditorLog::Write("Scene", "save %s -> %s", wstring_to_string(m_pCurrScene->GetScenePath()).c_str(), ok ? "ok" : "failed");
	if (ok)
	{
		TerrainData::SaveAllDirty();   // Unity 처럼 씬 저장 때 편집한 지형 데이터도 저장
		m_Scenes[m_pCurrScene->GetScenePath()] = m_pCurrScene;
		MarkCurrentSceneSaved();
		Debug::Log("씬을 저장했습니다: " + wstring_to_string(m_pCurrScene->GetScenePath()));
	}
	else if (!m_pCurrScene->GetScenePath().empty() && !saveAs)
		Debug::Log("씬 저장에 실패했습니다: " + wstring_to_string(m_pCurrScene->GetScenePath()));
	return ok;
}

size_t SceneManager::ComputeSceneHash() const
{
	if (m_pCurrScene == nullptr)
		return 0;
	json j = *m_pCurrScene;
	return std::hash<std::string>()(j.dump());
}

void SceneManager::MarkCurrentSceneSaved()
{
	m_SavedHash = m_CheckedHash = ComputeSceneHash();
	m_Dirty = false;
	m_LastDirtyCheck = -1.0;
}

bool SceneManager::IsCurrentSceneDirty()
{
	if (m_pCurrScene == nullptr)
		return false;
	// Play 중의 변화는 저장 대상이 아니므로 Play 직전 상태를 유지한다
	if (Application::IsPlaying())
		return m_Dirty;
	// 에디터: Undo 가 조작이 끝날 때 확정한 씬 JSON 의 해시를 쓴다 (씬 전체 직렬화는 변경이 확정될 때만).
	//  예전에는 0.25 초마다 여기서 씬 전체를 JSON 으로 만들어 오브젝트가 수천 개면 프레임마다 수백 ms 가 걸렸다
	size_t committed = 0;
	if (Undo::CommittedSceneHash(committed))
	{
		m_CheckedHash = committed;
		m_Dirty = m_CheckedHash != m_SavedHash || TerrainData::AnyDirty();
		return m_Dirty;
	}
	const double now = ::GetTickCount64() / 1000.0;
	if (m_LastDirtyCheck < 0.0 || now - m_LastDirtyCheck > 0.25)
	{
		m_LastDirtyCheck = now;
		m_CheckedHash = ComputeSceneHash();
		m_Dirty = m_CheckedHash != m_SavedHash || TerrainData::AnyDirty();
	}
	return m_Dirty;
}

bool SceneManager::OpenRecoveredScene(const std::string& sceneJson, const std::wstring& scenePath)
{
	if (Application::IsPlaying())
		return false;
	json j = json::parse(sceneJson, nullptr, false);
	if (j.is_discarded() || !j.is_object())
		return false;
	if (m_pCurrScene != nullptr)
	{
		Scene* old = m_pCurrScene;
		old->Exit();
		for (auto it = m_Scenes.begin(); it != m_Scenes.end();)
			it = it->second == old ? m_Scenes.erase(it) : std::next(it);
		m_pCurrScene = nullptr;
		SelectionManager::ClearSelection();
		delete old;
		TerrainData::DropUnsaved();
	}
	Scene* scene = new Scene();
	scene->SetScenePath(scenePath);
	from_json(j, *scene);
	if (!scenePath.empty())
		m_Scenes[scenePath] = scene;
	m_pCurrScene = scene;
	m_pCurrScene->Enter();
	DisplayManager::GetI()->Init();
	// 변경됨: 저장된 기준을 비워 둔다 (파일과 다르다)
	m_SavedHash = 0;
	m_Dirty = true;
	m_LastDirtyCheck = -1.0;
	EditorLog::Write("Scene", "recovered scene %s (%zu objects)", wstring_to_string(scenePath).c_str(), scene->GetAllGameObjects().size());
	return true;
}

void SceneManager::DiscardChanges()
{
	if (m_pCurrScene == nullptr || m_pCurrScene->GetScenePath().empty() || Application::IsPlaying())
		return;
	const std::wstring path = m_pCurrScene->GetScenePath();
	m_pCurrScene->Exit();
	m_Scenes.erase(path);
	delete m_pCurrScene;
	m_pCurrScene = nullptr;
	SelectionManager::ClearSelection();
	TerrainData::DropUnsaved();   // 지형 편집도 버린다 (예전에는 캐시에 남아 Discard 뒤에도 그대로였다)
	LoadScene(path);
}

void SceneManager::HandlePlay()
{
	if (m_pCurrScene == nullptr) 
		return;

	AutoSave::OnEnterPlay();   // Play 중 충돌해도 Play 직전 상태를 되살릴 수 있게 (변경이 있을 때만)
	json j = *m_pCurrScene;
	m_PlayModeSceneSnapshot = j.dump();
	m_PlayOriginalPath = m_pCurrScene->GetScenePath();
	m_pCurrScene->Enter();
}

void SceneManager::LoadSceneDuringPlay(const std::wstring& scenePath)
{
	AddLastUpdate([this, scenePath]() {
		Scene* next = Scene::Load(scenePath);
		if (next == nullptr)
		{
			Debug::LogError("SceneManager.LoadScene: could not load '" + wstring_to_string(scenePath) + "'");
			return;
		}
		next->SetScenePath(scenePath);
		EditorLog::Write("Scene", "load during play: %s", wstring_to_string(scenePath).c_str());
		Scene* old = m_pCurrScene;
		if (old)
		{
			UISystem::OnSceneUnloading();
			old->Exit();
			for (auto it = m_Scenes.begin(); it != m_Scenes.end();)
				it = it->second == old ? m_Scenes.erase(it) : std::next(it);
			SelectionManager::ClearSelection();
			delete old;
		}
		ScriptEngine::OnSceneSwapped();
		m_pCurrScene = next;
		DisplayManager::GetI()->Init();
		m_pCurrScene->Enter();   // Play 중이므로 Awake / Start (C# 포함)
		EditorLog::Write("Scene", "entered %s (%zu objects)", wstring_to_string(scenePath).c_str(), m_pCurrScene->GetAllGameObjects().size());
	});
}

void SceneManager::HandleStop()
{
	if (m_pCurrScene == nullptr)
		return;

	m_pCurrScene->Exit();

	if (!m_PlayModeSceneSnapshot.empty())
	{
		// Play 중 LoadScene 으로 다른 씬이 열려 있어도 Play 를 시작한 씬으로 돌아간다
		// 빈 경로도 원래 상태다 (Untitled). 실행 중 연 씬의 저장 경로를 가져오면 그 파일을 덮어쓸 수 있다.
		wstring scenePath = m_PlayOriginalPath;
		m_PlayOriginalPath.clear();
		for (auto it = m_Scenes.begin(); it != m_Scenes.end();)
			it = it->second == m_pCurrScene ? m_Scenes.erase(it) : std::next(it);
		delete m_pCurrScene;

		m_pCurrScene = new Scene();
		m_pCurrScene->SetScenePath(scenePath);
		json j = json::parse(m_PlayModeSceneSnapshot);
		from_json(j, *m_pCurrScene);
		m_PlayModeSceneSnapshot.clear();

		if (!scenePath.empty())
			m_Scenes[scenePath] = m_pCurrScene;
	}
	else
	{
		wstring scenePath = m_pCurrScene->GetScenePath();
		delete m_pCurrScene;
		m_pCurrScene = nullptr;
		m_Scenes.erase(scenePath);

		if (!scenePath.empty())
			SceneManager::LoadScene(scenePath);
		else
			CreateScene();
	}

	DisplayManager::GetI()->Init();
}

void SceneManager::CreateScene()
{
	if (m_pCurrScene != nullptr)
	{
		// 캐시(m_Scenes)에서도 뺀다 — 예전에는 지운 씬이 캐시에 남아, 그 씬을 다시 열면 지운 메모리를 썼다
		Scene* old = m_pCurrScene;
		old->Exit();
		for (auto it = m_Scenes.begin(); it != m_Scenes.end();)
			it = it->second == old ? m_Scenes.erase(it) : std::next(it);
		m_pCurrScene = nullptr;
		SelectionManager::ClearSelection();
		delete old;
		TerrainData::DropUnsaved();
	}

	m_pCurrScene = new Scene();

	GameObject* camera = GameObjectFactory::CreateCamera("Main Camera");
	camera->GetTransform()->SetPosition(Vec3(0.0f, 1.0f, -10.0f));   // Unity 기본 씬과 동일
	m_pCurrScene->AddRootGameObject(camera);

	GameObject* light = GameObjectFactory::CreateDirectionalLight("Directional Light");
	m_pCurrScene->AddRootGameObject(light);

	// Unity URP 기본 씬처럼 Global Volume (Bloom 이 켜진 SampleSceneProfile)
	if (!Application::IsPlayer() && !PathManager::GetProjectOverride().empty())
	{
		const std::string profile = RenderPipelineSettings::EnsureSampleSceneProfile();
		if (!profile.empty())
		{
			GameObject* volume = GameObjectFactory::CreateVolume(GameObjectFactory::VolumeShape::Global);
			if (Volume* v = volume->GetComponent<Volume>())
				v->SetProfile(profile);
			m_pCurrScene->AddRootGameObject(volume);
		}
	}

	DisplayManager::GetI()->Init();	MarkCurrentSceneSaved();   // 새 씬은 바뀐 내용이 생길 때부터 * 표시
}


void SceneManager::LoadStartupScene()
{
	// 빌드된 게임: Build Settings 의 0번 씬
	if (Application::IsPlayer())
	{
		const std::wstring first = PlayerRuntime::FirstScene();
		LoadScene(first);
		return;
	}

	EditorSetting* setting = EditorSettingManager::GetSetting();
	std::wstring last = setting ? setting->LastOpenedScenePath : L"";

	std::error_code ec;
	const std::filesystem::path lastPath(last);
	if (!last.empty() && std::filesystem::exists(lastPath.is_absolute() ? lastPath : std::filesystem::path(PathManager::GetI()->GetMovePathW(last)), ec))
	{
		LoadScene(last);
		return;
	}

	if (!PathManager::GetProjectOverride().empty())
	{
		// 새 프로젝트: Unity 의 SampleScene 처럼 기본 씬(Main Camera + Directional Light)을 만들어 저장
		const std::wstring samplePath = L"Assets\\Scenes\\SampleScene.scene";
		CreateScene();
		m_pCurrScene->SetScenePath(samplePath);
		Scene::Save(m_pCurrScene);
		m_Scenes[samplePath] = m_pCurrScene;
		m_pCurrScene->Enter();
		DisplayManager::GetI()->Init();
		MarkCurrentSceneSaved();
		return;
	}

	LoadScene(L"");
}

void SceneManager::RestoreSceneState(const std::string& sceneJson)
{
	if (m_pCurrScene == nullptr)
		return;
	json j = json::parse(sceneJson, nullptr, false);
	if (j.is_discarded())
		return;

	// 선택과 Hierarchy 펼침 상태를 fileID 로 기억
	GameObject* selected = SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT ? SelectionManager::GetSelectedGameObject() : nullptr;
	const uint64 selectedID = selected ? selected->GetFileID() : 0;
	std::unordered_set<uint64> expanded;
	for (GameObject* go : m_pCurrScene->GetAllGameObjects())
		if (go->m_Editor_HierachOpened)
			expanded.insert(go->GetFileID());

	const wstring scenePath = m_pCurrScene->GetScenePath();
	if (selected)
		SelectionManager::ClearSelection();
	// C# 의 id → 포인터 캐시를 비운다: 새 씬도 fileID 가 같아 지운 오브젝트를 가리키게 된다 (Undo · 다시 가져오기 뒤 exec / 스크립트)
	ScriptEngine::OnSceneSwapped();
	delete m_pCurrScene;
	m_pCurrScene = new Scene();
	m_pCurrScene->SetScenePath(scenePath);
	from_json(j, *m_pCurrScene);
	if (!scenePath.empty())
		m_Scenes[scenePath] = m_pCurrScene;

	for (GameObject* go : m_pCurrScene->GetAllGameObjects())
		go->m_Editor_HierachOpened = expanded.count(go->GetFileID()) > 0;
	if (GameObject* again = m_pCurrScene->FindByFileID(selectedID))
		SelectionManager::SetSelectedGameObject(again);

	DisplayManager::GetI()->Init();
	m_LastDirtyCheck = -1.0;   // "*" 표시를 바로 다시 계산
	EditorLog::Write("Scene", "restored state (%zu bytes, %zu objects, selection %s)", sceneJson.size(), m_pCurrScene->GetAllGameObjects().size(),
		SelectionManager::GetSelectedGameObject() ? SelectionManager::GetSelectedGameObject()->GetName().c_str() : "none");
}
