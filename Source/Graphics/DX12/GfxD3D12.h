#pragma once
#include "Gfx.h"
#include <memory>
#include <string>

namespace Rhi { class Device; }

// Gfx(D3D11 모양) 층의 DirectX 12 구현 (명시적 API — Vulkan 백엔드와 같은 구조).
//  - window = 본 창이면 그 창에 스왑체인 (Present), nullptr 이면 화면 없는 장치 (검사용)
//  - 엔진은 늘 텍스처(백버퍼)에 그리고 Present 가 스왑체인 버퍼로 복사한다 (Vulkan · GL 과 같은 방식)
//  - 셰이더 = ShaderCross::CompileEffectDxil (DXC → DXIL), 효과 하나 = 단계마다 디스크립터 표 (리플렉션 레지스터 → 효과 바인딩)
//  - 장치 · 컨텍스트는 만든 스레드에서만 쓴다
namespace GfxD3D12
{
	bool CreateDevice(HWND window, GfxDevice** device, GfxContext** context, std::string& error);

	// 같은 장치 위의 RHI 장치 (효과 FxEffect 를 이 장치로 불러온다)
	std::unique_ptr<Rhi::Device> CreateRhiDevice(GfxDevice* device, GfxContext* context, std::string& error);

	bool IsD3D12(const GfxObject* object);

	// backBuffer(RGBA8 텍스처) → 창 + 표시. syncInterval 0 = 수직 동기 없음
	void Present(GfxDevice* device, GfxTexture2D* backBuffer, int windowWidth, int windowHeight, int syncInterval);
	// 다른 OS 창 (ImGui 뷰포트) 에 표시: 창마다 스왑체인 (처음 부를 때 만든다). ReleaseWindow = 창을 닫을 때
	bool PresentWindow(GfxDevice* device, HWND window, GfxTexture2D* texture, int width, int height, int syncInterval);
	void ReleaseWindow(GfxDevice* device, HWND window);
	// 지금까지 기록한 명령을 GPU 에 보내고 끝날 때까지 기다린다 (검사 · 종료)
	void WaitIdle(GfxDevice* device);

	bool IsFormatSupported(DXGI_FORMAT format);

	// DirectXTex 이미지 → 텍스처 / 텍스처 → CPU 이미지 (Gfx::CreateTexture · CaptureTexture 의 DX12 쪽)
	HRESULT CreateTextureFromImages(GfxDevice* device, const DirectX::Image* images, size_t count, const DirectX::TexMetadata& meta,
		D3D11_USAGE usage, UINT bindFlags, UINT cpuAccess, UINT miscFlags, GfxResource** out);
	HRESULT CaptureTexture(GfxContext* context, GfxResource* texture, DirectX::ScratchImage& out);

	// GPU 이름 (로그용)
	std::string Description(GfxDevice* device);
}
