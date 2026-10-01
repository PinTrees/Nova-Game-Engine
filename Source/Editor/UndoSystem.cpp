#include "pch.h"
#include "UndoSystem.h"
#include "NovaCodeWindow.h"
#include "DisplayManager.h"
#include "Profiler.h"
#include "FrameProfiler.h"
#include <chrono>
#include <unordered_set>

namespace
{
	constexpr size_t kMaxRecords = 300;
	constexpr size_t kMaxBytes = 256ull * 1024 * 1024;

	std::vector<Undo::Record> s_Undo, s_Redo;
	size_t s_Bytes = 0;

	// ---- 씬 감시 ----
	Scene* s_Scene = nullptr;
	std::wstring s_ScenePath;
	bool s_WasPlaying = false;
	std::string s_PendingName;
	bool s_Requested = false;
	bool s_PrevInteracting = false;
	int s_CheckFrames = 0;
	int s_Frame = 0;

	// ---- 에셋 감시 ----
	struct Tracker
	{
		std::string Label;
		std::function<std::string()> Capture;
		std::function<void(const std::string&)> Restore;
		std::string Committed;
		int LastFrame = 0;
	};
	std::map<std::string, Tracker> s_Trackers;

	size_t s_CommittedHash = 0;

	// ---- 씬 스냅샷: 루트 GameObject 마다 JSON 문자열 ----
	//  씬 JSON = {"rootGameObjects":[루트1,루트2,...]} (자식은 루트 안에). 루트마다 dump 한 문자열을 이어 붙이면
	//  json(scene).dump() 와 바이트까지 같다. 스냅샷 = 루트 (fileID, 문자열 포인터) 목록이고 문자열은 캐시·기록끼리 같이 쓴다
	//  → 바뀌지 않은 루트는 기록마다 복사하지 않는다 (기록 메모리 = 바뀐 루트만)
	//
	//  확정(조작이 끝날 때)은 바뀌었을 수 있는 루트만 다시 직렬화한다:
	//   - 지난 확정 뒤 선택된 적이 있는 오브젝트의 루트 (Inspector·핸들 편집은 선택한 오브젝트에 한다)
	//   - 서명이 바뀐 루트: 오브젝트·컴포넌트 포인터, fileID, 이름, 활성, 컴포넌트 켜짐, 로컬 위치/회전/크기 (직렬화 없이 싸게 계산)
	//   - 확정마다 다른 루트 몇 개를 돌아가며 (위 둘에 안 걸리는 변경도 결국 잡힌다)
	//  Undo/Redo 직전 확정과 씬이 바뀔 때는 전체를 다시 직렬화한다 (놓친 변경이 사라지지 않게)
	//
	//  되돌리기는 지금 스냅샷과 목표 스냅샷을 루트 fileID 로 맞춰 다른 루트만 지우고 JSON 에서 다시 만든다
	//  (예전: 씬 전체를 지우고 다시 만듦). 절반 넘게 다르면 씬 전체 복원. 오브젝트 사이 참조는 fileID 로 찾으므로 안전하다
	using TextPtr = std::shared_ptr<const std::string>;
	struct RootSnap
	{
		uint64 FileID = 0;
		TextPtr Text;
	};
	using SceneSnap = std::vector<RootSnap>;

	SceneSnap s_Committed;
	bool s_HasCommitted = false;
	uint64_t s_CommittedVersion = 0;   // 확정 스냅샷이 바뀔 때마다 1 씩 (해시 다시 계산 판단)

	struct RootCache
	{
		uint64_t Signature = 0;
		TextPtr Text;
		uint32_t Seen = 0;   // 마지막으로 본 캡처 번호 (없어진 루트 정리용)
	};
	uint32_t s_CaptureGen = 0;
	std::unordered_map<const GameObject*, RootCache> s_RootCache;
	const Scene* s_RootCacheScene = nullptr;
	std::unordered_set<const GameObject*> s_TouchedRoots;   // 지난 확정 뒤 선택됐던 오브젝트의 루트 (포인터 비교만, 역참조 안 함)
	size_t s_SweepCursor = 0;
	constexpr size_t kSweepRoots = 8;
	size_t s_LastSerialized = 0, s_LastRoots = 0;   // 마지막 캡처에서 다시 직렬화한 루트 / 전체 (진단용)

