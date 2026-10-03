#include "pch.h"
#include "GraphicsBackendFactory.h"
#include "DX11GraphicsBackend.h"
#include "OpenGLGraphicsBackend.h"
#include "VulkanGraphicsBackend.h"

std::unique_ptr<IGraphicsBackend> GraphicsBackendFactory::Create(GraphicsAPI api)
{
	switch (api)
	{
	case GraphicsAPI::OpenGL:
		return std::make_unique<OpenGLGraphicsBackend>();
	case GraphicsAPI::Vulkan:
		return std::make_unique<VulkanGraphicsBackend>();
	case GraphicsAPI::DirectX11:
	default:
		return std::make_unique<DX11GraphicsBackend>();
	}
}
