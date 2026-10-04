#include "pch.h"
#include "GLESState.h"
#include "FxStates.h"
#include "MobileTextureFormats.h"

namespace GLESState
{
	Format FromDxgi(DXGI_FORMAT f, bool depthBind)
	{
		Format r;
		// 모바일 압축 (nova android export 가 구운 DDS): ASTC 0x93B0 + 블록 (sRGB 0x93D0 +), ETC2 · EAC (ES 3.0 기본)
		if (const int ai = MobileTex::AstcBlockIndex((unsigned)f); ai >= 0)
		{
			r.Internal = (MobileTex::AstcSrgb((unsigned)f) ? 0x93D0u : 0x93B0u) + (unsigned)ai;
			r.BlockBytes = 16;
			r.BlockW = (UINT)MobileTex::kAstcBlocks[ai].X;
			r.BlockH = (UINT)MobileTex::kAstcBlocks[ai].Y;
			return r;
		}
		switch ((unsigned)f)
		{
		case MobileTex::kEtc2RGB8: r.Internal = GL_COMPRESSED_RGB8_ETC2; r.BlockBytes = 8; return r;
		case MobileTex::kEtc2SRGB8: r.Internal = GL_COMPRESSED_SRGB8_ETC2; r.BlockBytes = 8; return r;
		case MobileTex::kEtc2RGBA8: r.Internal = GL_COMPRESSED_RGBA8_ETC2_EAC; r.BlockBytes = 16; return r;
		case MobileTex::kEtc2SRGB8A8: r.Internal = GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC; r.BlockBytes = 16; return r;
		default: break;
		}
		auto set = [&](GLenum i, GLenum u, GLenum t, UINT bits) { r.Internal = i; r.Upload = u; r.Type = t; r.Bits = bits; };
		auto bc = [&](GLenum i, UINT block) { r.Internal = i; r.BlockBytes = block; };
		auto unorm16 = [&](GLenum i, GLenum u, UINT channels) { set(i, u, GL_FLOAT, 16 * channels); r.Conv = Convert::Unorm16ToFloat; r.Channels = channels; };
		switch (f)
		{
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM: set(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 32); break;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: set(GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 32); break;
		// ES 에는 BGRA 텍스처가 없다 (확장 없음) → RGBA 로, 올릴 때 R · B 자리 바꿈
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM: set(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 32); r.Conv = Convert::SwapRB; break;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: set(GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 32); r.Conv = Convert::SwapRB; break;
		case DXGI_FORMAT_R8G8B8A8_UINT: set(GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, 32); r.Integer = true; break;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_FLOAT: set(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 64); break;
		case DXGI_FORMAT_R16G16B16A16_UINT: set(GL_RGBA16UI, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, 64); r.Integer = true; break;
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
		case DXGI_FORMAT_R32G32B32A32_FLOAT: set(GL_RGBA32F, GL_RGBA, GL_FLOAT, 128); break;
		case DXGI_FORMAT_R32G32B32A32_UINT: set(GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, 128); r.Integer = true; break;
		case DXGI_FORMAT_R32G32B32_FLOAT: set(GL_RGB32F, GL_RGB, GL_FLOAT, 96); break;
		case DXGI_FORMAT_R32G32_TYPELESS:
		case DXGI_FORMAT_R32G32_FLOAT: set(GL_RG32F, GL_RG, GL_FLOAT, 64); break;
		case DXGI_FORMAT_R32G32_UINT: set(GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT, 64); r.Integer = true; break;
		case DXGI_FORMAT_R16G16_TYPELESS:
		case DXGI_FORMAT_R16G16_FLOAT: set(GL_RG16F, GL_RG, GL_HALF_FLOAT, 32); break;
		case DXGI_FORMAT_R16_FLOAT: set(GL_R16F, GL_RED, GL_HALF_FLOAT, 16); break;
		case DXGI_FORMAT_R16_UINT: set(GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, 16); r.Integer = true; break;
		// 16 비트 UNORM 은 ES 에 없다 → 32 비트 float (값 그대로 0..1)
		case DXGI_FORMAT_R16G16B16A16_UNORM: unorm16(GL_RGBA32F, GL_RGBA, 4); break;
		case DXGI_FORMAT_R16G16_UNORM: unorm16(GL_RG32F, GL_RG, 2); break;
		case DXGI_FORMAT_R16_UNORM: unorm16(GL_R32F, GL_RED, 1); break;
		case DXGI_FORMAT_R8G8_TYPELESS:
		case DXGI_FORMAT_R8G8_UNORM: set(GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 16); break;
		case DXGI_FORMAT_R8_TYPELESS:
		case DXGI_FORMAT_R8_UNORM: set(GL_R8, GL_RED, GL_UNSIGNED_BYTE, 8); break;
		case DXGI_FORMAT_A8_UNORM: set(GL_R8, GL_RED, GL_UNSIGNED_BYTE, 8); break;
		case DXGI_FORMAT_R8_UINT: set(GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, 8); r.Integer = true; break;
		case DXGI_FORMAT_R11G11B10_FLOAT: set(GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV, 32); break;
		case DXGI_FORMAT_R9G9B9E5_SHAREDEXP: set(GL_RGB9_E5, GL_RGB, GL_UNSIGNED_INT_5_9_9_9_REV, 32); break;   // HDR 하늘 큐브맵
		case DXGI_FORMAT_R8G8B8A8_SNORM: set(GL_RGBA8_SNORM, GL_RGBA, GL_BYTE, 32); break;
		case DXGI_FORMAT_R8G8_SNORM: set(GL_RG8_SNORM, GL_RG, GL_BYTE, 16); break;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_UNORM: set(GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, 32); break;
		case DXGI_FORMAT_R32_UINT: set(GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, 32); r.Integer = true; break;
		case DXGI_FORMAT_R32_SINT: set(GL_R32I, GL_RED_INTEGER, GL_INT, 32); r.Integer = true; break;
		case DXGI_FORMAT_R32_FLOAT: set(GL_R32F, GL_RED, GL_FLOAT, 32); break;
		case DXGI_FORMAT_R32_TYPELESS:
			if (depthBind) { set(GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 32); r.Depth = true; }
			else set(GL_R32F, GL_RED, GL_FLOAT, 32);
			break;
		case DXGI_FORMAT_D32_FLOAT: set(GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 32); r.Depth = true; break;
		case DXGI_FORMAT_R16_TYPELESS:
			if (depthBind) { set(GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, 16); r.Depth = true; }
			else unorm16(GL_R32F, GL_RED, 1);
			break;
		case DXGI_FORMAT_D16_UNORM: set(GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, 16); r.Depth = true; break;
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT: set(GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 32); r.Depth = r.Stencil = true; break;
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: set(GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 64); r.Depth = r.Stencil = true; break;
		// BC: MuMu 의 GLES 가 S3TC · RGTC · BPTC 확장을 준다 (PC 에서 구운 DDS 를 그대로)
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM: bc(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 8); break;
		case DXGI_FORMAT_BC1_UNORM_SRGB: bc(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT, 8); break;
		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM: bc(GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 16); break;
		case DXGI_FORMAT_BC2_UNORM_SRGB: bc(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT, 16); break;
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM: bc(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 16); break;
		case DXGI_FORMAT_BC3_UNORM_SRGB: bc(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, 16); break;
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM: bc(GL_COMPRESSED_RED_RGTC1_EXT, 8); break;
		case DXGI_FORMAT_BC4_SNORM: bc(GL_COMPRESSED_SIGNED_RED_RGTC1_EXT, 8); break;
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM: bc(GL_COMPRESSED_RED_GREEN_RGTC2_EXT, 16); break;
		case DXGI_FORMAT_BC5_SNORM: bc(GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT, 16); break;
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16: bc(GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_EXT, 16); break;
		case DXGI_FORMAT_BC6H_SF16: bc(GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_EXT, 16); break;
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM: bc(GL_COMPRESSED_RGBA_BPTC_UNORM_EXT, 16); break;
		case DXGI_FORMAT_BC7_UNORM_SRGB: bc(GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_EXT, 16); break;
		default: break;
		}
		return r;
	}

