#pragma once

struct ImDrawData;

// ImGui 를 Gfx 층 + 효과(Shaders/56. ImGui.fx)로 그리는 렌더러 — API 와 상관없다 (지금은 Vulkan 에디터가 쓴다).
//  imgui_impl_dx11 과 같은 역할 · 같은 그림. 그림 텍스처 ImTextureID = GfxShaderResourceView* (엔진 전체의 규칙)
//  - 지금 묶인 렌더 타깃(백버퍼)에 그리고, 그리기 전 상태(래스터 · 블렌드 · 깊이 · 뷰포트 · 가위 · 입력)를 되돌린다
//  - 여러 OS 창(ImGui 뷰포트): 창마다 RGBA8 텍스처에 그리고 GfxVk::PresentWindow 가 그 창의 스왑체인으로 (Vulkan)
namespace ImGuiGfx
{
	bool Init();
	void Shutdown();
	void NewFrame();
	void RenderDrawData(ImDrawData* drawData);
	void InvalidateDeviceObjects();
	bool CreateDeviceObjects();
}
