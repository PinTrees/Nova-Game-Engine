#include "pch.h"
#include "VfxAssistantWindow.h"
#include "VfxGraphWindow.h"
#include "VfxAsset.h"
#include "EditorGUIManager.h"
#include "UnityGUI.h"
#include "ImGui/imgui_internal.h"
#include "IconsFontAwesome/IconsFontAwesome6.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

VfxAssistantWindow* VfxAssistantWindow::s_Instance = nullptr;

// 실행 중인 claude 프로세스: 표준 출력 (stream-json 한 줄 = 사건 하나) 을 읽는 스레드
struct VfxAssistantWindow::Job
{
	HANDLE Process = nullptr;
	HANDLE JobObject = nullptr;   // 닫으면 claude 와 그 자식 (bash · nova) 이 모두 끝난다
	HANDLE Out = nullptr;
	std::thread Reader;
	std::mutex Lock;
	std::vector<std::string> Lines;
	std::atomic<bool> Done{ false };

	~Job()
	{
		if (JobObject) CloseHandle(JobObject);   // KILL_ON_JOB_CLOSE
		if (Reader.joinable()) Reader.join();
		if (Out) CloseHandle(Out);
		if (Process) CloseHandle(Process);
	}
};

namespace
{
	std::wstring Env(const wchar_t* name)
	{
		wchar_t buf[32768];
		const DWORD n = GetEnvironmentVariableW(name, buf, 32768);
		return n > 0 && n < 32768 ? std::wstring(buf, n) : std::wstring();
	}

	bool IsFile(const fs::path& p)
	{
		std::error_code ec;
		return fs::is_regular_file(p, ec);
	}

	std::vector<fs::path> PathDirs()
	{
		std::vector<fs::path> dirs;
		const std::wstring path = Env(L"PATH");
		size_t start = 0;
		while (start <= path.size())
		{
			const size_t semi = path.find(L';', start);
			const std::wstring d = path.substr(start, semi == std::wstring::npos ? std::wstring::npos : semi - start);
			if (!d.empty()) dirs.push_back(fs::path(d));
			if (semi == std::wstring::npos) break;
			start = semi + 1;
		}
		// npm 전역 · 네이티브 설치 자리 (PATH 에 없어도)
		const std::wstring appdata = Env(L"APPDATA"), home = Env(L"USERPROFILE");
		if (!appdata.empty()) dirs.push_back(fs::path(appdata) / L"npm");
		if (!home.empty()) dirs.push_back(fs::path(home) / L".local" / L"bin");
		return dirs;
	}

	// CreateProcess 인자 따옴표 (MSVC 규칙: 따옴표 앞의 \ 는 두 배)
	std::wstring Quote(const std::wstring& a)
	{
		if (!a.empty() && a.find_first_of(L" \t\n\"") == std::wstring::npos)
			return a;
		std::wstring o = L"\"";
		size_t backslashes = 0;
		for (wchar_t c : a)
		{
			if (c == L'\\') { ++backslashes; continue; }
			if (c == L'"') { o.append(backslashes * 2 + 1, L'\\'); o += L'"'; }
			else { o.append(backslashes, L'\\'); o += c; }
			backslashes = 0;
		}
		o.append(backslashes * 2, L'\\');
		o += L'"';
		return o;
	}

