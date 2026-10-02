#include "pch.h"
#include "BuildPipeline.h"
#include "BuildSettings.h"
#include "ScriptEngine.h"
#include "PackageManager.h"
#include "ShaderCache.h"
#include "Debug.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <shellapi.h>

namespace fs = std::filesystem;

namespace
{
	struct Job
	{
		// 입력 (시작할 때 메인 스레드에서 정함)
		fs::path EngineRoot, ProjectRoot, BinDir, Out;
		std::string Product;
		std::vector<std::string> Scenes;
	std::vector<std::pair<std::string, std::wstring>> Packages;   // 프로젝트에 넣은 패키지 (이름, 폴더) — 시작할 때 모아 둔다
	bool NeedsShaderCross = true;   // 플레이어 API 에 OpenGL 이 있으면 HLSL → GLSL 변환기(dxcompiler·dxil 23 MB)가 필요
		json PlayerJson;
		bool Development = false, Run = false, Reveal = true;

		// 진행 / 결과
		std::atomic<float> Progress{ 0.0f };
		std::atomic<bool> Done{ false };
		std::mutex Lock;
		std::string Status;
		bool Success = false;
		std::string Error;
		fs::path ExePath;
		int Files = 0;
		uint64_t Bytes = 0;
		double Seconds = 0.0;

		void SetStatus(const std::string& s) { std::lock_guard<std::mutex> g(Lock); Status = s; }
	};

	std::shared_ptr<Job> s_Job;

