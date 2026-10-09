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
#include "ResourceManager.h"
#include "LightManager.h"
#include "Light.h"
#include "MemoryHeaps.h"
#include "Profiler.h"
#include "SceneStreaming.h"
#include "PhysicsManager.h"
#include "Utils.h"
#include <set>
#include <chrono>
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
		// 미리 짓기 (비동기): 파싱이 끝나면 차례가 온 작업의 루트를 프레임마다 예산만큼 이 씬에 짓는다 (아직 현재 씬이 아니다)
		std::unique_ptr<Scene> Staging;
		size_t NextRoot = 0, TotalRoots = 0;
		bool LegacyLayers = false;
		bool StagingDone = false;
		std::vector<GameObject*> PrewarmObjects;   // 다 지은 뒤 미리 데울 오브젝트 (PrewarmStaged)
		size_t NextPrewarm = 0;
		bool PrewarmListed = false;
		std::vector<std::shared_ptr<Light>> DeferredLights;   // 짓는 동안의 Light — 바꿔 끼울 때 LightManager 에 (Staging 보다 먼저 지워진다)
		Jobs::Future<void> Work;         // 마지막에 둔다: 먼저 지워지며 작업 스레드가 끝나기를 기다린다
	};

	std::vector<std::unique_ptr<PendingOp>> s_Pending;
	std::map<int, SceneManager::SceneOp> s_Ops;
	std::vector<SceneManager::RuntimeScene> s_Loaded;
	std::map<int, std::wstring> s_Known;            // 이번 Play 에서 쓴 핸들 → 경로 (내린 씬도 C# sceneUnloaded 에서 이름을 묻는다)
	std::unordered_map<uint64, int> s_RootScene;    // 루트 fileID → 핸들
	int s_Active = 0, s_NextHandle = 1, s_NextOp = 1;

	// 측정: 바꾸는 단계별 시간 (Activate* 가 채우고 UpdateSceneOps 가 작업에 옮긴다)
	using Clock = std::chrono::steady_clock;
	double MsSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }
	struct ActivateTiming { double BuildMs = 0, AssetMs = 0, TeardownMs = 0, EnterMs = 0; uint32_t AssetLoads = 0, Objects = 0; };
	ActivateTiming s_Timing;
	uint32_t AssetLoadCount() { const auto& s = ResourceManager::GetI()->Stats(); return s.Textures + s.Materials + s.MeshFiles; }
	Clock::time_point s_LastUpdate;
	bool s_HaveLastUpdate = false;
	int s_JustActivated = 0;   // 바로 앞 프레임에 바꾼 작업 — 이번 프레임 길이를 FrameAfterMs 로

	std::wstring ResolvePath(const std::wstring& path)
	{
		return fs::path(path).is_absolute() ? path : PathManager::GetI()->GetMovePathW(path);
	}

	void CollectStrings(const json& j, std::vector<const std::string*>& out)
	{
		if (j.is_string())
			out.push_back(&j.get_ref<const std::string&>());
		else if (j.is_array() || j.is_object())
			for (const auto& v : j)
				CollectStrings(v, out);
	}

	bool HasExt(const std::string& lower, std::initializer_list<const char*> exts)
	{
		for (const char* e : exts)
		{
			const size_t n = strlen(e);
			if (lower.size() > n && lower.compare(lower.size() - n, n, e) == 0)
				return true;
		}
		return false;
	}

	// 작업 스레드: 씬이 쓰는 텍스처 (씬 · 재질 파일에 적힌) 를 미리 디코드하고, 모델 캐시 파일을 미리 읽는다
	//  — 미리 짓는 메인 프레임이 디스크 · 디코드를 기다리지 않게 (Utils::PrefetchTexture · PrefetchFile)
	void PrefetchAssets(const json& scene)
	{
		std::vector<const std::string*> strings;
		CollectStrings(scene, strings);
		std::set<std::wstring> textures, files;
		std::set<std::string> materials;
		auto addTexture = [&](const std::string& rel) { textures.insert(PathManager::GetI()->GetMovePathW(string_to_wstring(rel))); };
		auto classify = [&](const std::string& s) {
			std::string lower = s;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			if (HasExt(lower, { ".mat" }))
				materials.insert(s);
			else if (HasExt(lower, { ".png", ".jpg", ".jpeg", ".tga", ".dds", ".bmp" }))
				addTexture(s);
			else if (HasExt(lower, { ".fbx", ".glb", ".gltf", ".vrm" }))
			{
				const std::wstring full = PathManager::GetI()->GetMovePathW(string_to_wstring(s));
				for (const wchar_t* ext : { L".mesh", L".animations", L".skeletons" })
					files.insert(full + ext);
			}
		};
		for (const std::string* s : strings)
			classify(*s);
		for (const std::string& m : materials)
		{
			try
			{
				std::ifstream is{ fs::path(PathManager::GetI()->GetMovePathW(string_to_wstring(m))), std::ios::binary };
				if (!is)
					continue;
				const json mat = json::parse(is);
				std::vector<const std::string*> matStrings;
				CollectStrings(mat, matStrings);
				for (const std::string* s : matStrings)
				{
					std::string lower = *s;
					std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
					if (HasExt(lower, { ".png", ".jpg", ".jpeg", ".tga", ".dds", ".bmp" }))
						addTexture(*s);
				}
			}
			catch (const std::exception&) {}
		}
		std::error_code ec;
		for (const std::wstring& t : textures)
			if (fs::exists(t, ec))
				Utils::PrefetchTexture(t);
		for (const std::wstring& f : files)
			if (fs::exists(f, ec))
				Utils::PrefetchFile(f);
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
			if (op->Async)
				PrefetchAssets(*op->Parsed);   // 텍스처 디코드 · 모델 캐시 읽기를 백그라운드에서 미리
		}
		catch (const std::exception& e)
		{
			op->Error = e.what();
		}
		op->Progress = 0.5f;   // 0.5 → 0.9 = 미리 짓기 (UpdateSceneOps)
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
	SceneStreaming::ClearPrewarmJobs();   // 미리 데우는 잡이 미리 지은 씬을 읽고 있을 수 있다 — 먼저 끝낸다
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
//  staged = 미리 지은 씬 (비동기 — 없으면 여기서 짓는다), deferredLights = 짓는 동안 모은 Light
struct SceneManagerRuntimeAccess
{
	static void ActivateSingle(SceneManager& sm, const std::wstring& path, const nlohmann::json& parsed, std::unique_ptr<Scene> staged, std::vector<std::shared_ptr<Light>>& deferredLights);
	static int ActivateAdditive(SceneManager& sm, const std::wstring& path, const nlohmann::json& parsed, std::unique_ptr<Scene> staged, std::vector<std::shared_ptr<Light>>& deferredLights);
	static void UnloadNow(SceneManager& sm, int handle);
};

