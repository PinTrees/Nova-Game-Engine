#pragma once
#include "IGraphicsBackend.h"

// OpenGL 4.5 백엔드. 셰이더 변환(HLSL → SPIR-V → GLSL)과 RHI 로 렌더러를 옮기는 중이라 아직 실행에는 쓰지 않는다.
class OpenGLGraphicsBackend : public IGraphicsBackend
{
public:
	GraphicsAPI GetAPI() const override { return GraphicsAPI::OpenGL; }
	const char* GetName() const override { return "OpenGL"; }
	// Gfx 층(GfxGL) + 셰이더 자동 변환으로 에디터·게임 모두 OpenGL 4.5 로 그린다. 아직 시험 단계
	//  (OpenGL 4.5 를 만들 수 없으면 App 이 DirectX 11 로 대체 — GraphicsSettings::FallBack)
	bool IsSupported() const override { return true; }
	bool IsExperimental() const override { return true; }
};