	std::string Lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }

	bool IsEngineRelative(const std::wstring& rel)
	{
		const size_t sep = rel.find_first_of(L"\\/");
		const std::wstring first = sep == std::wstring::npos ? rel : rel.substr(0, sep);
		return _wcsicmp(first.c_str(), L"ProjectSetting") == 0 || _wcsicmp(first.c_str(), L"Resources") == 0 || _wcsicmp(first.c_str(), L"Shaders") == 0;
	}

	const std::set<std::string> kJsonExt = { ".scene", ".prefab", ".mat", ".material", ".controller", ".volumeprofile", ".terraindata", ".terrainlayer", ".json", ".asset", ".anim", ".physicmaterial", ".mixer" };
	const std::set<std::string> kModelExt = { ".fbx", ".obj", ".dae", ".gltf", ".glb", ".3ds", ".blend", ".x" };
	const std::set<std::string> kTextureExt = { ".png", ".jpg", ".jpeg", ".tga", ".dds", ".bmp", ".psd", ".tif", ".tiff", ".hdr" };

	// 씬 JSON 에서 시작해 파일을 가리키는 문자열을 따라가며 필요한 파일을 모은다
	class DependencyCollector
	{
	public:
		DependencyCollector(const fs::path& engine, const fs::path& project) : m_Engine(engine), m_Project(project) {}

		// rel = 루트 기준 경로 (Assets\... / Resources\...). 이미 있으면 무시
		void AddRelative(const std::wstring& rawRel)
		{
			std::wstring rel = rawRel;
			std::replace(rel.begin(), rel.end(), L'/', L'\\');
			while (!rel.empty() && rel[0] == L'\\')
				rel.erase(0, 1);
			if (rel.empty() || rel.find(L':') != std::wstring::npos || rel.find(L"..") != std::wstring::npos)
				return;
			const fs::path full = (IsEngineRelative(rel) ? m_Engine : m_Project) / rel;
			std::error_code ec;
			if (!fs::is_regular_file(full, ec))
				return;
			std::wstring key = rel;
			std::transform(key.begin(), key.end(), key.begin(), ::towlower);
			if (!m_Seen.insert(key).second)
				return;
			Files.push_back({ rel, full });
			// Import Settings (.meta): 게임도 같은 설정으로 가져온다 (텍스처 크기 · 모델 Scale · 오디오 Load Type)
			if (fs::is_regular_file(full.wstring() + L".meta", ec))
				Files.push_back({ rel + L".meta", full.wstring() + L".meta" });
			const std::string ext = Lower(full.extension().string());
			if (kJsonExt.count(ext) || LooksLikeJson(full))
				Scan(full);
			if (kModelExt.count(ext))
				AddModelTextures(fs::path(rel).parent_path(), full.parent_path());
		}

		std::vector<std::pair<std::wstring, fs::path>> Files;

	private:
		static bool LooksLikeJson(const fs::path& p)
		{
			std::error_code ec;
			if (fs::file_size(p, ec) > 64ull * 1024 * 1024)
				return false;
			std::ifstream in(p, std::ios::binary);
			char c = 0;
			while (in.get(c))
				if (!isspace((unsigned char)c) && (unsigned char)c != 0xEF && (unsigned char)c != 0xBB && (unsigned char)c != 0xBF)
					return c == '{' || c == '[';
			return false;
		}

		void Scan(const fs::path& file)
		{
			std::ifstream in(file);
			json j = json::parse(in, nullptr, false);
			if (!j.is_discarded())
				Walk(j);
		}

		void Walk(const json& j)
		{
			if (j.is_string())
			{
				const std::string s = j.get<std::string>();
				if (s.size() >= 4 && s.size() < 400 && s.find('.') != std::string::npos && (s.find('\\') != std::string::npos || s.find('/') != std::string::npos))
					AddRelative(string_to_wstring(s));
			}
			else if (j.is_object() || j.is_array())
				for (const auto& item : j)
					Walk(item);
		}

		// 모델이 상대 경로로 쓰는 텍스처: 같은 폴더의 이미지 + textures/materials 하위 폴더
		void AddModelTextures(const fs::path& relDir, const fs::path& fullDir)
		{
			std::error_code ec;
			for (const auto& e : fs::directory_iterator(fullDir, ec))
			{
				const std::string name = Lower(e.path().filename().string());
				if (e.is_regular_file(ec) && kTextureExt.count(Lower(e.path().extension().string())))
					AddRelative((relDir / e.path().filename()).wstring());
				else if (e.is_directory(ec) && (name.find("texture") != std::string::npos || name.find("material") != std::string::npos))
					for (const auto& t : fs::recursive_directory_iterator(e.path(), ec))
						if (t.is_regular_file(ec) && kTextureExt.count(Lower(t.path().extension().string())))
							AddRelative((relDir / fs::relative(t.path(), fullDir, ec)).wstring());
			}
		}

		fs::path m_Engine, m_Project;
		std::set<std::wstring> m_Seen;
	};

	void RunJob(std::shared_ptr<Job> job)
	{
		const auto t0 = std::chrono::steady_clock::now();
		std::error_code ec;
		const fs::path data = job->Out / (string_to_wstring(job->Product) + L"_Data");
		std::vector<std::pair<fs::path, fs::path>> copies;   // 원본 → 대상
		auto addDir = [&](const fs::path& src, const fs::path& dst, bool recursive, const std::set<std::string>& skipExt = {}) {
			if (!fs::exists(src, ec))
				return;
			if (recursive)
			{
				for (const auto& e : fs::recursive_directory_iterator(src, ec))
					if (e.is_regular_file(ec) && !skipExt.count(Lower(e.path().extension().string())))
						copies.push_back({ e.path(), dst / fs::relative(e.path(), src, ec) });
			}
			else
				for (const auto& e : fs::directory_iterator(src, ec))
					if (e.is_regular_file(ec) && !skipExt.count(Lower(e.path().extension().string())))
						copies.push_back({ e.path(), dst / e.path().filename() });
		};
		auto addFile = [&](const fs::path& src, const fs::path& dst) {
			if (fs::is_regular_file(src, ec))
				copies.push_back({ src, dst });
		};

		// 1) 씬이 참조하는 에셋 모으기
		job->SetStatus("Collecting assets referenced by scenes");
		DependencyCollector deps(job->EngineRoot, job->ProjectRoot);
		for (const std::string& scene : job->Scenes)
			deps.AddRelative(string_to_wstring(scene));
		// 프로젝트 설정(기본 Volume Profile 등)이 가리키는 것도
		for (const auto& e : fs::directory_iterator(job->ProjectRoot / L"ProjectSettings", ec))
			if (e.is_regular_file(ec) && Lower(e.path().extension().string()) == ".json")
			{
				std::ifstream in(e.path());
				json j = json::parse(in, nullptr, false);
				std::function<void(const json&)> walk = [&](const json& v) {
					if (v.is_string()) deps.AddRelative(string_to_wstring(v.get<std::string>()));
					else if (v.is_object() || v.is_array()) for (const auto& x : v) walk(x);
				};
				if (!j.is_discarded()) walk(j);
			}
		deps.AddRelative(L"Resources\\Textures\\Skybox\\KloofendalPureSky.dds");   // 하늘 (코드에서 직접 읽음)
		for (const auto& f : deps.Files)
			copies.push_back({ f.second, data / f.first });

		// 2) 엔진 / 스크립트
		copies.push_back({ job->BinDir / L"NovaEngine.exe", job->Out / (string_to_wstring(job->Product) + L".exe") });
		// 엔진이 쓰지 않는 assimp 는 넣지 않는다 (Debug 엔진 = mtd 19 MB, Release 엔진 = mt) — 게임 용량
#if defined(_DEBUG) || !defined(NOVA_ASSIMP_RELEASE_DLL)
		const std::string unusedAssimp = "assimp-vc143-mt.dll";
#else
		const std::string unusedAssimp = "assimp-vc143-mtd.dll";
#endif
		for (const auto& e : fs::directory_iterator(job->BinDir, ec))
		{
			const std::string file = Lower(e.path().filename().string());
			if (!e.is_regular_file(ec) || Lower(e.path().extension().string()) != ".dll" || file == unusedAssimp)
				continue;
			// DirectX 11 만 쓰는 게임은 셰이더 변환기(OpenGL 용)가 필요 없다
			if (!job->NeedsShaderCross && (file == "dxcompiler.dll" || file == "dxil.dll"))
				continue;
			copies.push_back({ e.path(), job->Out / e.path().filename() });
		}
		// 패키지: 프로젝트에 넣은 것만 (package.json + Plugins 의 DLL·.abi + Resources) → <제품>_Data/Packages/<이름>/
		//  게임은 이것을 embedded 패키지로 읽는다. 쓰지 않는 패키지는 빌드에 들어가지 않는다 (C# 은 이미 Assembly-CSharp 에 컴파일됨)
		for (const auto& pk : job->Packages)
		{
			const fs::path src(pk.second);
			const fs::path dst = data / L"Packages" / string_to_wstring(pk.first);
			addFile(src / L"package.json", dst / L"package.json");
			addDir(src / L"Plugins", dst / L"Plugins", false, job->Development ? std::set<std::string>{} : std::set<std::string>{ ".pdb" });
			addDir(src / L"Resources", dst / L"Resources", true);
		}
		addDir(job->EngineRoot / L"Shaders", data / L"Shaders", true);
		// 셰이더 캐시: 이 엔진 구성의 컴파일 플래그(_f<n>.fxo)만 — 예전 이름(플래그 없음)·다른 구성(Debug 의 큰 디버그 정보 등)은 쓰이지 않는다.
		//  GLSL 변환 캐시는 OpenGL 을 쓸 때만
		{
			const std::wstring flagTag = L"_f" + std::to_wstring(ShaderCache::DefaultFlags()) + L".fxo";
			const fs::path cacheDir = job->BinDir / L"ShaderCache";
			for (const auto& e : fs::directory_iterator(cacheDir, ec))
			{
				if (!e.is_regular_file(ec))
					continue;
				const std::wstring name = e.path().filename().wstring();
				if (name.size() > flagTag.size() && name.compare(name.size() - flagTag.size(), flagTag.size(), flagTag) == 0)
					copies.push_back({ e.path(), data / L"Binaries" / L"ShaderCache" / name });
			}
			if (job->NeedsShaderCross)
				addDir(cacheDir / L"GLSL", data / L"Binaries" / L"ShaderCache" / L"GLSL", true);
		}
		addDir(job->BinDir / L"Scripting", data / L"Binaries" / L"Scripting", false, job->Development ? std::set<std::string>{} : std::set<std::string>{ ".pdb" });
		addDir(job->EngineRoot / L"ProjectSetting" / L"fonts", data / L"ProjectSetting" / L"fonts", false);
		addFile(job->EngineRoot / L"ProjectSetting" / L"icon.ico", data / L"ProjectSetting" / L"icon.ico");
		addDir(job->ProjectRoot / L"ProjectSettings", data / L"ProjectSettings", false);
		addFile(job->ProjectRoot / L"Library" / L"ScriptAssemblies" / L"Assembly-CSharp.dll", data / L"Library" / L"ScriptAssemblies" / L"Assembly-CSharp.dll");
		if (job->Development)
			addFile(job->ProjectRoot / L"Library" / L"ScriptAssemblies" / L"Assembly-CSharp.pdb", data / L"Library" / L"ScriptAssemblies" / L"Assembly-CSharp.pdb");

		// 3) 이전 결과 지우고 복사 (전체 바이트 기준 진행률)
		job->SetStatus("Preparing output folder");
		fs::remove_all(data, ec);
		fs::create_directories(data, ec);
		uint64_t total = 0, done = 0;
		for (const auto& c : copies)
			total += fs::file_size(c.first, ec);
		for (size_t i = 0; i < copies.size(); ++i)
		{
			const auto& c = copies[i];
			if (i % 8 == 0)
				job->SetStatus("Copying " + wstring_to_string(c.second.filename().wstring()) + "  (" + std::to_string(i + 1) + "/" + std::to_string(copies.size()) + ")");
			fs::create_directories(c.second.parent_path(), ec);
			ec.clear();
			if (!fs::copy_file(c.first, c.second, fs::copy_options::overwrite_existing, ec) || ec)
			{
				job->Error = "Could not copy " + wstring_to_string(c.first.wstring()) + " → " + wstring_to_string(c.second.wstring()) + " (" + ec.message() + ")";
				if (c.second.extension() == L".exe")
					job->Error += " — is the game still running?";
				job->Done = true;
				return;
			}
			done += fs::file_size(c.first, ec);
			job->Progress = total > 0 ? (float)((double)done / (double)total) : 1.0f;
		}

		// 4) player.json
		{
			std::ofstream os(data / L"player.json", std::ios::trunc);
			os << job->PlayerJson.dump(4);
		}
		job->ExePath = job->Out / (string_to_wstring(job->Product) + L".exe");
		job->Files = (int)copies.size() + 1;
		job->Bytes = total;
		job->Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		job->Success = true;
		job->Progress = 1.0f;
		job->Done = true;
	}

	std::string SafeFileName(std::string s)
	{
		for (char& c : s)
			if (strchr("<>:\"/\\|?*", c) || (unsigned char)c < 32)
				c = '_';
		while (!s.empty() && (s.back() == ' ' || s.back() == '.'))
			s.pop_back();
		return s.empty() ? "Game" : s;
	}
}

