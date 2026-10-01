#pragma once
#include "IGraphicsBackend.h"

// OpenGL 4.5 백엔드. 셰이더 변환(HLSL → SPIR-V → GLSL)과 RHI 로 렌더러를 옮기는 중이라 아직 실행에는 쓰지 않는다.
class OpenGLGraphicsBackend : public IGraphicsBackend
{
public:
	GraphicsAPI GetAPI() const override { return GraphicsAPI::OpenGL; }
	const char* GetName() const override { return "OpenGL"; }
	// Gfx 층(GfxGL) + 셰이더 자동 변환으로 에디터·게임 모두 OpenGL 4.5 로 그린다
	//  (OpenGL 4.5 를 만들 수 없으면 App 이 DirectX 11 로 대체 — GraphicsSettings::FallBack).
	//  2026-10-02 시험 단계 해제: 7 개 씬 화면이 DX11 과 같고(픽셀 차이 최대 1~14, 나무는 바람 잎만), 가장 무거운 씬(Trees)
	//  프레임 시간이 DX11 +4% (Release). 남은 것은 옛 예제만 쓰는 스트림 출력·UAV·버퍼 SRV
	bool IsSupported() const override { return true; }
	bool IsExperimental() const override { return false; }
};