	// 자식 환경: API 키를 지우고 (구독 로그인으로만), nova.exe 폴더를 PATH 앞에, NOVA_PROJECT = 이 프로젝트
	std::vector<wchar_t> ChildEnvironment(const std::wstring& binDir, const std::wstring& project)
	{
		std::vector<std::wstring> vars;
		if (wchar_t* block = GetEnvironmentStringsW())
		{
			for (const wchar_t* p = block; *p; p += wcslen(p) + 1)
			{
				std::wstring v = p;
				std::wstring upper = v.substr(0, v.find(L'='));
				for (wchar_t& c : upper) c = towupper(c);
				// API 키 · 다른 Claude 세션이 물려준 값 (ANTHROPIC_BASE_URL · CLAUDE_CODE_* — 에디터를 Claude Code 터미널에서 띄운 경우)
				//  을 모두 지운다 → 이 PC 에 로그인된 Claude Code (~/.claude) 로만 돈다
				if (upper.rfind(L"ANTHROPIC_", 0) == 0 || upper.rfind(L"CLAUDE_CODE_", 0) == 0 || upper == L"CLAUDECODE" || upper == L"CLAUDE_PID" ||
					upper == L"CLAUDE_AGENT_SDK_VERSION" || upper.rfind(L"CLAUDE_PREVIEW_", 0) == 0 || upper == L"NOVA_PROJECT" || upper == L"NOVA_PID")
					continue;
				if (upper == L"PATH")
					v = v.substr(0, 5) + binDir + L";" + v.substr(5);
				vars.push_back(v);
			}
			FreeEnvironmentStringsW(block);
		}
		vars.push_back(L"NOVA_PROJECT=" + project);
		vars.push_back(L"NOVA_PID=" + std::to_wstring(GetCurrentProcessId()));
		std::vector<wchar_t> out;
		for (const std::wstring& v : vars)
		{
			out.insert(out.end(), v.begin(), v.end());
			out.push_back(0);
		}
		out.push_back(0);
		return out;
	}

	struct Spawned { HANDLE Process = nullptr, Job = nullptr, Out = nullptr; };

	// 프로세스 시작 (stdin 에 글을 쓰고 닫는다, stdout + stderr 를 한 관으로)
	bool Spawn(const std::wstring& cmdline, const std::wstring& cwd, std::vector<wchar_t>* env, const std::string& stdinText, Spawned& out, std::string& error)
	{
		SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
		HANDLE outR = nullptr, outW = nullptr, inR = nullptr, inW = nullptr;
		if (!CreatePipe(&outR, &outW, &sa, 0) || !CreatePipe(&inR, &inW, &sa, 0))
		{
			error = "CreatePipe failed";
			return false;
		}
		SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
		SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
		STARTUPINFOW si = { sizeof(si) };
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput = inR;
		si.hStdOutput = outW;
		si.hStdError = outW;
		PROCESS_INFORMATION pi = {};
		std::wstring cl = cmdline;
		const BOOL ok = CreateProcessW(nullptr, cl.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
			env ? env->data() : nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
		CloseHandle(outW);
		CloseHandle(inR);
		if (!ok)
		{
			error = "CreateProcess failed (" + std::to_string(GetLastError()) + ")";
			CloseHandle(outR);
			CloseHandle(inW);
			return false;
		}
		HANDLE job = CreateJobObjectW(nullptr, nullptr);
		if (job)
		{
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
			li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof(li));
			AssignProcessToJobObject(job, pi.hProcess);
		}
		ResumeThread(pi.hThread);
		CloseHandle(pi.hThread);
		DWORD written = 0;
		if (!stdinText.empty())
			WriteFile(inW, stdinText.data(), (DWORD)stdinText.size(), &written, nullptr);
		CloseHandle(inW);
		out = { pi.hProcess, job, outR };
		return true;
	}

	std::wstring ProjectDir()
	{
		std::wstring p = PathManager::GetI()->GetContentPathW();
		while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
		return p;
	}

	std::wstring BinDir()
	{
		wchar_t exe[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, exe, MAX_PATH);
		return fs::path(exe).parent_path().wstring();
	}

