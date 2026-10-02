#include "pch.h"
#include "AutoSave.h"
#include "ImportSettingsInspector.h"
#include "ResourceManager.h"
#include "PathManager.h"
#include "EditorPrefs.h"
#include "Debug.h"
#include <filesystem>
#include <fstream>
#include <chrono>

namespace fs = std::filesystem;

namespace
{
	bool s_Initialized = false;
	double s_LastSave = 0.0;      // 초 (GetTickCount64)

	// 지난 세션(충돌)의 autosave
	struct Pending
	{
		bool Active = false;
		std::wstring Pid;
		std::wstring ScenePath;   // 원래 씬 경로 (빈 문자열 = 저장한 적 없는 씬)
		std::string SceneName;
		long long Time = 0;       // 유닉스 초
		int Objects = 0;
	};
	Pending s_Pending;
	bool s_PromptOpened = false;

	double Now() { return ::GetTickCount64() / 1000.0; }
	long long UnixNow() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
	std::wstring MyPid() { return std::to_wstring(::GetCurrentProcessId()); }
	std::wstring SessionFile(const std::wstring& pid) { return AutoSave::Folder() + L"\\session_" + pid + L".json"; }
	std::wstring SceneFile(const std::wstring& pid) { return AutoSave::Folder() + L"\\autosave_" + pid + L".scene"; }
	std::wstring MetaFile(const std::wstring& pid) { return AutoSave::Folder() + L"\\autosave_" + pid + L".json"; }

	std::string ReadText(const std::wstring& p)
	{
		std::ifstream in(p, std::ios::binary);
		return in ? std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()) : std::string();
	}

	bool WriteAtomic(const std::wstring& path, const std::string& text)
	{
		const std::wstring tmp = path + L".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out)
				return false;
			out << text;
			if (!out)
				return false;
		}
		return ::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
	}

	void RemoveSession(const std::wstring& pid)
	{
		std::error_code ec;
		fs::remove(SessionFile(pid), ec);
		fs::remove(SceneFile(pid), ec);
		fs::remove(MetaFile(pid), ec);
	}

	// pid 의 에디터가 아직 살아 있는지 (pid 가 다른 프로그램에 다시 쓰였으면 죽은 것으로)
	bool EditorAlive(DWORD pid)
	{
		HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
		if (h == nullptr)
			return ::GetLastError() == ERROR_ACCESS_DENIED;
		DWORD code = 0;
		bool alive = ::GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
		if (alive)
		{
			wchar_t name[MAX_PATH] = {};
			DWORD n = MAX_PATH;
			if (::QueryFullProcessImageNameW(h, 0, name, &n))
			{
				std::wstring s(name);
				std::transform(s.begin(), s.end(), s.begin(), ::towlower);
				alive = s.find(L"novaengine") != std::wstring::npos;
			}
		}
		::CloseHandle(h);
		return alive;
	}

	long long FileUnixTime(const std::wstring& path)
	{
		std::error_code ec;
		const auto ft = fs::last_write_time(path, ec);
		if (ec)
			return 0;
		const auto st = std::chrono::clock_cast<std::chrono::system_clock>(ft);
		return std::chrono::duration_cast<std::chrono::seconds>(st.time_since_epoch()).count();
	}

	bool WriteAutosave(std::string* error)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr)
		{
			if (error) *error = "no scene";
			return false;
		}
		std::error_code ec;
		fs::create_directories(AutoSave::Folder(), ec);
		json j = *scene;
		const std::string text = j.dump();
		const std::wstring pid = MyPid();
		if (!WriteAtomic(SceneFile(pid), text))
		{
			if (error) *error = "cannot write " + wstring_to_string(SceneFile(pid));
			return false;
		}
		json meta = {
			{ "scenePath", wstring_to_string(scene->GetScenePath()) },
			{ "sceneName", wstring_to_string(scene->GetName()) },
			{ "time", UnixNow() },
			{ "objects", (int)scene->GetAllGameObjects().size() },
		};
		WriteAtomic(MetaFile(pid), meta.dump(2));
		s_LastSave = Now();
		EditorLog::Write("AutoSave", "saved '%s' (%zu KB, %d objects)", wstring_to_string(scene->GetName()).c_str(), text.size() / 1024, (int)scene->GetAllGameObjects().size());
		return true;
	}

	void EmergencyWork() { WriteAutosave(nullptr); }

	std::string TimeText(long long t)
	{
		const time_t tt = (time_t)t;
		tm local = {};
		localtime_s(&local, &tt);
		char buf[64];
		strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &local);
		return buf;
	}
}

