#include "pch.h"
#include "WebTools.h"
#include "ShaderCross.h"
#include "ShaderCrossJson.h"
#include "CliServer.h"
#include "PathManager.h"
#include "AndroidTools.h"
#include <fstream>

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

	// 웹 게임: <out>/index.html · nova.js · nova.wasm · game.json (파일 목록 [경로, 위치, 크기]) · game.data (파일들을 이어 붙인 것)
	//  게임 데이터 = 안드로이드 내보내기 (같은 에셋 모음 · '/' 경로 · player.json, 텍스처는 BC — PC 브라우저) + Shaders/*.wgsl.json
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
		std::ofstream(out / L"game.json", std::ios::trunc) << nlohmann::json({ { "files", files }, { "bytes", offset } }).dump();
		// 플레이어 (엔진이 빌드해 둔 것) + 페이지
		for (const wchar_t* f : { L"nova.js", L"nova.wasm" })
			fs::copy_file(player / f, out / f, fs::copy_options::overwrite_existing, ec);
		const fs::path shell = fs::path(PathManager::GetI()->GetEnginePathW()) / L"Web" / L"Shell" / L"index.html";
		if (!args.value("keep-page", false) || !fs::exists(out / L"index.html", ec))
			fs::copy_file(shell, out / L"index.html", fs::copy_options::overwrite_existing, ec);
		if (!args.value("keep-stage", false))
			fs::remove_all(stage, ec);
		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		result = { { "out", wstring_to_string(out.wstring()) }, { "files", files.size() }, { "dataBytes", offset },
			{ "wasmBytes", fs::file_size(out / L"nova.wasm", ec) }, { "scenes", data.value("scenes", nlohmann::json::array()) },
			{ "textures", data.value("textures", nlohmann::json::array()).size() }, { "models", data.value("models", nlohmann::json::array()).size() },
			{ "csharp", data.value("csharp", nlohmann::json::object()) }, { "shaderPasses", shaders.value("passes", 0) }, { "shaderPassesFailed", shaders.value("passesFailed", 0) },
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
					"export --out folder [--scenes a.scene,b.scene] [--texture-compression dxt|none]: web game (index.html, nova.js, nova.wasm, game.json, game.data); serve the folder (node Tools/web/serve.mjs folder)" } },
					{ "player", wstring_to_string(PlayerDir().wstring()) },
					{ "tint", wstring_to_string(ShaderCross::TintPath()) } };
				return true;
			}
			if (op == "shaders")
				return ExportShaders(args, result, error);
			if (op == "export")
				return ExportGame(args, result, error);
			error = "unknown op " + op + " (nova web help)";
			return false;
		});
	}
}