	UINT64 RowBytes(const Format& f, UINT width)
	{
		if (f.BlockBytes) return (UINT64)(std::max)(1u, (width + f.BlockW - 1) / f.BlockW) * f.BlockBytes;
		return (UINT64)width * f.Bits / 8;
	}

	UINT64 SliceBytes(const Format& f, UINT width, UINT height)
	{
		if (f.BlockBytes) return RowBytes(f, width) * (std::max)(1u, (height + f.BlockH - 1) / f.BlockH);
		return RowBytes(f, width) * height;
	}

	UINT GLBytesPerPixel(const Format& f)
	{
		if (f.Conv == Convert::Unorm16ToFloat) return 4 * f.Channels;
		return (std::max)(1u, f.Bits / 8);
	}

	bool VertexFormat(DXGI_FORMAT f, GLint& size, GLenum& type, GLboolean& normalized, bool& integer, UINT& bytes)
	{
		normalized = GL_FALSE;
		integer = false;
		switch (f)
		{
		case DXGI_FORMAT_R32_FLOAT: size = 1; type = GL_FLOAT; bytes = 4; return true;
		case DXGI_FORMAT_R32G32_FLOAT: size = 2; type = GL_FLOAT; bytes = 8; return true;
		case DXGI_FORMAT_R32G32B32_FLOAT: size = 3; type = GL_FLOAT; bytes = 12; return true;
		case DXGI_FORMAT_R32G32B32A32_FLOAT: size = 4; type = GL_FLOAT; bytes = 16; return true;
		case DXGI_FORMAT_R16G16_FLOAT: size = 2; type = GL_HALF_FLOAT; bytes = 4; return true;
		case DXGI_FORMAT_R16G16B16A16_FLOAT: size = 4; type = GL_HALF_FLOAT; bytes = 8; return true;
		case DXGI_FORMAT_R8G8B8A8_UNORM: size = 4; type = GL_UNSIGNED_BYTE; normalized = GL_TRUE; bytes = 4; return true;
		case DXGI_FORMAT_R8G8B8A8_UINT: size = 4; type = GL_UNSIGNED_BYTE; integer = true; bytes = 4; return true;
		case DXGI_FORMAT_R16G16_SINT: size = 2; type = GL_SHORT; integer = true; bytes = 4; return true;
		case DXGI_FORMAT_R32_UINT: size = 1; type = GL_UNSIGNED_INT; integer = true; bytes = 4; return true;
		case DXGI_FORMAT_R32_SINT: size = 1; type = GL_INT; integer = true; bytes = 4; return true;
		case DXGI_FORMAT_R32G32_UINT: size = 2; type = GL_UNSIGNED_INT; integer = true; bytes = 8; return true;
		case DXGI_FORMAT_R32G32B32A32_UINT: size = 4; type = GL_UNSIGNED_INT; integer = true; bytes = 16; return true;
		case DXGI_FORMAT_R32G32B32A32_SINT: size = 4; type = GL_INT; integer = true; bytes = 16; return true;
		case DXGI_FORMAT_R16G16B16A16_UINT: size = 4; type = GL_UNSIGNED_SHORT; integer = true; bytes = 8; return true;
		default: return false;   // B8G8R8A8 정점 색: ES 에 BGRA 정점 형식이 없다
		}
	}

