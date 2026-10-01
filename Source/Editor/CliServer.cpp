#include "pch.h"
#include "CliServer.h"
#include "EngineInfo.h"
#include <mutex>
#include <random>

namespace
{
	using json = nlohmann::json;

	struct Entry
	{
		std::string Help;
		CliServer::Handler Fn;
		int Delay = 0;
		bool BeforePresent = false;
	};
	std::map<std::string, Entry>& Handlers()
	{
		static std::map<std::string, Entry> handlers;
		return handlers;
	}
	json s_CommandList = json::object();

	struct Request
	{
		json Id;
		std::string Cmd;
		json Args;
		int Delay = 0;
		std::promise<std::string> Reply;
	};

	std::mutex s_Mutex;
	std::deque<std::shared_ptr<Request>> s_Queue;
	HANDLE s_WakeEvent = nullptr;
	std::atomic<bool> s_Running{ false };
	std::thread s_Listener;
	std::wstring s_PipeName;
	std::string s_Token;
	std::wstring s_InstanceFile;
	int s_WakeFrames = 0;   // 처리 뒤 더 그릴 프레임 (Undo 확정·화면 갱신)

	std::string MakeToken()
	{
		std::random_device rd;
		char buf[40];
		snprintf(buf, sizeof(buf), "%08x%08x%08x%08x", rd(), rd(), rd(), rd());
		return buf;
	}

	std::string ReplyLine(const json& id, bool ok, const json& result, const std::string& error)
	{
		json j = { { "id", id }, { "ok", ok } };
		if (ok)
			j["result"] = result;
		else
			j["error"] = error;
		return j.dump(-1, ' ', false, json::error_handler_t::replace) + "\n";
	}

	// 한 줄 요청 → 한 줄 응답 (파이프 스레드). 실제 실행은 메인 스레드(Pump)가 하고 여기서는 기다린다
	std::string HandleLine(const std::string& line)
	{
		const json j = json::parse(line, nullptr, false);
		if (!j.is_object())
			return ReplyLine(nullptr, false, nullptr, "invalid JSON request");
		const json id = j.contains("id") ? j["id"] : json();
		if (!j.contains("token") || !j["token"].is_string() || j["token"].get<std::string>() != s_Token)
			return ReplyLine(id, false, nullptr, "invalid token");
		const std::string cmd = j.value("cmd", std::string());
		if (cmd == "ping")
			return ReplyLine(id, true, { { "pong", true }, { "pid", (unsigned)::GetCurrentProcessId() } }, "");
		auto it = Handlers().find(cmd);
		if (it == Handlers().end())
			return ReplyLine(id, false, nullptr, "unknown command '" + cmd + "' (nova help)");

		auto req = std::make_shared<Request>();
		req->Id = id;
		req->Cmd = cmd;
		req->Args = j.contains("args") && j["args"].is_object() ? j["args"] : json::object();
		req->Delay = it->second.Delay + (std::max)(0, j.value("waitFrames", 0));
		auto future = req->Reply.get_future();
		{
			std::lock_guard<std::mutex> lock(s_Mutex);
			s_Queue.push_back(req);
		}
		::SetEvent(s_WakeEvent);
		const int timeout = std::clamp(j.value("timeout", 120), 1, 3600);
		if (future.wait_for(std::chrono::seconds(timeout)) != std::future_status::ready)
			return ReplyLine(id, false, nullptr, "timeout: the editor did not answer in " + std::to_string(timeout) + " s (busy or a modal dialog is open)");
		return future.get();
	}

	bool WriteAll(HANDLE pipe, const std::string& s)
	{
		size_t off = 0;
		while (off < s.size())
		{
			DWORD written = 0;
			if (!::WriteFile(pipe, s.data() + off, (DWORD)(std::min)(s.size() - off, (size_t)65536), &written, nullptr) || written == 0)
				return false;
			off += written;
		}
		return true;
	}

	void ServeClient(HANDLE pipe)
	{
		std::string buffer;
		char chunk[8192];
		bool alive = true;
		while (alive && s_Running)
		{
			DWORD read = 0;
			if (!::ReadFile(pipe, chunk, sizeof(chunk), &read, nullptr) || read == 0)
				break;
			buffer.append(chunk, read);
			size_t nl;
			while (alive && (nl = buffer.find('\n')) != std::string::npos)
			{
				std::string line = buffer.substr(0, nl);
				buffer.erase(0, nl + 1);
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (!line.empty())
					alive = WriteAll(pipe, HandleLine(line));
			}
			if (buffer.size() > (64u << 20))
				break;
		}
		::FlushFileBuffers(pipe);
		::DisconnectNamedPipe(pipe);
		::CloseHandle(pipe);
	}

	void Listen()
	{
		while (s_Running)
		{
			HANDLE pipe = ::CreateNamedPipeW(s_PipeName.c_str(), PIPE_ACCESS_DUPLEX,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES, 1 << 16, 1 << 16, 0, nullptr);
			if (pipe == INVALID_HANDLE_VALUE)
			{
				::Sleep(200);
				continue;
			}
			const bool connected = ::ConnectNamedPipe(pipe, nullptr) ? true : ::GetLastError() == ERROR_PIPE_CONNECTED;
			if (!s_Running || !connected)
			{
				::CloseHandle(pipe);
				continue;
			}
			std::thread(ServeClient, pipe).detach();
		}
	}

