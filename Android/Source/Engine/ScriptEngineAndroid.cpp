#include "pch.h"
#include "ScriptEngine.h"
#include "ScriptBindings.h"
#include "AndroidEngine.h"
#include <dlfcn.h>
#include <filesystem>

// C# 스크립트 런타임의 안드로이드 판: Windows 는 .NET (hostfxr) 을 띄우지만 기기에는 Microsoft 의 Mono (.NET 8 의 모바일 런타임) 를 쓴다.
//  - libmonosgen-2.0.so 는 APK 의 lib/<ABI> (dlopen — 없으면 스크립트 없이 돈다, Windows 판과 같은 규칙)
//  - 어셈블리: 게임 데이터의 Managed/ (System.Private.CoreLib · BCL · NovaScriptCore · Assembly-CSharp) — nova android export 가 넣는다
//  - 진입점: Windows 와 같은 NovaEngine.Interop.Bridge 의 [UnmanagedCallersOnly] 함수 (mono_method_get_unmanaged_callers_only_ftnptr)
//  - 엔진 API 표 (ScriptBindings::Fill) 도 Windows 와 같다. 패키지의 DllImport ("NovaAnimation" …) 는 PINVOKE_OVERRIDE 로 libnova.so 안의 함수로
//  - 에디터 일 (컴파일 · 프로젝트 파일 · 코드 편집기) 은 없다 (빌드된 게임과 같이 Assembly-CSharp.dll 을 읽기만)
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
	// ---- Mono 임베딩 API (헤더 없이 — 런타임 묶음은 git 에 없고 dlsym 으로 찾는다)
	typedef void* (*PInvokeOverrideFn)(const char* libraryName, const char* entrypointName);
	struct TrustedAssemblies { uint32_t Count; char** Basenames; uint32_t* BasenameLens; char** Paths; };
	struct LookupPaths { uint32_t Count; char** Dirs; };
	struct RuntimeProperties { TrustedAssemblies* Tpa; LookupPaths* AppPaths; LookupPaths* NativeDirs; PInvokeOverrideFn PInvokeOverride; };
	struct MonoErrorStorage { alignas(8) unsigned char Bytes[256]; };   // MonoError (약 100 바이트) 보다 넉넉히

	struct MonoApi
	{
		int (*monovm_initialize_preparsed)(RuntimeProperties*, int, const char**, const char**) = nullptr;
		void* (*mono_jit_init_version)(const char*, const char*) = nullptr;
		void* (*mono_assembly_open)(const char*, int*) = nullptr;
		void* (*mono_assembly_get_image)(void*) = nullptr;
		void* (*mono_class_from_name)(void*, const char*, const char*) = nullptr;
		void* (*mono_class_get_method_from_name)(void*, const char*, int) = nullptr;
		void* (*mono_method_get_unmanaged_callers_only_ftnptr)(void*, void*) = nullptr;
		void (*mono_error_init)(void*) = nullptr;
		const char* (*mono_error_get_message)(void*) = nullptr;
		void (*mono_error_cleanup)(void*) = nullptr;
		void* (*mono_thread_attach)(void*) = nullptr;
		void* (*mono_get_root_domain)() = nullptr;
	} mono;

	// ---- 관리 코드 진입점 (Windows 판 ScriptEngine.cpp 와 같은 모양)
	struct Managed
	{
		int (*Initialize)(void*) = nullptr;
		void (*BeginFrame)() = nullptr;
		void (*FreeString)(void*) = nullptr;
		int (*LoadGameAssembly)(const char*) = nullptr;
		void (*UnloadGameAssembly)() = nullptr;
		void* (*GetClassesJson)() = nullptr;
		void* (*CreateInstance)(const char*, uint64_t, void*, const char*, int) = nullptr;
		void (*DestroyInstance)(void*) = nullptr;
		void (*Invoke)(void*, int) = nullptr;
		void (*SetEnabled)(void*, int) = nullptr;
		void (*InvokeCollision)(void*, int, int, uint64_t) = nullptr;
		void* (*GetFieldsJson)(void*) = nullptr;
		void (*SetFieldsJson)(void*, const char*) = nullptr;
		int (*InvokeMethod)(uint64_t, const char*, const char*, const char*) = nullptr;
		void (*InvokeUIEvent)(uint64_t, int, float, const char*) = nullptr;
	} m;

	ScriptEngine::State s_State = ScriptEngine::State::NotStarted;
	std::string s_StatusText;
	std::vector<ScriptEngine::ClassInfo> s_Classes;
	bool s_AssemblyLoaded = false;
	float s_PlayTime = 0.0f;
	int s_Frame = 0;
	uint8_t s_Keys[256] = {}, s_PrevKeys[256] = {};
	void* s_Self = nullptr;   // libnova.so (패키지 · 엔진의 내보낸 함수)

	fs::path ManagedDir() { return fs::path(wstring_to_string(PathManager::GetI()->GetMovePathW(L""))) / "Managed"; }

	// 패키지 C# 의 DllImport: "NovaAnimation" · "NovaNavigation" … = 안드로이드에서는 libnova.so 에 함께 들어 있다
	void* PInvokeOverride(const char* library, const char* entry)
	{
		if (!library || !entry || !s_Self) return nullptr;
		if (strncmp(library, "Nova", 4) == 0 || strncmp(library, "libNova", 7) == 0 || strcmp(library, "__Internal") == 0 || strncmp(library, "libnova", 7) == 0)
			return dlsym(s_Self, entry);
		return nullptr;   // System.Native 등은 Mono 가 보통대로 (APK 의 lib/<ABI>)
	}

	template <class F> bool Sym(void* lib, F& f, const char* name)
	{
		f = reinterpret_cast<F>(dlsym(lib, name));
		if (!f) EditorLog::Write("Script", "Mono: %s not found", name);
		return f != nullptr;
	}

	std::string TakeString(void* p)
	{
		if (p == nullptr) return std::string();
		std::string s(static_cast<const char*>(p));
		m.FreeString(p);
		return s;
	}

	bool InitRuntime()
	{
		const auto t0 = std::chrono::steady_clock::now();
		std::error_code ec;
		const fs::path managed = ManagedDir();
		if (!fs::exists(managed / "System.Private.CoreLib.dll", ec) || !fs::exists(managed / "NovaScriptCore.dll", ec))
		{
			EditorLog::Write("Script", "no C# runtime in the game data (Managed/) - scripts disabled");
			return false;
		}
		void* lib = dlopen("libmonosgen-2.0.so", RTLD_NOW | RTLD_GLOBAL);
		if (!lib)
		{
			EditorLog::Write("Script", "libmonosgen-2.0.so not in the APK (%s) - scripts disabled", dlerror());
			return false;
		}
		bool ok = Sym(lib, mono.monovm_initialize_preparsed, "monovm_initialize_preparsed") && Sym(lib, mono.mono_jit_init_version, "mono_jit_init_version") &&
			Sym(lib, mono.mono_assembly_open, "mono_assembly_open") && Sym(lib, mono.mono_assembly_get_image, "mono_assembly_get_image") &&
			Sym(lib, mono.mono_class_from_name, "mono_class_from_name") && Sym(lib, mono.mono_class_get_method_from_name, "mono_class_get_method_from_name") &&
			Sym(lib, mono.mono_method_get_unmanaged_callers_only_ftnptr, "mono_method_get_unmanaged_callers_only_ftnptr") &&
			Sym(lib, mono.mono_error_init, "mono_error_init") && Sym(lib, mono.mono_error_get_message, "mono_error_get_message") &&
			Sym(lib, mono.mono_error_cleanup, "mono_error_cleanup") && Sym(lib, mono.mono_thread_attach, "mono_thread_attach") &&
			Sym(lib, mono.mono_get_root_domain, "mono_get_root_domain");
		if (!ok) return false;

		// 이 라이브러리 (libnova.so) — 패키지 DllImport 를 여기서 찾는다
		Dl_info info = {};
		if (dladdr((void*)&PInvokeOverride, &info) && info.dli_fname)
			s_Self = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
		const std::string nativeDir = info.dli_fname ? fs::path(info.dli_fname).parent_path().string() : std::string();

		// 믿을 수 있는 어셈블리 (TPA) = Managed/ 의 모든 .dll (Assembly-CSharp 는 Bridge 가 따로 바이트로 읽는다)
		std::vector<std::string> paths;
		for (const auto& e : fs::directory_iterator(managed, ec))
			if (e.path().extension() == ".dll" && e.path().filename() != "Assembly-CSharp.dll")
				paths.push_back(e.path().string());
		// 구조체는 런타임이 계속 쓴다 (복사하지 않음) → 지우지 않는다
		auto* tpa = new TrustedAssemblies{ (uint32_t)paths.size(), new char* [paths.size()], new uint32_t[paths.size()], new char* [paths.size()] };
		for (size_t i = 0; i < paths.size(); ++i)
		{
			const std::string base = fs::path(paths[i]).filename().string();
			tpa->Basenames[i] = strdup(base.c_str());
			tpa->BasenameLens[i] = (uint32_t)base.size();
			tpa->Paths[i] = strdup(paths[i].c_str());
		}
		auto* appPaths = new LookupPaths{ 1, new char* [1] { strdup(managed.string().c_str()) } };
		auto* nativeDirs = new LookupPaths{ 1, new char* [1] { strdup(nativeDir.c_str()) } };
		RuntimeProperties props = { tpa, appPaths, nativeDirs, PInvokeOverride };
		// 문화권 데이터 (ICU) 없이 — 게임 스크립트는 고정 문화권 (Unity 의 기본과 같이 소수점 '.')
		const char* keys[] = { "System.Globalization.Invariant", "System.Globalization.PredefinedCulturesOnly", "RUNTIME_IDENTIFIER", "APP_CONTEXT_BASE_DIRECTORY" };
		const std::string base = managed.string() + "/";
		const char* values[] = { "true", "false", "android-x64", base.c_str() };
		if (mono.monovm_initialize_preparsed(&props, 4, keys, values) != 0)
		{
			EditorLog::Write("Script", "monovm_initialize failed");
			return false;
		}
		if (!mono.mono_jit_init_version("NOVA", "v4.0.30319"))
		{
			EditorLog::Write("Script", "mono_jit_init_version failed");
			return false;
		}
		int status = 0;
		void* core = mono.mono_assembly_open((managed / "NovaScriptCore.dll").string().c_str(), &status);
		void* klass = core ? mono.mono_class_from_name(mono.mono_assembly_get_image(core), "NovaEngine.Interop", "Bridge") : nullptr;
		if (!klass)
		{
			EditorLog::Write("Script", "NovaScriptCore.dll: Bridge not found (status %d)", status);
			return false;
		}
		auto get = [&](const char* name, int args, auto& fn) {
			void* method = mono.mono_class_get_method_from_name(klass, name, args);
			MonoErrorStorage err;
			mono.mono_error_init(&err);
			void* p = method ? mono.mono_method_get_unmanaged_callers_only_ftnptr(method, &err) : nullptr;
			if (!p)
			{
				const char* msg = method ? mono.mono_error_get_message(&err) : "method not found";
				EditorLog::Write("Script", "managed entry point %s: %s", name, msg ? msg : "?");
				ok = false;
			}
			mono.mono_error_cleanup(&err);
			fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(p);
		};
		get("Initialize", 1, m.Initialize);
		get("BeginFrame", 0, m.BeginFrame);
		get("FreeString", 1, m.FreeString);
		get("LoadGameAssembly", 1, m.LoadGameAssembly);
		get("UnloadGameAssembly", 0, m.UnloadGameAssembly);
		get("GetClassesJson", 0, m.GetClassesJson);
		get("CreateInstance", 5, m.CreateInstance);
		get("DestroyInstance", 1, m.DestroyInstance);
		get("Invoke", 2, m.Invoke);
		get("SetEnabled", 2, m.SetEnabled);
		get("InvokeCollision", 4, m.InvokeCollision);
		get("GetFieldsJson", 1, m.GetFieldsJson);
		get("SetFieldsJson", 2, m.SetFieldsJson);
		get("InvokeMethod", 4, m.InvokeMethod);
		get("InvokeUIEvent", 4, m.InvokeUIEvent);
		if (!ok) return false;

		std::vector<uint8_t> table(ScriptBindings::TableSize());
		ScriptBindings::Fill(table.data());
		const int r = m.Initialize(table.data());
		if (r != 1)
		{
			EditorLog::Write("Script", "NativeApiTable size mismatch (managed %d, native %d) - NovaScriptCore.dll is from another engine build", -r, ScriptBindings::TableSize());
			return false;
		}
		EditorLog::Write("Script", "Mono runtime ready (%d assemblies, %.0f ms)", (int)paths.size(),
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
		return true;
	}

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
		std::error_code ec;
		const fs::path dll = ManagedDir() / "Assembly-CSharp.dll";
		if (!fs::exists(dll, ec))
		{
			EditorLog::Write("Script", "no Assembly-CSharp.dll (the project has no C# scripts)");
			return;
		}
		const auto t0 = std::chrono::steady_clock::now();
		s_AssemblyLoaded = m.LoadGameAssembly(dll.string().c_str()) > 0;
		ParseClasses(TakeString(m.GetClassesJson()));
		EditorLog::Write("Script", "Assembly-CSharp loaded: %d MonoBehaviour classes (%.0f ms)", (int)s_Classes.size(),
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
	}

	bool Ready() { return s_State == ScriptEngine::State::Idle; }
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
			return;
		}
		s_State = State::Idle;
		LoadAssembly();
	}

	void Shutdown()
	{
		if (s_AssemblyLoaded && m.UnloadGameAssembly)
			m.UnloadGameAssembly();
		s_AssemblyLoaded = false;
	}

	void Update()
	{
		if (Ready())
			ScriptBindings::Update();
	}

	void BeginFrame()
	{
		if (!Ready())
			return;
		memcpy(s_PrevKeys, s_Keys, sizeof(s_Keys));
		for (int vk = 1; vk < 256; ++vk)
			s_Keys[vk] = (::GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
		if (!Application::IsPaused())
			s_PlayTime += (float)DT;
		++s_Frame;
		ScriptBindings::BeginFrame();
		m.BeginFrame();
	}

	void OnPlayModeChanged(bool)
	{
		s_PlayTime = 0.0f;
		s_Frame = 0;
		memset(s_Keys, 0, sizeof(s_Keys));
		memset(s_PrevKeys, 0, sizeof(s_PrevKeys));
		ScriptBindings::Reset();
	}

	void OnSceneSwapped()
	{
		ScriptBindings::Reset();
		if (s_AssemblyLoaded && m.InvokeUIEvent)
			m.InvokeUIEvent(0, -1, 0.0f, "");
	}

	State GetState() { return s_State; }
	bool IsAvailable() { return Ready(); }
	bool IsCompiling() { return false; }
	bool HasCompileErrors() { return false; }
	const std::string& StatusText() { return s_StatusText; }
	bool CanEnterPlayMode()
	{
		s_PlayTime = 0.0f;
		s_Frame = 0;
		return true;
	}
	void RequestRecompile() {}

	const std::vector<ClassInfo>& Classes() { return s_Classes; }
	const ClassInfo* FindClass(const std::string& name)
	{
		for (const ClassInfo& c : s_Classes)
			if (c.Name == name || c.FullName == name)
				return &c;
		return nullptr;
	}
	std::wstring FindScriptFile(const std::string&) { return {}; }
	std::wstring CreateScriptAsset(const std::wstring&, const std::string&) { return {}; }
	void RenameScriptClass(const std::wstring&, const std::string&) {}
	void OpenInCodeEditor(const std::wstring&, int) {}
	void RegenerateProjectFiles() {}
	std::string GetApiJson() { return "{}"; }
	std::vector<std::wstring> ScriptFilePaths() { return {}; }

	void* CreateInstance(const std::string& className, uint64_t gameObjectId, void* nativeComponent, const std::string& fieldsJson, bool enabled)
	{
		if (!s_AssemblyLoaded)
			return nullptr;
		return m.CreateInstance(className.c_str(), gameObjectId, nativeComponent, fieldsJson.c_str(), enabled ? 1 : 0);
	}
	void DestroyInstance(void* handle) { if (handle && s_AssemblyLoaded) m.DestroyInstance(handle); }
	void Invoke(void* handle, Message message) { if (handle && s_AssemblyLoaded) m.Invoke(handle, (int)message); }
	void SetInstanceEnabled(void* handle, bool enabled) { if (handle && s_AssemblyLoaded) m.SetEnabled(handle, enabled ? 1 : 0); }
	void InvokeCollision(void* handle, int kind, int phase, uint64_t other) { if (handle && s_AssemblyLoaded) m.InvokeCollision(handle, kind, phase, other); }
	void InvokeCollision(void* handle, bool trigger, int phase, uint64_t other) { InvokeCollision(handle, trigger ? 1 : 0, phase, other); }
	std::string GetFieldsJson(void* handle) { return handle && s_AssemblyLoaded ? TakeString(m.GetFieldsJson(handle)) : std::string("{}"); }
	void SetFieldsJson(void* handle, const std::string& j) { if (handle && s_AssemblyLoaded) m.SetFieldsJson(handle, j.c_str()); }
	bool InvokeMethod(uint64_t gameObjectId, const std::string& className, const std::string& method, const std::string& argument)
	{
		return s_AssemblyLoaded && m.InvokeMethod && m.InvokeMethod(gameObjectId, className.c_str(), method.c_str(), argument.c_str()) != 0;
	}
	bool Exec(const std::string&, std::string&, std::string& error) { error = "nova exec is editor only"; return false; }
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
		static const int kVk[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, 0, 0 };
		if (button < 0 || button >= 5 || kVk[button] == 0)
			return false;
		return KeyState(kVk[button], mode);
	}

	float PlayTime() { return s_PlayTime; }
	int FrameCount() { return s_Frame; }
}
