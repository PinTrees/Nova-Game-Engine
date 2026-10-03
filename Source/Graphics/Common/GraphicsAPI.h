#pragma once
#include <string>

// 렌더링 백엔드 종류. 새 API(Vulkan 등)를 추가하면 여기에 항목을 늘린다.
enum class GraphicsAPI
{
	DirectX11 = 0,
	OpenGL,
	Vulkan,
	Count
};

inline const char* GraphicsAPIToString(GraphicsAPI api)
{
	switch (api)
	{
	case GraphicsAPI::DirectX11: return "DirectX 11";
	case GraphicsAPI::OpenGL:    return "OpenGL";
	case GraphicsAPI::Vulkan:    return "Vulkan";
	default:                     return "Unknown";
	}
}

inline const char* GraphicsAPIToKey(GraphicsAPI api)
{
	switch (api)
	{
	case GraphicsAPI::DirectX11: return "DirectX11";
	case GraphicsAPI::OpenGL:    return "OpenGL";
	case GraphicsAPI::Vulkan:    return "Vulkan";
	default:                     return "DirectX11";
	}
}

GraphicsAPI GraphicsAPIFromKey(const std::string& key);