namespace
{
	std::string HeapName(const std::wstring& path) { return wstring_to_string(fs::path(path).stem().wstring()); }

	void ApplyDeferredLights(std::vector<std::shared_ptr<Light>>& lights)
	{
		for (auto& l : lights)
			LightManager::GetI()->SetLight(l);
		lights.clear();
	}

	// 맨 앞 비동기 작업의 루트를 예산 (ms) 만큼 짓는다. 다 지었거나 실패하면 true (실패는 op.Error)
	//  Single = 새 씬의 힙에, Additive = 지금 씬의 힙에 (바꿔 끼울 때 그 씬으로 옮긴다). Light 는 모아 두었다가 바꿔 끼울 때
	bool StageStep(PendingOp& op, double budgetMs, SceneManager::SceneOp& st)
	{
		if (!op.Staging)
		{
			if (!Scene::IsSceneJson(*op.Parsed))
			{
				op.Error = "scene rootGameObjects must be an array";
				return true;
			}
			op.Staging = std::make_unique<Scene>();
			op.Staging->SetScenePath(op.Path);
			Memory::Heaps::Rename(op.Staging->Heap(), (HeapName(op.Path) + " (loading)").c_str());
			op.TotalRoots = op.Parsed->at("rootGameObjects").size();
			op.LegacyLayers = Scene::UsesLegacyLayers(*op.Parsed);
			st.Roots = (uint32_t)op.TotalRoots;
		}
		PROFILE_SCOPE("Scene.Stage");
		Scene* current = SceneManager::GetI()->GetCurrentScene();
		Memory::Heaps::ActiveScope heapScope(op.Additive && current ? current->Heap() : op.Staging->Heap());
		LightManager::GetI()->SetStagingSink(&op.DeferredLights);
		struct SinkReset { ~SinkReset() { LightManager::GetI()->SetStagingSink(nullptr); } } sinkReset;
		const nlohmann::json& roots = op.Parsed->at("rootGameObjects");
		const auto t0 = Clock::now();
		const double asset0 = ResourceManager::GetI()->Stats().TotalMs;
		const uint32_t loads0 = AssetLoadCount();
		try
		{
			while (op.NextRoot < op.TotalRoots)
			{
				const auto r0 = Clock::now();
				op.Staging->LoadRoot(roots[op.NextRoot], op.LegacyLayers);
				if (MsSince(r0) > 2.0 * budgetMs)   // 루트 하나가 예산을 크게 넘었다 (그 아래 오브젝트가 많거나 에셋을 처음 불러옴)
					EditorLog::Write("Scene", "staging root %zu '%s' took %.1f ms (budget %.0f ms)", op.NextRoot,
						op.Staging->RootGameObjects().empty() ? "?" : op.Staging->RootGameObjects().back()->GetName().c_str(), MsSince(r0), budgetMs);
				if (!op.Staging->RootGameObjects().empty())
					op.Staging->RootGameObjects().back()->SetStaged(true);   // 바꿔 끼울 때까지 꺼진 것으로 (지금 씬에 끼어들지 않게)
				++op.NextRoot;
				if (MsSince(t0) >= budgetMs)
					break;
			}
		}
		catch (const std::exception& e)
		{
			op.Error = e.what();
			return true;
		}
		// 다 지었으면 같은 예산 안에서 미리 데우기: 처음 Start · 그리기가 만드는 캐시 (Animator Humanoid 표 · 나무 메시)
		if (op.NextRoot >= op.TotalRoots && MsSince(t0) < budgetMs)
		{
			if (!op.PrewarmListed)
			{
				op.PrewarmObjects = op.Staging->GetAllGameObjects();
				op.PrewarmListed = true;
				// 물리: 바디 형상을 백그라운드 잡으로 미리 (바꿔 끼운 뒤 첫 동기화가 같은 서명 · 자리면 쓰고, 새 바디는 한 번에 넣는다)
				if (op.Additive == false)
					PhysicsManager::GetI()->PrebuildStaged(op.Staging.get());
			}
			while (op.NextPrewarm < op.PrewarmObjects.size())
			{
				for (auto& c : op.PrewarmObjects[op.NextPrewarm]->GetComponents())
					if (c)
					{
						const auto w0 = Clock::now();
						c->PrewarmStaged();
						const double wms = MsSince(w0);
						if (wms > 2.0 * budgetMs)   // 한 번에 예산을 크게 넘은 미리 데우기 (나눌 수 없다) — 무엇인지 남긴다
							EditorLog::Write("Scene", "prewarm %s on '%s' took %.1f ms (budget %.0f ms)", c->GetType().c_str(), op.PrewarmObjects[op.NextPrewarm]->GetName().c_str(), wms, budgetMs);
					}
				++op.NextPrewarm;
				if (MsSince(t0) >= budgetMs)
					break;
			}
		}
		const double ms = MsSince(t0);
		st.StageMs += ms;
		st.StageMaxMs = (std::max)(st.StageMaxMs, ms);
		++st.StageFrames;
		st.StageAssetMs += ResourceManager::GetI()->Stats().TotalMs - asset0;
		st.StageAssetLoads += AssetLoadCount() - loads0;
		return op.NextRoot >= op.TotalRoots && op.PrewarmListed && op.NextPrewarm >= op.PrewarmObjects.size() && SceneStreaming::PrewarmJobsDone();
	}
}

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