	std::string SystemPrompt(const std::string& asset)
	{
		return
			"You are the VFX Assistant inside the NOVA game engine editor (a Unity-like engine). The user describes visual effects in natural language "
			"(often Korean); you build them as GPU-particle Visual Effect Graph assets (.vfx, like Unity VFX Graph) by running `nova vfx` commands in Bash. "
			"Reply in the user's language and keep replies short.\n"
			"Current asset: " + (asset.empty() ? std::string("(none yet - create one under Assets/VFX/, e.g. `nova vfx new Assets/VFX/Name.vfx --template Fireworks`)") : asset) + "\n"
			"Rules:\n"
			"- Only run: `nova vfx ...`, `nova create visual-effect --asset <path> --name <Name> --position x,y,z`, `nova camera ...`, `nova screenshot <file.png> --view scene`. "
			"Do not edit files directly.\n"
			"- Once per conversation run `nova vfx help` and `nova vfx blocks` to learn ops, block types and params; `nova vfx info <path>` shows the asset JSON.\n"
			"- For a new or heavily changed design write the whole asset: `nova vfx set <path> --data '<asset json>'` (single quotes in bash; same schema as info) or write it with `--file`. "
			"For tweaks use system.set (JSON merge), block.add/set/remove, property.add/set.\n"
			"- Look: colors are HDR - intensity 2-10 glows through bloom. Additive blend for light/fire/magic, Alpha for smoke and dust. Layer several systems "
			"(core glow + sparks + smoke + ring). GPU events (spawn.parent + countPerEvent) make fireworks-like chains. Exposed properties let the user tweak.\n"
			"- Budget: total capacity under ~300000; capacity >= rate x max lifetime.\n"
			"- If `nova vfx stats` shows no Visual Effect using the asset, place one with `nova create visual-effect`.\n"
			"- To check the result: `nova camera --frame <ObjectName>`, wait a moment with `nova vfx stats`, then `nova screenshot Temp/VfxAssistant/shot.png --view scene` "
			"and Read the image. Iterate at most 2-3 times.\n"
			"- End with a short summary: what you built and which properties the user can tweak.";
	}

	std::string Utf8(const std::wstring& w) { return wstring_to_string(w); }

	std::string Clip(const std::string& s, size_t n)
	{
		if (s.size() <= n) return s;
		size_t cut = n;
		while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) --cut;   // UTF-8 글자 가운데에서 자르지 않게
		return s.substr(0, cut) + " ...";
	}
}

VfxAssistantWindow::VfxAssistantWindow()
	: EditorWindow("VFX Assistant", ICON_FA_COMMENTS)
{
	s_Instance = this;
	SetIsOpened(false);
}

VfxAssistantWindow::~VfxAssistantWindow()
{
	m_Job.reset();
	if (s_Instance == this) s_Instance = nullptr;
}

void VfxAssistantWindow::BeforeBegin()
{
	if (m_FocusPending)
	{
		ImGui::SetNextWindowFocus();
		m_FocusPending = false;
	}
	ImGui::SetNextWindowSize(ImVec2(460.0f, 720.0f), ImGuiCond_FirstUseEver);
	// 처음에는 Inspector 옆 탭으로 (그래프 창 · 장면을 가리지 않게)
	if (EditorWindow* inspector = EditorGUIManager::GetI()->FindWindow("Inspector"))
		if (ImGuiWindow* w = ImGui::FindWindowByName(inspector->GetImGuiName().c_str()); w && w->DockId)
			ImGui::SetNextWindowDockID(w->DockId, ImGuiCond_FirstUseEver);
}

