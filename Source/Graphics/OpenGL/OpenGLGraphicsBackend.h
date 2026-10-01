#pragma once
#include "IGraphicsBackend.h"

// OpenGL 4.5 백엔드. 셰이더 변환(HLSL → SPIR-V → GLSL)과 RHI 로 렌더러를 옮기는 중이라 아직 실행에는 쓰지 않는다.
class OpenGLGraphicsBackend : public IGraphicsBackend
{
public:
	GraphicsAPI GetAPI() const override { return GraphicsAPI::OpenGL; }
	const char* GetName() const override { return "OpenGL"; }
	// 렌더러를 옮기는 중이라 시험 단계: 환경 변수 NOVA_OPENGL_EXPERIMENTAL=1 일 때만 쓴다
	bool IsSupported() const override
	{
		char buf[8] = {};
		return ::GetEnvironmentVariableA("NOVA_OPENGL_EXPERIMENTAL", buf, sizeof(buf)) && buf[0] == '1';
	}
	const char* GetUnsupportedReason() const override { return "the OpenGL renderer is still in development (set NOVA_OPENGL_EXPERIMENTAL=1 to try it)"; }
};