void SceneManagerRuntimeAccess::ActivateSingle(SceneManager& sm, const std::wstring& path, const nlohmann::json& parsed, std::unique_ptr<Scene> staged, std::vector<std::shared_ptr<Light>>& deferredLights)
{
	s_Timing = ActivateTiming();
	auto t0 = Clock::now();
	const double asset0 = ResourceManager::GetI()->Stats().TotalMs;
	const uint32_t loads0 = AssetLoadCount();
	std::unique_ptr<Scene> next = std::move(staged);
	if (!next)
	{
		// 동기 LoadScene (또는 미리 짓지 않은 작업): 여기서 — 새 씬의 힙에 (지금 씬의 힙에 지으면 그 씬을 지운 뒤에도 힙이 남는다)
		next = std::make_unique<Scene>();
		next->SetScenePath(path);
		Memory::Heaps::Rename(next->Heap(), HeapName(path).c_str());
		Memory::Heaps::ActiveScope heapScope(next->Heap());
		from_json(parsed, *next);
	}
	else
		Memory::Heaps::Rename(next->Heap(), HeapName(path).c_str());
	s_Timing.BuildMs = MsSince(t0);
	s_Timing.AssetMs = ResourceManager::GetI()->Stats().TotalMs - asset0;
	s_Timing.AssetLoads = AssetLoadCount() - loads0;
	t0 = Clock::now();

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
	s_Timing.TeardownMs = MsSince(t0);
	t0 = Clock::now();
	ScriptEngine::OnSceneSwapped();
	sm.m_pCurrScene = next.release();
	for (GameObject* r : sm.m_pCurrScene->RootGameObjects())
		r->SetStaged(false);
	ApplyDeferredLights(deferredLights);   // 미리 짓는 동안 모은 Light (앞 씬은 이제 없다)
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
	s_Timing.EnterMs = MsSince(t0);
	s_Timing.Objects = (uint32_t)sm.m_pCurrScene->GetAllGameObjects().size();
	EditorLog::Write("Scene", "entered %s (%zu objects) — build %.1f ms (assets %.1f ms, %u loads), teardown %.1f ms, enter %.1f ms", wstring_to_string(path).c_str(),
		sm.m_pCurrScene->GetAllGameObjects().size(), s_Timing.BuildMs, s_Timing.AssetMs, s_Timing.AssetLoads, s_Timing.TeardownMs, s_Timing.EnterMs);
}