bool VfxAssistantWindow::CliOp(const std::string& op, const json& args, json& result, std::string& error)
{
	VfxAssistantWindow* w = s_Instance;
	if (!w)
	{
		error = "no VFX Assistant window";
		return true;
	}
	if (op == "assistant.send")
	{
		if (w->m_Probe == 0)
			w->m_Probe = FindClaude(w->m_Runner, w->m_ProbeError) ? 1 : -1;
		if (w->m_Probe < 0) { error = w->m_ProbeError; return true; }
		if (w->m_Job) { error = "the assistant is still working (assistant.status / assistant.stop)"; return true; }
		const std::string message = args.value("message", std::string());
		if (message.empty()) { error = "assistant.send needs --message"; return true; }
		if (args.contains("path")) w->m_Asset = args["path"].get<std::string>();
		w->SetIsOpened(true);
		w->Send(message);
		result = { { "running", w->m_Job != nullptr } };
		return true;
	}
	if (op == "assistant.stop")
	{
		w->Stop();
		return true;
	}
	if (op == "assistant.status")
	{
		w->Poll();   // 창이 닫혀 있어도 진행
		json log = json::array();
		static const char* kinds[] = { "user", "text", "tool", "toolResult", "error", "info" };
		for (const Entry& e : w->m_Log)
			log.push_back({ { "kind", kinds[(int)e.Type] }, { "text", e.Body } });
		result = { { "running", w->m_Job != nullptr }, { "session", w->m_Session }, { "asset", w->m_Asset }, { "log", log } };
		return true;
	}
	return false;
}

void VfxAssistantWindow::Open(const std::string& assetPath)
{
	if (!s_Instance) return;
	if (!assetPath.empty()) s_Instance->m_Asset = assetPath;
	s_Instance->SetIsOpened(true);
	s_Instance->m_FocusPending = true;
}

bool VfxAssistantWindow::FindClaude(Runner& out, std::string& why)
{
	// 후보: PATH 의 claude.exe (네이티브 설치), npm 전역 설치 (node + cli.js), Claude 데스크톱 앱에 딸린 claude.exe
	//  여럿이면 판 번호가 가장 높은 것 (로그인 정보 ~/.claude 는 모두 같이 쓴다)
	struct Candidate { Runner R; std::vector<int> Version; };
	std::vector<Candidate> found;
	auto parseVersion = [](const std::string& v) {
		std::vector<int> parts;
		int cur = -1;
		for (char c : v)
		{
			if (isdigit((unsigned char)c)) cur = (cur < 0 ? 0 : cur * 10) + (c - '0');
			else if (c == '.') { parts.push_back((std::max)(cur, 0)); cur = -1; }
			else break;
		}
		if (cur >= 0) parts.push_back(cur);
		return parts;
	};
	const std::vector<fs::path> dirs = PathDirs();
	for (const fs::path& d : dirs)
		if (IsFile(d / L"claude.exe"))
		{
			found.push_back({ { (d / L"claude.exe").wstring(), L"", "" }, {} });   // 판을 모름 → 다른 후보가 판을 알면 그쪽이 앞선다 (아래에서 0 판)
			break;
		}
	for (const fs::path& d : dirs)
	{
		const fs::path pkg = d / L"node_modules" / L"@anthropic-ai" / L"claude-code";
		if (!IsFile(pkg / L"cli.js"))
			continue;
		fs::path node = d / L"node.exe";
		if (!IsFile(node))
		{
			node.clear();
			for (const fs::path& n : dirs)
				if (IsFile(n / L"node.exe")) { node = n / L"node.exe"; break; }
		}
		if (node.empty())
			continue;
		std::string version;
		std::ifstream in(pkg / L"package.json");
		const json j = json::parse(in, nullptr, false);
		if (j.is_object()) version = j.value("version", std::string());
		found.push_back({ { node.wstring(), (pkg / L"cli.js").wstring(), version }, parseVersion(version) });
		break;
	}
	// Claude 데스크톱 앱: %APPDATA%\Claude\claude-code\<판>\<id>\claude.exe
	const std::wstring appdata = Env(L"APPDATA");
	std::error_code ec;
	if (!appdata.empty())
		for (const auto& ver : fs::directory_iterator(fs::path(appdata) / L"Claude" / L"claude-code", ec))
			for (const auto& build : fs::directory_iterator(ver.path(), ec))
				if (IsFile(build.path() / L"claude.exe"))
				{
					const std::string version = Utf8(ver.path().filename().wstring());
					found.push_back({ { (build.path() / L"claude.exe").wstring(), L"", version }, parseVersion(version) });
				}
	if (found.empty())
	{
		why = "Claude Code is not installed on this PC. Install it (npm install -g @anthropic-ai/claude-code, or the native installer), "
			"run `claude` once in a terminal and log in (/login), then press Check again.";
		return false;
	}
	// PATH 의 claude.exe 는 사용자가 고른 것 → 판을 몰라도 맨 앞. 나머지는 높은 판부터
	std::stable_sort(found.begin() + (found.front().Version.empty() ? 1 : 0), found.end(),
		[](const Candidate& a, const Candidate& b) { return a.Version > b.Version; });
	out = found.front().R;
	return true;
}

