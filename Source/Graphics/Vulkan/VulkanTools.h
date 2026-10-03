#pragma once

// Vulkan 백엔드 개발 · 검사 도구 (CLI nova vulkan …)
//  - shaders: 모든 .fx 를 Vulkan SPIR-V 로 (ShaderCross::CompileEffectSpirv), pass 마다 단계 모듈을 ShaderCache/SPIRV/dump/*.spv 로 —
//    SDK 의 spirv-val 로 검증할 수 있게
namespace VulkanTools
{
	void RegisterEditor();
}