	std::wstring InstanceDir()
	{
		wchar_t buf[MAX_PATH] = {};
		if (::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH) == 0)
			return L"";
		return std::wstring(buf) + L"\\NOVA\\Instances";
	}

	void WriteInstanceFile()
	{
		const std::wstring dir = InstanceDir();
		if (dir.empty())
			return;
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		const DWORD pid = ::GetCurrentProcessId();
		s_InstanceFile = dir + L"\\" + std::to_wstring(pid) + L".json";
		wchar_t exe[MAX_PATH] = {};
		::GetModuleFileNameW(nullptr, exe, MAX_PATH);
		std::wstring project = PathManager::GetI()->GetContentPathW();
		while (!project.empty() && (project.back() == L'\\' || project.back() == L'/'))
			project.pop_back();
		const std::wstring log = std::filesystem::absolute(L"Logs\\Editor.log", ec).wstring();
		json j = {
			{ "pid", (unsigned)pid },
			{ "pipe", wstring_to_string(s_PipeName) },
			{ "token", s_Token },
			{ "project", wstring_to_string(project) },
			{ "projectName", wstring_to_string(std::filesystem::path(project).filename().wstring()) },
			{ "exe", wstring_to_string(exe) },
			{ "log", wstring_to_string(log) },
			{ "version", ENGINE_VERSION_A },
			{ "started", (long long)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() },
		};
		std::ofstream os(s_InstanceFile, std::ios::trunc);
		os << j.dump(2);
	}
}

namespace CliServer
{
	void Register(const std::string& cmd, const std::string& help, Handler handler, int delayFrames, bool beforePresent)
	{
		Handlers()[cmd] = Entry{ help, std::move(handler), delayFrames, beforePresent };
		s_CommandList[cmd] = help;
	}

	const json& Commands() { return s_CommandList; }
	const std::wstring& PipeName() { return s_PipeName; }

	void Start()
	{
		if (s_Running)
			return;
		s_WakeEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
		s_Token = MakeToken();
		s_PipeName = L"\\\\.\\pipe\\nova-editor-" + std::to_wstring(::GetCurrentProcessId());
		s_Running = true;
		s_Listener = std::thread(Listen);
		WriteInstanceFile();
		EditorLog::Write("CLI", "NOVA CLI server on %s (%zu commands)", wstring_to_string(s_PipeName).c_str(), Handlers().size());
	}

	void Stop()
	{
		if (!s_Running)
			return;
		s_Running = false;
		// ConnectNamedPipe 에서 기다리는 리스너를 깨운다
		HANDLE h = ::CreateFileW(s_PipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (h != INVALID_HANDLE_VALUE)
			::CloseHandle(h);
		if (s_Listener.joinable())
			s_Listener.join();
		{
			std::lock_guard<std::mutex> lock(s_Mutex);
			for (auto& r : s_Queue)
				r->Reply.set_value(ReplyLine(r->Id, false, nullptr, "the editor is closing"));
			s_Queue.clear();
		}
		std::error_code ec;
		if (!s_InstanceFile.empty())
			std::filesystem::remove(s_InstanceFile, ec);
	}

	bool HasWork()
	{
		std::lock_guard<std::mutex> lock(s_Mutex);
		return !s_Queue.empty() || s_WakeFrames > 0;
	}

	void WaitForWork(unsigned ms)
	{
		if (s_WakeEvent)
			::WaitForSingleObject(s_WakeEvent, ms);
		else
			::Sleep(ms);
	}

	// endOfFrame: 프레임 끝 (기다리는 프레임을 센다), 아니면 Present 직전 명령만
	void Run(bool endOfFrame)
	{
		std::vector<std::shared_ptr<Request>> ready;
		{
			std::lock_guard<std::mutex> lock(s_Mutex);
			std::deque<std::shared_ptr<Request>> waiting;
			for (auto& r : s_Queue)
			{
				if (r->Delay > 0)
				{
					if (endOfFrame)
						--r->Delay;
					waiting.push_back(r);
				}
				else if (Handlers()[r->Cmd].BeforePresent != endOfFrame)
					ready.push_back(r);   // 이 단계에서 실행할 명령
				else
					waiting.push_back(r);
			}
			s_Queue.swap(waiting);
			if (endOfFrame && ready.empty() && s_WakeFrames > 0)
				--s_WakeFrames;
		}
		for (auto& r : ready)
		{
			json result;
			std::string error;
			bool ok = false;
			const auto t0 = std::chrono::steady_clock::now();
			try
			{
				ok = Handlers()[r->Cmd].Fn(r->Args, result, error);
			}
			catch (const std::exception& e)
			{
				error = std::string("exception: ") + e.what();
			}
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			EditorLog::Write("CLI", "%s %s -> %s (%.1f ms)", r->Cmd.c_str(), r->Args.dump().substr(0, 160).c_str(), ok ? "ok" : error.c_str(), ms);
			r->Reply.set_value(ReplyLine(r->Id, ok, result, error));
		}
		if (!ready.empty())
		{
			std::lock_guard<std::mutex> lock(s_Mutex);
			s_WakeFrames = 4;
		}
	}

	void Pump() { Run(true); }
	void PumpBeforePresent() { Run(false); }
}