void VfxAssistantWindow::Stop()
{
	if (!m_Job) return;
	m_Job.reset();   // Job 객체를 닫으면 프로세스 나무가 끝난다
	m_Log.push_back({ Entry::Info, "stopped" });
}

void VfxAssistantWindow::Send(const std::string& text)
{
	if (m_Job || text.empty() || m_Probe != 1)
		return;
	// 그래프 창의 편집 중 내용은 Vfx::SetLive 로 이미 nova vfx 가 읽는다 (저장하지 않아도 Claude 가 최신을 본다)
	if (VfxGraphWindow* g = VfxGraphWindow::Instance(); g && m_Asset.empty())
		m_Asset = g->Path();
	std::vector<std::wstring> args;
	if (!m_Runner.Script.empty()) args.push_back(m_Runner.Script);
	args.insert(args.end(), { L"-p", L"--output-format", L"stream-json", L"--verbose", L"--max-turns", L"40",
		L"--append-system-prompt", string_to_wstring(SystemPrompt(m_Asset)) });
	if (!m_Session.empty())
		args.insert(args.end(), { L"--resume", string_to_wstring(m_Session) });
	args.insert(args.end(), { L"--disallowedTools", L"Edit", L"Write", L"NotebookEdit", L"WebFetch", L"WebSearch",
		L"--allowedTools", L"Bash(nova vfx:*)", L"Bash(nova screenshot:*)", L"Bash(nova camera:*)", L"Bash(nova create visual-effect:*)", L"Read" });
	std::wstring cmdline = Quote(m_Runner.Exe);
	for (const std::wstring& a : args) cmdline += L" " + Quote(a);

	std::error_code ec;
	fs::create_directories(fs::path(ProjectDir()) / L"Temp" / L"VfxAssistant", ec);
	std::vector<wchar_t> env = ChildEnvironment(BinDir(), ProjectDir());
	Spawned sp;
	std::string error;
	m_Log.push_back({ Entry::User, text });
	m_ScrollToEnd = true;
	if (!Spawn(cmdline, ProjectDir(), &env, text, sp, error))
	{
		m_Log.push_back({ Entry::Error, error });
		return;
	}
	auto job = std::make_shared<Job>();
	job->Process = sp.Process;
	job->JobObject = sp.Job;
	job->Out = sp.Out;
	Job* raw = job.get();
	job->Reader = std::thread([raw] {
		std::string pending;
		char buf[8192];
		DWORD n = 0;
		while (ReadFile(raw->Out, buf, sizeof(buf), &n, nullptr) && n > 0)
		{
			pending.append(buf, n);
			size_t nl;
			while ((nl = pending.find('\n')) != std::string::npos)
			{
				std::string line = pending.substr(0, nl);
				pending.erase(0, nl + 1);
				if (!line.empty() && line.back() == '\r') line.pop_back();
				if (line.empty()) continue;
				std::lock_guard<std::mutex> g(raw->Lock);
				raw->Lines.push_back(std::move(line));
			}
		}
		if (!pending.empty())
		{
			std::lock_guard<std::mutex> g(raw->Lock);
			raw->Lines.push_back(pending);
		}
		raw->Done = true;
	});
	m_Job = job;
	m_Started = std::chrono::steady_clock::now();
	m_Input[0] = 0;
}

