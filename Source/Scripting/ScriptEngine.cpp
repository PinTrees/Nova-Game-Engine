#include "pch.h"
#include "Debug.h"
#include "ScriptEngine.h"
#include "PackageManager.h"
#include "ScriptBindings.h"
#include "GameViewEditorWindow.h"
#include "ExternalScriptEditor.h"
#include <DotNet/hostfxr.h>
#include <DotNet/coreclr_delegates.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <thread>
#include <atomic>
#include <mutex>
#include <regex>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
	// ---------------------------------------------------------------- 관리 코드 진입점
	using Fn_Initialize = int(__stdcall*)(void*);
	using Fn_Void = void(__stdcall*)();
	using Fn_FreeString = void(__stdcall*)(void*);
	using Fn_LoadGameAssembly = int(__stdcall*)(const char*);
	using Fn_GetJson = void* (__stdcall*)();
	using Fn_CreateInstance = void* (__stdcall*)(const char*, uint64_t, void*, const char*, int);
	using Fn_Handle = void(__stdcall*)(void*);
	using Fn_Invoke = void(__stdcall*)(void*, int);
	using Fn_InvokeCollision = void(__stdcall*)(void*, int, int, uint64_t);
	using Fn_GetFields = void* (__stdcall*)(void*);
	using Fn_SetFields = void(__stdcall*)(void*, const char*);

	struct Managed
	{
		Fn_Initialize Initialize = nullptr;
		Fn_Void BeginFrame = nullptr;
		Fn_FreeString FreeString = nullptr;
		Fn_LoadGameAssembly LoadGameAssembly = nullptr;
		Fn_Void UnloadGameAssembly = nullptr;
		Fn_GetJson GetClassesJson = nullptr;
		Fn_CreateInstance CreateInstance = nullptr;
		Fn_Handle DestroyInstance = nullptr;
		Fn_Invoke Invoke = nullptr;
		Fn_Invoke SetEnabled = nullptr;
		Fn_InvokeCollision InvokeCollision = nullptr;
		Fn_GetFields GetFieldsJson = nullptr;
		Fn_SetFields SetFieldsJson = nullptr;
		Fn_GetJson GetApiJson = nullptr;   // 선택 (NOVA Code 자동 완성)
		int(__stdcall* InvokeMethod)(uint64_t, const char*, const char*, const char*) = nullptr;   // UI Button
		void* (__stdcall* ExecAssembly)(const char*) = nullptr;   // nova exec (선택)
		void(__stdcall* InvokeUIEvent)(uint64_t, int, float, const char*) = nullptr;
		void(__stdcall* AppPause)(int) = nullptr;   // 선택 (NovaEngine.Interop.AppEvents)
		void(__stdcall* AppFocus)(int) = nullptr;
		void(__stdcall* SceneEvent)(int, int, int) = nullptr;   // 선택 (AppEvents.SceneEvent)
	} m;

	ScriptEngine::State s_State = ScriptEngine::State::NotStarted;
	std::string s_StatusText;
	std::vector<ScriptEngine::ClassInfo> s_Classes;
	bool s_HasErrors = false;
	bool s_AssemblyLoaded = false;
	bool s_PendingReload = false;
	std::string s_LastHash;         // 마지막으로 빌드를 시작한 소스 지문
	double s_NextPoll = 0.0;
	std::wstring s_DotnetDir;       // dotnet.exe 가 있는 폴더

	// Play 시간 / 입력
	float s_PlayTime = 0.0f;
	int s_Frame = 0;
	uint8_t s_Keys[256] = {}, s_PrevKeys[256] = {};

	// 컴파일 작업 (백그라운드 스레드)
	struct CompileJob
	{
		std::atomic<bool> Done{ false };
		DWORD ExitCode = 1;
		std::string Output;
		std::string Hash;
		double StartTime = 0.0;
	};
	std::shared_ptr<CompileJob> s_Job;

	double Now()
	{
		static LARGE_INTEGER freq = [] { LARGE_INTEGER f; ::QueryPerformanceFrequency(&f); return f; }();
		LARGE_INTEGER t;
		::QueryPerformanceCounter(&t);
		return (double)t.QuadPart / (double)freq.QuadPart;
	}

	std::wstring ProjectRoot() { return PathManager::GetI()->GetMovePathW(L""); }
	std::wstring AssetsDir() { return PathManager::GetI()->GetMovePathW(L"Assets\\"); }
	std::wstring EngineScriptingDir() { return PathManager::GetI()->GetEnginePathW() + L"Binaries\\Scripting\\"; }
	std::wstring CsprojPath() { return ProjectRoot() + L"Assembly-CSharp.csproj"; }
	std::wstring OutputDll() { return ProjectRoot() + L"Library\\ScriptAssemblies\\Assembly-CSharp.dll"; }
	std::wstring HashFile() { return ProjectRoot() + L"Library\\ScriptAssemblies\\sources.hash"; }

	std::string ToUtf8(const std::wstring& w) { return wstring_to_string(w); }

	// ---------------------------------------------------------------- .NET 런타임 찾기
	std::vector<int> ParseVersion(const std::wstring& s)
	{
		std::vector<int> v;
		std::wstringstream ss(s);
		std::wstring part;
		while (std::getline(ss, part, L'.'))
			v.push_back(_wtoi(part.c_str()));
		return v;
	}

	std::wstring FindHostfxr()
	{
		std::vector<std::wstring> roots;
		wchar_t buf[MAX_PATH] = {};
		if (::GetEnvironmentVariableW(L"DOTNET_ROOT", buf, MAX_PATH) > 0)
			roots.push_back(buf);
		if (::GetEnvironmentVariableW(L"ProgramFiles", buf, MAX_PATH) > 0)
			roots.push_back(std::wstring(buf) + L"\\dotnet");
		for (const std::wstring& root : roots)
		{
			std::error_code ec;
			const fs::path fxr = fs::path(root) / L"host" / L"fxr";
			if (!fs::exists(fxr, ec))
				continue;
			std::wstring best;
			std::vector<int> bestV;
			for (const auto& e : fs::directory_iterator(fxr, ec))
			{
				const std::vector<int> v = ParseVersion(e.path().filename().wstring());
				if (fs::exists(e.path() / L"hostfxr.dll", ec) && (best.empty() || v > bestV))
				{
					best = (e.path() / L"hostfxr.dll").wstring();
					bestV = v;
				}
			}
			if (!best.empty())
			{
				s_DotnetDir = root;
				return best;
			}
		}
		return std::wstring();
	}

	bool InitRuntime()
	{
		const std::wstring fxrPath = FindHostfxr();
		if (fxrPath.empty())
		{
			EditorLog::Write("Script", ".NET runtime (hostfxr) not found");
			return false;
		}
		HMODULE lib = ::LoadLibraryW(fxrPath.c_str());
		if (lib == nullptr)
			return false;
		auto init = (hostfxr_initialize_for_runtime_config_fn)::GetProcAddress(lib, "hostfxr_initialize_for_runtime_config");
		auto getDelegate = (hostfxr_get_runtime_delegate_fn)::GetProcAddress(lib, "hostfxr_get_runtime_delegate");
		auto close = (hostfxr_close_fn)::GetProcAddress(lib, "hostfxr_close");
		if (!init || !getDelegate || !close)
			return false;

		const std::wstring dir = EngineScriptingDir();
		const std::wstring config = dir + L"NovaScriptCore.runtimeconfig.json";
		const std::wstring dll = dir + L"NovaScriptCore.dll";
		std::error_code ec;
		if (!fs::exists(dll, ec) || !fs::exists(config, ec))
		{
			EditorLog::Write("Script", "NovaScriptCore.dll not built (%s)", ToUtf8(dll).c_str());
			return false;
		}

		hostfxr_handle ctx = nullptr;
		int rc = init(config.c_str(), nullptr, &ctx);
		if (rc < 0 || ctx == nullptr)
		{
			EditorLog::Write("Script", "hostfxr_initialize_for_runtime_config failed 0x%08X (.NET 8+ runtime needed)", (unsigned)rc);
			if (ctx) close(ctx);
			return false;
		}
		load_assembly_and_get_function_pointer_fn load = nullptr;
		rc = getDelegate(ctx, hdt_load_assembly_and_get_function_pointer, (void**)&load);
		close(ctx);
		if (rc < 0 || load == nullptr)
			return false;

		const wchar_t* type = L"NovaEngine.Interop.Bridge, NovaScriptCore";
		bool ok = true;
		auto get = [&](const wchar_t* name, void** fn) {
			const int r = load(dll.c_str(), type, name, UNMANAGEDCALLERSONLY_METHOD, nullptr, fn);
			if (r < 0 || *fn == nullptr)
			{
				EditorLog::Write("Script", "managed entry point %s not found (0x%08X)", ToUtf8(name).c_str(), (unsigned)r);
				ok = false;
			}
		};
		get(L"Initialize", (void**)&m.Initialize);
		get(L"BeginFrame", (void**)&m.BeginFrame);
		get(L"FreeString", (void**)&m.FreeString);
		get(L"LoadGameAssembly", (void**)&m.LoadGameAssembly);
		get(L"UnloadGameAssembly", (void**)&m.UnloadGameAssembly);
		get(L"GetClassesJson", (void**)&m.GetClassesJson);
		get(L"CreateInstance", (void**)&m.CreateInstance);
		get(L"DestroyInstance", (void**)&m.DestroyInstance);
		get(L"Invoke", (void**)&m.Invoke);
		get(L"SetEnabled", (void**)&m.SetEnabled);
		get(L"InvokeCollision", (void**)&m.InvokeCollision);
		get(L"GetFieldsJson", (void**)&m.GetFieldsJson);
		get(L"SetFieldsJson", (void**)&m.SetFieldsJson);
		if (!ok)
			return false;
		// 없어도 스크립팅은 동작한다 (자동 완성만 키워드/문서 단어로)
		if (load(dll.c_str(), type, L"GetApiJson", UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&m.GetApiJson) < 0)
			m.GetApiJson = nullptr;
		if (load(dll.c_str(), type, L"InvokeMethod", UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&m.InvokeMethod) < 0)
			m.InvokeMethod = nullptr;
		if (load(dll.c_str(), type, L"InvokeUIEvent", UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&m.InvokeUIEvent) < 0)
			m.InvokeUIEvent = nullptr;
		if (load(dll.c_str(), type, L"ExecAssembly", UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&m.ExecAssembly) < 0)
			m.ExecAssembly = nullptr;
		const wchar_t* appEvents = L"NovaEngine.Interop.AppEvents, NovaScriptCore";
		if (load(dll.c_str(), appEvents, L"Pause", UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&m.AppPause) < 0)
			m.AppPause = nullptr;
		if (load(dll.c_str(), appEvents, L"Focus", UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&m.AppFocus) < 0)
			m.AppFocus = nullptr;
		if (load(dll.c_str(), appEvents, L"SceneEvent", UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&m.SceneEvent) < 0)
			m.SceneEvent = nullptr;

		std::vector<uint8_t> table(ScriptBindings::TableSize());
		ScriptBindings::Fill(table.data());
		const int init2 = m.Initialize(table.data());
		if (init2 != 1)
		{
			EditorLog::Write("Script", "NativeApiTable size mismatch (managed %d, native %d) - rebuild NovaScriptCore", -init2, ScriptBindings::TableSize());
			return false;
		}
		EditorLog::Write("Script", ".NET runtime ready (%s)", ToUtf8(fxrPath).c_str());
		return true;
	}

	std::string TakeString(void* p)
	{
		if (p == nullptr)
			return std::string();
		std::string s(static_cast<const char*>(p));
		m.FreeString(p);
		return s;
	}

	// ---------------------------------------------------------------- 프로젝트 / 소스
	// 폴더만 받는다 (작업 스레드에서도 — PathManager · PackageManager 는 메인에서 읽어 넘긴다)
	std::vector<fs::path> ScanScriptFiles(const fs::path& root, const std::vector<std::wstring>& packageFolders)
	{
		std::vector<fs::path> out;
		std::error_code ec;
		if (!fs::exists(root, ec))
			return out;
		for (const auto& e : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec))
			if (e.is_regular_file(ec) && _wcsicmp(e.path().extension().c_str(), L".cs") == 0)
				out.push_back(e.path());
		// 프로젝트에 넣은 패키지의 C# (Runtime) — 같은 Assembly-CSharp 로 컴파일
		for (const std::wstring& dir : packageFolders)
			for (const auto& e : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
				if (e.is_regular_file(ec) && _wcsicmp(e.path().extension().c_str(), L".cs") == 0)
					out.push_back(e.path());
		std::sort(out.begin(), out.end());
		return out;
	}

	std::vector<fs::path> ScriptFiles() { return ScanScriptFiles(AssetsDir(), PackageManager::ScriptFolders()); }

	std::string HashOfSources(const std::vector<fs::path>& files, const std::wstring& coreDll)
	{
		std::ostringstream ss;
		std::error_code ec;
		ss << "v1|";
		for (const fs::path& p : files)
			ss << p.string() << '|' << fs::last_write_time(p, ec).time_since_epoch().count() << '|' << fs::file_size(p, ec) << ';';
		ss << fs::last_write_time(coreDll, ec).time_since_epoch().count();
		return std::to_string(std::hash<std::string>{}(ss.str()));
	}

	std::string SourcesHash() { return HashOfSources(ScriptFiles(), EngineScriptingDir() + L"NovaScriptCore.dll"); }

	// 1 초마다 스크립트 변경 확인 — 폴더 훑기는 작업 스레드에서. 큰 프로젝트 (에셋 파일 수천 개 — 열린 월드 식생 팩) 에서는
	//  메인이 매초 25 ~ 35 ms 멈췄다 (Scripts.Update). 결과 (지문 · 스크립트 유무) 는 다음 프레임들에 메인이 받는다
	struct ScriptPoll
	{
		std::mutex Lock;
		bool Running = false, Ready = false, Empty = false;
		std::string Hash;
	};
	std::shared_ptr<ScriptPoll> s_Poll = std::make_shared<ScriptPoll>();

	std::string ReadFile(const std::wstring& path)
	{
		std::ifstream in(path, std::ios::binary);
		return in ? std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()) : std::string();
	}

	void WriteFileIfChanged(const std::wstring& path, const std::string& content)
	{
		if (ReadFile(path) == content)
			return;
		std::error_code ec;
		fs::create_directories(fs::path(path).parent_path(), ec);
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << content;
	}

	void WriteCsproj()
	{
		// Unity 처럼 프로젝트 루트에 Assembly-CSharp.csproj (VS Code / Visual Studio 가 그대로 연다)
		const std::string coreDll = ToUtf8(EngineScriptingDir() + L"NovaScriptCore.dll");
		std::ostringstream x;
		x << "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
			<< "  <!-- NOVA 에디터가 만드는 파일입니다. 직접 고치면 덮어씁니다. -->\n"
			<< "  <PropertyGroup>\n"
			<< "    <TargetFramework>net8.0</TargetFramework>\n"
			<< "    <AssemblyName>Assembly-CSharp</AssemblyName>\n"
			<< "    <RootNamespace></RootNamespace>\n"
			<< "    <Nullable>disable</Nullable>\n"
			<< "    <ImplicitUsings>disable</ImplicitUsings>\n"
			<< "    <LangVersion>latest</LangVersion>\n"
			<< "    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n"
			<< "    <EnableDefaultItems>false</EnableDefaultItems>\n"
			<< "    <GenerateAssemblyInfo>false</GenerateAssemblyInfo>\n"
			<< "    <OutputPath>Library\\ScriptAssemblies\\</OutputPath>\n"
			<< "    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>\n"
			<< "    <DebugType>portable</DebugType>\n"
			<< "    <!-- Unity 와 같이 직렬화 필드의 '값을 할당하지 않음' 경고는 끈다 -->\n"
			<< "    <NoWarn>$(NoWarn);CS0649;CS0169;CS0414;CS8618</NoWarn>\n"
			<< "  </PropertyGroup>\n"
			<< "  <ItemGroup>\n"
			<< "    <Compile Include=\"Assets\\**\\*.cs\" />\n";
		for (const std::wstring& dir : PackageManager::ScriptFolders())
		{
			// 패키지 C#: 솔루션 탐색기에서 Packages/<이름>/... 로 보이게
			const std::string name = ToUtf8(fs::path(dir).parent_path().filename().wstring());
			x << "    <Compile Include=\"" << ToUtf8(dir) << "\\**\\*.cs\" Link=\"Packages\\" << name << "\\%(RecursiveDir)%(Filename)%(Extension)\" />\n";
		}
		x << "  </ItemGroup>\n"
			<< "  <ItemGroup>\n"
			<< "    <Reference Include=\"NovaScriptCore\">\n"
			<< "      <HintPath>" << coreDll << "</HintPath>\n"
			<< "      <Private>false</Private>\n"
			<< "    </Reference>\n"
			<< "  </ItemGroup>\n"
			<< "</Project>\n";
		WriteFileIfChanged(CsprojPath(), x.str());
	}

	// ---------------------------------------------------------------- 외부 프로세스
	std::string DecodeOutput(const std::string& raw)
	{
		// UTF-8 이 아니면 콘솔 코드 페이지(예: CP949)로 보고 UTF-8 로 바꾼다
		if (raw.empty() || ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), (int)raw.size(), nullptr, 0) > 0)
			return raw;
		const int n = ::MultiByteToWideChar(CP_OEMCP, 0, raw.data(), (int)raw.size(), nullptr, 0);
		std::wstring w(n, L'\0');
		::MultiByteToWideChar(CP_OEMCP, 0, raw.data(), (int)raw.size(), w.data(), n);
		return ToUtf8(w);
	}

	DWORD RunProcess(const std::wstring& commandLine, const std::wstring& workDir, std::string& output)
	{
		SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
		HANDLE readPipe = nullptr, writePipe = nullptr;
		if (!::CreatePipe(&readPipe, &writePipe, &sa, 0))
			return (DWORD)-1;
		::SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
		STARTUPINFOW si = {};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdOutput = writePipe;
		si.hStdError = writePipe;
		si.hStdInput = nullptr;
		PROCESS_INFORMATION pi = {};
		std::wstring cmd = commandLine;
		const BOOL ok = ::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, workDir.c_str(), &si, &pi);
		::CloseHandle(writePipe);
		if (!ok)
		{
			::CloseHandle(readPipe);
			return (DWORD)-1;
		}
		std::string raw;
		char buf[4096];
		DWORD read = 0;
		while (::ReadFile(readPipe, buf, sizeof(buf), &read, nullptr) && read > 0)
			raw.append(buf, read);
		::WaitForSingleObject(pi.hProcess, INFINITE);
		DWORD code = 1;
		::GetExitCodeProcess(pi.hProcess, &code);
		::CloseHandle(pi.hProcess);
		::CloseHandle(pi.hThread);
		::CloseHandle(readPipe);
		output = DecodeOutput(raw);
		return code;
	}

	void StartCompile(const std::string& hash)
	{
		if (s_DotnetDir.empty())
			return;
		WriteCsproj();
		auto job = std::make_shared<CompileJob>();
		job->Hash = hash;
		job->StartTime = Now();
		s_Job = job;
		s_State = ScriptEngine::State::Compiling;
		s_StatusText = "Compiling scripts...";
		EditorLog::Write("Script", "compile start (%d files)", (int)ScriptFiles().size());

		const std::wstring dotnet = s_DotnetDir + L"\\dotnet.exe";
		std::error_code ec;
		const bool restored = fs::exists(ProjectRoot() + L"obj\\project.assets.json", ec);
		std::wstring cmd = L"\"" + dotnet + L"\" build \"" + CsprojPath() + L"\" -c Debug --nologo -v q -clp:NoSummary -nodeReuse:false";
		if (restored)
			cmd += L" --no-restore";
		const std::wstring dir = ProjectRoot();
		::SetEnvironmentVariableW(L"DOTNET_CLI_TELEMETRY_OPTOUT", L"1");
		::SetEnvironmentVariableW(L"DOTNET_NOLOGO", L"1");
		::SetEnvironmentVariableW(L"DOTNET_SKIP_FIRST_TIME_EXPERIENCE", L"1");
		::SetEnvironmentVariableW(L"DOTNET_CLI_FORCE_UTF8_ENCODING", L"1");
		std::thread([job, cmd, dir]() {
			job->ExitCode = RunProcess(cmd, dir, job->Output);
			job->Done = true;
		}).detach();
	}

	// "C:\...\Player.cs(12,5): error CS1002: ; 필요합니다 [C:\...\Assembly-CSharp.csproj]"
	void ReportCompileOutput(const std::string& output, int& errors, int& warnings)
	{
		static const std::regex re(R"(^\s*(.+?)\((\d+),(\d+)\):\s*(error|warning)\s+([A-Za-z]+\d+)\s*:\s*(.*?)(\s*\[[^\]]*\])?\s*$)");
		std::set<std::string> seen;
		std::istringstream in(output);
		std::string line;
		errors = warnings = 0;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			std::smatch mm;
			if (std::regex_match(line, mm, re))
			{
				const std::string key = mm[1].str() + mm[2].str() + mm[5].str() + mm[6].str();
				if (!seen.insert(key).second)
					continue;
				std::string file = mm[1].str();
				std::string rel = file;
				const size_t a = rel.find("Assets\\");
				if (a != std::string::npos)
					rel = rel.substr(a);
				std::replace(rel.begin(), rel.end(), '\\', '/');
				const bool isError = mm[4].str() == "error";
				// Unity 형식: Assets/Scripts/Player.cs(12,5): error CS1002: ; expected
				const std::string msg = rel + "(" + mm[2].str() + "," + mm[3].str() + "): " + mm[4].str() + " " + mm[5].str() + ": " + mm[6].str();
				Debug::Write(isError ? LogType::Error : LogType::Warning, msg, std::string(), file, std::stoi(mm[2].str()), true);
				(isError ? errors : warnings)++;
			}
			else if (line.find("error") != std::string::npos && line.find(": error") != std::string::npos)
			{
				Debug::Write(LogType::Error, line, std::string(), std::string(), 0, true);
				++errors;
			}
		}
	}

	// ---------------------------------------------------------------- 어셈블리 읽기
	void ParseClasses(const std::string& text)
	{
		s_Classes.clear();
		json j = json::parse(text, nullptr, false);
		if (j.is_discarded() || !j.contains("classes"))
			return;
		for (const json& c : j["classes"])
		{
			ScriptEngine::ClassInfo ci;
			ci.Name = c.value("name", std::string());
			ci.FullName = c.value("fullName", std::string());
			for (const json& f : c["fields"])
			{
				ScriptEngine::FieldInfo fi;
				fi.Name = f.value("name", std::string());
				fi.Label = f.value("label", fi.Name);
				fi.Type = f.value("type", std::string("unsupported"));
				fi.TypeName = f.value("typeName", std::string());
				fi.Default = f.contains("value") ? f["value"] : json();
				fi.Header = f.value("header", std::string());
				fi.Tooltip = f.value("tooltip", std::string());
				fi.Space = f.value("space", 0.0f);
				if (f.contains("range") && f["range"].is_array() && f["range"].size() == 2)
				{
					fi.HasRange = true;
					fi.RangeMin = f["range"][0].get<float>();
					fi.RangeMax = f["range"][1].get<float>();
				}
				if (f.contains("options"))
					for (const json& o : f["options"]) fi.Options.push_back(o.get<std::string>());
				if (f.contains("optionValues"))
					for (const json& o : f["optionValues"]) fi.OptionValues.push_back(o.get<long long>());
				ci.Fields.push_back(std::move(fi));
			}
			if (c.contains("methods") && c["methods"].is_array())
				for (const json& mt : c["methods"])
					ci.Methods.push_back({ mt.value("name", std::string()), mt.value("param", std::string()) });
			s_Classes.push_back(std::move(ci));
		}
	}

	void LoadAssembly()
	{
		s_PendingReload = false;
		std::error_code ec;
		if (!fs::exists(OutputDll(), ec))
		{
			s_Classes.clear();
			return;
		}
		const double t0 = Now();
		const int r = m.LoadGameAssembly(ToUtf8(OutputDll()).c_str());
		s_AssemblyLoaded = r > 0;
		ParseClasses(TakeString(m.GetClassesJson()));
		EditorLog::Write("Script", "Assembly-CSharp loaded: %d MonoBehaviour classes (%.0f ms)", (int)s_Classes.size(), (Now() - t0) * 1000.0);
	}

	void FinishCompile()
	{
		auto job = s_Job;
		s_Job.reset();
		s_State = ScriptEngine::State::Idle;
		s_StatusText.clear();
		Debug::ClearCompileMessages();
		int errors = 0, warnings = 0;
		ReportCompileOutput(job->Output, errors, warnings);
		const double secs = Now() - job->StartTime;
		if (job->ExitCode != 0 && errors == 0)
		{
			// 파싱되지 않은 실패 (dotnet 없음, 복원 실패 등): 출력 그대로
			std::string out = job->Output.size() > 2000 ? job->Output.substr(0, 2000) + "..." : job->Output;
			Debug::Write(LogType::Error, "Script compilation failed:\n" + out, std::string(), std::string(), 0, true);
			errors = 1;
		}
		s_HasErrors = errors > 0;
		EditorLog::Write("Script", "compile %s in %.1fs (%d errors, %d warnings)", s_HasErrors ? "FAILED" : "ok", secs, errors, warnings);
		if (s_HasErrors)
			return;
		WriteFileIfChanged(HashFile(), job->Hash);
		if (Application::IsPlaying())
			s_PendingReload = true;   // Play 가 끝난 뒤 다시 읽는다
		else
			LoadAssembly();
	}
}