namespace BuildPipeline
{
	bool IsRunning() { return s_Job && !s_Job->Done; }
	float Progress() { return s_Job ? s_Job->Progress.load() : 0.0f; }
	std::string Status()
	{
		if (!s_Job)
			return std::string();
		std::lock_guard<std::mutex> g(s_Job->Lock);
		return s_Job->Status;
	}

	bool Start(const Options& options, std::string& error)
	{
		if (IsRunning()) { error = "A build is already running."; return false; }
		if (Application::IsPlaying()) { error = "Exit Play mode before building."; return false; }
		if (options.OutputFolder.empty()) { error = "Choose an output folder."; return false; }

		// 씬: Build Settings 에 켜진 씬 (없으면 현재 씬)
		std::vector<std::string> scenes = BuildSettings::EnabledScenes();
		if (scenes.empty())
			if (Scene* scene = SceneManager::GetI()->GetCurrentScene(); scene && !scene->GetScenePath().empty())
				scenes.push_back(wstring_to_string(scene->GetScenePath()));
		if (scenes.empty()) { error = "There are no scenes to build. Add scenes in Build Settings (Add Open Scenes)."; return false; }
		std::error_code ec;
		for (const std::string& s : scenes)
			if (!fs::exists(PathManager::GetI()->GetMovePathW(string_to_wstring(s)), ec)) { error = "Scene file not found: " + s; return false; }
		if (SceneManager::GetI()->IsCurrentSceneDirty())
			Debug::LogWarning("Build: the open scene has unsaved changes — the saved version on disk is built.");

		// 스크립트: 컴파일 중이면 기다리지 말고 알림, 오류가 있으면 빌드하지 않는다 (Unity 와 같음)
		if (ScriptEngine::IsCompiling()) { error = "Scripts are still compiling. Try again in a moment."; return false; }
		if (ScriptEngine::HasCompileErrors()) { error = "Error building Player because scripts have compile errors in the editor."; return false; }

		auto job = std::make_shared<Job>();
		job->EngineRoot = PathManager::GetI()->GetEnginePathW();
		job->ProjectRoot = PathManager::GetI()->GetContentPathW();
		wchar_t exe[MAX_PATH] = {};
		::GetModuleFileNameW(nullptr, exe, MAX_PATH);
		job->BinDir = fs::path(exe).parent_path();
		job->Out = options.OutputFolder;
		job->Product = SafeFileName(BuildSettings::ProductName());
		job->Scenes = scenes;
	for (const PackageInfo* p : PackageManager::InProject())
		job->Packages.push_back({ p->Name, p->Folder });
		job->Development = options.Development;
		job->Run = options.Run;
		job->Reveal = options.Reveal;
		const BuildSettings::Player& p = BuildSettings::GetPlayer();
		job->PlayerJson = json{
			{ "productName", BuildSettings::ProductName() }, { "companyName", p.CompanyName }, { "version", p.Version },
			{ "scenes", scenes }, { "fullscreenMode", (int)p.Mode }, { "width", p.Width }, { "height", p.Height },
			{ "resizable", p.Resizable }, { "runInBackground", p.RunInBackground }, { "development", options.Development } };
		// 그래픽 API 순서 (게임이 위에서부터 이 PC 에서 되는 첫 API 를 쓴다)
		{
			json apis = json::array();
			job->NeedsShaderCross = false;
			for (GraphicsAPI api : BuildSettings::PlayerGraphicsAPIs())
			{
				apis.push_back(GraphicsAPIToKey(api));
				if (api == GraphicsAPI::OpenGL)
					job->NeedsShaderCross = true;
			}
			job->PlayerJson["graphicsAPIs"] = apis;
		}
		fs::create_directories(job->Out, ec);
		if (!fs::is_directory(job->Out, ec)) { error = "Could not create the output folder."; return false; }

		s_Job = job;
		EditorLog::Write("Build", "start: %s → %s (%d scenes)", job->Product.c_str(), wstring_to_string(job->Out.wstring()).c_str(), (int)scenes.size());
		std::thread([job]() { RunJob(job); }).detach();
		return true;
	}

