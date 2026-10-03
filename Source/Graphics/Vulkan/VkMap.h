#pragma once
#include "VkLoader.h"
#include <d3d11.h>

// D3D11 (Gfx 층의 설명 구조체 · DXGI 형식) → Vulkan 값.
//  뜻은 늘 D3D11 기준 (Gfx.h). 좌표 규칙: 셰이더가 -fvk-invert-y → 프레임버퍼 행 0 = D3D 의 위, 뷰포트 · 가위 숫자 그대로,
//  D3D 앞면 (시계 방향) = VK_FRONT_FACE_CLOCKWISE
namespace VkMap
{
	struct Format
	{
		VkFormat Vk = VK_FORMAT_UNDEFINED;   // UNDEFINED = 지원하지 않음
		VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_COLOR_BIT;   // 이미지 전체의 면 (깊이 + 스텐실 …)
		VkImageAspectFlags ViewAspect = VK_IMAGE_ASPECT_COLOR_BIT;   // 셰이더가 읽을 면 (R24_UNORM_X8 = 깊이, X24_G8 = 스텐실)
		UINT Bytes = 0;          // 화소당 (압축 = 4x4 블록당)
		bool Compressed = false;
		bool Depth = false, Stencil = false;
		bool Integer = false;
		bool Typeless = false;   // 다른 형식의 뷰가 있을 수 있다 (VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT)
		bool Srgb = false;
		VkComponentMapping Swizzle = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
	};
	// depthBind = D3D11_BIND_DEPTH_STENCIL 로 만든 텍스처 (R32_TYPELESS · R24G8_TYPELESS 를 깊이 형식으로)
	Format FromDxgi(DXGI_FORMAT format, bool depthBind = false);
	UINT64 RowBytes(const Format& f, UINT width);
	UINT64 SliceBytes(const Format& f, UINT width, UINT height);

	// ---- 상태
	VkCompareOp Compare(D3D11_COMPARISON_FUNC f);
	VkStencilOp StencilOp(D3D11_STENCIL_OP op);
	VkBlendFactor BlendFactor(D3D11_BLEND b, bool alpha);
	VkBlendOp BlendOp(D3D11_BLEND_OP op);
	VkCullModeFlags Cull(D3D11_CULL_MODE m);
	VkPrimitiveTopology Topology(D3D11_PRIMITIVE_TOPOLOGY t, uint32_t& patchPoints);
	bool IsStrip(D3D11_PRIMITIVE_TOPOLOGY t);
	VkSamplerCreateInfo Sampler(const D3D11_SAMPLER_DESC& d, float maxAnisotropy, bool mirrorClampToEdge);
}