void VfxAssistantWindow::Poll()
{
	if (!m_Job) return;
	std::vector<std::string> lines;
	{
		std::lock_guard<std::mutex> g(m_Job->Lock);
		lines.swap(m_Job->Lines);
	}
	for (const std::string& line : lines)
	{
		const json j = json::parse(line, nullptr, false);
		if (!j.is_object())
		{
			m_Log.push_back({ Entry::Info, Clip(line, 600) });   // 경고 · 오류 글 (stderr)
			continue;
		}
		const std::string type = j.value("type", std::string());
		if (type == "system" && j.value("subtype", std::string()) == "init")
		{
			if (j.contains("session_id")) m_Session = j["session_id"].get<std::string>();
			continue;
		}
		if (type == "assistant" && j.contains("message") && j["message"].contains("content"))
		{
			for (const json& c : j["message"]["content"])
			{
				const std::string ct = c.value("type", std::string());
				if (ct == "text" && !c.value("text", std::string()).empty())
					m_Log.push_back({ Entry::Text, c["text"].get<std::string>() });
				else if (ct == "tool_use")
				{
					const json& in = c.contains("input") ? c["input"] : json::object();
					std::string body = in.contains("command") && in["command"].is_string() ? "$ " + in["command"].get<std::string>()
						: in.contains("file_path") ? c.value("name", std::string()) + " " + in["file_path"].get<std::string>()
						: c.value("name", std::string()) + " " + in.dump();
					m_Log.push_back({ Entry::Tool, Clip(body, 1200) });
				}
			}
			m_ScrollToEnd = true;
			continue;
		}
		if (type == "user" && j.contains("message") && j["message"].contains("content") && j["message"]["content"].is_array())
		{
			for (const json& c : j["message"]["content"])
			{
				if (c.value("type", std::string()) != "tool_result") continue;
				std::string text;
				if (c.contains("content") && c["content"].is_string()) text = c["content"].get<std::string>();
				else if (c.contains("content") && c["content"].is_array())
					for (const json& part : c["content"])
						text += part.value("type", std::string()) == "text" ? part.value("text", std::string()) : "[" + part.value("type", std::string()) + "]";
				m_Log.push_back({ c.value("is_error", false) ? Entry::Error : Entry::ToolResult, Clip(text, 500) });
			}
			continue;
		}
		if (type == "result")
		{
			if (j.contains("session_id") && j["session_id"].is_string()) m_Session = j["session_id"].get<std::string>();
			m_CostUsd += j.value("total_cost_usd", 0.0);
			if (j.value("is_error", false))
			{
				const std::string r = j.value("result", j.value("subtype", std::string("error")));
				m_Log.push_back({ Entry::Error, r });
				if (r.find("/login") != std::string::npos || r.find("authenticate") != std::string::npos || r.find("API key") != std::string::npos)
					m_Log.push_back({ Entry::Info, "Claude Code needs a login on this PC: open a terminal, run `claude`, type /login and sign in with your Claude account (subscription). Then send again." });
			}
			const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_Started).count();
			char buf[96];
			snprintf(buf, sizeof(buf), "done in %.0f s, %d turns", secs, j.value("num_turns", 0));
			m_Log.push_back({ Entry::Info, buf });
		}
	}
	if (!lines.empty()) m_ScrollToEnd = true;
	if (m_Job->Done)
	{
		DWORD code = 0;
		WaitForSingleObject(m_Job->Process, 2000);
		GetExitCodeProcess(m_Job->Process, &code);
		if (code != 0)
			m_Log.push_back({ Entry::Error, "claude exited with code " + std::to_string(code) + (code == 1 ? " (not logged in? run `claude` once in a terminal)" : "") });
		m_Job.reset();
		m_ScrollToEnd = true;
	}
}

