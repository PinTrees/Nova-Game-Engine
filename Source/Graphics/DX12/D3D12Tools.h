#pragma once

// DirectX 12 백엔드 개발 · 검사 도구 (CLI nova d3d12 …)
//  - shaders: 모든 .fx 를 DXIL 로 (ShaderCross::CompileEffectDxil) — pass 마다 성공 · 실패
//  - gfx-test · rhi-test: 화면 없는 DirectX 12 장치와 DirectX 11 에 같은 장면을 그려 화소 비교 (Vulkan 도구와 같은 장면)
namespace D3D12Tools
{
	void RegisterEditor();
}
