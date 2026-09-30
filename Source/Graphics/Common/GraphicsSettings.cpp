#include "pch.h"
#include "GraphicsSettings.h"
#include "GraphicsBackendFactory.h"
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace
{
	// 실행 파일 폴더(Binaries)의 상위에 있는 ProjectSetting 폴더
	const char* kSettingsPath = "../ProjectSetting/GraphicsSettings.json";
}

GraphicsAPI GraphicsSettings::s_Requested = GraphicsAPI::DirectX11;
std::unique_ptr<IGraphicsBackend> GraphicsSettings::s_Backend;

GraphicsAPI GraphicsAPIFromKey(const std::string& key)
{
	for (int i = 0; i < static_cast<int>(GraphicsAPI::Count); ++i)
	{
		if (key == GraphicsAPIToKey(static_cast<GraphicsAPI>(i)))
			return static_cast<GraphicsAPI>(i);
	}
	return GraphicsAPI::DirectX11;
}

void GraphicsSettings::Init()
{
	std::ifstream in(kSettingsPath);
	if (in.is_open())
	{
		try
		{
			nlohmann::json j;
			in >> j;
			s_Requested = GraphicsAPIFromKey(j.value("GraphicsAPI", std::string("DirectX11")));
		}
		catch (...)
		{
			s_Requested = GraphicsAPI::DirectX11;
		}
	}

	s_Backend = GraphicsBackendFactory::Create(s_Requested);
	if (!s_Backend->IsSupported())
	{
		// 선택한 API가 아직 구현되지 않았다면 DirectX11로 대체 (설정 파일은 유지)
		s_Backend = GraphicsBackendFactory::Create(GraphicsAPI::DirectX11);
	}
}

void GraphicsSettings::SetRequestedAPI(GraphicsAPI api)
{
	s_Requested = api;

	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(kSettingsPath).parent_path(), ec);
	std::ofstream out(kSettingsPath);
	if (out.is_open())
	{
		nlohmann::json j;
		j["GraphicsAPI"] = GraphicsAPIToKey(api);
		out << j.dump(4);
	}
}

GraphicsAPI GraphicsSettings::GetActiveAPI()
{
	return s_Backend ? s_Backend->GetAPI() : GraphicsAPI::DirectX11;
}