	void Update()
	{
		if (!s_Job || !s_Job->Done)
			return;
		auto job = s_Job;
		s_Job.reset();
		if (!job->Success)
		{
			Debug::LogError("Build failed: " + job->Error);
			EditorLog::Write("Build", "FAILED: %s", job->Error.c_str());
			return;
		}
		char msg[512];
		snprintf(msg, sizeof(msg), "Build completed: %s (%d files, %.1f MB) in %.1f s", wstring_to_string(job->ExePath.wstring()).c_str(), job->Files, job->Bytes / (1024.0 * 1024.0), job->Seconds);
		Debug::Log(msg);
		EditorLog::Write("Build", "%s", msg);
		if (job->Run)
			::ShellExecuteW(nullptr, L"open", job->ExePath.c_str(), nullptr, job->ExePath.parent_path().c_str(), SW_SHOWNORMAL);
		else if (job->Reveal)
			::ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + job->ExePath.wstring() + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
	}

	void DrawProgress()
	{
		if (!IsRunning())
			return;
		// Unity 의 "Building Player" 진행 창
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(460, 0));
		ImGui::SetNextWindowFocus();
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
		if (ImGui::Begin("Building Player", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::TextUnformatted(Status().c_str());
			ImGui::Spacing();
			ImGui::ProgressBar(Progress(), ImVec2(-1, 18));
			ImGui::Spacing();
		}
		ImGui::End();
		ImGui::PopStyleColor();
	}
}
