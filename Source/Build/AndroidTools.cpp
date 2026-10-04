#include "pch.h"
#include "AndroidTools.h"
#include "ShaderCross.h"
#include "ShaderCrossJson.h"
#include "CliServer.h"
#include "PathManager.h"
#include <fstream>

namespace AndroidTools
{
	void RegisterEditor()
	{
		// nova android shaders --out Android/build/assets/Shaders [--path one.fx]
		CliServer::Register("android", "Android build tools: {op: shaders, out, path?} (nova android help)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
			const std::string op = args.value("op", std::string("help"));
			if (op == "help")
			{
				result = { { "ops", { "shaders --out folder [--path file.fx]: convert every .fx to OpenGL ES 3.20 (<name>.json for the APK assets/Shaders)" } } };
				return true;
			}
			if (op == "shaders")
			{
				const std::string outArg = args.value("out", std::string());
				if (outArg.empty()) { error = "--out folder is required"; return false; }
				const std::filesystem::path out = string_to_wstring(outArg);
				std::error_code ec;
				std::filesystem::create_directories(out, ec);
				std::vector<std::filesystem::path> files;
				const std::string one = args.value("path", std::string());
				if (!one.empty())
					files.push_back(std::filesystem::absolute(string_to_wstring(one), ec));
				else
					for (const auto& f : std::filesystem::directory_iterator(std::filesystem::path(PathManager::GetI()->GetEnginePathW()) / L"Shaders", ec))
						if (f.path().extension() == L".fx")
							files.push_back(f.path());
				std::sort(files.begin(), files.end());
				int passes = 0, failed = 0, written = 0;
				nlohmann::json errors = nlohmann::json::array();
				for (const auto& f : files)
				{
					EditorLog::Heartbeat();
					ShaderCross::EffectGlsl e;
					if (!ShaderCross::CompileEffectGles(f.wstring(), e) || !e.Error.empty())
					{
						errors.push_back(wstring_to_string(f.filename().wstring()) + ": " + e.Error.substr(0, 300));
						continue;
					}
					for (const auto& p : e.Passes)
					{
						++passes;
						if (!p.Error.empty())
						{
							++failed;
							if (errors.size() < 40) errors.push_back(wstring_to_string(f.filename().wstring()) + " " + p.Technique + "/" + p.Pass + ": " + p.Error.substr(0, 300));
						}
					}
					std::ofstream(out / (f.stem().wstring() + L".json"), std::ios::binary | std::ios::trunc) << ShaderCross::Json::ToJson(e, kGlesShaderVersion).dump();
					++written;
				}
				result = { { "effects", files.size() }, { "written", written }, { "passes", passes }, { "passesFailed", failed }, { "out", wstring_to_string(std::filesystem::absolute(out, ec).wstring()) },
					{ "errors", errors } };
				return true;
			}
			error = "unknown op '" + op + "' (nova android help)";
			return false;
		});
	}
}
