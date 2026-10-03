#include "pch.h"
#include "VkMap.h"

namespace VkMap
{
	Format FromDxgi(DXGI_FORMAT format, bool depthBind)
	{
		Format f;
		auto set = [&](VkFormat vk, UINT bytes) { f.Vk = vk; f.Bytes = bytes; };
		auto depth = [&](VkFormat vk, UINT bytes, bool stencil, VkImageAspectFlags viewAspect) {
			f.Vk = vk;
			f.Bytes = bytes;
			f.Depth = true;
			f.Stencil = stencil;
			f.Aspect = VK_IMAGE_ASPECT_DEPTH_BIT | (stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
			f.ViewAspect = viewAspect;
		};
		auto bc = [&](VkFormat vk, UINT bytes) { f.Vk = vk; f.Bytes = bytes; f.Compressed = true; };
		switch (format)
		{
		case DXGI_FORMAT_R32G32B32A32_TYPELESS: f.Typeless = true; set(VK_FORMAT_R32G32B32A32_SFLOAT, 16); break;
		case DXGI_FORMAT_R32G32B32A32_FLOAT: set(VK_FORMAT_R32G32B32A32_SFLOAT, 16); break;
		case DXGI_FORMAT_R32G32B32A32_UINT: set(VK_FORMAT_R32G32B32A32_UINT, 16); f.Integer = true; break;
		case DXGI_FORMAT_R32G32B32A32_SINT: set(VK_FORMAT_R32G32B32A32_SINT, 16); f.Integer = true; break;
		case DXGI_FORMAT_R32G32B32_TYPELESS: f.Typeless = true; set(VK_FORMAT_R32G32B32_SFLOAT, 12); break;
		case DXGI_FORMAT_R32G32B32_FLOAT: set(VK_FORMAT_R32G32B32_SFLOAT, 12); break;
		case DXGI_FORMAT_R32G32B32_UINT: set(VK_FORMAT_R32G32B32_UINT, 12); f.Integer = true; break;
		case DXGI_FORMAT_R32G32B32_SINT: set(VK_FORMAT_R32G32B32_SINT, 12); f.Integer = true; break;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS: f.Typeless = true; set(VK_FORMAT_R16G16B16A16_SFLOAT, 8); break;
		case DXGI_FORMAT_R16G16B16A16_FLOAT: set(VK_FORMAT_R16G16B16A16_SFLOAT, 8); break;
		case DXGI_FORMAT_R16G16B16A16_UNORM: set(VK_FORMAT_R16G16B16A16_UNORM, 8); break;
		case DXGI_FORMAT_R16G16B16A16_SNORM: set(VK_FORMAT_R16G16B16A16_SNORM, 8); break;
		case DXGI_FORMAT_R16G16B16A16_UINT: set(VK_FORMAT_R16G16B16A16_UINT, 8); f.Integer = true; break;
		case DXGI_FORMAT_R16G16B16A16_SINT: set(VK_FORMAT_R16G16B16A16_SINT, 8); f.Integer = true; break;
		case DXGI_FORMAT_R32G32_TYPELESS: f.Typeless = true; set(VK_FORMAT_R32G32_SFLOAT, 8); break;
		case DXGI_FORMAT_R32G32_FLOAT: set(VK_FORMAT_R32G32_SFLOAT, 8); break;
		case DXGI_FORMAT_R32G32_UINT: set(VK_FORMAT_R32G32_UINT, 8); f.Integer = true; break;
		case DXGI_FORMAT_R32G32_SINT: set(VK_FORMAT_R32G32_SINT, 8); f.Integer = true; break;
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: depth(VK_FORMAT_D32_SFLOAT_S8_UINT, 8, true, VK_IMAGE_ASPECT_DEPTH_BIT); break;
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: depth(VK_FORMAT_D32_SFLOAT_S8_UINT, 8, true, VK_IMAGE_ASPECT_DEPTH_BIT); break;
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT: depth(VK_FORMAT_D32_SFLOAT_S8_UINT, 8, true, VK_IMAGE_ASPECT_STENCIL_BIT); f.Integer = true; break;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS: f.Typeless = true; set(VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4); break;
		case DXGI_FORMAT_R10G10B10A2_UNORM: set(VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4); break;
		case DXGI_FORMAT_R10G10B10A2_UINT: set(VK_FORMAT_A2B10G10R10_UINT_PACK32, 4); f.Integer = true; break;
		case DXGI_FORMAT_R11G11B10_FLOAT: set(VK_FORMAT_B10G11R11_UFLOAT_PACK32, 4); break;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS: f.Typeless = true; set(VK_FORMAT_R8G8B8A8_UNORM, 4); break;
		case DXGI_FORMAT_R8G8B8A8_UNORM: set(VK_FORMAT_R8G8B8A8_UNORM, 4); break;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: set(VK_FORMAT_R8G8B8A8_SRGB, 4); f.Srgb = true; break;
		case DXGI_FORMAT_R8G8B8A8_UINT: set(VK_FORMAT_R8G8B8A8_UINT, 4); f.Integer = true; break;
		case DXGI_FORMAT_R8G8B8A8_SNORM: set(VK_FORMAT_R8G8B8A8_SNORM, 4); break;
		case DXGI_FORMAT_R8G8B8A8_SINT: set(VK_FORMAT_R8G8B8A8_SINT, 4); f.Integer = true; break;
		case DXGI_FORMAT_R16G16_TYPELESS: f.Typeless = true; set(VK_FORMAT_R16G16_SFLOAT, 4); break;
		case DXGI_FORMAT_R16G16_FLOAT: set(VK_FORMAT_R16G16_SFLOAT, 4); break;
		case DXGI_FORMAT_R16G16_UNORM: set(VK_FORMAT_R16G16_UNORM, 4); break;
		case DXGI_FORMAT_R16G16_SNORM: set(VK_FORMAT_R16G16_SNORM, 4); break;
		case DXGI_FORMAT_R16G16_UINT: set(VK_FORMAT_R16G16_UINT, 4); f.Integer = true; break;
		case DXGI_FORMAT_R16G16_SINT: set(VK_FORMAT_R16G16_SINT, 4); f.Integer = true; break;
		case DXGI_FORMAT_R32_TYPELESS:
			if (depthBind) depth(VK_FORMAT_D32_SFLOAT, 4, false, VK_IMAGE_ASPECT_DEPTH_BIT);
			else { f.Typeless = true; set(VK_FORMAT_R32_SFLOAT, 4); }
			break;
		case DXGI_FORMAT_D32_FLOAT: depth(VK_FORMAT_D32_SFLOAT, 4, false, VK_IMAGE_ASPECT_DEPTH_BIT); break;
		case DXGI_FORMAT_R32_FLOAT: set(VK_FORMAT_R32_SFLOAT, 4); break;
		case DXGI_FORMAT_R32_UINT: set(VK_FORMAT_R32_UINT, 4); f.Integer = true; break;
		case DXGI_FORMAT_R32_SINT: set(VK_FORMAT_R32_SINT, 4); f.Integer = true; break;
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT: depth(VK_FORMAT_D24_UNORM_S8_UINT, 4, true, VK_IMAGE_ASPECT_DEPTH_BIT); break;
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: depth(VK_FORMAT_D24_UNORM_S8_UINT, 4, true, VK_IMAGE_ASPECT_DEPTH_BIT); break;
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT: depth(VK_FORMAT_D24_UNORM_S8_UINT, 4, true, VK_IMAGE_ASPECT_STENCIL_BIT); f.Integer = true; break;
		case DXGI_FORMAT_R8G8_TYPELESS: f.Typeless = true; set(VK_FORMAT_R8G8_UNORM, 2); break;
		case DXGI_FORMAT_R8G8_UNORM: set(VK_FORMAT_R8G8_UNORM, 2); break;
		case DXGI_FORMAT_R8G8_UINT: set(VK_FORMAT_R8G8_UINT, 2); f.Integer = true; break;
		case DXGI_FORMAT_R8G8_SNORM: set(VK_FORMAT_R8G8_SNORM, 2); break;
		case DXGI_FORMAT_R8G8_SINT: set(VK_FORMAT_R8G8_SINT, 2); f.Integer = true; break;
		case DXGI_FORMAT_R16_TYPELESS:
			if (depthBind) depth(VK_FORMAT_D16_UNORM, 2, false, VK_IMAGE_ASPECT_DEPTH_BIT);
			else { f.Typeless = true; set(VK_FORMAT_R16_SFLOAT, 2); }
			break;
		case DXGI_FORMAT_D16_UNORM: depth(VK_FORMAT_D16_UNORM, 2, false, VK_IMAGE_ASPECT_DEPTH_BIT); break;
		case DXGI_FORMAT_R16_FLOAT: set(VK_FORMAT_R16_SFLOAT, 2); break;
		case DXGI_FORMAT_R16_UNORM: set(VK_FORMAT_R16_UNORM, 2); break;
		case DXGI_FORMAT_R16_UINT: set(VK_FORMAT_R16_UINT, 2); f.Integer = true; break;
		case DXGI_FORMAT_R16_SNORM: set(VK_FORMAT_R16_SNORM, 2); break;
		case DXGI_FORMAT_R16_SINT: set(VK_FORMAT_R16_SINT, 2); f.Integer = true; break;
		case DXGI_FORMAT_R8_TYPELESS: f.Typeless = true; set(VK_FORMAT_R8_UNORM, 1); break;
		case DXGI_FORMAT_R8_UNORM: set(VK_FORMAT_R8_UNORM, 1); break;
		case DXGI_FORMAT_R8_UINT: set(VK_FORMAT_R8_UINT, 1); f.Integer = true; break;
		case DXGI_FORMAT_R8_SNORM: set(VK_FORMAT_R8_SNORM, 1); break;
		case DXGI_FORMAT_R8_SINT: set(VK_FORMAT_R8_SINT, 1); f.Integer = true; break;
		case DXGI_FORMAT_A8_UNORM:
			set(VK_FORMAT_R8_UNORM, 1);
			f.Swizzle = { VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_R };
			break;
		case DXGI_FORMAT_R9G9B9E5_SHAREDEXP: set(VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, 4); break;
		case DXGI_FORMAT_BC1_TYPELESS: f.Typeless = true; bc(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 8); break;
		case DXGI_FORMAT_BC1_UNORM: bc(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 8); break;
		case DXGI_FORMAT_BC1_UNORM_SRGB: bc(VK_FORMAT_BC1_RGBA_SRGB_BLOCK, 8); f.Srgb = true; break;
		case DXGI_FORMAT_BC2_TYPELESS: f.Typeless = true; bc(VK_FORMAT_BC2_UNORM_BLOCK, 16); break;
		case DXGI_FORMAT_BC2_UNORM: bc(VK_FORMAT_BC2_UNORM_BLOCK, 16); break;
		case DXGI_FORMAT_BC2_UNORM_SRGB: bc(VK_FORMAT_BC2_SRGB_BLOCK, 16); f.Srgb = true; break;
		case DXGI_FORMAT_BC3_TYPELESS: f.Typeless = true; bc(VK_FORMAT_BC3_UNORM_BLOCK, 16); break;
		case DXGI_FORMAT_BC3_UNORM: bc(VK_FORMAT_BC3_UNORM_BLOCK, 16); break;
		case DXGI_FORMAT_BC3_UNORM_SRGB: bc(VK_FORMAT_BC3_SRGB_BLOCK, 16); f.Srgb = true; break;
		case DXGI_FORMAT_BC4_TYPELESS: f.Typeless = true; bc(VK_FORMAT_BC4_UNORM_BLOCK, 8); break;
		case DXGI_FORMAT_BC4_UNORM: bc(VK_FORMAT_BC4_UNORM_BLOCK, 8); break;
		case DXGI_FORMAT_BC4_SNORM: bc(VK_FORMAT_BC4_SNORM_BLOCK, 8); break;
		case DXGI_FORMAT_BC5_TYPELESS: f.Typeless = true; bc(VK_FORMAT_BC5_UNORM_BLOCK, 16); break;
		case DXGI_FORMAT_BC5_UNORM: bc(VK_FORMAT_BC5_UNORM_BLOCK, 16); break;
		case DXGI_FORMAT_BC5_SNORM: bc(VK_FORMAT_BC5_SNORM_BLOCK, 16); break;
		case DXGI_FORMAT_BC6H_TYPELESS: f.Typeless = true; bc(VK_FORMAT_BC6H_UFLOAT_BLOCK, 16); break;
		case DXGI_FORMAT_BC6H_UF16: bc(VK_FORMAT_BC6H_UFLOAT_BLOCK, 16); break;
		case DXGI_FORMAT_BC6H_SF16: bc(VK_FORMAT_BC6H_SFLOAT_BLOCK, 16); break;
		case DXGI_FORMAT_BC7_TYPELESS: f.Typeless = true; bc(VK_FORMAT_BC7_UNORM_BLOCK, 16); break;
		case DXGI_FORMAT_BC7_UNORM: bc(VK_FORMAT_BC7_UNORM_BLOCK, 16); break;
		case DXGI_FORMAT_BC7_UNORM_SRGB: bc(VK_FORMAT_BC7_SRGB_BLOCK, 16); f.Srgb = true; break;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS: f.Typeless = true; set(VK_FORMAT_B8G8R8A8_UNORM, 4); break;
		case DXGI_FORMAT_B8G8R8A8_UNORM: set(VK_FORMAT_B8G8R8A8_UNORM, 4); break;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: set(VK_FORMAT_B8G8R8A8_SRGB, 4); f.Srgb = true; break;
		case DXGI_FORMAT_B8G8R8X8_TYPELESS: f.Typeless = true; [[fallthrough]];
		case DXGI_FORMAT_B8G8R8X8_UNORM:
			set(VK_FORMAT_B8G8R8A8_UNORM, 4);
			f.Swizzle.a = VK_COMPONENT_SWIZZLE_ONE;
			break;
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			set(VK_FORMAT_B8G8R8A8_SRGB, 4);
			f.Srgb = true;
			f.Swizzle.a = VK_COMPONENT_SWIZZLE_ONE;
			break;
		case DXGI_FORMAT_B5G6R5_UNORM: set(VK_FORMAT_R5G6B5_UNORM_PACK16, 2); break;
		case DXGI_FORMAT_B5G5R5A1_UNORM: set(VK_FORMAT_A1R5G5B5_UNORM_PACK16, 2); break;
		case DXGI_FORMAT_B4G4R4A4_UNORM: set(VK_FORMAT_A4R4G4B4_UNORM_PACK16, 2); break;
		default: break;
		}
		return f;
	}

	UINT64 RowBytes(const Format& f, UINT width)
	{
		if (f.Compressed) return (UINT64)(std::max)(1u, (width + 3) / 4) * f.Bytes;
		return (UINT64)width * f.Bytes;
	}

	UINT64 SliceBytes(const Format& f, UINT width, UINT height)
	{
		if (f.Compressed) return RowBytes(f, width) * (std::max)(1u, (height + 3) / 4);
		return RowBytes(f, width) * height;
	}

	VkCompareOp Compare(D3D11_COMPARISON_FUNC f)
	{
		switch (f)
		{
		case D3D11_COMPARISON_NEVER: return VK_COMPARE_OP_NEVER;
		case D3D11_COMPARISON_LESS: return VK_COMPARE_OP_LESS;
		case D3D11_COMPARISON_EQUAL: return VK_COMPARE_OP_EQUAL;
		case D3D11_COMPARISON_LESS_EQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
		case D3D11_COMPARISON_GREATER: return VK_COMPARE_OP_GREATER;
		case D3D11_COMPARISON_NOT_EQUAL: return VK_COMPARE_OP_NOT_EQUAL;
		case D3D11_COMPARISON_GREATER_EQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
		default: return VK_COMPARE_OP_ALWAYS;
		}
	}

	VkStencilOp StencilOp(D3D11_STENCIL_OP op)
	{
		switch (op)
		{
		case D3D11_STENCIL_OP_ZERO: return VK_STENCIL_OP_ZERO;
		case D3D11_STENCIL_OP_REPLACE: return VK_STENCIL_OP_REPLACE;
		case D3D11_STENCIL_OP_INCR_SAT: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
		case D3D11_STENCIL_OP_DECR_SAT: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
		case D3D11_STENCIL_OP_INVERT: return VK_STENCIL_OP_INVERT;
		case D3D11_STENCIL_OP_INCR: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
		case D3D11_STENCIL_OP_DECR: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
		default: return VK_STENCIL_OP_KEEP;
		}
	}

	VkBlendFactor BlendFactor(D3D11_BLEND b, bool alpha)
	{
		switch (b)
		{
		case D3D11_BLEND_ZERO: return VK_BLEND_FACTOR_ZERO;
		case D3D11_BLEND_ONE: return VK_BLEND_FACTOR_ONE;
		case D3D11_BLEND_SRC_COLOR: return VK_BLEND_FACTOR_SRC_COLOR;
		case D3D11_BLEND_INV_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case D3D11_BLEND_SRC_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
		case D3D11_BLEND_INV_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case D3D11_BLEND_DEST_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
		case D3D11_BLEND_INV_DEST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		case D3D11_BLEND_DEST_COLOR: return VK_BLEND_FACTOR_DST_COLOR;
		case D3D11_BLEND_INV_DEST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		case D3D11_BLEND_SRC_ALPHA_SAT: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
		// D3D 의 BLEND_FACTOR 는 색 · 알파 모두 상수 색 (알파 칸에서는 상수의 a)
		case D3D11_BLEND_BLEND_FACTOR: return alpha ? VK_BLEND_FACTOR_CONSTANT_ALPHA : VK_BLEND_FACTOR_CONSTANT_COLOR;
		case D3D11_BLEND_INV_BLEND_FACTOR: return alpha ? VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA : VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
		case D3D11_BLEND_SRC1_COLOR: return VK_BLEND_FACTOR_SRC1_COLOR;
		case D3D11_BLEND_INV_SRC1_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
		case D3D11_BLEND_SRC1_ALPHA: return VK_BLEND_FACTOR_SRC1_ALPHA;
		case D3D11_BLEND_INV_SRC1_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
		default: return VK_BLEND_FACTOR_ONE;
		}
	}

	VkBlendOp BlendOp(D3D11_BLEND_OP op)
	{
		switch (op)
		{
		case D3D11_BLEND_OP_SUBTRACT: return VK_BLEND_OP_SUBTRACT;
		case D3D11_BLEND_OP_REV_SUBTRACT: return VK_BLEND_OP_REVERSE_SUBTRACT;
		case D3D11_BLEND_OP_MIN: return VK_BLEND_OP_MIN;
		case D3D11_BLEND_OP_MAX: return VK_BLEND_OP_MAX;
		default: return VK_BLEND_OP_ADD;
		}
	}

	VkCullModeFlags Cull(D3D11_CULL_MODE m)
	{
		switch (m)
		{
		case D3D11_CULL_FRONT: return VK_CULL_MODE_FRONT_BIT;
		case D3D11_CULL_BACK: return VK_CULL_MODE_BACK_BIT;
		default: return VK_CULL_MODE_NONE;
		}
	}

	VkPrimitiveTopology Topology(D3D11_PRIMITIVE_TOPOLOGY t, uint32_t& patchPoints)
	{
		patchPoints = 0;
		if (t >= D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST && t <= D3D11_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST)
		{
			patchPoints = (uint32_t)(t - D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1);
			return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
		}
		switch (t)
		{
		case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		case D3D11_PRIMITIVE_TOPOLOGY_LINELIST: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
		case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		case D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
		case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
		case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
		case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
		default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		}
	}

	bool IsStrip(D3D11_PRIMITIVE_TOPOLOGY t)
	{
		return t == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP || t == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP ||
			t == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ || t == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
	}

	namespace
	{
		VkSamplerAddressMode Address(D3D11_TEXTURE_ADDRESS_MODE m, bool mirrorClamp)
		{
			switch (m)
			{
			case D3D11_TEXTURE_ADDRESS_MIRROR: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			case D3D11_TEXTURE_ADDRESS_CLAMP: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			case D3D11_TEXTURE_ADDRESS_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			case D3D11_TEXTURE_ADDRESS_MIRROR_ONCE: return mirrorClamp ? VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
			}
		}
	}

	VkSamplerCreateInfo Sampler(const D3D11_SAMPLER_DESC& d, float maxAnisotropy, bool mirrorClampToEdge)
	{
		VkSamplerCreateInfo s = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		const UINT f = (UINT)d.Filter;
		const bool anisotropic = (f & 0x40) != 0;
		s.minFilter = anisotropic || ((f >> 4) & 1) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		s.magFilter = anisotropic || ((f >> 2) & 1) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		s.mipmapMode = anisotropic || (f & 1) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
		s.addressModeU = Address(d.AddressU, mirrorClampToEdge);
		s.addressModeV = Address(d.AddressV, mirrorClampToEdge);
		s.addressModeW = Address(d.AddressW, mirrorClampToEdge);
		s.mipLodBias = d.MipLODBias;
		s.anisotropyEnable = anisotropic && d.MaxAnisotropy > 1 && maxAnisotropy > 1 ? VK_TRUE : VK_FALSE;
		s.maxAnisotropy = s.anisotropyEnable ? (std::min)((float)d.MaxAnisotropy, maxAnisotropy) : 1.0f;
		s.compareEnable = (f & 0x180) == 0x80 ? VK_TRUE : VK_FALSE;
		s.compareOp = s.compareEnable ? Compare(d.ComparisonFunc) : VK_COMPARE_OP_NEVER;
		s.minLod = d.MinLOD;
		s.maxLod = d.MaxLOD >= 1000.0f ? VK_LOD_CLAMP_NONE : d.MaxLOD;
		if (s.maxLod < s.minLod) s.maxLod = s.minLod;
		// 테두리 색: Vulkan 기본은 세 가지뿐 (가장 가까운 것)
		const float* c = d.BorderColor;
		const bool rgbZero = c[0] == 0 && c[1] == 0 && c[2] == 0;
		if (rgbZero && c[3] == 0) s.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		else if (rgbZero) s.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		else s.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		return s;
	}
}
