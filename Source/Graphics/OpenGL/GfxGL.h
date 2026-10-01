#pragma once
#include "Gfx.h"
#include <string>

namespace Rhi { class Device; }

// Gfx(D3D11 모양) 층의 OpenGL 4.5 구현.
//  - window = 본 창이면 그 창에 GL 컨텍스트, nullptr 이면 숨은 창 (검사용). 만든 스레드에서만 쓴다
//  - 화면 표시: 엔진은 늘 텍스처(백버퍼)에 그리고 Present 가 창으로 위아래를 뒤집어 복사한다
//    (텍스처 행 0 = D3D 의 위 = GL 창의 아래이므로)
namespace GfxGL
{
	bool CreateDevice(HWND window, GfxDevice** device, GfxContext** context, std::string& error);

	// 같은 컨텍스트 위의 RHI 장치 (효과 FxEffect 를 이 장치로 불러온다. pass 상태는 이 Gfx 컨텍스트로)
	std::unique_ptr<Rhi::Device> CreateRhiDevice(GfxDevice* device, GfxContext* context, std::string& error);

	// backBuffer(RGBA8 텍스처) → 창 (위아래 뒤집기) + SwapBuffers. syncInterval 0 = 수직 동기 없음
	void Present(GfxDevice* device, GfxTexture2D* backBuffer, int windowWidth, int windowHeight, int syncInterval);

	// GL 을 직접 만진 뒤 (ImGui 렌더러 등): Gfx 컨텍스트가 알고 있는 상태(타깃·뷰포트·가위·래스터·블렌드·깊이)를 다시 GL 에
	void RestoreState(GfxContext* context);

	// GL 이 지원하는 형식인지 (텍스처 만들기 전에 검사)
	bool IsFormatSupported(DXGI_FORMAT format);

	// DirectXTex 이미지 → GL 텍스처 / 텍스처 → CPU 이미지 (Gfx::CreateTexture·CaptureTexture 의 GL 쪽)
	HRESULT CreateTextureFromImages(GfxDevice* device, const DirectX::Image* images, size_t count, const DirectX::TexMetadata& meta,
		D3D11_USAGE usage, UINT bindFlags, UINT cpuAccess, UINT miscFlags, GfxResource** out);
	HRESULT CaptureTexture(GfxContext* context, GfxResource* texture, DirectX::ScratchImage& out);
}
