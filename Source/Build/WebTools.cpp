#include "pch.h"
#include "WebTools.h"
#include "ShaderCross.h"
#include "ShaderCrossJson.h"
#include "CliServer.h"
#include "PathManager.h"
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

	void RegisterEditor()
	{
		// nova web shaders --out Web/build/Shaders [--path one.fx]
		CliServer::Register("web", "Web (WebGPU) build tools: {op: shaders, out, path?} (nova web help)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
			const std::string op = args.value("op", std::string("help"));
			if (op == "help")
			{
				result = { { "ops", { "shaders --out folder [--path file.fx]: convert every .fx to WGSL for WebGPU (<name>.wgsl.json; tessellation / geometry passes are skipped)" } },
					{ "tint", wstring_to_string(ShaderCross::TintPath()) } };
				return true;
			}
			if (op == "shaders")
				return ExportShaders(args, result, error);
			error = "unknown op " + op + " (nova web help)";
			return false;
		});
	}
}