namespace ScriptEngine
{
	void Init()
	{
		if (s_State != State::NotStarted)
			return;
		if (!InitRuntime())
		{
			s_State = State::Unavailable;
			Debug::Write(LogType::Warning, "C# scripting is disabled: .NET 8 (or newer) runtime/SDK was not found, or NovaScriptCore.dll is missing. Install the .NET SDK from https://dotnet.microsoft.com and rebuild the engine.");
			return;
		}
		s_State = State::Idle;
		if (Application::IsPlayer())
		{
			// 빌드된 게임: 빌드 때 넣은 Assembly-CSharp.dll 을 읽기만 한다 (소스/컴파일 없음)
			LoadAssembly();
			return;
		}
		const std::vector<fs::path> files = ScriptFiles();
		if (!files.empty())
			WriteCsproj();
		const std::string hash = SourcesHash();
		std::error_code ec;
		if (!files.empty() && fs::exists(OutputDll(), ec) && ReadFile(HashFile()) == hash)
		{
			s_LastHash = hash;
			LoadAssembly();   // 바뀐 것이 없으면 바로 읽기
		}
		else if (!files.empty())
		{
			s_LastHash = hash;
			StartCompile(hash);
		}
		else
			s_LastHash = hash;
		s_NextPoll = Now() + 1.0;
	}