	GLenum Topology(D3D11_PRIMITIVE_TOPOLOGY t, GLint& patchVertices)
	{
		patchVertices = 0;
		switch (t)
		{
		case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST: return GL_POINTS;
		case D3D11_PRIMITIVE_TOPOLOGY_LINELIST: return GL_LINES;
		case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP: return GL_LINE_STRIP;
		case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP: return GL_TRIANGLE_STRIP;
		case D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ: return GL_LINES_ADJACENCY;
		case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ: return GL_LINE_STRIP_ADJACENCY;
		case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ: return GL_TRIANGLES_ADJACENCY;
		case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ: return GL_TRIANGLE_STRIP_ADJACENCY;
		default:
			if (t >= D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST && t <= D3D11_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST)
			{
				patchVertices = (GLint)(t - D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1);
				return GL_PATCHES;
			}
			return GL_TRIANGLES;
		}
	}

	GLenum Compare(D3D11_COMPARISON_FUNC f)
	{
		switch (f)
		{
		case D3D11_COMPARISON_NEVER: return GL_NEVER;
		case D3D11_COMPARISON_LESS: return GL_LESS;
		case D3D11_COMPARISON_EQUAL: return GL_EQUAL;
		case D3D11_COMPARISON_LESS_EQUAL: return GL_LEQUAL;
		case D3D11_COMPARISON_GREATER: return GL_GREATER;
		case D3D11_COMPARISON_NOT_EQUAL: return GL_NOTEQUAL;
		case D3D11_COMPARISON_GREATER_EQUAL: return GL_GEQUAL;
		default: return GL_ALWAYS;
		}
	}