void VfxAssistantWindow::DrawLog(float height)
{
	ImGui::BeginChild("##log", ImVec2(0, height), true);
	const float wrap = ImGui::GetContentRegionAvail().x;
	if (m_Log.empty())
	{
		ImGui::TextDisabled("Describe an effect. Claude Code builds it as a Visual Effect Graph.");
		ImGui::Dummy(ImVec2(0, 6));
		static const char* kIdeas[] = {
			"\xEB\xB0\xA4\xED\x95\x98\xEB\x8A\x98\xEC\x9D\x84 \xEA\xB0\x80\xEB\x93\x9D \xEC\xB1\x84\xEC\x9A\xB0\xEB\x8A\x94 \xED\x99\x94\xEB\xA0\xA4\xED\x95\x9C \xEB\xB6\x88\xEA\xBD\x83\xEB\x86\x80\xEC\x9D\xB4 \xEB\xA7\x8C\xEB\x93\xA4\xEC\x96\xB4\xEC\xA4\x98"  /* 밤하늘을 가득 채우는 화려한 불꽃놀이 만들어줘 */,
			"\xEB\xB0\x94\xEB\x8B\xA5\xEC\x97\x90\xEC\x84\x9C \xEB\xB9\x9B\xEB\x82\x98\xEB\xA9\xB0 \xED\x9A\x8C\xEC\xA0\x84\xED\x95\x98\xEB\x8A\x94 \xEB\xB3\xB4\xEB\x9D\xBC\xEC\x83\x89 \xEB\xA7\x88\xEB\xB2\x95\xEC\xA7\x84"  /* 바닥에서 빛나며 회전하는 보라색 마법진 */,
			"\xEC\x9A\xA9\xEC\x95\x94\xEC\x97\x90\xEC\x84\x9C \xED\x8A\x80\xEC\x96\xB4 \xEC\x98\xA4\xEB\xA5\xB4\xEB\x8A\x94 \xEB\xB6\x88\xED\x8B\xB0\xEC\x99\x80 \xEC\x97\xB0\xEA\xB8\xB0"  /* 용암에서 튀어 오르는 불티와 연기 */,
			"\xEC\xB4\x88\xEB\xA1\x9D\xEB\xB9\x9B \xED\x8F\xAC\xED\x84\xB8, \xEB\xB9\x9B \xEC\x9E\x85\xEC\x9E\x90\xEA\xB0\x80 \xEB\xB9\xA8\xEB\xA0\xA4 \xEB\x93\xA4\xEC\x96\xB4\xEA\xB0\x80\xEA\xB2\x8C"  /* 초록빛 포털, 빛 입자가 빨려 들어가게 */,
			"\xEC\xA7\x80\xEA\xB8\x88 \xEC\x9D\xB4\xED\x8E\x99\xED\x8A\xB8\xEB\xA5\xBC \xEB\x91\x90 \xEB\xB0\xB0 \xEB\x8D\x94 \xED\x99\x94\xEB\xA0\xA4\xED\x95\x98\xEA\xB2\x8C"  /* 지금 이펙트를 두 배 더 화려하게 */,
		};
		for (const char* idea : kIdeas)
			if (ImGui::Selectable(idea, false, ImGuiSelectableFlags_None, ImVec2(wrap, 0)))
				snprintf(m_Input, sizeof(m_Input), "%s", idea);
	}
	for (const Entry& e : m_Log)
	{
		switch (e.Type)
		{
		case Entry::User:
		{
			ImGui::Dummy(ImVec2(0, 4));
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const ImVec2 size = ImGui::CalcTextSize(e.Body.c_str(), nullptr, false, wrap - 24.0f);
			ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + wrap, p.y + size.y + 10), IM_COL32(52, 78, 120, 255), 6.0f);
			ImGui::SetCursorScreenPos(ImVec2(p.x + 8, p.y + 5));
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrap - 20.0f);
			ImGui::TextUnformatted(e.Body.c_str());
			ImGui::PopTextWrapPos();
			ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + size.y + 14));
			break;
		}
		case Entry::Text:
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(e.Body.c_str());
			ImGui::PopTextWrapPos();
			break;
		case Entry::Tool:
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.8f, 1.0f, 1.0f));
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(e.Body.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
			break;
		case Entry::ToolResult:
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(e.Body.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
			break;
		case Entry::Error:
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.35f, 1.0f));
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(e.Body.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
			break;
		case Entry::Info:
			ImGui::TextDisabled("%s", e.Body.c_str());
			break;
		}
	}
	if (m_Job)
	{
		const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_Started).count();
		static const char* spin[] = { "|", "/", "-", "\\" };
		ImGui::TextDisabled("%s Claude is working... %.0f s", spin[(int)(secs * 8) % 4], secs);
	}
	if (m_ScrollToEnd)
	{
		ImGui::SetScrollHereY(1.0f);
		m_ScrollToEnd = false;
	}
	ImGui::EndChild();
}