	void Shutdown()
	{
		if (s_AssemblyLoaded && m.UnloadGameAssembly)
			m.UnloadGameAssembly();
		s_AssemblyLoaded = false;
	}

	void Update()
	{
		if (s_State == State::NotStarted || s_State == State::Unavailable)
			return;
		ScriptBindings::Update();

		if (s_Job && s_Job->Done)
			FinishCompile();

		// 1초마다 스크립트 변경 확인 (Unity 는 에디터 포커스 때 확인). 빌드된 게임은 하지 않는다. 훑기는 작업 스레드 (ScriptPoll)
		const double now = Now();
		if (now >= s_NextPoll && !s_Job && !Application::IsPlayer())
		{
			std::lock_guard<std::mutex> lock(s_Poll->Lock);
			if (!s_Poll->Running)
			{
				s_NextPoll = now + 1.0;
				s_Poll->Running = true;
				std::thread([poll = s_Poll, root = fs::path(AssetsDir()), packages = PackageManager::ScriptFolders(), core = EngineScriptingDir() + L"NovaScriptCore.dll"]() {
					const std::vector<fs::path> files = ScanScriptFiles(root, packages);
					const std::string hash = HashOfSources(files, core);
					std::lock_guard<std::mutex> lock(poll->Lock);
					poll->Hash = hash;
					poll->Empty = files.empty();
					poll->Ready = true;
					poll->Running = false;
				}).detach();
			}
		}
		std::string polled;
		bool polledEmpty = false;
		{
			std::lock_guard<std::mutex> lock(s_Poll->Lock);
			if (s_Poll->Ready)
			{
				s_Poll->Ready = false;
				polled = s_Poll->Hash;
				polledEmpty = s_Poll->Empty;
			}
		}
		if (!polled.empty() && !s_Job)
		{
			const std::string hash = polled;
			if (hash != s_LastHash)
			{
				s_LastHash = hash;
				if (polledEmpty)
				{
					// 스크립트를 모두 지웠다: 빈 목록
					Debug::ClearCompileMessages();
					s_HasErrors = false;
					if (s_AssemblyLoaded) { m.UnloadGameAssembly(); s_AssemblyLoaded = false; }
					s_Classes.clear();
				}
				else
					StartCompile(hash);
			}
		}
		if (s_Job)
		{
			static const char* kDots[] = { "", ".", "..", "..." };
			s_StatusText = std::string("Compiling scripts") + kDots[(int)(now * 3.0) % 4];
		}
	}

