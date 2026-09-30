#include "pch.h"
#include "UndoSystem.h"
#include "NovaCodeWindow.h"

namespace
{
	constexpr size_t kMaxRecords = 300;
	constexpr size_t kMaxBytes = 256ull * 1024 * 1024;

	std::vector<Undo::Record> s_Undo, s_Redo;
	size_t s_Bytes = 0;

	// ---- 씬 감시 ----
	Scene* s_Scene = nullptr;
	std::wstring s_ScenePath;
	std::string s_SceneCommitted;
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

	std::string CaptureScene()
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr)
			return std::string();
		json j = *scene;
		return j.dump();
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

	void RestoreScene(const std::string& text)
	{
		SceneManager::GetI()->RestoreSceneState(text);
		s_Scene = SceneManager::GetI()->GetCurrentScene();
		s_SceneCommitted = CaptureScene();
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
			s_SceneCommitted = CaptureScene();
		}
		s_WasPlaying = playing;
	}

	void Commit()
	{
		TrackScene();
		if (Application::IsPlaying())
			return;

		// 씬
		const std::string now = CaptureScene();
		if (!now.empty() && now != s_SceneCommitted)
		{
			const std::string before = s_SceneCommitted, after = now;
			Undo::Record r;
			r.Name = SceneChangeName(before, after);
			r.UndoAction = [before]() { RestoreScene(before); };
			r.RedoAction = [after]() { RestoreScene(after); };
			r.Bytes = before.size() + after.size();
			s_SceneCommitted = now;
			Undo::Push(std::move(r));
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
		s_Scene = SceneManager::GetI()->GetCurrentScene();
		s_SceneCommitted = CaptureScene();
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

	bool PerformUndo()
	{
		if (Application::IsPlaying())
			return false;
		Commit();   // 아직 확정되지 않은 변경이 있으면 먼저 한 단계로
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
