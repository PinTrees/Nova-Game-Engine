#pragma once
#include "Gfx.h"
#include "Rhi.h"

// Gfx 층 비교 장면: 엔진 렌더러와 같은 방식(GfxDevice/GfxContext + FxEffect, D3D11 설명 구조체)으로 그린다.
//  RhiTest 와 같은 장면(PBR 구·금속 상자·바닥, 방향광 + 점광, 체커 밉 텍스처, 하늘 큐브맵) +
//  엔진 ShadowMap 과 같은 그림자 맵 (R24G8 타입 없는 배열, 조각마다 DSV, R24_UNORM_X8 SRV, 래스터 상태 객체)
namespace GfxTest
{
	struct Result
	{
		std::vector<uint8_t> Rgba;   // 행 0 = 위
		int Width = 0, Height = 0;
		double LoadMs = 0, DrawMs = 0;
	};

	// rhi = 효과를 불러올 장치 (dev 와 같은 API·컨텍스트)
	bool Render(GfxDevice* dev, GfxContext* ctx, Rhi::Device* rhi, int width, int height, Result& out, std::string& error);
}
