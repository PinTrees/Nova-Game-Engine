#pragma once
#include "Gfx.h"
#include <memory>
#include <string>

namespace Rhi { class Device; }

// Gfx (D3D11 모양) 층의 WebGPU 구현 — 웹 플레이어.
//  - 장치 = 페이지의 시작 스크립트가 미리 받은 GPUDevice (Module.preinitializedWebGPUDevice → emscripten_webgpu_get_device)
//  - 좌표 규칙은 D3D 와 같다 (클립 z 0..1, 텍스처 행 0 = 위) — 셰이더는 Y 뒤집기 없이 변환 (ShaderCross::CompileEffectWgsl)
//  - 즉시 컨텍스트를 흉내 낸다: 그리기 때 렌더 패스를 늦게 열고 (타깃 · 지우기), 파이프라인 · 바인드 그룹은 상태와 합쳐 캐시
//  - 프레임 안에서 바뀌는 데이터 (DYNAMIC 버퍼 · 상수) = 링 버퍼의 새 자리 (queue.writeBuffer 는 제출 앞에 실행되므로 자리를 다시 쓰지 않는다)
//  - 엔진은 늘 백버퍼 텍스처에 그리고 Present 가 캔버스로 복사한다
namespace GfxWgpu
{
	bool CreateDevice(GfxDevice** device, GfxContext** context, std::string& error);
	std::unique_ptr<Rhi::Device> CreateRhiDevice(GfxDevice* device, GfxContext* context, std::string& error);
	// backBuffer → 캔버스 (크기가 바뀌었으면 스왑 체인을 다시) + 제출. 프레임 끝
	void Present(GfxDevice* device, GfxTexture2D* backBuffer, int canvasWidth, int canvasHeight);
	bool IsWgpu(GfxObject* object);
	std::string Description(GfxDevice* device);
}
