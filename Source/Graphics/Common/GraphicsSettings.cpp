#include "pch.h"
#include "GraphicsSettings.h"
#include "GraphicsBackendFactory.h"
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace
{
	// 실행 파일 폴더(Binaries)의 상위에 있는 ProjectSetting 폴더 (엔진 설치마다 = 에디터 설정)
	const char* kSettingsPath = "../ProjectSetting/GraphicsSettings.json";
	bool s_Forced = false;
	GraphicsAPI s_ForcedAPI = GraphicsAPI::DirectX11;

	nlohmann::json ReadSettings()
	{
		std::ifstream in(kSettingsPath);
		const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
		return j.is_object() ? j : nlohmann::json::object();
	}

	// 실행 인자 -force-d3d11 / -force-d3d12 / -force-opengl / -force-vulkan (Unity 와 같은 이름, -- 도 받는다)
	bool CommandLineAPI(GraphicsAPI& out)
	{
		int argc = 0;
		LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
		bool found = false;
		for (int i = 1; argv && i < argc; ++i)
		{
			std::wstring a = argv[i];
			while (!a.empty() && a[0] == L'-')
				a.erase(0, 1);
			if (a == L"force-d3d11" || a == L"force-directx11") { out = GraphicsAPI::DirectX11; found = true; }
			else if (a == L"force-opengl" || a == L"force-glcore") { out = GraphicsAPI::OpenGL; found = true; }
			else if (a == L"force-vulkan") { out = GraphicsAPI::Vulkan; found = true; }
			else if (a == L"force-d3d12" || a == L"force-directx12") { out = GraphicsAPI::DirectX12; found = true; }
		}
		::LocalFree(argv);
		return found;
	}
}

GraphicsAPI GraphicsSettings::s_Requested = GraphicsAPI::DirectX11;
std::unique_ptr<IGraphicsBackend> GraphicsSettings::s_Backend;
std::string GraphicsSettings::s_Log;

GraphicsAPI GraphicsAPIFromKey(const std::string& key)
{
	for (int i = 0; i < static_cast<int>(GraphicsAPI::Count); ++i)
	{
		if (key == GraphicsAPIToKey(static_cast<GraphicsAPI>(i)))
			return static_cast<GraphicsAPI>(i);
	}
	return GraphicsAPI::DirectX11;
}

std::vector<GraphicsAPI> GraphicsSettings::AllAPIs()
{
	std::vector<GraphicsAPI> all;
	for (int i = 0; i < static_cast<int>(GraphicsAPI::Count); ++i)
		all.push_back(static_cast<GraphicsAPI>(i));
	return all;
}

bool GraphicsSettings::IsSupported(GraphicsAPI api, std::string* reason)
{
	auto backend = GraphicsBackendFactory::Create(api);
	const bool ok = backend && backend->IsSupported();
	if (!ok && reason)
		*reason = backend ? backend->GetUnsupportedReason() : "unknown API";
	return ok;
}

bool GraphicsSettings::IsExperimental(GraphicsAPI api)
{
	auto backend = GraphicsBackendFactory::Create(api);
	return backend && backend->IsExperimental();
}

void GraphicsSettings::FallBack(GraphicsAPI api, const std::string& why)
{
	s_Backend = GraphicsBackendFactory::Create(api);
	s_Log += ", " + why + " -> using " + std::string(s_Backend ? s_Backend->GetName() : "?");
}

void GraphicsSettings::ForceForThisProcess(GraphicsAPI api)
{
	s_Forced = true;
	s_ForcedAPI = api;
}

GraphicsAPI GraphicsSettings::GetEditorAPI()
{
	const nlohmann::json j = ReadSettings();
	// 예전 키 "GraphicsAPI" 도 읽는다
	return GraphicsAPIFromKey(j.value("EditorGraphicsAPI", j.value("GraphicsAPI", std::string("DirectX11"))));
}

void GraphicsSettings::SetEditorAPI(GraphicsAPI api)
{
	nlohmann::json j = ReadSettings();
	j.erase("GraphicsAPI");
	j["EditorGraphicsAPI"] = GraphicsAPIToKey(api);
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(kSettingsPath).parent_path(), ec);
	std::ofstream out(kSettingsPath, std::ios::trunc);
	if (out.is_open())
		out << j.dump(4);
}

void GraphicsSettings::Init(const std::vector<GraphicsAPI>& playerList)
{
	// ---- 원하는 순서
	std::vector<GraphicsAPI> wanted;
	std::string source;
	GraphicsAPI forced;
	if (CommandLineAPI(forced))
	{
		wanted = { forced };
		source = "command line";
	}
	else if (s_Forced)
	{
		wanted = { s_ForcedAPI };
		source = "fixed for this program";
	}
	else if (!playerList.empty())
	{
		wanted = playerList;
		source = "Player Settings";
	}
	else
	{
		wanted = { GetEditorAPI() };
		source = "Preferences";
	}
	s_Requested = wanted.front();

	// ---- 쓸 수 있는 첫 API (없으면 DirectX 11)
	s_Log = std::string("requested ") + GraphicsAPIToString(s_Requested) + " (" + source + ")";
	s_Backend.reset();
	for (GraphicsAPI api : wanted)
	{
		std::string reason;
		if (IsSupported(api, &reason))
		{
			s_Backend = GraphicsBackendFactory::Create(api);
			break;
		}
		s_Log += std::string(", ") + GraphicsAPIToString(api) + " unavailable: " + reason;
	}
	if (!s_Backend)
	{
		s_Backend = GraphicsBackendFactory::Create(GraphicsAPI::DirectX11);
		s_Log += ", falling back to DirectX 11";
	}
	s_Log += std::string(" -> using ") + s_Backend->GetName();
}

GraphicsAPI GraphicsSettings::GetActiveAPI()
{
	return s_Backend ? s_Backend->GetAPI() : GraphicsAPI::DirectX11;
}
