#include "pch.h"
#include "GraphicsBackendFactory.h"
#include "DX11GraphicsBackend.h"
#include "OpenGLGraphicsBackend.h"

std::unique_ptr<IGraphicsBackend> GraphicsBackendFactory::Create(GraphicsAPI api)
{
	switch (api)
	{
	case GraphicsAPI::OpenGL:
		return std::make_unique<OpenGLGraphicsBackend>();
	case GraphicsAPI::DirectX11:
	default:
		return std::make_unique<DX11GraphicsBackend>();
	}
}
