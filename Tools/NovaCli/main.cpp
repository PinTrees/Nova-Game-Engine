// NOVA CLI (nova.exe) — 터미널·AI 에이전트가 실행 중인 NOVA 에디터를 다루는 명령줄 도구 (Unity CLI 처럼).
//  - 에디터는 시작할 때 %LOCALAPPDATA%\NOVA\Instances\<pid>.json 에 파이프 이름·토큰을 적는다 (Source/Editor/CliServer.*)
//  - nova 는 그 파일로 에디터를 찾아 \\.\pipe\nova-editor-<pid> 로 한 줄 JSON 요청을 보내고 응답을 출력한다
//  - 에디터 창을 앞으로 가져오거나 마우스로 조작하지 않아도 된다 (포커스를 잃은 에디터도 요청이 오면 깨어나 처리)
// 종료 코드: 0 성공, 1 명령 실패, 2 에디터를 찾지 못함/연결 실패, 3 사용법 오류
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <thread>
#include <sstream>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{
	constexpr const char* kVersion = "0.1.0";
	bool g_Json = false;            // --json: 결과를 JSON 그대로
	int g_Timeout = 120;

	// ------------------------------------------------------------------ 문자열
	std::string Utf8(const std::wstring& w)
	{
		if (w.empty()) return {};
		const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string s(n, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
		return s;
	}
	std::wstring Wide(const std::string& s)
	{
		if (s.empty()) return {};
		const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring w(n, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
		return w;
	}
	std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
		return s;
	}
	void Out(const std::string& s) { fwrite(s.data(), 1, s.size(), stdout); }
	void Err(const std::string& s) { fwrite(s.data(), 1, s.size(), stderr); }

	std::string NormPath(const std::string& p)
	{
		std::error_code ec;
		std::wstring w = fs::weakly_canonical(fs::absolute(Wide(p), ec), ec).wstring();
		while (!w.empty() && (w.back() == L'\\' || w.back() == L'/'))
			w.pop_back();
		return Lower(Utf8(w));
	}

	std::wstring LocalAppData()
	{
		wchar_t buf[MAX_PATH] = {};
		::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
		return buf;
	}

	// ------------------------------------------------------------------ 인수
	struct Args
	{
		std::vector<std::string> Pos;                 // 위치 인수 (명령 다음)
		std::map<std::string, std::string> Opt;       // --이름 값 / --플래그 (값 "")
		bool Has(const std::string& k) const { return Opt.count(k) > 0; }
		std::string Get(const std::string& k, const std::string& def = "") const { auto it = Opt.find(k); return it == Opt.end() ? def : it->second; }
	};

	// 값이 없는 플래그 (뒤 단어를 값으로 먹지 않는다)
	bool IsFlag(const std::string& name)
	{
		static const char* flags[] = { "json", "components", "force", "background", "run", "follow", "errors", "none", "root", "wait", "help", "no-select", "world", "float", "triggers", "clear", "keep-going" };
		for (const char* f : flags)
			if (name == f) return true;
		return false;
	}

	Args Parse(const std::vector<std::string>& in, size_t start)
	{
		Args a;
		for (size_t i = start; i < in.size(); ++i)
		{
			const std::string& s = in[i];
			if (s.size() > 2 && s[0] == '-' && s[1] == '-')
			{
				std::string name = s.substr(2), value;
				const size_t eq = name.find('=');
				if (eq != std::string::npos)
				{
					value = name.substr(eq + 1);
					name = name.substr(0, eq);
				}
				else if (!IsFlag(name) && i + 1 < in.size() && in[i + 1].rfind("--", 0) != 0)
					value = in[++i];   // 다음 낱말이 --이름 이면 값이 없는 플래그 (뒤 옵션을 먹지 않는다)
				a.Opt[name] = value;
			}
			else if (s == "-n" && i + 1 < in.size())
				a.Opt["n"] = in[++i];
			else
				a.Pos.push_back(s);
		}
		return a;
	}

	// "1,2,3" / "[1,2,3]" → [1,2,3]
	json Vec(const std::string& s)
	{
		json j = json::parse(s, nullptr, false);
		if (j.is_array()) return j;
		float x, y, z;
		if (sscanf_s(s.c_str(), "%f,%f,%f", &x, &y, &z) == 3)
			return json::array({ x, y, z });
		return json(s);
	}

	// "a,b,c,…" 가 모두 숫자면 배열 (2 · 4 개도), 아니면 문자열 그대로
	json NumberList(const std::string& s)
	{
		if (s.find(',') == std::string::npos) return json(s);
		json arr = json::array();
		std::stringstream ss(s);
		std::string t;
		while (std::getline(ss, t, ','))
		{
			char* end = nullptr;
			const double d = strtod(t.c_str(), &end);
			if (t.empty() || end == t.c_str() || *end != 0) return json(s);
			if (t.find_first_of(".eE") == std::string::npos) arr.push_back((long long)d);   // 정수는 정수로 (--ids 1,2,3)
			else arr.push_back(d);
		}
		return arr;
	}

	// 값: JSON 으로 읽히면 그대로 (숫자·true·배열·객체), 아니면 문자열
	json Value(const std::string& s)
	{
		json j = json::parse(s, nullptr, false);
		return j.is_discarded() ? json(s) : j;
	}

	// ------------------------------------------------------------------ 에디터 찾기
	struct Instance
	{
		unsigned Pid = 0;
		std::string Pipe, Token, Project, ProjectName, Exe, Log, Version;
		long long Started = 0;
	};

	bool Alive(unsigned pid)
	{
		HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
		if (!h) return false;
		DWORD code = 0;
		const bool alive = ::GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
		::CloseHandle(h);
		return alive;
	}

	std::vector<Instance> Instances()
	{
		std::vector<Instance> out;
		const fs::path dir = fs::path(LocalAppData()) / L"NOVA" / L"Instances";
		std::error_code ec;
		for (const auto& e : fs::directory_iterator(dir, ec))
		{
			if (e.path().extension() != L".json") continue;
			std::ifstream is(e.path());
			const json j = json::parse(is, nullptr, false);
			is.close();
			if (!j.is_object()) continue;
			Instance i;
			i.Pid = j.value("pid", 0u);
			if (!Alive(i.Pid))
			{
				fs::remove(e.path(), ec);   // 비정상 종료한 에디터의 남은 파일
				continue;
			}
			i.Pipe = j.value("pipe", std::string());
			i.Token = j.value("token", std::string());
			i.Project = j.value("project", std::string());
			i.ProjectName = j.value("projectName", std::string());
			i.Exe = j.value("exe", std::string());
			i.Log = j.value("log", std::string());
			i.Version = j.value("version", std::string());
			i.Started = j.value("started", 0ll);
			out.push_back(i);
		}
		return out;
	}

	bool Pick(const Args& a, Instance& out, std::string& error)
	{
		auto list = Instances();
		if (list.empty())
		{
			error = "no NOVA editor is running (start one with: nova open <project folder>)";
			return false;
		}
		if (a.Has("pid"))
		{
			for (auto& i : list)
				if (std::to_string(i.Pid) == a.Get("pid")) { out = i; return true; }
			error = "no editor with pid " + a.Get("pid");
			return false;
		}
		std::string want = a.Get("project");
		if (want.empty())
		{
			wchar_t env[MAX_PATH] = {};
			if (::GetEnvironmentVariableW(L"NOVA_PROJECT", env, MAX_PATH) > 0)
				want = Utf8(env);
		}
		if (!want.empty())
		{
			const std::string n = NormPath(want);
			for (auto& i : list)
				if (NormPath(i.Project) == n || Lower(i.ProjectName) == Lower(want)) { out = i; return true; }
			error = "no editor has project '" + want + "' open (nova status)";
			return false;
		}
		// 지금 폴더가 들어 있는 프로젝트
		const std::string cwd = NormPath(".");
		for (auto& i : list)
		{
			const std::string p = NormPath(i.Project);
			if (cwd == p || cwd.rfind(p + "\\", 0) == 0) { out = i; return true; }
		}
		if (list.size() == 1) { out = list[0]; return true; }
		error = std::to_string(list.size()) + " editors are running; choose one with --project <folder|name> or --pid <pid> (nova status)";
		return false;
	}

	// ------------------------------------------------------------------ 파이프
	bool Request(const Instance& inst, const std::string& cmd, const json& args, json& result, std::string& error, int waitFrames = 0)
	{
		const std::wstring pipe = Wide(inst.Pipe);
		HANDLE h = INVALID_HANDLE_VALUE;
		for (int attempt = 0; attempt < 50; ++attempt)
		{
			h = ::CreateFileW(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
			if (h != INVALID_HANDLE_VALUE) break;
			if (::GetLastError() != ERROR_PIPE_BUSY && attempt > 3) break;
			::WaitNamedPipeW(pipe.c_str(), 200);
		}
		if (h == INVALID_HANDLE_VALUE)
		{
			error = "cannot connect to the editor (pid " + std::to_string(inst.Pid) + ")";
			return false;
		}
		json req = { { "token", inst.Token }, { "id", 1 }, { "cmd", cmd }, { "args", args }, { "timeout", g_Timeout } };
		if (waitFrames > 0) req["waitFrames"] = waitFrames;
		const std::string line = req.dump() + "\n";
		DWORD written = 0;
		if (!::WriteFile(h, line.data(), (DWORD)line.size(), &written, nullptr))
		{
			::CloseHandle(h);
			error = "write to the editor failed";
			return false;
		}
		std::string buf;
		char chunk[65536];
		while (buf.find('\n') == std::string::npos)
		{
			DWORD read = 0;
			if (!::ReadFile(h, chunk, sizeof(chunk), &read, nullptr) || read == 0) break;
			buf.append(chunk, read);
		}
		::CloseHandle(h);
		const json resp = json::parse(buf.substr(0, buf.find('\n')), nullptr, false);
		if (!resp.is_object())
		{
			error = "no answer from the editor";
			return false;
		}
		if (!resp.value("ok", false))
		{
			error = resp.value("error", std::string("failed"));
			return false;
		}
		result = resp.contains("result") ? resp["result"] : json();
		return true;
	}

	// ------------------------------------------------------------------ 출력
	void PrintTree(const json& nodes, const std::string& indent)
	{
		for (size_t i = 0; i < nodes.size(); ++i)
		{
			const json& n = nodes[i];
			const bool last = i + 1 == nodes.size();
			std::string line = indent + (last ? "└─ " : "├─ ") + n.value("name", std::string()) + "  " + n.value("id", std::string());
			if (n.contains("active") && !n["active"].get<bool>()) line += "  (inactive)";
			if (n.contains("components"))
			{
				line += "  [";
				for (size_t c = 0; c < n["components"].size(); ++c)
					line += (c ? ", " : "") + n["components"][c].get<std::string>();
				line += "]";
			}
			if (n.contains("childCount")) line += "  (+" + std::to_string(n["childCount"].get<int>()) + " children)";
			Out(line + "\n");
			if (n.contains("children"))
				PrintTree(n["children"], indent + (last ? "   " : "│  "));
		}
	}

	void PrintResult(const std::string& cmd, const json& r)
	{
		if (g_Json)
		{
			Out(r.dump(2) + "\n");
			return;
		}
		if (cmd == "hierarchy" && r.is_array())
		{
			PrintTree(r, "");
			return;
		}
		if ((cmd == "find" && r.is_array()))
		{
			for (const auto& e : r) Out(e.value("id", std::string()) + "  " + e.value("path", std::string()) + "\n");
			if (r.empty()) Out("(nothing found)\n");
			return;
		}
		if (cmd == "shader-cross" && r.is_object())
		{
			for (const auto& f : r["results"])
			{
				Out(f.value("file", std::string()) + "  " + std::to_string(f.value("ok", 0)) + "/" + std::to_string(f.value("passes", 0)) + " passes" +
					(f.contains("error") ? "  FAILED: " + f["error"].get<std::string>() : "") + "\n");
				if (f.contains("errors"))
					for (const auto& e : f["errors"]) Out("    " + e.get<std::string>() + "\n");
			}
			Out("total: " + std::to_string(r.value("passesOk", 0)) + "/" + std::to_string(r.value("passes", 0)) + " passes, " + std::to_string(r.value("failedFiles", 0)) +
				" files failed, " + std::to_string((int)r.value("ms", 0.0)) + " ms\n");
			return;
		}
		if (cmd == "assets" && r.is_array())
		{
			for (const auto& e : r) Out(e.get<std::string>() + "\n");
			return;
		}
		if (cmd == "help" && r.is_object())
		{
			for (auto it = r.begin(); it != r.end(); ++it)
			{
				std::string name = it.key();
				name.resize((std::max)(name.size(), (size_t)18), ' ');
				Out("  " + name + it.value().get<std::string>() + "\n");
			}
			return;
		}
		if (r.is_object() && (cmd == "info" || cmd == "camera" || cmd == "screenshot" || cmd == "screenshot-editor" || cmd == "build-status" || cmd == "graphics" || cmd == "window" || cmd == "perf"))
		{
			for (auto it = r.begin(); it != r.end(); ++it)
				Out(it.key() + ": " + (it.value().is_string() ? it.value().get<std::string>() : it.value().dump()) + "\n");
			return;
		}
		if (r.is_null()) { Out("ok\n"); return; }
		Out(r.dump(2) + "\n");
	}

	// ------------------------------------------------------------------ 엔진 위치 (nova open, nova log)
	std::wstring EngineExe()
	{
		wchar_t env[MAX_PATH] = {};
		if (::GetEnvironmentVariableW(L"NOVA_ENGINE", env, MAX_PATH) > 0 && fs::exists(env))
			return env;
		wchar_t self[MAX_PATH] = {};
		::GetModuleFileNameW(nullptr, self, MAX_PATH);
		// Hub 가 설치하며 적어 둔 엔진 위치: nova.exe 옆 → 기본 설치 폴더
		for (const fs::path& file : { fs::path(self).parent_path() / L"engine.json", fs::path(LocalAppData()) / L"NOVA" / L"CLI" / L"engine.json" })
		{
			std::ifstream is(file);
			const json j = json::parse(is, nullptr, false);
			if (j.is_object() && j.contains("exe") && fs::exists(Wide(j["exe"].get<std::string>())))
				return Wide(j["exe"].get<std::string>());
		}
		// 개발: nova.exe 가 엔진 Binaries 안에 있을 때
		const fs::path next = fs::path(self).parent_path() / L"NovaEngine.exe";
		if (fs::exists(next)) return next.wstring();
		return L"";
	}

	// ------------------------------------------------------------------ 명령: 로컬 (에디터 없이)
	int CmdStatus()
	{
		const auto list = Instances();
		if (g_Json)
		{
			json arr = json::array();
			for (auto& i : list)
				arr.push_back({ { "pid", i.Pid }, { "project", i.Project }, { "projectName", i.ProjectName }, { "version", i.Version }, { "log", i.Log } });
			Out(arr.dump(2) + "\n");
			return 0;
		}
		if (list.empty())
		{
			Out("no NOVA editor is running\n");
			return 0;
		}
		for (auto& i : list)
			Out("pid " + std::to_string(i.Pid) + "  " + i.ProjectName + "  " + i.Project + "  (v" + i.Version + ")\n");
		return 0;
	}

	int CmdOpen(const Args& a)
	{
		if (a.Pos.empty()) { Err("usage: nova open <project folder> [--background]\n"); return 3; }
		const std::string project = Utf8(fs::absolute(Wide(a.Pos[0])).wstring());
		if (!fs::exists(fs::path(Wide(project)) / L"Assets"))
		{
			Err("not a NOVA project (no Assets folder): " + project + "\n");
			return 3;
		}
		for (auto& i : Instances())
			if (NormPath(i.Project) == NormPath(project))
			{
				Out("already open: pid " + std::to_string(i.Pid) + "\n");
				return 0;
			}
		const std::wstring exe = EngineExe();
		if (exe.empty())
		{
			Err("NovaEngine.exe not found (install the CLI from NOVA Hub > Installs, or set NOVA_ENGINE)\n");
			return 2;
		}
		std::wstring cmdLine = L"\"" + exe + L"\" --project \"" + Wide(project) + L"\"";
		if (a.Has("background")) cmdLine += L" --no-activate";
		// 이번 실행만 그래픽 API 를 정한다 (설정 파일은 그대로): --graphics opengl | d3d11
		if (a.Has("graphics"))
		{
			std::string g = a.Get("graphics");
			for (char& c : g) c = (char)tolower((unsigned char)c);
			if (g == "opengl" || g == "gl") cmdLine += L" -force-opengl";
			else if (g == "d3d11" || g == "dx11" || g == "directx11") cmdLine += L" -force-d3d11";
		}
		STARTUPINFOW si = { sizeof(si) };
		if (a.Has("background"))
		{
			si.dwFlags = STARTF_USESHOWWINDOW;
			si.wShowWindow = SW_SHOWNOACTIVATE;
		}
		PROCESS_INFORMATION pi = {};
		const std::wstring workDir = fs::path(exe).parent_path().wstring();
		if (!::CreateProcessW(exe.c_str(), cmdLine.data(), nullptr, nullptr, FALSE, 0, nullptr, workDir.c_str(), &si, &pi))
		{
			Err("could not start " + Utf8(exe) + "\n");
			return 2;
		}
		::CloseHandle(pi.hThread);
		const DWORD pid = pi.dwProcessId;
		// CLI 서버가 준비될 때까지 (최대 Timeout)
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds((std::max)(g_Timeout, 30));
		while (std::chrono::steady_clock::now() < deadline)
		{
			DWORD code = 0;
			if (::GetExitCodeProcess(pi.hProcess, &code) && code != STILL_ACTIVE)
			{
				::CloseHandle(pi.hProcess);
				Err("the editor exited during startup (code " + std::to_string(code) + ")\n");
				return 2;
			}
			for (auto& i : Instances())
				if (i.Pid == pid)
				{
					json r;
					std::string e;
					if (Request(i, "ping", json::object(), r, e))
					{
						::CloseHandle(pi.hProcess);
						Out("opened: pid " + std::to_string(pid) + "  " + project + "\n");
						return 0;
					}
				}
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
		}
		::CloseHandle(pi.hProcess);
		Err("the editor started (pid " + std::to_string(pid) + ") but the CLI server did not answer in time\n");
		return 2;
	}

	int CmdLog(const Args& a)
	{
		std::wstring file;
		Instance inst;
		std::string error;
		if (Pick(a, inst, error) && !inst.Log.empty())
			file = Wide(inst.Log);
		else
		{
			const std::wstring exe = EngineExe();
			if (!exe.empty()) file = (fs::path(exe).parent_path() / L"Logs" / L"Editor.log").wstring();
		}
		if (file.empty() || !fs::exists(file))
		{
			Err("Editor.log not found\n");
			return 2;
		}
		const int n = std::stoi(a.Get("n", "40"));
		const std::string grep = Lower(a.Get("grep"));
		const bool errors = a.Has("errors");
		auto keep = [&](const std::string& line) {
			const std::string l = Lower(line);
			if (!grep.empty() && l.find(grep) == std::string::npos) return false;
			if (errors && l.find("error") == std::string::npos && l.find("fail") == std::string::npos && l.find("exception") == std::string::npos && l.find("[hang]") == std::string::npos)
				return false;
			return true;
		};
		std::ifstream is(file, std::ios::binary);
		std::vector<std::string> lines;
		std::string line;
		while (std::getline(is, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (keep(line)) lines.push_back(line);
		}
		const size_t from = lines.size() > (size_t)n ? lines.size() - n : 0;
		for (size_t i = from; i < lines.size(); ++i) Out(lines[i] + "\n");
		if (!a.Has("follow")) return 0;
		std::streamoff pos = (std::streamoff)fs::file_size(file);
		for (;;)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
			std::error_code ec;
			const auto size = (std::streamoff)fs::file_size(file, ec);
			if (ec) continue;
			if (size < pos) pos = 0;   // 새 로그 (에디터 재시작)
			if (size == pos) continue;
			std::ifstream f(file, std::ios::binary);
			f.seekg(pos);
			std::string chunk((size_t)(size - pos), '\0');
			f.read(chunk.data(), chunk.size());
			pos = size;
			size_t start = 0, nl;
			while ((nl = chunk.find('\n', start)) != std::string::npos)
			{
				std::string l = chunk.substr(start, nl - start);
				if (!l.empty() && l.back() == '\r') l.pop_back();
				if (keep(l)) Out(l + "\n");
				start = nl + 1;
			}
			fflush(stdout);
		}
	}

	const char* kUsage =
		"NOVA CLI %s - control a running NOVA editor from the terminal (no window focus or mouse needed)\n"
		"\n"
		"usage: nova <command> [arguments] [--project <folder|name>] [--pid <pid>] [--json] [--timeout <s>]\n"
		"\n"
		"editors\n"
		"  status                                 running editors\n"
		"  open <project> [--background] [--graphics opengl|d3d11]   start the editor for a project and wait until it is ready\n"
		"  quit [--force]                         close the editor (--force discards unsaved changes)\n"
		"  info                                   project, scene, dirty, playing, selection\n"
		"  log [-n 40] [--grep text] [--errors] [--follow]\n"
		"\n"
		"scene objects (target = name, Parent/Child path, or #id)\n"
		"  hierarchy [--components] [--depth N] [--root <target>]\n"
		"  find [name] [--component Type]\n"
		"  get <target> [--component Type]       details + component JSON\n"
		"  set <target> [--name N] [--active true|false] [--tag T] [--layer N] [--static true|false]\n"
		"               [--position x,y,z] [--rotation x,y,z] [--scale x,y,z] [--world-position x,y,z]\n"
		"               [Component.field=value ...]          e.g. Light.intensity=2 MeshRenderer.castShadows=1\n"
		"  create <type> [--name N] [--parent P] [--position x,y,z] [--rotation ..] [--scale ..] [--preset N]\n"
"                character: [--model <FBX>] [--controller <.controller>]\n"
		"         types: empty cube sphere capsule cylinder plane quad directional-light point-light spot-light camera\n"
		"                terrain tree rock rock-scatter ocean lake river particle-system audio-source volume character\n"
		"                third-person-character (character + Character Controller + ThirdPersonController + Follow Camera, adds packages)\n"
		"                ui:Image ui:Text ui:Panel ui:Button ui:Toggle ui:Slider ui:Scrollbar ui:ScrollView ui:Dropdown ui:InputField ui:Canvas ui:EventSystem\n"
		"                (= GameObject > UI menu: under --parent or the first Canvas; makes Canvas + EventSystem if missing)\n"
		"  delete <target>\n"
		"  add-component <target> <Type|C# class> [--values '{...}']   remove-component <target> <Type>\n"
		"  parent <target> <new parent> | parent <target> --root\n"
		"  select <target> | select --asset <Assets/...> | select --none\n"
		"  import-settings <asset> [--values '{...}'] [--reset]   texture / model / audio Import Settings (.meta): show, or change + reimport\n"
		"         texture {maxSize, compression: None|NormalQuality|HighQuality, mipmaps}  model {scaleFactor, importAnimation,\n"
		"         animationType: Generic|Humanoid, humanBones: {LeftFoot: node}}  audio {loadType: DecompressOnLoad|CompressedInMemory|Streaming, forceToMono}\n"
		"\n"
		"scene / play / view\n"
		"  scene open <Assets/...scene> [--force]  scene new [--force]  scene save\n"
		"  play | stop | pause [on|off] | step | undo | redo\n"
		"  camera [--position x,y,z --target x,y,z | --frame <target> [--distance d]]   Scene view camera\n"
		"  screenshot <file.png> [--view scene|game|editor]   (editor = whole window incl. menus; relative = current folder)\n"
		"  window <preferences|project-settings|build-settings|scene|game|project|console|hierarchy|inspector|animator> [--category C] [--close] [--float]\n"
		"  gfx-test [both|DirectX11|OpenGL] [--out folder] [--width W --height H]   same, drawn the engine way (Gfx + effects)\n"
		"  rhi-test [both|DirectX11|OpenGL] [--out folder] [--width W --height H]   draw the RHI test scene per API, save PNGs, compare pixels\n"
		"  wait [frames]                          keep the editor rendering N frames (default 60), then return (background editors pause otherwise)\n"
		"  raycast <x,y,z> <dx,dy,dz> [--max d]   Physics.Raycast in Play mode: hit object, point, normal, distance\n"
		"  exec <C# code> | exec --file f.cs      run C# in the editor (expression -> its value; statements -> return value)\n"
		"  batch <file | -> [--keep-going]        run many nova commands (one per line) in one process\n"
		"  perf [--frames N] [--depth D]          measure N frames (default 240): frame ms / fps, CPU ms, GPU ms, top GPU passes / CPU scopes\n"
		"  shader-cross [file] [--out folder] [--max-errors N]   convert engine .fx shaders to GLSL (OpenGL) and report\n"
		"  graphics [--editor DirectX11|OpenGL] [--player OpenGL,DirectX11] [--auto true|false]   graphics API settings\n"
		"  assets [folder] [--pattern text]\n"
		"  build <output folder> [--run]   build-status [--wait]\n"
		"\n"
		"  autosave [status|now|recover|discard]  auto save (Library/AutoSave) and crash recovery\n"
		"\n"
		"packages (Unity Package Manager: only packages added to the project are loaded and built)\n"
		"  package list                           registry packages + what the project uses (loaded / error)\n"
		"  package add <name> | package remove <name>   e.g. com.nova.cameras (writes Packages/manifest.json)\n"
		"\n"
		"model editor (package com.nova.modeling: Window > Model Editor)\n"
		"  model <op> [path] [--key value ...]    mesh modeling: add, select.*, extrude, inset, loopcut, bevel, mirror, subsurf,\n"
		"                                         import / export (fbx obj glb), render (PNG per view) ... list: nova model help\n"
		"  model ref.add <image> --view front --height 1.6   reference image;  model compare [--out diff.png]  silhouette IoU + hints\n"
		"  model batch <file | ->                 one op per line (\"add --type cube\"), all as ONE undo step, stops at the first error\n"
		"\n"
		"2D animator (package com.nova.animation2d: Window > 2D Animator, Spine-style)\n"
		"  anim2d <op> [path] [--key value ...]   bone.add, image.add (PNG on a bone), slot.set, anim.new, pose, anim.key,\n"
		"                                         render (PNG), export (sprite sheet + JSON), image.make ... list: nova anim2d help\n"
		"  anim2d batch <file | ->                one op per line, all as ONE undo step\n"
		"\n"
		"shader graph (Window > Shader Graph: node-based material shaders, Unity Shader Graph style)\n"
		"  shadergraph new <Assets/X.shadergraph> [--material Lit|Unlit|Decal] then node.add, connect, property.add, save ...\n"
		"  shadergraph <op> [--key value ...]     nodes (types + ports), info, node.add/set/delete, connect --from 2 --out Out\n"
		"                                         --to Master --in \"Base Color\", property.add, save (builds; errors), material\n"
		"  shadergraph batch <file | ->           one op per line, all as ONE undo step ... list: nova shadergraph help\n"
		"\n"
		"layers / physics (Project Settings > Tags and Layers / Physics)\n"
		"  layers [--set 8 --name Enemy] [--add-tag Boss] [--remove-tag Boss]   layer names (3, 6..31) and tags\n"
		"  physics [--gravity 0,-9.81,0] [--ignore Player,Enemy] [--collide A,B] [--all true|false]   Layer Collision Matrix\n"
		"  physics --2d [--gravity 0,-9.81] [--ignore A,B] ...   Physics 2D (Box2D) settings\n"
		"  set <target> --layer Water             layer by name or number\n"
		"  layers --add-sorting-layer Background [--move-sorting-layer Background --by -1]   sprite Sorting Layers\n"
		"  sprite-slice <png> --mode grid --cell 32,32 [--animation 12]   Sprite Mode = Multiple (+ .spriteanim)\n"
		"\n"
		"other\n"
		"  call <command> [json args]             raw request (see: nova help --editor)\n"
		"  ai-guide                               how an AI agent should use NOVA CLI\n"
		"  version\n";

	const char* kAiGuide =
		"# NOVA CLI guide for AI agents\n"
		"\n"
		"NOVA CLI lets you inspect and edit a running NOVA editor without focusing its window or using the mouse.\n"
		"Every edit becomes one Undo step named \"CLI ...\" (the user can press Ctrl+Z).\n"
		"\n"
		"## Workflow\n"
		"1. `nova status` - is an editor running? If not: `nova open <project folder> --background`.\n"
		"2. `nova info --json` - project, open scene, unsaved changes, Play mode, selection.\n"
		"3. `nova hierarchy --components` - the scene tree with #ids. Prefer #ids for targets (names can repeat).\n"
		"4. `nova get <target> --json` - fields of every component (field names are what `set` accepts).\n"
		"5. Edit: `nova create cube --name Box --position 0,1,0`, `nova set Box --scale 2,1,2`,\n"
		"   `nova set Box MeshRenderer.castShadows=1`, `nova add-component Box RigidBody`.\n"
		"6. Look at the result: `nova camera --frame Box` then `nova screenshot shot.png` and open the image.\n"
		"7. Check `nova log --errors` after changes. Save only when asked: `nova scene save`.\n"
		"\n"
		"## 3D modeling (package com.nova.modeling)\n"
		"- `nova package add com.nova.modeling`, then `nova model help --json` lists every op with its arguments.\n"
		"- World coords in meters, Y up, a character faces +Z (its right hand = +X). Prefer index-free selection:\n"
		"  `model select.box --min x,y,z --max x,y,z`, `model select.normal --direction 0,1,0`.\n"
		"- Every op returns counts, bounds and the selection; `boundaryEdges` 0 = closed mesh. `model undo` undoes one op.\n"
		"- Look at your work often: `model render --dir shots --views front,right,three-quarter --shading toon --wire false`\n"
		"  and open the PNGs. Export for the game: `model export Assets/Models/Name.fbx`. Spec: docs/MODEL_EDITOR.md\n"
		"- With drawings: `model ref.add front.png --view front --height 1.5`, then `model compare --json` gives IoU and\n"
		"  hints like \"widen by 0.3 m near y 0.6\". Name parts with `model group.assign --name Arm.R`, try ideas after\n"
		"  `model checkpoint --save v1`, and run many ops as one undo step with `model batch steps.txt`.\n"
		"\n"
		"## 2D skeletal animation (package com.nova.animation2d, Spine-style)\n"
		"- `nova package add com.nova.animation2d`, then `nova anim2d help --json`. Pixels, y up, rotation in degrees CCW\n"
		"  (0 = right, 90 = up, -90 = down). An image's width runs along its bone. Example: docs/examples/anim2d_walker.txt\n"
		"- Parts: `anim2d image.make --path Assets/Parts/arm.png --shape capsule --size 46,16 --color 0.2,0.5,0.9,1`;\n"
		"  bones: `anim2d bone.add --name arm --parent torso --world --x 4 --y 170 --rotation -90 --length 38`;\n"
		"  images back to front: `anim2d image.add --bone arm --image Assets/Parts/arm.png --x 19`.\n"
		"- Animate: `anim2d anim.new --name walk --length 0.8`, then per key time `anim2d anim.time --time 0.2`,\n"
		"  `anim2d pose --bone arm --rotation -60 --world`, `anim2d anim.key --curve smooth`.\n"
		"- Look: `anim2d render --path f.png --anim walk --time 0.2 --bones`. Ship: `anim2d export --path walk.png --fps 12`.\n"
		"\n"
		"## Rules\n"
		"- Use --json when you parse output. Exit code: 0 ok, 1 command failed (message on stderr), 2 no editor, 3 usage.\n"
		"- Edits are refused in Play mode; `nova stop` first. `scene open` refuses unsaved changes unless --force.\n"
		"- Component.field=value takes JSON values: numbers, true/false, [1,2,3], \"text\" (quote for the shell).\n"
		"- Transform is changed with --position/--rotation/--scale (local) or --world-position.\n"
		"- Do not quit the user's editor or discard changes unless they asked.\n";

	int Usage() { Out(std::string(kUsage).replace(std::string(kUsage).find("%s"), 2, kVersion)); return 0; }
}

int Run(const std::vector<std::string>& in);

// 한 줄을 셸처럼 나눈다: 빈칸으로 나누고 "…" / '…' 안은 그대로, "…" 안의 \" 는 따옴표
std::vector<std::string> SplitLine(const std::string& line)
{
	std::vector<std::string> out;
	std::string cur;
	bool has = false;
	char quote = 0;
	for (size_t i = 0; i < line.size(); ++i)
	{
		const char c = line[i];
		if (quote)
		{
			if (c == '\\' && quote == '"' && i + 1 < line.size() && (line[i + 1] == '"' || line[i + 1] == '\\')) { cur += line[++i]; continue; }
			if (c == quote) { quote = 0; continue; }
			cur += c;
			continue;
		}
		if (c == '\\' && i + 1 < line.size() && line[i + 1] == '"') { cur += line[++i]; continue; }   // 따옴표 밖의 \" 도 따옴표
		if (c == '"' || c == '\'') { quote = c; has = true; continue; }
		if (c == ' ' || c == '\t')
		{
			if (has || !cur.empty()) out.push_back(cur);
			cur.clear();
			has = false;
			continue;
		}
		cur += c;
	}
	if (has || !cur.empty()) out.push_back(cur);
	return out;
}

// nova batch <파일 | -> : 줄마다 nova 명령 하나 (앞의 "nova" 는 있어도 됨, # 은 주석). 한 프로세스에서 차례로 → 명령마다 nova 를 새로 띄우지 않는다.
// batch 에 준 --project / --pid / --json / --timeout 은 모든 줄에 붙는다. 실패하면 멈춘다 (--keep-going 이면 계속)
int RunBatch(const std::vector<std::string>& in)
{
	Args a = Parse(in, 1);
	if (a.Pos.empty())
	{
		Err("usage: nova batch <file | -> [--keep-going] [--project P]\n");
		return 3;
	}
	std::string text;
	if (a.Pos[0] == "-")
	{
		std::string line;
		while (std::getline(std::cin, line)) text += line + "\n";
	}
	else
	{
		std::ifstream f(fs::path(Wide(a.Pos[0])), std::ios::binary);
		if (!f) { Err("cannot open " + a.Pos[0] + "\n"); return 3; }
		text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) text.erase(0, 3);
	}
	std::vector<std::string> shared;
	for (const char* k : { "project", "pid", "timeout" })
		if (a.Has(k)) { shared.push_back(std::string("--") + k); shared.push_back(a.Get(k)); }
	if (a.Has("json")) shared.push_back("--json");
	const bool keepGoing = a.Has("keep-going");
	int failures = 0, count = 0;
	std::istringstream lines(text);
	std::string line;
	while (std::getline(lines, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const size_t first = line.find_first_not_of(" \t");
		if (first == std::string::npos || line[first] == '#') continue;
		std::string body = line.substr(first);
		if (body.rfind("nova ", 0) == 0) body = body.substr(5);
		std::vector<std::string> args = SplitLine(body);
		if (args.empty()) continue;
		for (const std::string& s : shared)
			args.push_back(s);
		Out("> nova " + body + "\n");
		++count;
		const int rc = Run(args);
		if (rc != 0)
		{
			++failures;
			if (!keepGoing) { Err("batch stopped at line " + std::to_string(count) + " (exit " + std::to_string(rc) + ")\n"); return rc; }
		}
	}
	Out("batch: " + std::to_string(count) + " commands, " + std::to_string(failures) + " failed\n");
	return failures ? 1 : 0;
}

int wmain(int argc, wchar_t** argv)
{
	::SetConsoleOutputCP(CP_UTF8);
	std::vector<std::string> in;
	for (int i = 1; i < argc; ++i) in.push_back(Utf8(argv[i]));
	if (in.empty() || in[0] == "help" && in.size() == 1 || in[0] == "--help" || in[0] == "-h")
		return Usage();
	if (in[0] == "batch")
		return RunBatch(in);
	return Run(in);
}

int Run(const std::vector<std::string>& in)
{
	std::string cmd = in[0];
	Args a = Parse(in, 1);
	g_Json = a.Has("json");
	if (a.Has("timeout")) g_Timeout = (std::max)(1, std::stoi(a.Get("timeout")));

	if (cmd == "version") { Out(std::string("nova ") + kVersion + "\n"); return 0; }
	if (cmd == "ai-guide") { Out(kAiGuide); return 0; }
	if (cmd == "status") return CmdStatus();
	if (cmd == "open") return CmdOpen(a);
	if (cmd == "log") return CmdLog(a);

	// ---- 에디터에 보내는 명령: 인수 → 요청
	json args = json::object();
	std::string rc = cmd;   // 서버 명령 이름
	auto need = [&](size_t n, const char* usage) {
		if (a.Pos.size() >= n) return true;
		Err(std::string("usage: nova ") + usage + "\n");
		return false;
	};
	std::vector<std::pair<std::string, json>> fieldSets;   // set 의 Component.field=value 묶음

	if (cmd == "help")
	{
		rc = "help";
	}
	else if (cmd == "info" || cmd == "play" || cmd == "stop" || cmd == "step" || cmd == "undo" || cmd == "redo" || cmd == "build-status")
	{
	}
	else if (cmd == "quit")
	{
		args["force"] = a.Has("force");
	}
	else if (cmd == "pause")
	{
		if (!a.Pos.empty()) args["on"] = a.Pos[0] == "on" || a.Pos[0] == "true" || a.Pos[0] == "1";
	}
	else if (cmd == "hierarchy")
	{
		args["components"] = a.Has("components");
		if (a.Has("depth")) args["depth"] = std::stoi(a.Get("depth"));
		if (a.Has("root")) args["root"] = a.Get("root");
	}
	else if (cmd == "find")
	{
		if (!a.Pos.empty()) args["name"] = a.Pos[0];
		if (a.Has("component")) args["component"] = a.Get("component");
		if (a.Has("limit")) args["limit"] = std::stoi(a.Get("limit"));
	}
	else if (cmd == "get")
	{
		if (!need(1, "get <target> [--component Type]")) return 3;
		args["target"] = a.Pos[0];
		if (a.Has("component")) args["component"] = a.Get("component");
	}
	else if (cmd == "set")
	{
		if (!need(1, "set <target> [--position x,y,z] [Component.field=value ...]")) return 3;
		args["target"] = a.Pos[0];
		if (a.Has("name")) args["name"] = a.Get("name");
		if (a.Has("active")) args["active"] = a.Get("active") == "true" || a.Get("active") == "1";
		if (a.Has("static")) args["static"] = a.Get("static") == "true" || a.Get("static") == "1";
		if (a.Has("tag")) args["tag"] = a.Get("tag");
		if (a.Has("layer")) args["layer"] = Value(a.Get("layer"));   // 번호 또는 이름 (Water, UI …)
		if (a.Has("position")) args["position"] = Vec(a.Get("position"));
		if (a.Has("rotation")) args["rotation"] = Vec(a.Get("rotation"));
		if (a.Has("scale")) args["scale"] = Vec(a.Get("scale"));
		if (a.Has("world-position")) args["worldPosition"] = Vec(a.Get("world-position"));
		if (a.Has("component"))
		{
			args["component"] = a.Get("component");
			args["values"] = Value(a.Get("values", "{}"));
		}
		// Component.field=value
		std::map<std::string, json> byComp;
		for (size_t i = 1; i < a.Pos.size(); ++i)
		{
			const std::string& p = a.Pos[i];
			const size_t eq = p.find('='), dot = p.find('.');
			if (eq == std::string::npos || dot == std::string::npos || dot > eq)
			{
				Err("expected Component.field=value, got '" + p + "'\n");
				return 3;
			}
			byComp[p.substr(0, dot)][p.substr(dot + 1, eq - dot - 1)] = Value(p.substr(eq + 1));
		}
		for (auto& [comp, values] : byComp)
			fieldSets.push_back({ comp, values });
	}
	else if (cmd == "create")
	{
		if (!need(1, "create <type> [--name N] [--parent P] [--position x,y,z]")) return 3;
		args["type"] = a.Pos[0];
		if (a.Has("name")) args["name"] = a.Get("name");
		if (a.Has("parent")) args["parent"] = a.Get("parent");
		if (a.Has("position")) args["position"] = Vec(a.Get("position"));
		if (a.Has("rotation")) args["rotation"] = Vec(a.Get("rotation"));
		if (a.Has("scale")) args["scale"] = Vec(a.Get("scale"));
		if (a.Has("preset")) args["preset"] = std::stoi(a.Get("preset"));
		if (a.Has("model")) args["model"] = a.Get("model");               // character: 모델 FBX
		if (a.Has("controller")) args["controller"] = a.Get("controller");   // character: Animator Controller
		if (a.Has("no-select")) args["select"] = false;
	}
	else if (cmd == "delete")
	{
		if (!need(1, "delete <target>")) return 3;
		args["target"] = a.Pos[0];
	}
	else if (cmd == "add-component" || cmd == "remove-component")
	{
		if (!need(2, "add-component <target> <Type> [--values '{...}']")) return 3;
		args["target"] = a.Pos[0];
		args["type"] = a.Pos[1];
		if (a.Has("values")) args["values"] = Value(a.Get("values"));
	}
	else if (cmd == "parent")
	{
		if (!need(1, "parent <target> <new parent> | parent <target> --root")) return 3;
		args["target"] = a.Pos[0];
		args["parent"] = a.Has("root") || a.Pos.size() < 2 ? json() : json(a.Pos[1]);
		if (a.Has("keep-world")) args["keepWorld"] = a.Get("keep-world") != "false";
	}
	else if (cmd == "select")
	{
		if (a.Has("asset")) args["asset"] = a.Get("asset");
		else if (!a.Has("none") && !a.Pos.empty()) args["target"] = a.Pos[0];
	}
	else if (cmd == "import-settings")
	{
		if (!need(1, "import-settings <asset> [--values '{...}'] [--reset]")) return 3;
		args["path"] = a.Pos[0];
		if (a.Has("values")) args["values"] = Value(a.Get("values"));
		if (a.Has("reset")) args["reset"] = true;
	}
	else if (cmd == "autosave")
	{
		args["action"] = a.Pos.empty() ? std::string("status") : a.Pos[0];
	}
	else if (cmd == "package")
	{
		if (!need(1, "package list | package add <name> | package remove <name>")) return 3;
		if (a.Pos[0] == "list")
			rc = "package-list";
		else if (a.Pos[0] == "add" || a.Pos[0] == "remove")
		{
			if (!need(2, "package add|remove <name>   (e.g. com.nova.cameras)")) return 3;
			rc = "package-" + a.Pos[0];
			args["name"] = a.Pos[1];
		}
		else
		{
			Err("usage: nova package list | package add <name> | package remove <name>\n");
			return 3;
		}
	}
	else if (cmd == "scene")
	{
		if (!need(1, "scene open <path> [--force] | scene save")) return 3;
		if (a.Pos[0] == "open")
		{
			if (!need(2, "scene open <Assets/...scene> [--force]")) return 3;
			rc = "scene-open";
			args["path"] = a.Pos[1];
			args["force"] = a.Has("force");
		}
		else if (a.Pos[0] == "save")
		{
			rc = "scene-save";
			if (a.Has("as")) args["as"] = a.Get("as");
		}
		else if (a.Pos[0] == "new")
		{
			rc = "scene-new";
			args["force"] = a.Has("force");
		}
		else
		{
			Err("usage: nova scene open <path> | nova scene new [--force] | nova scene save\n");
			return 3;
		}
	}
	else if (cmd == "camera")
	{
		if (a.Has("frame")) { args["frame"] = a.Get("frame"); if (a.Has("distance")) args["distance"] = std::stof(a.Get("distance")); }
		if (a.Has("position")) args["position"] = Vec(a.Get("position"));
		if (a.Has("target")) args["target"] = Vec(a.Get("target"));
	}
	else if (cmd == "screenshot")
	{
		if (!need(1, "screenshot <file.png> [--view scene|game]")) return 3;
		args["path"] = Utf8(fs::absolute(Wide(a.Pos[0])).wstring());
		args["view"] = a.Get("view", "scene");
		if (Lower(a.Get("view")) == "editor")
			rc = "screenshot-editor";   // 에디터 전체 (메뉴·창 포함)
	}
	else if (cmd == "window")
	{
		if (!need(1, "window <preferences|project-settings|build-settings|scene|game|project|console|hierarchy|inspector|animator> [--category C] [--close]")) return 3;
		args["name"] = a.Pos[0];
		if (a.Has("category")) args["category"] = a.Get("category");
		if (a.Has("close")) args["close"] = true;
		if (a.Has("float")) args["float"] = true;
	}
	else if (cmd == "layers")
	{
		// nova layers [--set 8 --name Enemy] [--add-tag Boss] [--remove-tag Boss]
		rc = "layers";
		if (a.Has("set")) { args["set"] = std::stoi(a.Get("set")); args["name"] = a.Get("name"); }
		if (a.Has("add-tag")) args["addTag"] = a.Get("add-tag");
		if (a.Has("remove-tag")) args["removeTag"] = a.Get("remove-tag");
		if (a.Has("add-sorting-layer")) args["addSortingLayer"] = a.Get("add-sorting-layer");
		if (a.Has("move-sorting-layer")) { args["moveSortingLayer"] = a.Get("move-sorting-layer"); args["by"] = std::stoi(a.Get("by", "-1")); }
	}
	else if (cmd == "sprite-slice")
	{
		// nova sprite-slice <Assets/…png> [--mode grid|count|auto|sheet] [--cell 32,32] [--count 4,1] [--offset x,y] [--padding x,y]
		//   [--pivot 0.5,0] [--keep-empty] [--min-size 4] [--ppu 16] [--filter point] [--animation 12]
		if (!need(1, "sprite-slice <Assets/...png> [--mode grid|count|auto|sheet] [--cell w,h] [--count c,r] [--animation fps]")) return 3;
		rc = "sprite-slice";
		args["path"] = a.Pos[0];
		args["mode"] = a.Get("mode", "grid");
		for (const char* key : { "cell", "count", "offset", "padding", "pivot" })
			if (a.Has(key)) args[key] = NumberList(a.Get(key));
		if (a.Has("keep-empty")) args["keepEmpty"] = true;
		if (a.Has("min-size")) args["minSize"] = std::stoi(a.Get("min-size"));
		if (a.Has("ppu")) args["ppu"] = std::stof(a.Get("ppu"));
		if (a.Has("filter")) args["filter"] = a.Get("filter");
		if (a.Has("animation")) args["animation"] = a.Get("animation").empty() || a.Get("animation") == "true" ? json(true) : json(std::stof(a.Get("animation")));
	}
	else if (cmd == "physics")
	{
		// nova physics [--gravity 0,-9.81,0] [--ignore Player,Enemy] [--collide Player,Enemy] [--all true|false]
		rc = "physics-settings";
		if (a.Has("2d")) args["2d"] = true;   // Project Settings > Physics 2D
		if (a.Has("gravity")) args["gravity"] = NumberList(a.Get("gravity"));
		for (const char* key : { "ignore", "collide" })
			if (a.Has(key))
			{
				const std::string v = a.Get(key);
				const size_t comma = v.find(',');
				if (comma == std::string::npos) { Err(std::string("--") + key + " A,B\n"); return 3; }
				args[key] = json::array({ Value(v.substr(0, comma)), Value(v.substr(comma + 1)) });
			}
		if (a.Has("all")) args["all"] = a.Get("all") == "true";
	}
	else if (cmd == "graphics")
	{
		if (a.Has("editor")) args["editor"] = a.Get("editor");
		if (a.Has("player"))
		{
			json list = json::array();
			std::string s = a.Get("player");
			size_t start = 0;
			while (start <= s.size())
			{
				const size_t c = s.find(',', start);
				const std::string part = s.substr(start, c == std::string::npos ? std::string::npos : c - start);
				if (!part.empty()) list.push_back(part);
				if (c == std::string::npos) break;
				start = c + 1;
			}
			args["player"] = list;
		}
		if (a.Has("auto")) args["auto"] = a.Get("auto") != "false" && a.Get("auto") != "0";
	}
	else if (cmd == "rhi-test" || cmd == "gfx-test")
	{
		if (!a.Pos.empty()) args["api"] = a.Pos[0];
		if (a.Has("out")) args["out"] = Utf8(fs::absolute(Wide(a.Get("out"))).wstring());
		if (a.Has("width")) args["width"] = std::stoi(a.Get("width"));
		if (a.Has("height")) args["height"] = std::stoi(a.Get("height"));
	}
	else if (cmd == "shader-cross")
	{
		if (!a.Pos.empty()) args["file"] = a.Pos[0];
		if (a.Has("out")) args["out"] = Utf8(fs::absolute(Wide(a.Get("out"))).wstring());
		if (a.Has("max-errors")) args["errors"] = std::stoi(a.Get("max-errors"));
	}
	else if (cmd == "assets")
	{
		if (!a.Pos.empty()) args["path"] = a.Pos[0];
		if (a.Has("pattern")) args["pattern"] = a.Get("pattern");
		if (a.Has("limit")) args["limit"] = std::stoi(a.Get("limit"));
	}
	else if (cmd == "build")
	{
		if (!need(1, "build <output folder> [--run]")) return 3;
		args["output"] = Utf8(fs::absolute(Wide(a.Pos[0])).wstring());
		args["run"] = a.Has("run");
	}
	else if (cmd == "wait")
	{
		// 인수 없음 (프레임 수는 요청의 waitFrames 로)
	}
	else if (cmd == "exec")
	{
		// C# 실행: nova exec "GameObject.Find(\"Box\").transform.position" | nova exec --file 코드.cs
		if (a.Has("file"))
		{
			std::ifstream f(fs::path(Wide(a.Get("file"))), std::ios::binary);
			if (!f) { Err("cannot open " + a.Get("file") + "\n"); return 3; }
			args["code"] = std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		}
		else
		{
			if (!need(1, "exec <C# code> | exec --file <file.cs>")) return 3;
			std::string code;
			for (size_t i = 0; i < a.Pos.size(); ++i) code += (i ? " " : "") + a.Pos[i];
			args["code"] = code;
		}
		if (!a.Has("timeout")) g_Timeout = (std::max)(g_Timeout, 180);   // 첫 빌드는 오래 걸릴 수 있다
	}
	else if (cmd == "terrain-trees")
	{
		// 지형에 나무 흩뿌리기: nova terrain-trees <지형> [--count N] [--clear]
		if (!need(1, "terrain-trees <terrain> [--count N] [--clear]")) return 3;
		args["target"] = a.Pos[0];
		if (a.Has("count")) args["count"] = std::stoi(a.Get("count"));
		if (a.Has("clear")) args["clear"] = true;
	}
	else if (cmd == "raycast")
	{
		// Unity 의 Physics.Raycast (Play 중): nova raycast x,y,z dx,dy,dz [--max d] [--triggers]
		if (!need(2, "raycast <x,y,z> <dx,dy,dz> [--max d] [--triggers]")) return 3;
		args["origin"] = Vec(a.Pos[0]);
		args["direction"] = Vec(a.Pos[1]);
		if (a.Has("max")) args["maxDistance"] = std::stof(a.Get("max"));
		if (a.Has("triggers")) args["triggers"] = true;
	}
	else if (cmd == "perf")
	{
		// perf-begin 을 보낸 뒤 N 프레임 뒤에 perf (아래)
		if (a.Has("depth")) args["depth"] = std::stoi(a.Get("depth"));
	}
	else if (cmd == "model" || cmd == "anim2d" || cmd == "shadergraph")
	{
		// 모델 편집기 (com.nova.modeling) · 2D 애니메이터 (com.nova.animation2d): nova model|anim2d <op> [경로] [--이름 값 …]
		//  값은 JSON 으로 읽히면 그대로 (숫자 · true · [1,2,3]), "1,2,3" 은 배열, 아니면 문자열. 값 없는 --이름 = true
		if (!need(1, (cmd + " <op> [path] [--key value ...]   (nova " + cmd + " help)").c_str())) return 3;
		rc = cmd;
		// 낱말들 → 연산 인자 (첫 위치 인수 = path)
		auto parseOpts = [&](const std::vector<std::string>& t, size_t start, json& out) {
			for (size_t i = start; i < t.size(); ++i)
			{
				const std::string& s = t[i];
				if (s.size() <= 2 || s[0] != '-' || s[1] != '-')
				{
					if (!out.contains("path")) out["path"] = s;   // 위치 인수 = 파일 경로 (import · export · open · save)
					continue;
				}
				std::string name = s.substr(2), value;
				bool hasValue = false;
				const size_t eq = name.find('=');
				if (eq != std::string::npos) { value = name.substr(eq + 1); name = name.substr(0, eq); hasValue = true; }
				else if (i + 1 < t.size() && !(t[i + 1].size() > 2 && t[i + 1][0] == '-' && t[i + 1][1] == '-')) { value = t[++i]; hasValue = true; }
				if (name == "project" || name == "pid" || name == "json" || name == "timeout")
					continue;
				if (!hasValue) { out[name] = true; continue; }
				json v = Value(value);
				if (v.is_string()) v = NumberList(value);   // "84,46" · "1,0.5,0.2,1" → 배열 (개수 상관없이)
				out[name] = v;
			}
		};
		args["op"] = a.Pos[0];
		parseOpts(in, 2, args);
		// batch <파일 | -> : 줄마다 연산 하나 ("add --type cube" — 앞의 "nova model" · "nova anim2d" 는 있어도 됨, # 주석) 또는 JSON 배열 → 한 요청 (Undo 한 번)
		if (a.Pos[0] == "batch" && args.contains("path") && !args.contains("steps"))
		{
			const std::string file = args["path"].get<std::string>();
			args.erase("path");
			std::string text;
			if (file == "-")
			{
				std::string line;
				while (std::getline(std::cin, line)) text += line + "\n";
			}
			else
			{
				std::ifstream f(std::filesystem::u8path(file), std::ios::binary);
				if (!f) { Err("cannot read " + file + "\n"); return 3; }
				text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
			}
			if (text.size() >= 3 && (unsigned char)text[0] == 0xEF) text = text.substr(3);   // BOM
			json steps = json::array();
			const size_t first = text.find_first_not_of(" \t\r\n");
			if (first != std::string::npos && text[first] == '[')
			{
				steps = json::parse(text, nullptr, false);
				if (!steps.is_array()) { Err("bad JSON in " + file + "\n"); return 3; }
			}
			else
			{
				std::stringstream ss(text);
				std::string line;
				while (std::getline(ss, line))
				{
					if (!line.empty() && line.back() == '\r') line.pop_back();
					std::vector<std::string> t = SplitLine(line);
					if (t.empty() || t[0].rfind("#", 0) == 0) continue;
					size_t at = 0;
					if (at < t.size() && (t[at] == "nova" || t[at] == "nova.exe")) ++at;
					if (at < t.size() && t[at] == cmd) ++at;
					if (at >= t.size()) continue;
					json step = { { "op", t[at] } };
					parseOpts(t, at + 1, step);
					steps.push_back(step);
				}
			}
			args["steps"] = steps;
		}
	}
	else if (cmd == "call")
	{
		if (!need(1, "call <command> [json args]")) return 3;
		rc = a.Pos[0];
		if (a.Pos.size() > 1)
		{
			args = json::parse(a.Pos[1], nullptr, false);
			if (!args.is_object()) { Err("args must be a JSON object\n"); return 3; }
		}
	}
	else
	{
		Err("unknown command '" + cmd + "' (nova help)\n");
		return 3;
	}

	Instance inst;
	std::string error;
	if (!Pick(a, inst, error))
	{
		Err(error + "\n");
		return 2;
	}

	json result;
	if (cmd == "set" && !fieldSets.empty())
	{
		// 이름·위치 등 + 컴포넌트마다 한 번씩
		const bool hasBase = args.size() > 1;
		if (hasBase && !Request(inst, "set", args, result, error)) { Err(error + "\n"); return 1; }
		for (auto& [comp, values] : fieldSets)
		{
			json one = { { "target", args["target"] }, { "component", comp }, { "values", values } };
			if (!Request(inst, "set", one, result, error)) { Err(error + "\n"); return 1; }
		}
		PrintResult("set", result);
		return 0;
	}
	// wait N: 에디터가 N 프레임을 그릴 때까지 기다린다 (백그라운드 에디터는 할 일이 없으면 멈추므로, 장면 불러오기 같은 여러 프레임 작업을 기다릴 때)
	int waitFrames = cmd == "wait" ? (std::max)(1, a.Pos.empty() ? 60 : std::stoi(a.Pos[0])) : 0;
	if (cmd == "perf")
	{
		// 성능 측정: 프로파일러를 켜고 N 프레임(기본 240, 최대 290 = 기록 300 안) 그린 뒤 평균
		waitFrames = std::clamp(a.Has("frames") ? std::stoi(a.Get("frames")) : 240, 30, 290);
		if (!Request(inst, "perf-begin", json::object(), result, error)) { Err(error + "\n"); return 1; }
	}
	if (!Request(inst, rc, args, result, error, waitFrames))
	{
		Err(error + "\n");
		return 1;
	}
	if (cmd == "build-status" && a.Has("wait"))
	{
		while (result.value("running", false))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			if (!Request(inst, rc, args, result, error)) { Err(error + "\n"); return 1; }
		}
	}
	PrintResult(rc, result);
	return 0;
}