	namespace
	{
		GLenum Blend(D3D11_BLEND b)
		{
			switch (b)
			{
			case D3D11_BLEND_ZERO: return GL_ZERO;
			case D3D11_BLEND_ONE: return GL_ONE;
			case D3D11_BLEND_SRC_COLOR: return GL_SRC_COLOR;
			case D3D11_BLEND_INV_SRC_COLOR: return GL_ONE_MINUS_SRC_COLOR;
			case D3D11_BLEND_SRC_ALPHA: return GL_SRC_ALPHA;
			case D3D11_BLEND_INV_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
			case D3D11_BLEND_DEST_ALPHA: return GL_DST_ALPHA;
			case D3D11_BLEND_INV_DEST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;
			case D3D11_BLEND_DEST_COLOR: return GL_DST_COLOR;
			case D3D11_BLEND_INV_DEST_COLOR: return GL_ONE_MINUS_DST_COLOR;
			case D3D11_BLEND_SRC_ALPHA_SAT: return GL_SRC_ALPHA_SATURATE;
			case D3D11_BLEND_BLEND_FACTOR: return GL_CONSTANT_COLOR;
			case D3D11_BLEND_INV_BLEND_FACTOR: return GL_ONE_MINUS_CONSTANT_COLOR;
			default: return GL_ONE;   // 이중 소스 블렌드는 ES 에 없다
			}
		}

		GLenum BlendOp(D3D11_BLEND_OP o)
		{
			switch (o)
			{
			case D3D11_BLEND_OP_SUBTRACT: return GL_FUNC_SUBTRACT;
			case D3D11_BLEND_OP_REV_SUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
			case D3D11_BLEND_OP_MIN: return GL_MIN;
			case D3D11_BLEND_OP_MAX: return GL_MAX;
			default: return GL_FUNC_ADD;
			}
		}

		GLenum StencilOp(D3D11_STENCIL_OP o)
		{
			switch (o)
			{
			case D3D11_STENCIL_OP_ZERO: return GL_ZERO;
			case D3D11_STENCIL_OP_REPLACE: return GL_REPLACE;
			case D3D11_STENCIL_OP_INCR_SAT: return GL_INCR;
			case D3D11_STENCIL_OP_DECR_SAT: return GL_DECR;
			case D3D11_STENCIL_OP_INVERT: return GL_INVERT;
			case D3D11_STENCIL_OP_INCR: return GL_INCR_WRAP;
			case D3D11_STENCIL_OP_DECR: return GL_DECR_WRAP;
			default: return GL_KEEP;
			}
		}

		GLenum Address(D3D11_TEXTURE_ADDRESS_MODE m)
		{
			switch (m)
			{
			case D3D11_TEXTURE_ADDRESS_WRAP: return GL_REPEAT;
			case D3D11_TEXTURE_ADDRESS_MIRROR: return GL_MIRRORED_REPEAT;
			case D3D11_TEXTURE_ADDRESS_BORDER: return GL_CLAMP_TO_BORDER;
			default: return GL_CLAMP_TO_EDGE;   // MIRROR_ONCE 는 ES 에 없다
			}
		}
	}

	// 채우기 모드(선) · 깊이 자르기 끄기(depth clamp) 는 ES 에 없다 → 무시
	void ApplyRasterizer(const D3D11_RASTERIZER_DESC& d)
	{
		if (d.CullMode == D3D11_CULL_NONE) glDisable(GL_CULL_FACE);
		else
		{
			glEnable(GL_CULL_FACE);
			glCullFace(d.CullMode == D3D11_CULL_FRONT ? GL_FRONT : GL_BACK);
		}
		glFrontFace(d.FrontCounterClockwise ? GL_CW : GL_CCW);   // 창 y = D3D 행 번호 → 감김 방향이 뒤집혀 보인다
		if (d.DepthBias != 0 || d.SlopeScaledDepthBias != 0.0f)
		{
			glEnable(GL_POLYGON_OFFSET_FILL);
			glPolygonOffset(d.SlopeScaledDepthBias, (GLfloat)d.DepthBias);
		}
		else glDisable(GL_POLYGON_OFFSET_FILL);
		if (d.ScissorEnable) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
	}

