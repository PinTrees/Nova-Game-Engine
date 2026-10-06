#include "pch.h"
#include "WebTools.h"
#include "ShaderCross.h"
#include "ShaderCrossJson.h"
#include "CliServer.h"
#include "PathManager.h"
#include "AndroidTools.h"
#include "BuildSettings.h"
#include "WebBuild.h"
#include <fstream>
#include <set>

namespace WebTools
{
	namespace
	{
		// 엔진 셰이더 + 공식 패키지 셰이더 (Packages/<이름>/Shaders)
		std::vector<std::filesystem::path> EngineShaders()
		{
			std::vector<std::filesystem::path> files;
			std::error_code ec;
			const std::filesystem::path engine = PathManager::GetI()->GetEnginePathW();
			for (const auto& f : std::filesystem::directory_iterator(engine / L"Shaders", ec))
				if (f.path().extension() == L".fx")
					files.push_back(f.path());
			for (const auto& pkg : std::filesystem::directory_iterator(engine / L"Packages", ec))
				for (const auto& f : std::filesystem::directory_iterator(pkg.path() / L"Shaders", ec))
					if (f.path().extension() == L".fx")
						files.push_back(f.path());
			std::sort(files.begin(), files.end());
			return files;
		}
	}

	bool ExportShaders(const nlohmann::json& args, nlohmann::json& result, std::string& error)
	{
		const std::string outArg = args.value("out", std::string());
		if (outArg.empty()) { error = "--out folder is required"; return false; }
		if (ShaderCross::TintPath().empty()) { error = "tint.exe not found (Tools/web/build_tint.ps1, or set NOVA_TINT)"; return false; }
		const std::filesystem::path out = string_to_wstring(outArg);
		std::error_code ec;
		std::filesystem::create_directories(out, ec);
		std::vector<std::filesystem::path> files;
		const std::string one = args.value("path", std::string());
		if (!one.empty())
			files.push_back(std::filesystem::absolute(string_to_wstring(one), ec));
		else
			files = EngineShaders();
		int passes = 0, failed = 0, skipped = 0, written = 0;
		nlohmann::json errors = nlohmann::json::array();
		std::map<std::string, int> reasons;   // 실패 이유 → 수 (Tint 메시지의 앞부분)
		for (const auto& f : files)
		{
			EditorLog::Heartbeat();
			ShaderCross::EffectSpirv e;
			const std::string name = wstring_to_string(f.filename().wstring());
			if (!ShaderCross::CompileEffectWgsl(f.wstring(), e) || !e.Error.empty())
			{
				errors.push_back(name + ": " + e.Error.substr(0, 300));
				continue;
			}
			for (const auto& p : e.Passes)
			{
				++passes;
				if (p.Error.empty())
					continue;
				if (p.Error.rfind("WebGPU has no", 0) == 0)
				{
					++skipped;   // 테셀레이션 · 지오메트리 — 웹에 없는 단계 (셰이더가 다른 기법으로)
					continue;
				}
				++failed;
				const size_t colon = p.Error.find("Tint:");
				++reasons[(colon == std::string::npos ? p.Error : p.Error.substr(colon)).substr(0, 90)];
				if (errors.size() < 60) errors.push_back(name + " " + p.Technique + "/" + p.Pass + ": " + p.Error.substr(0, 300));
			}
			std::ofstream(out / (f.stem().wstring() + L".wgsl.json"), std::ios::binary | std::ios::trunc) << ShaderCross::Json::WgslToJson(e, kWgslShaderVersion).dump();
			++written;
		}
		nlohmann::json why = nlohmann::json::object();
		for (const auto& [r, n] : reasons)
			why[r] = n;
		result = { { "effects", files.size() }, { "written", written }, { "passes", passes }, { "passesFailed", failed }, { "passesSkipped", skipped },
			{ "out", wstring_to_string(std::filesystem::absolute(out, ec).wstring()) }, { "reasons", why }, { "errors", errors } };
		return true;
	}

	std::filesystem::path PlayerDir()
	{
		std::error_code ec;
		const std::filesystem::path engine = PathManager::GetI()->GetEnginePathW();
		for (const auto& d : { engine / L"Binaries" / L"Web", engine / L"Web" / L"build" / L"Release" })
			if (std::filesystem::exists(d / L"nova.wasm", ec) && std::filesystem::exists(d / L"nova.js", ec))
				return d;
		return {};
	}

