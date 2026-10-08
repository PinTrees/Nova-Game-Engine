#include "pch.h"
#include "SceneManager.h"
#include "Scene.h"
#include "GameObject.h"
#include "Debug.h"
#include "UISystem.h"
#include "ScriptEngine.h"
#include "SelectionManager.h"
#include "DisplayManager.h"
#include "PathManager.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include "JobSystem.h"
#include <unordered_set>

// 실행 중 여러 씬 (Unity 의 SceneManager): LoadSceneMode.Additive · LoadSceneAsync · UnloadSceneAsync · DontDestroyOnLoad.
//  - 엔진이 그리고 · 물리 · 스크립트를 돌리는 씬 객체는 하나 (m_pCurrScene). 더해 읽은 씬은 오브젝트를 그 안으로 옮기고
//    루트 오브젝트마다 어느 씬 것인지 (핸들) 를 fileID 로 기억한다 → C# 의 Scene · GetRootGameObjects · 내리기가 핸들로 나뉜다
//  - 더해 읽을 때는 fileID 를 새로 만든다 (같은 씬을 두 번 더해도 C# 이 오브젝트를 fileID 로 찾으므로), 씬 안의 참조는 함께 바꾼다
//  - 비동기: 파일 읽기 · JSON 해석은 작업 스레드 (웹은 스레드가 없어 그 프레임에), 오브젝트를 만들고 Awake 하는 것은 메인 스레드의 프레임 끝.
//    allowSceneActivation = false 면 0.9 에서 기다린다. 작업은 요청한 차례대로 끝난다 (Unity 와 같음)
//  - C# 에 알림 (ScriptEngine::OnSceneEvent): 0 sceneLoaded (핸들, 모드) — Awake 뒤 Start 전, 1 sceneUnloaded, 2 activeSceneChanged, 3 작업 끝, 4 Play 시작
namespace
{
	namespace fs = std::filesystem;
	using json = nlohmann::json;

	enum SceneEvent { kLoaded = 0, kUnloaded = 1, kActiveChanged = 2, kOpDone = 3, kPlayStarted = 4 };

	struct PendingOp
	{
		int Id = 0;
		std::wstring Path;
		bool Additive = false;
		bool Async = false;
		int UnloadHandle = 0;          // 0 이 아니면 내리기
		bool Started = false;
		bool ParseDone = false;
		std::shared_ptr<json> Parsed;
		std::string Error;
		std::atomic<float> Progress{ 0.0f };
		Jobs::Future<void> Work;         // 마지막에 둔다: 먼저 지워지며 작업 스레드가 끝나기를 기다린다
	};

	std::vector<std::unique_ptr<PendingOp>> s_Pending;
	std::map<int, SceneManager::SceneOp> s_Ops;
	std::vector<SceneManager::RuntimeScene> s_Loaded;
	std::map<int, std::wstring> s_Known;            // 이번 Play 에서 쓴 핸들 → 경로 (내린 씬도 C# sceneUnloaded 에서 이름을 묻는다)
	std::unordered_map<uint64, int> s_RootScene;    // 루트 fileID → 핸들
	int s_Active = 0, s_NextHandle = 1, s_NextOp = 1;

	std::wstring ResolvePath(const std::wstring& path)
	{
		return fs::path(path).is_absolute() ? path : PathManager::GetI()->GetMovePathW(path);
	}

