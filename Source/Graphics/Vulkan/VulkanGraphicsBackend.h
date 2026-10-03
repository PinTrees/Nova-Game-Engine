#pragma once
#include "IGraphicsBackend.h"

// Vulkan 1.3 백엔드 (안드로이드로 가는 첫 단계). Gfx 층(GfxVk*.cpp) + 셰이더 자동 변환(HLSL → SPIR-V, ShaderCross::CompileEffectSpirv).
//  에디터 창: 스왑체인 + ImGuiGfx (창 밖 OS 창은 아직 없음). 시험 단계 — 안 되면 App 이 DirectX 11 로 대체
class VulkanGraphicsBackend : public IGraphicsBackend
{
public:
	GraphicsAPI GetAPI() const override { return GraphicsAPI::Vulkan; }
	const char* GetName() const override { return "Vulkan"; }
	bool IsSupported() const override { return true; }
	bool IsExperimental() const override { return true; }
};