int SceneManagerRuntimeAccess::ActivateAdditive(SceneManager& sm, const std::wstring& path, const nlohmann::json& parsed, std::unique_ptr<Scene> staged, std::vector<std::shared_ptr<Light>>& deferredLights)
{
	if (sm.m_pCurrScene == nullptr)
		return 0;
	s_Timing = ActivateTiming();
	auto t0 = Clock::now();
	const double asset0 = ResourceManager::GetI()->Stats().TotalMs;
	const uint32_t loads0 = AssetLoadCount();
	std::vector<GameObject*> roots;
	if (staged)
	{
		roots = staged->ReleaseAll();   // 미리 지은 루트 (지금 씬의 힙에 지었다) — staged 는 빈 채로 지워진다
		staged.reset();
	}
	else
	{
		Scene temp;
		temp.SetScenePath(path);
		Memory::Heaps::ActiveScope heapScope(sm.m_pCurrScene->Heap());   // 지금 씬으로 옮기므로 그 힙에 (temp 의 힙에 지으면 temp 를 지운 뒤 남는다)
		from_json(parsed, temp);
		roots = temp.ReleaseAll();   // temp 는 빈 채로 지워진다
	}
	for (GameObject* r : roots)
		r->SetStaged(false);
	ApplyDeferredLights(deferredLights);
	s_Timing.BuildMs = MsSince(t0);
	s_Timing.AssetMs = ResourceManager::GetI()->Stats().TotalMs - asset0;
	s_Timing.AssetLoads = AssetLoadCount() - loads0;
	t0 = Clock::now();
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
	s_Timing.EnterMs = MsSince(t0);
	s_Timing.Objects = (uint32_t)objects.size();
	EditorLog::Write("Scene", "added %s (handle %d, %zu objects) — build %.1f ms (assets %.1f ms, %u loads), enter %.1f ms", wstring_to_string(path).c_str(), handle, objects.size(),
		s_Timing.BuildMs, s_Timing.AssetMs, s_Timing.AssetLoads, s_Timing.EnterMs);
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
	// 프레임 길이 (이 함수는 프레임마다 한 번): 기다리는 작업의 가장 긴 프레임 · 바꾼 다음 프레임
	const Clock::time_point now = Clock::now();
	const double frameMs = s_HaveLastUpdate ? std::chrono::duration<double, std::milli>(now - s_LastUpdate).count() : 0.0;
	s_LastUpdate = now;
	s_HaveLastUpdate = true;
	if (s_JustActivated)
	{
		if (auto it = s_Ops.find(s_JustActivated); it != s_Ops.end())
			it->second.FrameAfterMs = frameMs;
		s_JustActivated = 0;
	}
	for (auto& p : s_Pending)
	{
		SceneOp& st = s_Ops[p->Id];
		st.MaxWaitFrameMs = (std::max)(st.MaxWaitFrameMs, frameMs);
		++st.WaitFrames;
	}
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
		// 비동기: 차례가 온 작업의 씬을 프레임마다 예산 (Application.backgroundLoadingPriority) 만큼 미리 짓는다 — 다 지을 때까지 0.5 → 0.9
		// (개발/비교용) NOVA_DEV_NO_STAGING=1: 예전처럼 바꿔 끼우는 프레임에 다 짓는다 (파싱만 작업 스레드)
		static const bool s_NoStaging = [] { char v[8] = {}; return ::GetEnvironmentVariableA("NOVA_DEV_NO_STAGING", v, sizeof(v)) > 0 && v[0] == '1'; }();
		if (op.Async && !s_NoStaging && op.Error.empty() && op.Parsed && !op.StagingDone)
		{
			const bool built = StageStep(op, Application::BackgroundLoadingBudgetMs(), st);
			const float builtRatio = op.TotalRoots ? (float)op.NextRoot / (float)op.TotalRoots : 1.0f;
			const float warmed = op.PrewarmObjects.empty() ? (op.PrewarmListed ? 1.0f : 0.0f) : (float)op.NextPrewarm / (float)op.PrewarmObjects.size();
			st.Progress = 0.5f + 0.35f * builtRatio + 0.05f * warmed;   // 짓기 0.5 → 0.85, 미리 데우기 → 0.9
			if (!built)
				break;   // 다음 프레임에 이어서 (뒤의 작업도 기다린다 — 요청한 차례)
			op.StagingDone = true;
			SceneStreaming::ClearPrewarmJobs();   // 다 끝났다
		}
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
		const Clock::time_point activate0 = Clock::now();
		PROFILE_SCOPE("Scene.Activate");
		try
		{
			if (owned->Additive)
				handle = SceneManagerRuntimeAccess::ActivateAdditive(*this, owned->Path, *owned->Parsed, std::move(owned->Staging), owned->DeferredLights);
			else
			{
				SceneManagerRuntimeAccess::ActivateSingle(*this, owned->Path, *owned->Parsed, std::move(owned->Staging), owned->DeferredLights);
				handle = s_Active;
			}
		}
		catch (const std::exception& e)
		{
			Debug::LogError("SceneManager: could not load '" + wstring_to_string(owned->Path) + "': " + e.what());
			FinishOp(*owned, true, 0);
			continue;
		}
		{
			SceneOp& done = s_Ops[owned->Id];
			done.ActivateMs = MsSince(activate0);
			done.BuildMs = s_Timing.BuildMs;
			done.AssetMs = s_Timing.AssetMs;
			done.AssetLoads = s_Timing.AssetLoads;
			done.TeardownMs = s_Timing.TeardownMs;
			done.EnterMs = s_Timing.EnterMs;
			done.Objects = s_Timing.Objects;
			s_JustActivated = owned->Id;
		}
		FinishOp(*owned, handle == 0, handle);
	}
	if (s_Pending.empty())
		Utils::ClearPrefetched();   // 불러오기가 다 끝났다 — 쓰지 않은 미리 디코드한 이미지를 버린다
}
