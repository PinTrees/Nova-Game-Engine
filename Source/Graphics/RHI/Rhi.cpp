#include "pch.h"
#include "Rhi.h"

#ifdef __ANDROID__
std::unique_ptr<Rhi::Device> CreateGlesRhiDevice(std::string& error);   // Android/Source/GLESRhi.cpp (지금 EGL 컨텍스트)
#else
std::unique_ptr<Rhi::Device> CreateDx11RhiDevice(std::string& error);   // Dx11Rhi.cpp
std::unique_ptr<Rhi::Device> CreateGLRhiDevice(std::string& error);     // GLRhi.cpp
std::unique_ptr<Rhi::Device> CreateVkRhiDeviceHeadless(std::string& error);   // VkRhi.cpp
#endif

namespace Rhi
{
	uint32_t BytesPerPixel(Format format)
	{
		switch (format)
		{
		case Format::RGBA8_UNorm: return 4;
		case Format::RGBA16_Float: return 8;
		case Format::RGBA32_Float: return 16;
		case Format::R32_Float: return 4;
		case Format::RG16_Float: return 4;
		case Format::D32_Float: return 4;
		case Format::D24_UNorm_S8_UInt: return 4;
		default: return 4;
		}
	}

	bool IsDepth(Format format)
	{
		return format == Format::D32_Float || format == Format::D24_UNorm_S8_UInt;
	}

	static std::unique_ptr<Device> s_Main;
	Device* Main() { return s_Main.get(); }
	void SetMain(std::unique_ptr<Device> device) { s_Main = std::move(device); }

	std::unique_ptr<Device> CreateDevice(GraphicsAPI api, std::string& error)
	{
#ifdef __ANDROID__
		(void)api;
		return CreateGlesRhiDevice(error);   // 안드로이드: OpenGL ES 3.2
#else
		switch (api)
		{
		case GraphicsAPI::DirectX11: return CreateDx11RhiDevice(error);
		case GraphicsAPI::OpenGL: return CreateGLRhiDevice(error);
		case GraphicsAPI::Vulkan: return CreateVkRhiDeviceHeadless(error);
		default: error = "unknown graphics API"; return nullptr;
		}
#endif
	}
}
