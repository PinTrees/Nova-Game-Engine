#include "pch.h"
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

void SceneManager::HandleSaveScene()
{
	// GetAsyncKeyState 기반이라 한글 입력(IME) 상태에서도 동작한다. Ctrl 과 S 를 같은 프레임에 눌러도 되도록 TAP 도 허용.
	const bool ctrl = (INPUT_KEY_HOLD(KEY::CTRL)) || (INPUT_KEY_DOWN(KEY::CTRL));
	if (!ctrl || !(INPUT_KEY_DOWN(KEY::S)))
		return;
	// NOVA Code 에서의 Ctrl+S 는 스크립트 저장이다
	if (NovaCodeWindow::IsFocused())
		return;
	const bool shift = (INPUT_KEY_HOLD(KEY::LSHIFT)) || (INPUT_KEY_DOWN(KEY::LSHIFT)) || (::GetAsyncKeyState(VK_RSHIFT) & 0x8000);
	SaveCurrentScene(shift);
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
	LoadScene(path);
}

void SceneManager::HandlePlay()
{
	if (m_pCurrScene == nullptr) 
		return;

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
		wstring scenePath = m_PlayOriginalPath.empty() ? m_pCurrScene->GetScenePath() : m_PlayOriginalPath;
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
		m_pCurrScene->Exit();
		delete m_pCurrScene;
		m_pCurrScene = nullptr;
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
	if (!last.empty() && std::filesystem::exists(PathManager::GetI()->GetMovePathW(last), ec))
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