	// C# 이 있는 게임의 플레이어: .NET 웹어셈블리 런타임 + 엔진 (Web/build.sh Release host → host/wwwroot/_framework)
	std::filesystem::path HostFrameworkDir()
	{
		std::error_code ec;
		const std::filesystem::path engine = PathManager::GetI()->GetEnginePathW();
		for (const auto& d : { engine / L"Binaries" / L"Web" / L"_framework", engine / L"Web" / L"build" / L"Release" / L"host" / L"wwwroot" / L"_framework" })
			if (std::filesystem::exists(d / L"dotnet.js", ec) && std::filesystem::exists(d / L"dotnet.native.wasm", ec))
				return d;
		return {};
	}

	// .NET 판 플레이어 (_framework) 를 게임에 맞게 줄인다: 쓰는 BCL 만 (안드로이드와 같은 규칙 — 진입 어셈블리 · 엔진 API · 게임 스크립트에서
	//  AssemblyRef 를 따라간다). 나머지 .wasm 은 지우고 dotnet.js 안의 자원 목록 (/*json-start*/ … /*json-end*/) 에서도 뺀다. 반환 = 지운 수
	int PruneFramework(const std::filesystem::path& fw, const std::filesystem::path& gameDll, std::string& error)
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		std::string js;
		{
			std::ifstream in(fw / L"dotnet.js", std::ios::binary);
			js.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}
		const std::string startTag = "/*json-start*/", endTag = "/*json-end*/";
		const size_t a = js.find(startTag), b = js.find(endTag);
		if (a == std::string::npos || b == std::string::npos || b < a) { error = "dotnet.js: no boot config"; return -1; }
		nlohmann::json boot = nlohmann::json::parse(js.substr(a + startTag.size(), b - a - startTag.size()), nullptr, false);
		if (boot.is_discarded() || !boot.contains("resources") || !boot["resources"].contains("assembly")) { error = "dotnet.js: unreadable boot config"; return -1; }
		auto stem = [](const std::string& file) { return file.size() > 5 && file.substr(file.size() - 5) == ".wasm" ? file.substr(0, file.size() - 5) : file; };
		std::set<std::string> available;
		for (const auto& r : boot["resources"]["assembly"]) available.insert(stem(r.value("name", std::string())));
		std::set<std::string> needed;
		std::vector<fs::path> queue = { gameDll };
		auto push = [&](const std::string& name) { if (available.count(name) && !needed.count(name)) queue.push_back(fw / string_to_wstring(name + ".wasm")); };
		push(boot.value("mainAssemblyName", std::string("NovaWebHost")));
		for (const char* always : { "NovaScriptCore", "System.Runtime", "System.Private.Uri", "System.Runtime.InteropServices", "System.Console" })
			push(always);
		for (const auto& r : boot["resources"].value("coreAssembly", nlohmann::json::array()))
			queue.push_back(fw / string_to_wstring(r.value("name", std::string())));
		while (!queue.empty())
		{
			const fs::path next = queue.back();
			queue.pop_back();
			const std::string name = stem(wstring_to_string(next.filename().wstring()));
			if (next != gameDll && !needed.insert(name).second) continue;
			for (const std::string& ref : AndroidTools::AssemblyReferences(next))
				push(ref);
		}
		nlohmann::json kept = nlohmann::json::array();
		int removed = 0;
		for (const auto& r : boot["resources"]["assembly"])
		{
			const std::string file = r.value("name", std::string());
			if (needed.count(stem(file))) kept.push_back(r);
			else
			{
				fs::remove(fw / string_to_wstring(file), ec);
				++removed;
			}
		}
		boot["resources"]["assembly"] = kept;
		js = js.substr(0, a + startTag.size()) + boot.dump(2) + js.substr(b);
		std::ofstream(fw / L"dotnet.js", std::ios::binary | std::ios::trunc) << js;
		return removed;
	}

	// 웹 게임: <out>/index.html · nova.js · nova.wasm · game.json (파일 목록 [경로, 위치, 크기]) · game.data (파일들을 이어 붙인 것)
	//  게임 데이터 = 안드로이드 내보내기 (같은 에셋 모음 · '/' 경로 · player.json, 텍스처는 BC — PC 브라우저) + Shaders/*.wgsl.json
	//  C# 스크립트가 있으면 플레이어 = _framework (.NET 런타임 + 엔진 한 wasm) 이고 게임 데이터에는 Managed/Assembly-CSharp.dll 만
	bool ExportGame(const nlohmann::json& args, nlohmann::json& result, std::string& error)
	{
		namespace fs = std::filesystem;
		const std::string outArg = args.value("out", std::string());
		if (outArg.empty()) { error = "--out folder is required"; return false; }
		const fs::path player = PlayerDir();
		if (player.empty()) { error = "web player not built (Web/build.sh Release, or Binaries/Web/nova.wasm)"; return false; }
		const auto t0 = std::chrono::steady_clock::now();
		std::error_code ec;
		const fs::path out = fs::absolute(string_to_wstring(outArg), ec);
		const fs::path stage = out / L"_stage";
		fs::create_directories(out, ec);
		nlohmann::json a = args;
		a["out"] = wstring_to_string(stage.wstring());
		if (!a.contains("texture-compression")) a["texture-compression"] = "dxt";
		a["managed"] = "game-only";
		nlohmann::json data;
		if (!AndroidTools::ExportGame(a, data, error))
			return false;
		const fs::path game = stage / L"game";
		nlohmann::json shaders;
		if (!ExportShaders({ { "out", wstring_to_string((game / L"Shaders").wstring()) } }, shaders, error))
			return false;
		if (shaders.value("written", 0) == 0) { error = "no shaders converted"; return false; }
		// 한 덩어리로
		std::ofstream blob(out / L"game.data", std::ios::binary | std::ios::trunc);
		nlohmann::json files = nlohmann::json::array();
		uint64_t offset = 0;
		std::vector<fs::path> all;
		for (const auto& e : fs::recursive_directory_iterator(game, ec))
			if (e.is_regular_file(ec) && e.path().filename() != L"files.txt")
				all.push_back(e.path());
		std::sort(all.begin(), all.end());
		std::vector<char> buf;
		for (const fs::path& f : all)
		{
			std::ifstream in(f, std::ios::binary);
			buf.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			blob.write(buf.data(), (std::streamsize)buf.size());
			std::string rel = wstring_to_string(fs::relative(f, game, ec).wstring());
			std::replace(rel.begin(), rel.end(), '\\', '/');
			files.push_back({ rel, offset, buf.size() });
			offset += buf.size();
		}
		blob.close();
		// 플레이어 (엔진이 빌드해 둔 것) + 페이지. C# 이 있으면 .NET 판 — 없으면 (빌드 안 함) 스크립트 없이 엔진만
		nlohmann::json csharp = data.value("csharp", nlohmann::json::object());
		const fs::path framework = csharp.value("included", false) ? HostFrameworkDir() : fs::path();
		if (csharp.value("included", false) && framework.empty())
			csharp["error"] = "C# web player not built (Web/build.sh Release host) - the game runs without scripts";
		const bool dotnet = !framework.empty();
		nlohmann::json manifest = { { "files", files }, { "bytes", offset } };
		if (dotnet) manifest["runtime"] = "dotnet";
		std::ofstream(out / L"game.json", std::ios::trunc) << manifest.dump();
		uint64_t playerBytes = 0;
		fs::remove_all(out / L"_framework", ec);
		if (dotnet)
		{
			for (const wchar_t* f : { L"nova.js", L"nova.wasm" })
				fs::remove(out / f, ec);
			fs::copy(framework, out / L"_framework", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
			const fs::path gameDll = PathManager::GetI()->GetMovePathW(L"Library\\ScriptAssemblies\\Assembly-CSharp.dll");
			std::string pruneError;
			const int removed = PruneFramework(out / L"_framework", gameDll, pruneError);
			if (removed < 0) csharp["pruneError"] = pruneError;
			else csharp["frameworkAssembliesRemoved"] = removed;
			for (const auto& e : fs::recursive_directory_iterator(out / L"_framework", ec))
				if (e.is_regular_file(ec)) playerBytes += e.file_size(ec);
		}
		else
			for (const wchar_t* f : { L"nova.js", L"nova.wasm" })
			{
				fs::copy_file(player / f, out / f, fs::copy_options::overwrite_existing, ec);
				playerBytes += fs::file_size(out / f, ec);
			}
		const fs::path shell = fs::path(PathManager::GetI()->GetEnginePathW()) / L"Web" / L"Shell" / L"index.html";
		if (!args.value("keep-page", false) || !fs::exists(out / L"index.html", ec))
		{
			// 페이지 제목 = 제품 이름 (Player Settings)
			std::ifstream in(shell, std::ios::binary);
			std::string page((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			std::string title;
			for (char c : BuildSettings::ProductName())
				title += c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '&' ? "&amp;" : std::string(1, c);
			const size_t t = page.find("<title>NOVA</title>");
			if (t != std::string::npos && !title.empty())
				page.replace(t, 19, "<title>" + title + "</title>");
			std::ofstream(out / L"index.html", std::ios::binary | std::ios::trunc) << page;
		}
		if (!args.value("keep-stage", false))
			fs::remove_all(stage, ec);
		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		result = { { "out", wstring_to_string(out.wstring()) }, { "files", files.size() }, { "dataBytes", offset },
			{ "runtime", dotnet ? "dotnet" : "engine" }, { "playerBytes", playerBytes }, { "scenes", data.value("scenes", nlohmann::json::array()) },
			{ "textures", data.value("textures", nlohmann::json::array()).size() }, { "models", data.value("models", nlohmann::json::array()).size() },
			{ "csharp", csharp }, { "shaderPasses", shaders.value("passes", 0) }, { "shaderPassesFailed", shaders.value("passesFailed", 0) },
			{ "seconds", std::round(seconds * 10.0) / 10.0 } };
		EditorLog::Write("Web", "exported %d files (%llu bytes) to %s", (int)files.size(), (unsigned long long)offset, outArg.c_str());
		return true;
	}

	void RegisterEditor()
	{
		// nova web shaders --out Web/build/Shaders [--path one.fx]
		CliServer::Register("web", "Web (WebGPU) build tools: {op: shaders, out, path?} (nova web help)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
			const std::string op = args.value("op", std::string("help"));
			if (op == "help")
			{
				result = { { "ops", { "shaders --out folder [--path file.fx]: convert every .fx to WGSL for WebGPU (<name>.wgsl.json; tessellation / geometry passes are skipped)",
					"export --out folder [--scenes a.scene,b.scene] [--texture-compression dxt|none]: web game (index.html, nova.js + nova.wasm or _framework/ with C#, game.json, game.data); serve the folder (node Tools/web/serve.mjs folder)",
					"build --out folder [--run] [--port N] [--open]: Build Settings web build (scenes in Build Settings); --run serves it at http://localhost:<port>/ (--open also opens the default browser)",
					"serve --path folder [--port N] | stop-server: the editor's preview web server" } },
					{ "player", wstring_to_string(PlayerDir().wstring()) },
					{ "dotnetPlayer", wstring_to_string(HostFrameworkDir().wstring()) },
					{ "tint", wstring_to_string(ShaderCross::TintPath()) } };
				return true;
			}
			if (op == "shaders")
				return ExportShaders(args, result, error);
			if (op == "export")
				return ExportGame(args, result, error);
			if (op == "build")
			{
				const std::string outArg = args.value("out", std::string());
				if (outArg.empty()) { error = "--out folder is required"; return false; }
				WebBuild::Options o;
				o.OutputFolder = string_to_wstring(outArg);
				o.TextureCompression = args.value("texture-compression", std::string());
				if (!WebBuild::Build(o, result, error))
					return false;
				// --run: 미리 보기 서버 (CLI 는 브라우저를 열지 않는다 — --open 일 때만)
				if (args.value("run", false))
				{
					const std::string url = WebBuild::Serve(o.OutputFolder, args.value("port", 0));
					if (url.empty()) { error = "built, but the web server could not start (port in use?)"; return false; }
					result["url"] = url;
					if (args.value("open", false))
						::ShellExecuteW(nullptr, L"open", string_to_wstring(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				}
				return true;
			}
			if (op == "serve")
			{
				const std::string path = args.value("path", std::string());
				if (path.empty()) { error = "--path folder is required"; return false; }
				const std::string url = WebBuild::Serve(string_to_wstring(path), args.value("port", 0));
				if (url.empty()) { error = "could not start the web server (port in use?)"; return false; }
				result = { { "url", url } };
				return true;
			}
			if (op == "stop-server")
			{
				WebBuild::StopServer();
				result = { { "stopped", true } };
				return true;
			}
			error = "unknown op " + op + " (nova web help)";
			return false;
		});
	}
}
