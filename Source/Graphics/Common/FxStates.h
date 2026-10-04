#pragma once
#include "FxParser.h"

// .fx 상태 블록 (FxParser) → D3D11 설명 구조체. 그래픽 API 와 상관없다 (OpenGL · Vulkan · OpenGL ES 가 같이 쓴다).
//  기본값에서 블록에 적힌 것만 바꾼다 (Effects11 과 같음). D3D11 설명 구조체는 Windows 는 d3d11.h, 안드로이드는 WinCompat.h
namespace FxStates
{
	D3D11_RASTERIZER_DESC DefaultRasterizer();
	D3D11_BLEND_DESC DefaultBlend();
	D3D11_DEPTH_STENCIL_DESC DefaultDepthStencil();
	D3D11_SAMPLER_DESC DefaultSampler();

	D3D11_RASTERIZER_DESC Rasterizer(const FxParser::StateBlock& block);
	D3D11_BLEND_DESC Blend(const FxParser::StateBlock& block);
	D3D11_DEPTH_STENCIL_DESC DepthStencil(const FxParser::StateBlock& block);
	D3D11_SAMPLER_DESC Sampler(const FxParser::StateBlock& block);
}