	void Parse(PendingOp* op)
	{
		try
		{
			std::ifstream is{ fs::path(ResolvePath(op->Path)), std::ios::binary };
			if (!is)
				throw std::runtime_error("could not read the scene file");
			std::string text((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
			op->Progress = 0.3f;
			op->Parsed = std::make_shared<json>(json::parse(text));
		}
		catch (const std::exception& e)
		{
			op->Error = e.what();
		}
		op->Progress = 0.9f;
	}

	void Notify(int kind, int a, int b) { ScriptEngine::OnSceneEvent(kind, a, b); }

	int NewHandle(const std::wstring& path)
	{
		const int h = s_NextHandle++;
		s_Known[h] = path;
		return h;
	}

	void CollectTree(GameObject* g, std::vector<GameObject*>& out)
	{
		if (g == nullptr) return;
		out.push_back(g);
		for (GameObject* c : g->GetChildren())
			CollectTree(c, out);
	}

	GameObject* RootOf(GameObject* g)
	{
		while (g && g->GetParent())
			g = g->GetParent();
		return g;
	}
}

void SceneManager::BeginRuntimeScenes()
{
	EndRuntimeScenes();
	// C# 의 SceneManager 정적 상태 (sceneLoaded 구독 · 작업) 를 비운다 — 어셈블리를 다시 읽지 않아 지난 Play 의 구독이 남는다 (Unity 는 Play 때 도메인을 다시 읽는다)
	Notify(kPlayStarted, 0, 0);
	const int h = NewHandle(m_pCurrScene ? m_pCurrScene->GetScenePath() : std::wstring());
	s_Loaded.push_back({ h, s_Known[h] });
	s_Active = h;
	if (m_pCurrScene)
		for (GameObject* r : m_pCurrScene->RootGameObjects())
			s_RootScene[r->GetFileID()] = h;
}

void SceneManager::EndRuntimeScenes()
{
	s_Pending.clear();   // 작업 스레드가 끝나기를 기다린 뒤 지운다
	s_Ops.clear();
	s_Loaded.clear();
	s_Known.clear();
	s_RootScene.clear();
	s_Active = 0;
}

void SceneManager::NotifyFirstSceneLoaded()
{
	if (!s_Loaded.empty())
		Notify(kLoaded, s_Loaded.front().Handle, 0);
}

int SceneManager::RequestSceneLoad(const std::wstring& scenePath, bool additive, bool async)
{
	if (!Application::IsPlaying() || scenePath.empty())
		return 0;
	auto op = std::make_unique<PendingOp>();
	op->Id = s_NextOp++;
	op->Path = scenePath;
	op->Additive = additive;
	op->Async = async;
	SceneOp state;
	state.AllowActivation = true;
	s_Ops[op->Id] = state;
	EditorLog::Write("Scene", "load request #%d %s (%s%s)", op->Id, wstring_to_string(scenePath).c_str(), additive ? "additive" : "single", async ? ", async" : "");
	const int id = op->Id;
	s_Pending.push_back(std::move(op));
	return id;
}

int SceneManager::RequestSceneUnload(int handle)
{
	if (!Application::IsPlaying())
		return 0;
	const bool loaded = std::any_of(s_Loaded.begin(), s_Loaded.end(), [&](const RuntimeScene& s) { return s.Handle == handle; });
	if (!loaded || s_Loaded.size() <= 1)
		return 0;   // Unity: 마지막으로 남은 씬은 내릴 수 없다
	auto op = std::make_unique<PendingOp>();
	op->Id = s_NextOp++;
	op->UnloadHandle = handle;
	op->Path = s_Known[handle];
	s_Ops[op->Id] = SceneOp{};
	const int id = op->Id;
	s_Pending.push_back(std::move(op));
	return id;
}

const SceneManager::SceneOp* SceneManager::GetSceneOp(int op) const
{
	auto it = s_Ops.find(op);
	return it == s_Ops.end() ? nullptr : &it->second;
}

void SceneManager::SetSceneOpAllowActivation(int op, bool allow)
{
	auto it = s_Ops.find(op);
	if (it != s_Ops.end())
		it->second.AllowActivation = allow;
}

std::vector<SceneManager::RuntimeScene> SceneManager::LoadedScenes() const { return s_Loaded; }

std::wstring SceneManager::ScenePathOfHandle(int handle) const
{
	auto it = s_Known.find(handle);
	return it == s_Known.end() ? std::wstring() : it->second;
}
int SceneManager::ActiveSceneHandle() const { return s_Active; }

bool SceneManager::SetActiveSceneHandle(int handle)
{
	const bool loaded = std::any_of(s_Loaded.begin(), s_Loaded.end(), [&](const RuntimeScene& s) { return s.Handle == handle; });
	if (!loaded)
		return false;
	if (handle != s_Active)
	{
		const int prev = s_Active;
		s_Active = handle;
		Notify(kActiveChanged, prev, handle);
	}
	return true;
}

int SceneManager::SceneHandleOf(GameObject* go) const
{
	GameObject* root = RootOf(go);
	if (root == nullptr)
		return 0;
	auto it = s_RootScene.find(root->GetFileID());
	if (it != s_RootScene.end())
		return it->second;
	return s_Active;   // 지도에 없는 루트 (스크립트가 부모에서 떼어 낸 것 등) = 활성 씬
}

bool SceneManager::MoveRootToScene(GameObject* go, int handle)
{
	if (go == nullptr || go->GetParent() != nullptr)
		return false;
	const bool ok = handle == kDontDestroyOnLoadHandle ||
		std::any_of(s_Loaded.begin(), s_Loaded.end(), [&](const RuntimeScene& s) { return s.Handle == handle; });
	if (!ok)
		return false;
	s_RootScene[go->GetFileID()] = handle;
	return true;
}

std::vector<GameObject*> SceneManager::RootsOfScene(int handle) const
{
	std::vector<GameObject*> out;
	if (m_pCurrScene == nullptr)
		return out;
	for (GameObject* r : m_pCurrScene->RootGameObjects())
		if (SceneHandleOf(r) == handle)
			out.push_back(r);
	return out;
}

void SceneManager::OnRuntimeRootCreated(GameObject* root)
{
	if (root && s_Active != 0 && !s_RootScene.count(root->GetFileID()))   // DontDestroyOnLoad 를 Awake 에서 이미 정했으면 그대로
		s_RootScene[root->GetFileID()] = s_Active;
}

// ---- 바꾸기 (프레임 끝)
struct SceneManagerRuntimeAccess
{
	static void ActivateSingle(SceneManager& sm, const std::wstring& path, const nlohmann::json& parsed);
	static int ActivateAdditive(SceneManager& sm, const std::wstring& path, const nlohmann::json& parsed);
	static void UnloadNow(SceneManager& sm, int handle);
};

namespace
{
	void FinishOp(PendingOp& op, bool failed, int handle)
	{
		SceneManager::SceneOp& st = s_Ops[op.Id];
		st.Progress = 1.0f;
		st.Done = true;
		st.Failed = failed;
		st.Handle = handle;
		Notify(kOpDone, op.Id, failed ? 0 : 1);
	}
}

void SceneManagerRuntimeAccess::ActivateSingle(SceneManager& sm, const std::wstring& path, const nlohmann::json& parsed)
{
	auto next = std::make_unique<Scene>();
	next->SetScenePath(path);
	from_json(parsed, *next);

	// DontDestroyOnLoad 루트는 새 씬으로 옮긴다 (지우지 않고, 다시 Awake 하지 않는다)
	std::vector<GameObject*> keep;
	std::unordered_set<GameObject*> keepAll;
	Scene* old = sm.m_pCurrScene;
	if (old)
	{
		for (GameObject* r : old->GetRootGameObjects())
			if (sm.SceneHandleOf(r) == SceneManager::kDontDestroyOnLoadHandle)
				keep.push_back(r);
		for (GameObject* r : keep)
		{
			std::vector<GameObject*> tree;
			CollectTree(r, tree);
			keepAll.insert(tree.begin(), tree.end());
			old->DetachTree(r);
		}
	}
	std::vector<int> unloaded;
	for (const SceneManager::RuntimeScene& s : s_Loaded)
		unloaded.push_back(s.Handle);

	EditorLog::Write("Scene", "load during play: %s (%zu kept by DontDestroyOnLoad)", wstring_to_string(path).c_str(), keep.size());
	if (old)
	{
		UISystem::OnSceneUnloading();
		old->Exit();
		for (auto it = sm.m_Scenes.begin(); it != sm.m_Scenes.end();)
			it = it->second == old ? sm.m_Scenes.erase(it) : std::next(it);
		SelectionManager::ClearSelection();
		delete old;
	}
	ScriptEngine::OnSceneSwapped();
	sm.m_pCurrScene = next.release();
	DisplayManager::GetI()->Init();

	const int handle = NewHandle(path);
	const int prevActive = s_Active;
	s_Loaded = { { handle, path } };
	std::unordered_map<uint64, int> roots;
	for (GameObject* r : sm.m_pCurrScene->RootGameObjects())
		roots[r->GetFileID()] = handle;
	for (GameObject* r : keep)
	{
		roots[r->GetFileID()] = SceneManager::kDontDestroyOnLoadHandle;
		sm.m_pCurrScene->AddRootGameObject(r);
	}
	s_RootScene.swap(roots);
	s_Active = handle;
	for (int h : unloaded)
		Notify(kUnloaded, h, 0);
	sm.m_pCurrScene->Enter([&]() {
		Notify(kLoaded, handle, 0);
		Notify(kActiveChanged, prevActive, handle);
	}, &keepAll);
	EditorLog::Write("Scene", "entered %s (%zu objects)", wstring_to_string(path).c_str(), sm.m_pCurrScene->GetAllGameObjects().size());
}

int SceneManagerRuntimeAccess::ActivateAdditive(SceneManager& sm, const std::wstring& path, const nlohmann::json& parsed)
{
	if (sm.m_pCurrScene == nullptr)
		return 0;
	std::vector<GameObject*> roots;
	{
		Scene temp;
		temp.SetScenePath(path);
		from_json(parsed, temp);
		roots = temp.ReleaseAll();   // temp 는 빈 채로 지워진다
	}
	// 새 fileID (같은 씬을 두 번 더해도, 처음 씬과 같은 id 가 있어도 겹치지 않게) — 씬 안의 참조도 함께
	GameObject::RegenerateFileIDs(roots);

	const int handle = NewHandle(path);
	s_Loaded.push_back({ handle, path });
	std::vector<GameObject*> objects;
	for (GameObject* r : roots)
	{
		sm.m_pCurrScene->AddRootGameObject(r);
		s_RootScene[r->GetFileID()] = handle;
		CollectTree(r, objects);
	}
	// Scene::Enter 와 같은 차례: Awake → sceneLoaded → Start (물리 바디는 다음 물리 단계가 씬에서 만든다)
	for (GameObject* g : objects)
		for (auto& c : g->GetComponents())
			c->Awake();
	Notify(kLoaded, handle, 1);
	for (GameObject* g : objects)
		for (auto& c : g->GetComponents())
			c->Start();
	EditorLog::Write("Scene", "added %s (handle %d, %zu objects)", wstring_to_string(path).c_str(), handle, objects.size());
	return handle;
}

void SceneManagerRuntimeAccess::UnloadNow(SceneManager& sm, int handle)
{
	if (sm.m_pCurrScene)
		for (GameObject* r : sm.RootsOfScene(handle))
		{
			s_RootScene.erase(r->GetFileID());
			sm.m_pCurrScene->DestroyGameObject(r);   // OnDisable · OnDestroy, delete 는 프레임 끝
		}
	s_Loaded.erase(std::remove_if(s_Loaded.begin(), s_Loaded.end(), [&](const SceneManager::RuntimeScene& s) { return s.Handle == handle; }), s_Loaded.end());
	EditorLog::Write("Scene", "unloaded handle %d %s", handle, wstring_to_string(s_Known[handle]).c_str());
	Notify(kUnloaded, handle, 0);
	if (s_Active == handle && !s_Loaded.empty())
	{
		s_Active = s_Loaded.front().Handle;
		Notify(kActiveChanged, handle, s_Active);
	}
}

void SceneManager::UpdateSceneOps()
{
	if (s_Pending.empty())
		return;
	if (!Application::IsPlaying())
	{
		s_Pending.clear();
		return;
	}
#if !defined(__EMSCRIPTEN__)
	// 비동기 읽기는 요청하자마자 모두 작업 스레드에서 (웹은 스레드가 없다 → 아래에서 차례가 오면 바로)
	for (auto& p : s_Pending)
		if (p->Async && !p->UnloadHandle && !p->Started)
		{
			p->Started = true;
			PendingOp* raw = p.get();
			p->Work = Jobs::Async([raw]() { Parse(raw); }, Jobs::Priority::Background, "Scene Load (Parse)");
		}
#endif
	for (auto& p : s_Pending)
		if (!p->UnloadHandle && !p->ParseDone)
			s_Ops[p->Id].Progress = p->Progress;

	// 요청한 차례대로 끝낸다. 바꾸는 동안 스크립트 (Awake · Start) 가 새 요청을 뒤에 더할 수 있다
	while (!s_Pending.empty())
	{
		PendingOp& op = *s_Pending.front();
		if (op.UnloadHandle)
		{
			SceneManagerRuntimeAccess::UnloadNow(*this, op.UnloadHandle);
			FinishOp(op, false, op.UnloadHandle);
			s_Pending.erase(s_Pending.begin());
			continue;
		}
		if (!op.ParseDone)
		{
			if (op.Work.valid())
			{
				if (op.Work.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
					break;
				op.Work.get();
			}
			else
				Parse(&op);   // 동기 LoadScene · 웹
			op.ParseDone = true;
		}
		SceneOp& st = s_Ops[op.Id];
		st.Progress = 0.9f;
		if (!op.Error.empty() || !op.Parsed)
		{
			Debug::LogError("SceneManager: could not load '" + wstring_to_string(op.Path) + "': " + op.Error);
			FinishOp(op, true, 0);
			s_Pending.erase(s_Pending.begin());
			continue;
		}
		if (!st.AllowActivation)
			break;   // Unity: allowSceneActivation 이 켜질 때까지 0.9 에서 기다린다 (뒤의 작업도)
		std::unique_ptr<PendingOp> owned = std::move(s_Pending.front());
		s_Pending.erase(s_Pending.begin());
		int handle = 0;
		try
		{
			if (owned->Additive)
				handle = SceneManagerRuntimeAccess::ActivateAdditive(*this, owned->Path, *owned->Parsed);
			else
			{
				SceneManagerRuntimeAccess::ActivateSingle(*this, owned->Path, *owned->Parsed);
				handle = s_Active;
			}
		}
		catch (const std::exception& e)
		{
			Debug::LogError("SceneManager: could not load '" + wstring_to_string(owned->Path) + "': " + e.what());
			FinishOp(*owned, true, 0);
			continue;
		}
		FinishOp(*owned, handle == 0, handle);
	}
}