	void ApplyBlend(const D3D11_BLEND_DESC& d, const float factor[4], UINT sampleMask)
	{
		if (d.AlphaToCoverageEnable) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE); else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
		for (GLuint i = 0; i < 8; ++i)
		{
			const auto& rt = d.RenderTarget[d.IndependentBlendEnable ? i : 0];
			if (rt.BlendEnable)
			{
				glEnablei(GL_BLEND, i);
				glBlendFuncSeparatei(i, Blend(rt.SrcBlend), Blend(rt.DestBlend), Blend(rt.SrcBlendAlpha), Blend(rt.DestBlendAlpha));
				glBlendEquationSeparatei(i, BlendOp(rt.BlendOp), BlendOp(rt.BlendOpAlpha));
			}
			else glDisablei(GL_BLEND, i);
			const UINT8 m = rt.RenderTargetWriteMask;
			glColorMaski(i, (m & 1) != 0, (m & 2) != 0, (m & 4) != 0, (m & 8) != 0);
		}
		const float one[4] = { 1, 1, 1, 1 };
		const float* f = factor ? factor : one;
		glBlendColor(f[0], f[1], f[2], f[3]);
		if (sampleMask != 0xFFFFFFFF) { glEnable(GL_SAMPLE_MASK); glSampleMaski(0, sampleMask); }
		else glDisable(GL_SAMPLE_MASK);
	}

	void ApplyDepthStencil(const D3D11_DEPTH_STENCIL_DESC& d, UINT ref)
	{
		if (d.DepthEnable)
		{
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(Compare(d.DepthFunc));
			glDepthMask(d.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL ? GL_TRUE : GL_FALSE);
		}
		else
		{
			glDisable(GL_DEPTH_TEST);
			glDepthMask(GL_FALSE);
		}
		if (d.StencilEnable)
		{
			glEnable(GL_STENCIL_TEST);
			glStencilMask(d.StencilWriteMask);
			glStencilFuncSeparate(GL_FRONT, Compare(d.FrontFace.StencilFunc), (GLint)ref, d.StencilReadMask);
			glStencilOpSeparate(GL_FRONT, StencilOp(d.FrontFace.StencilFailOp), StencilOp(d.FrontFace.StencilDepthFailOp), StencilOp(d.FrontFace.StencilPassOp));
			glStencilFuncSeparate(GL_BACK, Compare(d.BackFace.StencilFunc), (GLint)ref, d.StencilReadMask);
			glStencilOpSeparate(GL_BACK, StencilOp(d.BackFace.StencilFailOp), StencilOp(d.BackFace.StencilDepthFailOp), StencilOp(d.BackFace.StencilPassOp));
		}
		else glDisable(GL_STENCIL_TEST);
	}

	GLuint CreateSampler(const D3D11_SAMPLER_DESC& d)
	{
		GLuint s = 0;
		glGenSamplers(1, &s);
		const UINT f = (UINT)d.Filter;
		const bool aniso = (f & 0x40) != 0;
		const bool minLinear = aniso || (f & 0x10), magLinear = aniso || (f & 0x4), mipLinear = aniso || (f & 0x1);
		glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, minLinear ? (mipLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST) : (mipLinear ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST));
		glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, magLinear ? GL_LINEAR : GL_NEAREST);
		glSamplerParameteri(s, GL_TEXTURE_WRAP_S, Address(d.AddressU));
		glSamplerParameteri(s, GL_TEXTURE_WRAP_T, Address(d.AddressV));
		glSamplerParameteri(s, GL_TEXTURE_WRAP_R, Address(d.AddressW));
		glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, (std::max)(d.MinLOD, -1000.0f));
		glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, (std::min)(d.MaxLOD, 1000.0f));
		glSamplerParameterfv(s, GL_TEXTURE_BORDER_COLOR, d.BorderColor);
		if (aniso && d.MaxAnisotropy > 1)
			glSamplerParameterf(s, GL_TEXTURE_MAX_ANISOTROPY_EXT, (float)d.MaxAnisotropy);
		if ((f & 0x180) == 0x80)
		{
			glSamplerParameteri(s, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
			glSamplerParameteri(s, GL_TEXTURE_COMPARE_FUNC, Compare(d.ComparisonFunc));
		}
		return s;
	}

	void ApplyDefaults()
	{
		ApplyRasterizer(FxStates::DefaultRasterizer());
		ApplyBlend(FxStates::DefaultBlend(), nullptr, 0xFFFFFFFF);
		ApplyDepthStencil(FxStates::DefaultDepthStencil(), 0);
		glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX);   // D3D 의 0xFFFF / 0xFFFFFFFF 끊기
	}
}
