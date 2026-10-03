#pragma once
#include "IGraphicsBackend.h"

// Vulkan 1.3 백엔드 (안드로이드로 가는 첫 단계). Gfx 층(GfxVk*.cpp) + 셰이더 자동 변환(HLSL → SPIR-V, ShaderCross::CompileEffectSpirv).
//  1 단계: 화면 없는 장치로 검사 장면(nova vulkan gfx-test)을 DirectX 11 과 같게 그린다. 에디터 창(스왑체인 · ImGui)은 다음 단계 —
//  그때까지 설정 메뉴에서는 고를 수 없다 (IsSupported = false)
class VulkanGraphicsBackend : public IGraphicsBackend
{
public:
	GraphicsAPI GetAPI() const override { return GraphicsAPI::Vulkan; }
	const char* GetName() const override { return "Vulkan"; }
	bool IsSupported() const override { return false; }
	const char* GetUnsupportedReason() const override { return "Vulkan backend is in development (editor window not yet)"; }
	bool IsExperimental() const override { return true; }
};