namespace AutoSave
{
	std::wstring Folder() { return PathManager::GetI()->GetMovePathW(L"Library\\AutoSave"); }

	bool Enabled() { return EditorPrefs::GetBool("AutoSave.Enabled", true); }
	void SetEnabled(bool enabled) { EditorPrefs::SetBool("AutoSave.Enabled", enabled); }
	int IntervalMinutes() { return std::clamp(EditorPrefs::GetInt("AutoSave.IntervalMinutes", 5), 1, 60); }
	void SetIntervalMinutes(int minutes) { EditorPrefs::SetInt("AutoSave.IntervalMinutes", std::clamp(minutes, 1, 60)); }

	void Init()
	{
		if (Application::IsPlayer())
			return;
		std::error_code ec;
		fs::create_directories(Folder(), ec);
		const std::wstring me = MyPid();

		// 지난 세션: 주인이 없는 session 파일 = 충돌·강제 종료로 끝남
		Pending best;
		for (const auto& e : fs::directory_iterator(Folder(), ec))
		{
			const std::wstring name = e.path().filename().wstring();
			if (name.rfind(L"session_", 0) != 0 || e.path().extension() != L".json")
				continue;
			const std::wstring pid = name.substr(8, name.size() - 8 - 5);
			if (pid == me || EditorAlive((DWORD)_wtoi(pid.c_str())))
				continue;   // 같은 프로젝트를 연 다른 에디터
			json meta = json::parse(ReadText(MetaFile(pid)), nullptr, false);
			if (meta.is_discarded() || !fs::exists(SceneFile(pid), ec))
			{
				RemoveSession(pid);   // 변경 없이 끝난 세션
				continue;
			}
			Pending p;
			p.Active = true;
			p.Pid = pid;
			p.ScenePath = string_to_wstring(meta.value("scenePath", ""));
			p.SceneName = meta.value("sceneName", "Untitled");
			p.Time = meta.value("time", 0ll);
			p.Objects = meta.value("objects", 0);
			// 원래 씬 파일을 그 뒤에 저장했다면 복구할 것이 없다
			const std::wstring full = p.ScenePath.empty() ? std::wstring() :
				(fs::path(p.ScenePath).is_absolute() ? p.ScenePath : PathManager::GetI()->GetMovePathW(p.ScenePath));
			if (!full.empty() && fs::exists(full, ec) && FileUnixTime(full) >= p.Time)
			{
				RemoveSession(pid);
				continue;
			}
			if (!best.Active || p.Time > best.Time)
			{
				if (best.Active)
					RemoveSession(best.Pid);
				best = p;
			}
			else
				RemoveSession(pid);
		}
		s_Pending = best;
		if (s_Pending.Active)
			EditorLog::Write("AutoSave", "previous session ended abnormally: autosave of '%s' from %s", s_Pending.SceneName.c_str(), TimeText(s_Pending.Time).c_str());

		// 이번 세션 표시
		json session = { { "pid", ::GetCurrentProcessId() }, { "started", UnixNow() } };
		WriteAtomic(SessionFile(me), session.dump(2));
		s_LastSave = Now();
		s_Initialized = true;
	}

	void Update()
	{
		if (!s_Initialized || !Enabled() || Application::IsPlaying())
			return;
		if (Now() - s_LastSave < IntervalMinutes() * 60.0)
			return;
		s_LastSave = Now();
		SceneManager* sm = SceneManager::GetI();
		if (sm->IsScenePromptOpen() || !sm->IsCurrentSceneDirty())
			return;
		WriteAutosave(nullptr);
	}

	void OnEnterPlay()
	{
		// Play 중 충돌하면 Play 직전 상태를 되살린다
		if (s_Initialized && Enabled() && SceneManager::GetI()->IsCurrentSceneDirty())
			WriteAutosave(nullptr);
	}

	void Shutdown()
	{
		if (!s_Initialized)
			return;
		RemoveSession(MyPid());   // 정상 종료: 복구할 것 없음
		s_Initialized = false;
	}

