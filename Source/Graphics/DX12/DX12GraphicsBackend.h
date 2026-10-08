#pragma once
#include "IGraphicsBackend.h"

// DirectX 12 백엔드 (명시적 API — 렌더링 현대화 3 단계). Gfx 층(GfxD3D12*.cpp) + 셰이더 자동 변환 (HLSL → DXIL, ShaderCross::CompileEffectDxil).
//  에디터 창: 플립 스왑체인 + ImGuiGfx (창 밖 OS 창도). 시험 단계 — 안 되면 App 이 DirectX 11 로 대체
class DX12GraphicsBackend : public IGraphicsBackend
{
public:
	GraphicsAPI GetAPI() const override { return GraphicsAPI::DirectX12; }
	const char* GetName() const override { return "DirectX 12"; }
	bool IsSupported() const override { return true; }
	bool IsExperimental() const override { return true; }
};