	uint64_t Mix(uint64_t h, uint64_t v) { return h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2)); }
	uint64_t Bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

	void Signature(GameObject* go, uint64_t& h)
	{
		h = Mix(h, (uint64_t)(uintptr_t)go);
		h = Mix(h, go->GetFileID());
		h = Mix(h, std::hash<std::string>()(go->GetName()));
		h = Mix(h, go->IsActive() ? 1 : 2);
		for (const auto& c : go->GetComponents())
		{
			h = Mix(h, (uint64_t)(uintptr_t)c.get());
			h = Mix(h, c->IsEnabled() ? 3 : 4);
		}
		if (Transform* t = go->GetTransform())
		{
			const Vec3 p = t->GetLocalPosition(), s = t->GetLocalScale();
			const Quaternion q = t->GetLocalRotation();
			for (float f : { p.x, p.y, p.z, s.x, s.y, s.z, q.x, q.y, q.z, q.w })
				h = Mix(h, Bits(f));
		}
		for (GameObject* child : go->Children())
			Signature(child, h);
	}

	const GameObject* RootOf(GameObject* go)
	{
		while (go && go->GetParent())
			go = go->GetParent();
		return go;
	}

	bool SameText(const TextPtr& a, const TextPtr& b) { return a == b || (a && b && *a == *b); }

	// full = 모든 루트를 다시 직렬화
	SceneSnap CaptureSnap(bool full)
	{
		SceneSnap snap;
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr)
			return snap;
		if (scene != s_RootCacheScene)
		{
			s_RootCache.clear();
			s_RootCacheScene = scene;
			full = true;
		}
		const auto& roots = scene->RootGameObjects();
		const size_t n = roots.size();
		s_LastSerialized = 0;
		s_LastRoots = n;
		const size_t sweepBegin = n ? s_SweepCursor % n : 0;
		const uint32_t gen = ++s_CaptureGen;
		snap.reserve(n);
		for (size_t i = 0; i < n; ++i)
		{
			GameObject* root = roots[i];
			uint64_t sig = 1469598103934665603ull;
			Signature(root, sig);
			RootCache& entry = s_RootCache[root];   // 제자리에서 갱신 (맵을 매번 새로 만들지 않는다)
			entry.Seen = gen;
			const bool swept = ((i + n - sweepBegin) % n) < kSweepRoots;
			if (full || !entry.Text || entry.Signature != sig || swept || s_TouchedRoots.count(root))
			{
				++s_LastSerialized;
				json j = *root;
				std::string text = j.dump();
				if (!entry.Text || *entry.Text != text)   // 같으면 예전 문자열을 그대로 (기록끼리 공유)
					entry.Text = std::make_shared<const std::string>(std::move(text));
				entry.Signature = sig;
			}
			snap.push_back({ root->GetFileID(), entry.Text });
		}
		if (s_RootCache.size() > n)   // 없어진 루트는 버린다
			for (auto it = s_RootCache.begin(); it != s_RootCache.end();)
				it = it->second.Seen != gen ? s_RootCache.erase(it) : std::next(it);
		s_SweepCursor = sweepBegin + kSweepRoots;
		s_TouchedRoots.clear();
		return snap;
	}

	bool SameSnap(const SceneSnap& a, const SceneSnap& b)
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); ++i)
			if (a[i].FileID != b[i].FileID || !SameText(a[i].Text, b[i].Text))
				return false;
		return true;
	}

	std::string Assemble(const SceneSnap& snap)
	{
		size_t size = 32;
		for (const RootSnap& r : snap)
			size += r.Text->size() + 1;
		std::string out;
		out.reserve(size);
		out = "{\"rootGameObjects\":[";
		for (size_t i = 0; i < snap.size(); ++i)
		{
			if (i > 0)
				out += ',';
			out += *snap[i].Text;
		}
		out += "]}";
		return out;
	}

	// 두 스냅샷에서 서로 다른 루트 문자열만 (기록 이름 추측·메모리 계산용)
	void Difference(const SceneSnap& before, const SceneSnap& after, std::string& onlyBefore, std::string& onlyAfter, size_t& bytes)
	{
		std::unordered_map<uint64, const TextPtr*> afterByID;
		for (const RootSnap& r : after)
			afterByID[r.FileID] = &r.Text;
		std::unordered_set<uint64> same;
		for (const RootSnap& r : before)
		{
			auto it = afterByID.find(r.FileID);
			if (it != afterByID.end() && SameText(r.Text, *it->second))
				same.insert(r.FileID);
			else
				onlyBefore += *r.Text;
		}
		for (const RootSnap& r : after)
			if (!same.count(r.FileID))
				onlyAfter += *r.Text;
		bytes = onlyBefore.size() + onlyAfter.size() + (before.size() + after.size()) * sizeof(RootSnap);
	}

	size_t Count(const std::string& text, const char* token)
	{
		size_t n = 0;
		for (size_t p = text.find(token); p != std::string::npos; p = text.find(token, p + 1))
			++n;
		return n;
	}

	// 기록 이름: 도구가 알려 준 이름, 없으면 바뀐 내용으로 추측 (Unity 의 "Undo Create GameObject" 등)
	std::string SceneChangeName(const std::string& before, const std::string& after)
	{
		if (!s_PendingName.empty())
			return s_PendingName;
		const size_t objBefore = Count(before, "\"fileID\""), objAfter = Count(after, "\"fileID\"");
		if (objAfter > objBefore) return objAfter - objBefore == 1 ? "Create GameObject" : "Create GameObjects";
		if (objAfter < objBefore) return "Delete GameObject";
		const size_t compBefore = Count(before, "\"type\""), compAfter = Count(after, "\"type\"");
		if (compAfter > compBefore) return "Add Component";
		if (compAfter < compBefore) return "Remove Component";
		if (GameObject* go = SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT ? SelectionManager::GetSelectedGameObject() : nullptr)
			return "Modify " + go->GetName();
		return "Modify Scene";
	}

	void TrimHistory()
	{
		while (!s_Undo.empty() && (s_Undo.size() > kMaxRecords || s_Bytes > kMaxBytes))
		{
			s_Bytes -= (std::min)(s_Bytes, s_Undo.front().Bytes);
			s_Undo.erase(s_Undo.begin());
		}
	}

	void SetCommitted(SceneSnap snap)
	{
		s_Committed = std::move(snap);
		s_HasCommitted = true;
		++s_CommittedVersion;
	}

	// Hierarchy 펼침 상태 (fileID → 펼침)
	void CollectExpanded(GameObject* go, std::unordered_map<uint64, bool>& out)
	{
		out[go->GetFileID()] = go->m_Editor_HierachOpened;
		for (GameObject* child : go->Children())
			CollectExpanded(child, out);
	}
	void ApplyExpanded(GameObject* go, const std::unordered_map<uint64, bool>& in)
	{
		if (auto it = in.find(go->GetFileID()); it != in.end())
			go->m_Editor_HierachOpened = it->second;
		for (GameObject* child : go->Children())
			ApplyExpanded(child, in);
	}

	// 씬을 목표 스냅샷으로: 다른 루트만 지우고 다시 만든다
	void RestoreScene(const SceneSnap& target)
	{
		const auto t0 = std::chrono::steady_clock::now();
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr)
			return;
		const SceneSnap current = CaptureSnap(false);   // 캐시를 지금 씬에 맞춘다 (Redo 앞에 확정 안 된 변경이 있어도)
		const auto& roots = scene->RootGameObjects();

		// 루트마다 그대로 둘지 결정 (fileID 와 문자열이 같으면 그대로)
		std::unordered_map<uint64, size_t> currentByID;
		for (size_t i = 0; i < current.size(); ++i)
			currentByID[current[i].FileID] = i;
		std::vector<int> keepIndex(target.size(), -1);
		size_t rebuild = 0;
		for (size_t t = 0; t < target.size(); ++t)
		{
			auto it = currentByID.find(target[t].FileID);
			if (it != currentByID.end() && SameText(current[it->second].Text, target[t].Text))
				keepIndex[t] = (int)it->second;
			else
				++rebuild;
		}
		const size_t removed = current.size() - (target.size() - rebuild);
		if (rebuild + removed > (std::max)(current.size(), target.size()) / 2 + 1)
		{
			// 절반 넘게 다르면 씬 전체를 다시 만든다
			SceneManager::GetI()->RestoreSceneState(Assemble(target));
			s_Scene = SceneManager::GetI()->GetCurrentScene();
			SetCommitted(CaptureSnap(true));
			EditorLog::Write("Undo", "full restore (%zu roots, %.1f ms)", target.size(),
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
			return;
		}

		// 선택·펼침은 fileID 로 이어 간다
		GameObject* selected = SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT ? SelectionManager::GetSelectedGameObject() : nullptr;
		const uint64 selectedID = selected ? selected->GetFileID() : 0;
		std::unordered_set<const GameObject*> keep;
		for (int k : keepIndex)
			if (k >= 0)
				keep.insert(roots[k]);
		std::unordered_map<uint64, bool> expanded;
		std::vector<GameObject*> toRemove;
		for (GameObject* root : roots)
			if (!keep.count(root))
			{
				CollectExpanded(root, expanded);
				toRemove.push_back(root);
			}
		if (selected && !keep.count(RootOf(selected)))
			SelectionManager::ClearSelection();

		// 다시 만들 루트 (JSON → GameObject), 순서는 목표 그대로
		std::vector<GameObject*> order;
		order.reserve(target.size());
		for (size_t t = 0; t < target.size(); ++t)
		{
			if (keepIndex[t] >= 0)
			{
				order.push_back(roots[keepIndex[t]]);
				continue;
			}
			json j = json::parse(*target[t].Text, nullptr, false);
			if (j.is_discarded())
				continue;
			GameObject* go = new GameObject();
			from_json(j, *go);
			scene->AddRootGameObject(go);
			ApplyExpanded(go, expanded);
			order.push_back(go);
		}
		// 바뀐 옛 루트는 지운다 (Hierarchy 의 삭제와 같은 경로: OnDestroy 로 빛·카메라 등록 해제)
		for (GameObject* root : toRemove)
			scene->DestroyGameObject(root);
		scene->SetRootOrder(order);

		if (selectedID != 0 && SelectionManager::GetSelectedGameObject() == nullptr)
			if (GameObject* again = scene->FindByFileID(selectedID))
				SelectionManager::SetSelectedGameObject(again);
		if (rebuild > 0 || !toRemove.empty())
			DisplayManager::GetI()->Init();   // 카메라 목록

		s_Scene = scene;
		SetCommitted(CaptureSnap(false));   // 그대로 둔 루트는 캐시, 새 루트만 직렬화
		EditorLog::Write("Undo", "partial restore: rebuilt %zu, removed %zu of %zu roots (%.1f ms)", rebuild, toRemove.size(), target.size(),
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
	}

	// 씬이 다른 것으로 바뀌었는지 (씬 열기/새 씬은 기록을 비우고, Play/Stop 이나 우리 복원은 기준만 다시 잡는다)
	void TrackScene()
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		const bool playing = Application::IsPlaying();
		if (scene != s_Scene)
		{
			const std::wstring path = scene ? scene->GetScenePath() : std::wstring();
			const bool playTransition = playing || s_WasPlaying;
			if (s_Scene != nullptr && !playTransition && path != s_ScenePath)
				Undo::Clear();
			s_Scene = scene;
			s_ScenePath = path;
			if (scene)
				SetCommitted(CaptureSnap(true));
			else
				s_HasCommitted = false;
		}
		s_WasPlaying = playing;
	}

	void Commit(bool full = false)
	{
		PROFILE_SCOPE("Undo.Commit");
		TrackScene();
		if (Application::IsPlaying())
			return;

		// 씬 (바뀌었을 수 있는 루트만 다시 직렬화)
		if (SceneManager::GetI()->GetCurrentScene() != nullptr)
		{
			const auto t0 = std::chrono::steady_clock::now();
			SceneSnap now = CaptureSnap(full);
			if (FrameProfiler::Enabled())   // NOVA_DEV_PROFILE=1: 확정마다 걸린 시간
				EditorLog::Write("Undo", "capture %.2f ms (%zu / %zu roots serialized%s)",
					std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(), s_LastSerialized, s_LastRoots, full ? ", full" : "");
			if (!s_HasCommitted || !SameSnap(now, s_Committed))
			{
				const SceneSnap before = s_Committed, after = now;
				std::string onlyBefore, onlyAfter;
				size_t bytes = 0;
				Difference(before, after, onlyBefore, onlyAfter, bytes);
				Undo::Record r;
				r.Name = SceneChangeName(onlyBefore, onlyAfter);
				r.UndoAction = [before]() { RestoreScene(before); };
				r.RedoAction = [after]() { RestoreScene(after); };
				r.Bytes = bytes;
				const bool hadBaseline = s_HasCommitted;
				SetCommitted(std::move(now));
				if (hadBaseline)
					Undo::Push(std::move(r));
			}
		}
		s_PendingName.clear();

		// 에셋
		for (auto& [key, t] : s_Trackers)
		{
			if (s_Frame - t.LastFrame > 2)
				continue;
			const std::string cur = t.Capture();
			if (cur == t.Committed)
				continue;
			const std::string before = t.Committed, after = cur;
			const std::string k = key;
			Undo::Record r;
			r.Name = "Modify " + t.Label;
			// 창을 닫아 감시가 끝난 뒤에도 되돌릴 수 있도록 복원 함수를 기록에 담아 둔다
			auto restore = t.Restore;
			auto apply = [k, restore](const std::string& text) {
				restore(text);
				auto it = s_Trackers.find(k);
				if (it != s_Trackers.end())
					it->second.Committed = it->second.Capture();
			};
			r.UndoAction = [apply, before]() { apply(before); };
			r.RedoAction = [apply, after]() { apply(after); };
			r.Bytes = before.size() + after.size();
			t.Committed = cur;
			Undo::Push(std::move(r));
		}
	}

	void Rebaseline()
	{
		// 씬 복원(RestoreScene)은 이미 새 기준을 잡았다 → 씬이 그대로면 다시 직렬화하지 않는다
		if (s_Scene != SceneManager::GetI()->GetCurrentScene() || !s_HasCommitted)
		{
			s_Scene = SceneManager::GetI()->GetCurrentScene();
			SetCommitted(CaptureSnap(true));
		}
		for (auto& [key, t] : s_Trackers)
			t.Committed = t.Capture();
	}
}