	void EmergencySave()
	{
		if (!s_Initialized || Application::IsPlaying())
			return;
		__try
		{
			EmergencyWork();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}

	bool SaveNow(std::string* error)
	{
		if (!s_Initialized)
		{
			if (error) *error = "auto save is not running (player?)";
			return false;
		}
		if (Application::IsPlaying())
		{
			if (error) *error = "stop Play mode first";
			return false;
		}
		return WriteAutosave(error);
	}

	bool HasPendingRecovery() { return s_Pending.Active; }

	std::string PendingRecoveryInfo()
	{
		if (!s_Pending.Active)
			return std::string();
		return s_Pending.SceneName + " (" + (s_Pending.ScenePath.empty() ? std::string("never saved") : wstring_to_string(s_Pending.ScenePath)) +
			"), " + TimeText(s_Pending.Time) + ", " + std::to_string(s_Pending.Objects) + " objects";
	}

	bool Recover(bool accept)
	{
		if (!s_Pending.Active)
			return false;
		const Pending p = s_Pending;
		s_Pending = Pending{};
		if (!accept)
		{
			RemoveSession(p.Pid);
			EditorLog::Write("AutoSave", "recovery discarded");
			return true;
		}
		const std::string text = ReadText(SceneFile(p.Pid));
		const bool ok = SceneManager::GetI()->OpenRecoveredScene(text, p.ScenePath);
		if (ok)
		{
			RemoveSession(p.Pid);
			Debug::Log("Recovered the scene '" + p.SceneName + "' from the autosave of " + TimeText(p.Time) + ". Save it (Ctrl+S) to keep the changes.");
		}
		else
			Debug::LogError("Could not recover the autosave " + wstring_to_string(SceneFile(p.Pid)));
		EditorLog::Write("AutoSave", "recovery %s", ok ? "done" : "FAILED");
		return ok;
	}

	void DrawRecoveryPrompt()
	{
		const char* id = "Recover Unsaved Changes##NovaRecovery";
		if (!s_Pending.Active)
		{
			// CLI(nova autosave recover|discard)로 정했으면 떠 있던 창을 닫는다
			if (s_PromptOpened && ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoSavedSettings))
			{
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}
			s_PromptOpened = false;
			return;
		}
		if (Application::IsPlaying())
			return;
		if (!s_PromptOpened)
		{
			ImGui::OpenPopup(id);
			s_PromptOpened = true;
		}
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(470, 0), ImGuiCond_Appearing);
		if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
			return;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
		ImGui::TextWrapped("NOVA did not shut down correctly last time.");
		ImGui::TextWrapped("An autosave of the scene \"%s\" from %s (%d objects) was found. %s",
			s_Pending.SceneName.c_str(), TimeText(s_Pending.Time).c_str(), s_Pending.Objects,
			s_Pending.ScenePath.empty() ? "The scene had never been saved." : "It is newer than the saved scene file.");
		ImGui::TextDisabled("Recover opens it with unsaved changes - save it with Ctrl+S to keep it.");
		ImGui::Spacing();
		int choice = 0;
		if (ImGui::Button("Recover", ImVec2(140, 0))) choice = 1;
		ImGui::SameLine();
		if (ImGui::Button("Discard", ImVec2(140, 0))) choice = 2;
		ImGui::PopStyleVar();
		if (choice != 0)
		{
			ImGui::CloseCurrentPopup();
			const bool accept = choice == 1;
			// 씬 교체는 프레임 끝에 (이번 프레임의 창들이 지금 씬을 쓰는 중)
			SceneManager::GetI()->AddLastUpdate([accept]() { Recover(accept); });
		}
		ImGui::EndPopup();
	}

	void WatchModels()
	{
		static auto s_Last = std::chrono::steady_clock::now();
		const auto now = std::chrono::steady_clock::now();
		if (now - s_Last < std::chrono::seconds(1) || Application::IsPlaying())
			return;
		s_Last = now;
		for (const std::string& rel : ResourceManager::GetI()->TakeChangedModels())
		{
			EditorLog::Write("Import", "%s changed on disk - reimporting", rel.c_str());
			ImportSettingsInspector::Reimport(string_to_wstring(PathManager::GetI()->GetMovePathS(rel)));
		}
	}
}