void VfxAssistantWindow::OnRender()
{
	if (m_Probe == 0)
		m_Probe = FindClaude(m_Runner, m_ProbeError) ? 1 : -1;
	Poll();

	// 머리: 연결 상태 · 다루는 에셋
	if (m_Probe < 0)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.35f, 1.0f));
		ImGui::TextWrapped("%s", m_ProbeError.c_str());
		ImGui::PopStyleColor();
		if (ImGui::Button("Check again"))
			m_Probe = 0;
		return;
	}
	ImGui::TextDisabled("Claude Code %s (local, your login)", m_Runner.Version.c_str());
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Utf8(m_Runner.Script.empty() ? m_Runner.Exe : m_Runner.Script).c_str());
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70.0f + ImGui::GetCursorPosX() - 8.0f);
	ImGui::BeginDisabled(m_Job != nullptr);
	if (ImGui::SmallButton("New Chat"))
	{
		m_Session.clear();
		m_Log.clear();
		m_CostUsd = 0.0;
	}
	ImGui::EndDisabled();
	if (VfxGraphWindow* g = VfxGraphWindow::Instance(); g && !g->Path().empty() && m_Asset.empty())
		m_Asset = g->Path();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Asset");
	ImGui::SameLine(60.0f);
	ImGui::SetNextItemWidth(-1);
	if (ImGui::BeginCombo("##asset", m_Asset.empty() ? "(Claude creates one under Assets/VFX)" : m_Asset.c_str()))
	{
		if (ImGui::Selectable("(new asset)", m_Asset.empty())) m_Asset.clear();
		for (const std::string& p : Vfx::FindAssets())
			if (ImGui::Selectable(p.c_str(), p == m_Asset))
			{
				m_Asset = p;
				VfxGraphWindow::Open(p);
			}
		ImGui::EndCombo();
	}

	const float inputH = 76.0f;
	DrawLog((std::max)(80.0f, ImGui::GetContentRegionAvail().y - inputH - ImGui::GetFrameHeightWithSpacing() - 6.0f));
	ImGui::BeginDisabled(m_Job != nullptr);
	ImGui::InputTextMultiline("##input", m_Input, sizeof(m_Input), ImVec2(-1, inputH), ImGuiInputTextFlags_None);
	const bool ctrlEnter = ImGui::IsItemFocused() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter);
	ImGui::EndDisabled();
	if (m_Job)
	{
		if (ImGui::Button(ICON_FA_STOP " Stop", ImVec2(-1, 0))) Stop();
	}
	else if (ImGui::Button(ICON_FA_PAPER_PLANE " Send  (Ctrl+Enter)", ImVec2(-1, 0)) || ctrlEnter)
	{
		std::string text = m_Input;
		while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
		Send(text);
	}
}