namespace Undo
{
	void Push(Record record)
	{
		EditorLog::Write("Undo", "record '%s' (%zu bytes, history %zu)", record.Name.c_str(), record.Bytes, s_Undo.size() + 1);
		s_Bytes += record.Bytes;
		s_Undo.push_back(std::move(record));
		s_Redo.clear();
		TrimHistory();
	}

	bool CanUndo() { return !s_Undo.empty(); }
	bool CanRedo() { return !s_Redo.empty(); }
	std::string UndoName() { return s_Undo.empty() ? std::string() : s_Undo.back().Name; }
	std::string RedoName() { return s_Redo.empty() ? std::string() : s_Redo.back().Name; }
	int HistoryCount() { return (int)s_Undo.size(); }

	bool CommittedSceneHash(size_t& outHash)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr || s_Scene != scene || !s_HasCommitted)
			return false;
		// 확정 스냅샷이 바뀌었을 때만 이어 붙여 해시 (json(scene).dump() 의 해시와 같다 → 저장 시점 해시와 비교)
		static uint64_t s_HashedVersion = ~0ull;
		if (s_HashedVersion != s_CommittedVersion)
		{
			s_HashedVersion = s_CommittedVersion;
			s_CommittedHash = std::hash<std::string>()(Assemble(s_Committed));
		}
		outHash = s_CommittedHash;
		return true;
	}

	bool PerformUndo()
	{
		if (Application::IsPlaying())
			return false;
		Commit(true);   // 아직 확정되지 않은 변경이 있으면 먼저 한 단계로 (전체 비교: 놓친 변경이 없게)
		if (s_Undo.empty())
			return false;
		Record r = std::move(s_Undo.back());
		s_Undo.pop_back();
		s_Bytes -= (std::min)(s_Bytes, r.Bytes);
		EditorLog::Write("Undo", "undo '%s'", r.Name.c_str());
		if (r.UndoAction)
			r.UndoAction();
		s_Redo.push_back(std::move(r));
		Rebaseline();
		return true;
	}

	bool PerformRedo()
	{
		if (Application::IsPlaying() || s_Redo.empty())
			return false;
		Record r = std::move(s_Redo.back());
		s_Redo.pop_back();
		EditorLog::Write("Undo", "redo '%s'", r.Name.c_str());
		if (r.RedoAction)
			r.RedoAction();
		s_Bytes += r.Bytes;
		s_Undo.push_back(std::move(r));
		Rebaseline();
		return true;
	}

	void Clear()
	{
		EditorLog::Write("Undo", "clear history (%zu undo, %zu redo)", s_Undo.size(), s_Redo.size());
		s_Undo.clear();
		s_Redo.clear();
		s_Bytes = 0;
	}

	void SetActionName(const std::string& name) { s_PendingName = name; }
	void RequestCheck() { s_Requested = true; }

	void WatchAsset(const std::string& key, const std::string& label,
		std::function<std::string()> capture, std::function<void(const std::string&)> restore)
	{
		auto it = s_Trackers.find(key);
		if (it == s_Trackers.end() || s_Frame - it->second.LastFrame > 2)
		{
			Tracker t;
			t.Label = label;
			t.Capture = capture;
			t.Restore = restore;
			t.Committed = capture();
			t.LastFrame = s_Frame;
			s_Trackers[key] = std::move(t);
			return;
		}
		it->second.Capture = std::move(capture);
		it->second.Restore = std::move(restore);
		it->second.LastFrame = s_Frame;
	}

	void Update()
	{
		++s_Frame;
		TrackScene();
		// 선택된 오브젝트의 루트는 다음 확정에서 다시 직렬화한다 (값을 바꾼 뒤 다른 오브젝트를 눌러도 놓치지 않게)
		if (SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT)
			if (GameObject* go = SelectionManager::GetSelectedGameObject())
				s_TouchedRoots.insert(RootOf(go));

		ImGuiIO& io = ImGui::GetIO();
		// 단축키 (글자 입력 중이면 입력 칸의 자체 Undo 에 맡긴다)
		if (!io.WantTextInput && io.KeyCtrl && !NovaCodeWindow::IsFocused())   // NOVA Code 는 자체 Undo
		{
			if (ImGui::IsKeyPressed(ImGuiKey_Z, false) && !io.KeyShift)
				PerformUndo();
			else if (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (ImGui::IsKeyPressed(ImGuiKey_Z, false) && io.KeyShift))
				PerformRedo();
		}

		// 조작이 끝난 순간 (위젯을 놓음 / 마우스·키를 뗌) 확정 검사. 한 프레임 늦게 반영되는 동작을 위해 다음 프레임에도 한 번 더.
		const bool interacting = ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
			ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle);
		bool keyReleased = false;
		for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END && !keyReleased; ++k)
			keyReleased = ImGui::IsKeyReleased((ImGuiKey)k);
		if ((s_PrevInteracting && !interacting) || keyReleased || s_Requested)
			s_CheckFrames = 2;
		s_Requested = false;
		if (s_CheckFrames > 0 && !interacting)
		{
			--s_CheckFrames;
			Commit();
		}
		s_PrevInteracting = interacting;

		// 오래 안 쓴 에셋 감시 정리
		for (auto it = s_Trackers.begin(); it != s_Trackers.end();)
			it = s_Frame - it->second.LastFrame > 600 ? s_Trackers.erase(it) : std::next(it);
	}
}
