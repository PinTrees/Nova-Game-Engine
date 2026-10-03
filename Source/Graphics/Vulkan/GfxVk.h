#pragma once
#include "Gfx.h"
#include <memory>
#include <string>

namespace Rhi { class Device; }

// Gfx(D3D11 모양) 층의 Vulkan 1.3 구현.
//  - window = 본 창이면 그 창에 스왑체인 (Present), nullptr 이면 화면 없는 장치 (검사용)
//  - 엔진은 늘 텍스처(백버퍼)에 그리고 Present 가 스왑체인 이미지로 복사한다 (GL 과 같은 방식)
//  - 장치 · 컨텍스트는 만든 스레드에서만 쓴다
namespace GfxVk
{
	bool CreateDevice(HWND window, GfxDevice** device, GfxContext** context, std::string& error);

	// 같은 장치 위의 RHI 장치 (효과 FxEffect 를 이 장치로 불러온다. pass 상태는 이 Gfx 컨텍스트로)
	std::unique_ptr<Rhi::Device> CreateRhiDevice(GfxDevice* device, GfxContext* context, std::string& error);

	// 이 Gfx 객체가 Vulkan 구현의 것인지 (Native() 가 nullptr 인 GL 과 가르기)
	bool IsVulkan(const GfxObject* object);

	// backBuffer(RGBA8 텍스처) → 창 + 표시. syncInterval 0 = 수직 동기 없음
	void Present(GfxDevice* device, GfxTexture2D* backBuffer, int windowWidth, int windowHeight, int syncInterval);
	// 지금까지 기록한 명령을 GPU 에 보내고 끝날 때까지 기다린다 (검사 · 종료)
	void WaitIdle(GfxDevice* device);

	bool IsFormatSupported(DXGI_FORMAT format);

	// DirectXTex 이미지 → 텍스처 / 텍스처 → CPU 이미지 (Gfx::CreateTexture · CaptureTexture 의 Vulkan 쪽)
	HRESULT CreateTextureFromImages(GfxDevice* device, const DirectX::Image* images, size_t count, const DirectX::TexMetadata& meta,
		D3D11_USAGE usage, UINT bindFlags, UINT cpuAccess, UINT miscFlags, GfxResource** out);
	HRESULT CaptureTexture(GfxContext* context, GfxResource* texture, DirectX::ScratchImage& out);

	// GPU · 드라이버 이름 (로그용)
	std::string Description(GfxDevice* device);
}
