#pragma once
#include "IGraphicsBackend.h"

// OpenGL 백엔드 자리(placeholder). RHI 추상화가 끝나면 구현한다.
class OpenGLGraphicsBackend : public IGraphicsBackend
{
public:
	GraphicsAPI GetAPI() const override { return GraphicsAPI::OpenGL; }
	const char* GetName() const override { return "OpenGL"; }
	bool IsSupported() const override { return false; }
	const char* GetUnsupportedReason() const override { return "Not implemented yet"; }
};
