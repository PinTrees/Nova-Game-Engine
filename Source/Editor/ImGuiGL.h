#pragma once

struct ImDrawData;

// ImGui 를 OpenGL 로 그리는 렌더러 (imgui_impl_dx11 과 같은 역할, 에디터가 OpenGL 일 때).
//  - 그림 텍스처 ImTextureID = GfxShaderResourceView* (DX11 쪽과 같음) → GL 텍스처 이름
//  - 지금 묶인 렌더 타깃(백버퍼)에 그린다. 좌표 규칙은 엔진과 같음: 백버퍼 행 0 = 화면 위 (Present 가 뒤집어 보여 줌)
//  - 여러 OS 창(ImGui 뷰포트)은 아직 지원하지 않는다 → GL 에서는 ViewportsEnable 을 끈다
namespace ImGuiGL
{
	bool Init();
	void Shutdown();
	void NewFrame();
	void RenderDrawData(ImDrawData* drawData);
	void InvalidateDeviceObjects();
	bool CreateDeviceObjects();
}
