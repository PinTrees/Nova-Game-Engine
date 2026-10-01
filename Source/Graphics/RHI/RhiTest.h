#pragma once
#include "Rhi.h"

// RHI 비교 장면: 같은 코드·같은 셰이더(32. InstancedBasic.fx Tech = 메시 PBR)를 API 마다 그려 픽셀을 비교한다.
//  구·금속 상자·바닥, 방향광 + 점광, 체커 텍스처(밉, 왼쪽 위 빨간 표시 = uv 방향 확인), 하늘 큐브맵(밉, +X 쪽 주황 = 면 방향 확인),
//  방향광 그림자 (26. BuildShadowMap.fx 로 깊이 배열에 그린 뒤 비교 샘플러 PCF — 깊이 타깃·배열·비교 샘플러·텍스처 공간 행렬 확인)
namespace RhiTest
{
	struct Result
	{
		std::vector<uint8_t> Rgba;   // 행 0 = 위
		int Width = 0, Height = 0;
		std::string Device;
		double LoadMs = 0, DrawMs = 0;
	};

	bool RenderLitScene(Rhi::Device& device, int width, int height, Result& out, std::string& error);
}