	void BeginFrame()
	{
		if (!IsAvailable())
			return;
		memcpy(s_PrevKeys, s_Keys, sizeof(s_Keys));
		const bool focus = GameViewEditorWindow::HasInputFocus();
		for (int vk = 1; vk < 256; ++vk)
			s_Keys[vk] = focus && (::GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
		if (!Application::IsPaused())
			s_PlayTime += (float)DT;
		++s_Frame;
		ScriptBindings::BeginFrame();
		m.BeginFrame();
	}

	void OnSceneSwapped()
	{
		// Play 중 SceneManager.LoadScene: 같은 씬을 다시 열면 fileID 가 같아서, 지운 오브젝트를 가리키는 id → 포인터 캐시와
		// 이전 씬 스크립트의 UI 리스너가 새 오브젝트에 그대로 붙는다
		ScriptBindings::Reset();
		if (s_AssemblyLoaded && m.InvokeUIEvent)
			m.InvokeUIEvent(0, -1, 0.0f, "");
	}

	void OnApplicationPause(bool paused)
	{
		if (s_AssemblyLoaded && m.AppPause)
			m.AppPause(paused ? 1 : 0);
	}

	void OnApplicationFocus(bool focused)
	{
		if (s_AssemblyLoaded && m.AppFocus)
			m.AppFocus(focused ? 1 : 0);
	}

	void OnSceneEvent(int kind, int a, int b)
	{
		if (s_AssemblyLoaded && m.SceneEvent)
			m.SceneEvent(kind, a, b);
	}

	void OnPlayModeChanged(bool playing)
	{
		s_PlayTime = 0.0f;
		s_Frame = 0;
		memset(s_Keys, 0, sizeof(s_Keys));
		memset(s_PrevKeys, 0, sizeof(s_PrevKeys));
		ScriptBindings::Reset();
		if (s_AssemblyLoaded && m.InvokeUIEvent)
			m.InvokeUIEvent(0, -1, 0.0f, "");   // 이전 Play 의 UI 리스너(onClick, onValueChanged ...) 정리
		if (!playing && s_PendingReload && IsAvailable())
			LoadAssembly();
	}

	State GetState() { return s_State; }
	bool IsAvailable() { return s_State == State::Idle || s_State == State::Compiling; }
	bool IsCompiling() { return s_State == State::Compiling; }
	bool HasCompileErrors() { return s_HasErrors; }
	const std::string& StatusText() { return s_StatusText; }

	bool CanEnterPlayMode()
	{
		// Unity 의 Console "Clear on Play" (컴파일 오류는 남는다)
		if (Debug::ClearOnPlay)
			Debug::ClearAll();
		s_PlayTime = 0.0f;
		s_Frame = 0;
		memset(s_Keys, 0, sizeof(s_Keys));
		memset(s_PrevKeys, 0, sizeof(s_PrevKeys));
		ScriptBindings::Reset();
		if (!IsAvailable())
			return true;   // 스크립팅 없음: 스크립트 없이 Play
		// 방금 저장한 파일도 반영되도록 한 번 확인
		const std::string hash = SourcesHash();
		if (hash != s_LastHash && !s_Job)
		{
			s_LastHash = hash;
			if (!ScriptFiles().empty())
				StartCompile(hash);
		}
		const double t0 = Now();
		while (s_Job && !s_Job->Done && Now() - t0 < 120.0)
			::Sleep(20);
		if (s_Job && s_Job->Done)
			FinishCompile();
		if (s_PendingReload)
			LoadAssembly();
		if (s_HasErrors)
		{
			Debug::Write(LogType::Error, "All compiler errors have to be fixed before you can enter playmode!");
			return false;
		}
		return true;
	}

	void RequestRecompile()
	{
		s_LastHash.clear();
		s_NextPoll = 0.0;
	}

	const std::vector<ClassInfo>& Classes() { return s_Classes; }

	const ClassInfo* FindClass(const std::string& name)
	{
		for (const ClassInfo& c : s_Classes)
			if (c.FullName == name)
				return &c;
		for (const ClassInfo& c : s_Classes)
			if (c.Name == name)
				return &c;
		return nullptr;
	}

	std::wstring FindScriptFile(const std::string& className)
	{
		std::string shortName = className;
		const size_t dot = shortName.rfind('.');
		if (dot != std::string::npos)
			shortName = shortName.substr(dot + 1);
		for (const fs::path& p : ScriptFiles())
			if (p.stem().string() == shortName)
				return p.wstring();
		return std::wstring();
	}

	static std::string SanitizeClassName(std::string name)
	{
		std::string out;
		for (char c : name)
			if (isalnum((unsigned char)c) || c == '_')
				out += c;
		if (out.empty() || isdigit((unsigned char)out[0]))
			out = "_" + out;
		return out;
	}

	std::wstring CreateScriptAsset(const std::wstring& directory, const std::string& className)
	{
		fs::path dir(directory);
		fs::path file = dir / (string_to_wstring(className) + L".cs");
		for (int i = 1; fs::exists(file); ++i)
			file = dir / (string_to_wstring(className) + std::to_wstring(i) + L".cs");
		const std::string cls = SanitizeClassName(file.stem().string());
		std::ostringstream s;
		s << "using NovaEngine;\n"
			<< "\n"
			<< "public class " << cls << " : MonoBehaviour\n"
			<< "{\n"
			<< "    // Start is called once before the first execution of Update after the MonoBehaviour is created\n"
			<< "    void Start()\n"
			<< "    {\n"
			<< "        \n"
			<< "    }\n"
			<< "\n"
			<< "    // Update is called once per frame\n"
			<< "    void Update()\n"
			<< "    {\n"
			<< "        \n"
			<< "    }\n"
			<< "}\n";
		std::ofstream out(file, std::ios::binary);
		out << s.str();
		out.close();
		EditorLog::Write("Script", "created script %s", ToUtf8(file.wstring()).c_str());
		s_NextPoll = 0.0;
		return file.wstring();
	}

	void RenameScriptClass(const std::wstring& newPath, const std::string& oldClassName)
	{
		std::string text = ReadFile(newPath);
		const std::string newName = SanitizeClassName(fs::path(newPath).stem().string());
		const std::string from = "class " + oldClassName;
		const size_t at = text.find(from);
		if (at == std::string::npos || newName == oldClassName)
			return;
		// 뒤가 식별자 문자가 아닐 때만 (다른 클래스 이름의 일부가 아니도록)
		const size_t end = at + from.size();
		if (end < text.size() && (isalnum((unsigned char)text[end]) || text[end] == '_'))
			return;
		text.replace(at, from.size(), "class " + newName);
		std::ofstream out(newPath, std::ios::binary | std::ios::trunc);
		out << text;
		EditorLog::Write("Script", "renamed class %s -> %s", oldClassName.c_str(), newName.c_str());
	}

	void OpenInCodeEditor(const std::wstring& file, int line)
	{
		// Preferences > External Tools > External Script Editor 에서 고른 편집기 (기본: NOVA Code)
		ExternalScriptEditor::Open(file, line);
	}

	void RegenerateProjectFiles()
	{
		WriteCsproj();
		EditorLog::Write("Script", "project files regenerated (%s)", ToUtf8(CsprojPath()).c_str());
	}

	std::string GetApiJson()
	{
		return IsAvailable() && m.GetApiJson ? TakeString(m.GetApiJson()) : std::string();
	}

	std::vector<std::wstring> ScriptFilePaths()
	{
		std::vector<std::wstring> out;
		for (const fs::path& p : ScriptFiles())
			out.push_back(p.wstring());
		return out;
	}

	// ---------------------------------------------------------------- 관리 코드 호출
	void* CreateInstance(const std::string& className, uint64_t gameObjectId, void* nativeComponent, const std::string& fieldsJson, bool enabled)
	{
		if (!s_AssemblyLoaded || m.CreateInstance == nullptr)
			return nullptr;
		return m.CreateInstance(className.c_str(), gameObjectId, nativeComponent, fieldsJson.c_str(), enabled ? 1 : 0);
	}

	void DestroyInstance(void* handle)
	{
		if (handle && m.DestroyInstance && s_AssemblyLoaded)
			m.DestroyInstance(handle);
	}

	void Invoke(void* handle, Message message)
	{
		if (handle && m.Invoke)
			m.Invoke(handle, (int)message);
	}

	void SetInstanceEnabled(void* handle, bool enabled)
	{
		if (handle && m.SetEnabled)
			m.SetEnabled(handle, enabled ? 1 : 0);
	}

	void InvokeCollision(void* handle, int kind, int phase, uint64_t other)
	{
		if (handle && m.InvokeCollision)
			m.InvokeCollision(handle, kind, phase, other);
	}

	void InvokeCollision(void* handle, bool trigger, int phase, uint64_t other)
	{
		if (handle && m.InvokeCollision)
			m.InvokeCollision(handle, trigger ? 1 : 0, phase, other);
	}

	std::string GetFieldsJson(void* handle)
	{
		return handle && m.GetFieldsJson ? TakeString(m.GetFieldsJson(handle)) : std::string();
	}

	void SetFieldsJson(void* handle, const std::string& j)
	{
		if (handle && m.SetFieldsJson)
			m.SetFieldsJson(handle, j.c_str());
	}

	bool InvokeMethod(uint64_t gameObjectId, const std::string& className, const std::string& method, const std::string& argument)
	{
		if (!s_AssemblyLoaded || m.InvokeMethod == nullptr)
			return false;
		return m.InvokeMethod(gameObjectId, className.c_str(), method.c_str(), argument.c_str()) != 0;
	}

	bool Exec(const std::string& code, std::string& result, std::string& error)
	{
		if (!IsAvailable() || m.ExecAssembly == nullptr || s_DotnetDir.empty())
		{
			error = "C# scripting is not available (.NET SDK / NovaScriptCore)";
			return false;
		}
		// Library/NovaExec/ 에 작은 프로젝트: 엔진 API + 게임 스크립트(있으면)를 참조하고 NovaExec.Run() 하나
		const std::wstring dir = ProjectRoot() + L"Library\\NovaExec\\";
		std::error_code ec;
		fs::create_directories(dir, ec);
		const std::string coreDll = ToUtf8(EngineScriptingDir() + L"NovaScriptCore.dll");
		std::ostringstream x;
		x << "<Project Sdk=\"Microsoft.NET.Sdk\">\n  <PropertyGroup>\n    <TargetFramework>net8.0</TargetFramework>\n    <AssemblyName>NovaExec</AssemblyName>\n"
		  << "    <Nullable>disable</Nullable>\n    <ImplicitUsings>disable</ImplicitUsings>\n    <LangVersion>latest</LangVersion>\n    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n"
		  << "    <EnableDefaultItems>false</EnableDefaultItems>\n    <GenerateAssemblyInfo>false</GenerateAssemblyInfo>\n    <OutputPath>bin\\</OutputPath>\n"
		  << "    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>\n    <DebugType>none</DebugType>\n"
		  << "    <NoWarn>$(NoWarn);CS0162;CS8321</NoWarn>\n  </PropertyGroup>\n"
		  << "  <ItemGroup>\n    <Compile Include=\"Exec.cs\" />\n  </ItemGroup>\n  <ItemGroup>\n"
		  << "    <Reference Include=\"NovaScriptCore\"><HintPath>" << coreDll << "</HintPath><Private>false</Private></Reference>\n";
		if (fs::exists(OutputDll(), ec))
			x << "    <Reference Include=\"Assembly-CSharp\"><HintPath>" << ToUtf8(OutputDll()) << "</HintPath><Private>false</Private></Reference>\n";
		x << "  </ItemGroup>\n</Project>\n";
		WriteFileIfChanged(dir + L"NovaExec.csproj", x.str());

		auto build = [&](const std::string& body, std::string& output) -> bool {
			std::ofstream cs(dir + L"Exec.cs", std::ios::binary | std::ios::trunc);
			cs << "using System;\nusing System.Linq;\nusing System.Collections.Generic;\nusing NovaEngine;\nusing NovaEngine.UI;\n"
			   << "public static class NovaExec\n{\n    public static object Run()\n    {\n" << body << "\n    }\n}\n";
			cs.close();
			const std::wstring cmd = L"\"" + s_DotnetDir + L"\\dotnet.exe\" build \"" + dir + L"NovaExec.csproj\" -c Debug --nologo -v q -clp:NoSummary -nodeReuse:false";
			return RunProcess(cmd, dir, output) == 0;
		};
		// 식이면 그 값, 아니면(문장) 그대로 + return null. 식으로 안 되면(void 호출 등) 문장으로 한 번 더
		std::string trimmed = code;
		while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\n' || trimmed.back() == '\r')) trimmed.pop_back();
		const bool statements = !trimmed.empty() && (trimmed.back() == ';' || trimmed.back() == '}');
		std::string output;
		const double t0 = Now();
		bool ok = !statements && build("        return (object)(" + trimmed + ");", output);
		if (!ok)
			ok = build("        " + trimmed + (statements ? "" : ";") + "\n        return null;", output);
		if (!ok)
		{
			// 오류 줄만 (경로 앞부분·뒤의 [프로젝트] 빼고, dotnet 이 두 번 찍는 같은 줄은 한 번)
			std::string lines;
			std::set<std::string> seen;
			std::istringstream ss(output);
			for (std::string line; std::getline(ss, line);)
				if (line.find("error CS") != std::string::npos)
				{
					const size_t p = line.find("Exec.cs");
					std::string t = p != std::string::npos ? line.substr(p) : line;
					const size_t proj = t.rfind(" [");
					if (proj != std::string::npos) t.erase(proj);
					while (!t.empty() && (t.back() == '\r' || t.back() == ' ')) t.pop_back();
					if (seen.insert(t).second) lines += t + "\n";
				}
			error = lines.empty() ? output : lines;
			return false;
		}
		const std::string dll = ToUtf8(dir + L"bin\\NovaExec.dll");
		void* p = m.ExecAssembly(dll.c_str());
		std::string r = p ? static_cast<const char*>(p) : "";
		if (p && m.FreeString) m.FreeString(p);
		EditorLog::Write("Script", "exec (%.1f s): %s", Now() - t0, r.substr(0, 200).c_str());
		if (r.rfind("ok:", 0) == 0) { result = r.substr(3); return true; }
		error = r.rfind("error:", 0) == 0 ? r.substr(6) : r;
		return false;
	}

	void InvokeUIEvent(uint64_t gameObjectId, int kind, float number, const std::string& text)
	{
		if (s_AssemblyLoaded && m.InvokeUIEvent)
			m.InvokeUIEvent(gameObjectId, kind, number, text.c_str());
	}

	bool KeyState(int vk, int mode)
	{
		if (vk <= 0 || vk >= 256)
			return false;
		switch (mode)
		{
		case 0: return s_Keys[vk] != 0;
		case 1: return s_Keys[vk] && !s_PrevKeys[vk];
		default: return !s_Keys[vk] && s_PrevKeys[vk];
		}
	}

	bool MouseButtonState(int button, int mode)
	{
		static const int kVk[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
		if (button < 0 || button >= 5)
			return false;
		return KeyState(kVk[button], mode);
	}

	float PlayTime() { return s_PlayTime; }
	int FrameCount() { return s_Frame; }
}
