#include "pch.h"
#include "GLState.h"
#include "FxStates.h"
#include <cfloat>

namespace
{

	GLenum GLCompare(D3D11_COMPARISON_FUNC f)
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

	GLenum GLBlend(D3D11_BLEND b)
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
		case D3D11_BLEND_SRC1_COLOR: return 0x88F9;           // GL_SRC1_COLOR
		case D3D11_BLEND_INV_SRC1_COLOR: return 0x88FA;       // GL_ONE_MINUS_SRC1_COLOR
		case D3D11_BLEND_SRC1_ALPHA: return 0x8589;           // GL_SRC1_ALPHA
		case D3D11_BLEND_INV_SRC1_ALPHA: return 0x88FB;       // GL_ONE_MINUS_SRC1_ALPHA
		default: return GL_ONE;
		}
	}

	GLenum GLBlendOp(D3D11_BLEND_OP o)
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

	GLenum GLStencilOp(D3D11_STENCIL_OP o)
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

	GLenum GLAddress(D3D11_TEXTURE_ADDRESS_MODE m)
	{
		switch (m)
		{
		case D3D11_TEXTURE_ADDRESS_WRAP: return GL_REPEAT;
		case D3D11_TEXTURE_ADDRESS_MIRROR: return GL_MIRRORED_REPEAT;
		case D3D11_TEXTURE_ADDRESS_BORDER: return GL_CLAMP_TO_BORDER;
		case D3D11_TEXTURE_ADDRESS_MIRROR_ONCE: return GL_MIRROR_CLAMP_TO_EDGE;
		default: return GL_CLAMP_TO_EDGE;
		}
	}

	void APIENTRY DebugCallback(GLenum, GLenum, GLuint, GLenum severity, GLsizei, const GLchar* message, const void*)
	{
		static std::mutex m;
		static std::set<std::string> seen;   // 같은 메시지는 한 번만 (프레임마다 수백 줄이 되지 않게)
		if (severity != GL_DEBUG_SEVERITY_HIGH && severity != GL_DEBUG_SEVERITY_MEDIUM)
			return;
		std::lock_guard<std::mutex> lock(m);
		if (seen.size() < 300 && seen.insert(message).second)
			EditorLog::Write("OpenGL", "%s", message);
	}
}

namespace GLState
{
	D3D11_RASTERIZER_DESC DefaultRasterizer() { return FxStates::DefaultRasterizer(); }
	D3D11_BLEND_DESC DefaultBlend() { return FxStates::DefaultBlend(); }
	D3D11_DEPTH_STENCIL_DESC DefaultDepthStencil() { return FxStates::DefaultDepthStencil(); }
	D3D11_SAMPLER_DESC DefaultSampler() { return FxStates::DefaultSampler(); }
	D3D11_RASTERIZER_DESC FxRasterizer(const FxParser::StateBlock& b) { return FxStates::Rasterizer(b); }
	D3D11_BLEND_DESC FxBlend(const FxParser::StateBlock& b) { return FxStates::Blend(b); }
	D3D11_DEPTH_STENCIL_DESC FxDepthStencil(const FxParser::StateBlock& b) { return FxStates::DepthStencil(b); }
	D3D11_SAMPLER_DESC FxSampler(const FxParser::StateBlock& b) { return FxStates::Sampler(b); }

	void ApplyRasterizer(const D3D11_RASTERIZER_DESC& d)
	{
		glPolygonMode(GL_FRONT_AND_BACK, d.FillMode == D3D11_FILL_WIREFRAME ? GL_LINE : GL_FILL);
		if (d.CullMode == D3D11_CULL_NONE)
			glDisable(GL_CULL_FACE);
		else
		{
			glEnable(GL_CULL_FACE);
			glCullFace(d.CullMode == D3D11_CULL_FRONT ? GL_FRONT : GL_BACK);
		}
		glFrontFace(d.FrontCounterClockwise ? GL_CW : GL_CCW);   // 창 y 가 D3D 행 번호라 시계/반시계가 뒤집혀 보인다
		if (d.DepthBias != 0 || d.SlopeScaledDepthBias != 0.0f)
		{
			glEnable(GL_POLYGON_OFFSET_FILL);
			glEnable(GL_POLYGON_OFFSET_LINE);
			glPolygonOffset(d.SlopeScaledDepthBias, (GLfloat)d.DepthBias);
		}
		else
		{
			glDisable(GL_POLYGON_OFFSET_FILL);
			glDisable(GL_POLYGON_OFFSET_LINE);
		}
		if (d.DepthClipEnable) glDisable(GL_DEPTH_CLAMP); else glEnable(GL_DEPTH_CLAMP);
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
				glBlendFuncSeparatei(i, GLBlend(rt.SrcBlend), GLBlend(rt.DestBlend), GLBlend(rt.SrcBlendAlpha), GLBlend(rt.DestBlendAlpha));
				glBlendEquationSeparatei(i, GLBlendOp(rt.BlendOp), GLBlendOp(rt.BlendOpAlpha));
			}
			else
				glDisablei(GL_BLEND, i);
			const UINT8 m = rt.RenderTargetWriteMask;
			glColorMaski(i, (m & 1) != 0, (m & 2) != 0, (m & 4) != 0, (m & 8) != 0);
		}
		const float one[4] = { 1, 1, 1, 1 };
		const float* f = factor ? factor : one;
		glBlendColor(f[0], f[1], f[2], f[3]);
		glSampleMaski(0, sampleMask);
	}

	void ApplyDepthStencil(const D3D11_DEPTH_STENCIL_DESC& d, UINT ref)
	{
		if (d.DepthEnable)
		{
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GLCompare(d.DepthFunc));
			glDepthMask(d.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL ? GL_TRUE : GL_FALSE);
		}
		else
		{
			glDisable(GL_DEPTH_TEST);   // D3D: 깊이 끔 = 비교도 쓰기도 없음
			glDepthMask(GL_FALSE);
		}
		if (d.StencilEnable)
		{
			glEnable(GL_STENCIL_TEST);
			glStencilMask(d.StencilWriteMask);
			glStencilFuncSeparate(GL_FRONT, GLCompare(d.FrontFace.StencilFunc), (GLint)ref, d.StencilReadMask);
			glStencilOpSeparate(GL_FRONT, GLStencilOp(d.FrontFace.StencilFailOp), GLStencilOp(d.FrontFace.StencilDepthFailOp), GLStencilOp(d.FrontFace.StencilPassOp));
			glStencilFuncSeparate(GL_BACK, GLCompare(d.BackFace.StencilFunc), (GLint)ref, d.StencilReadMask);
			glStencilOpSeparate(GL_BACK, GLStencilOp(d.BackFace.StencilFailOp), GLStencilOp(d.BackFace.StencilDepthFailOp), GLStencilOp(d.BackFace.StencilPassOp));
		}
		else
			glDisable(GL_STENCIL_TEST);
	}

	GLuint CreateSampler(const D3D11_SAMPLER_DESC& d)
	{
		GLuint s = 0;
		glCreateSamplers(1, &s);
		const UINT f = (UINT)d.Filter;
		const bool aniso = (f & 0x7F) == 0x55;
		const bool minLinear = aniso || (f & 0x10), magLinear = aniso || (f & 0x4), mipLinear = aniso || (f & 0x1);
		glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, minLinear ? (mipLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST) : (mipLinear ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST));
		glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, magLinear ? GL_LINEAR : GL_NEAREST);
		if (aniso) glSamplerParameterf(s, GL_TEXTURE_MAX_ANISOTROPY, (GLfloat)(std::max)(1u, d.MaxAnisotropy));
		glSamplerParameteri(s, GL_TEXTURE_WRAP_S, GLAddress(d.AddressU));
		glSamplerParameteri(s, GL_TEXTURE_WRAP_T, GLAddress(d.AddressV));
		glSamplerParameteri(s, GL_TEXTURE_WRAP_R, GLAddress(d.AddressW));
		glSamplerParameterfv(s, GL_TEXTURE_BORDER_COLOR, d.BorderColor);
		glSamplerParameterf(s, GL_TEXTURE_LOD_BIAS, d.MipLODBias);
		glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, (std::max)(d.MinLOD, -1000.0f));
		glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, (std::min)(d.MaxLOD, 1000.0f));
		if (f & 0x80)
		{
			glSamplerParameteri(s, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
			glSamplerParameteri(s, GL_TEXTURE_COMPARE_FUNC, GLCompare(d.ComparisonFunc));
		}
		return s;
	}

	void ApplyDefaults()
	{
		const D3D11_RASTERIZER_DESC r = DefaultRasterizer();
		const D3D11_BLEND_DESC b = DefaultBlend();
		const D3D11_DEPTH_STENCIL_DESC ds = DefaultDepthStencil();
		ApplyRasterizer(r);
		ApplyBlend(b, nullptr, 0xFFFFFFFF);
		ApplyDepthStencil(ds, 0);
		glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);   // D3D: nointerpolation 값은 첫 정점
		glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);          // D3D 큐브 샘플링은 면 경계가 이어진다
		glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX);      // D3D: 띠 그리기에서 0xFFFF / 0xFFFFFFFF = 끊기
		glEnable(GL_FRAMEBUFFER_SRGB);                   // sRGB 형식 타깃에 쓸 때만 바꿈 (D3D _SRGB RTV 와 같음)
	}

	Format FromDxgi(DXGI_FORMAT f, bool depthBind)
	{
		Format r;
		auto set = [&](GLenum i, GLenum u, GLenum t, UINT bits) { r.Internal = i; r.Upload = u; r.Type = t; r.Bits = bits; };
		auto bc = [&](GLenum i, UINT block) { r.Internal = i; r.BlockBytes = block; };
		switch (f)
		{
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM: set(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 32); break;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: set(GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 32); break;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM: set(GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE, 32); break;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: set(GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, 32); break;
		case DXGI_FORMAT_R8G8B8A8_UINT: set(GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, 32); r.Integer = true; break;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_FLOAT: set(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 64); break;
		case DXGI_FORMAT_R16G16B16A16_UNORM: set(GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT, 64); break;
		case DXGI_FORMAT_R16G16B16A16_UINT: set(GL_RGBA16UI, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, 64); r.Integer = true; break;
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
		case DXGI_FORMAT_R32G32B32A32_FLOAT: set(GL_RGBA32F, GL_RGBA, GL_FLOAT, 128); break;
		case DXGI_FORMAT_R32G32B32A32_UINT: set(GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, 128); r.Integer = true; break;
		case DXGI_FORMAT_R32G32B32_FLOAT: set(GL_RGB32F, GL_RGB, GL_FLOAT, 96); break;
		case DXGI_FORMAT_R32G32_FLOAT: set(GL_RG32F, GL_RG, GL_FLOAT, 64); break;
		case DXGI_FORMAT_R32G32_UINT: set(GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT, 64); r.Integer = true; break;
		case DXGI_FORMAT_R16G16_TYPELESS:
		case DXGI_FORMAT_R16G16_FLOAT: set(GL_RG16F, GL_RG, GL_HALF_FLOAT, 32); break;
		case DXGI_FORMAT_R16G16_UNORM: set(GL_RG16, GL_RG, GL_UNSIGNED_SHORT, 32); break;
		case DXGI_FORMAT_R16_FLOAT: set(GL_R16F, GL_RED, GL_HALF_FLOAT, 16); break;
		case DXGI_FORMAT_R16_UNORM: set(GL_R16, GL_RED, GL_UNSIGNED_SHORT, 16); break;
		case DXGI_FORMAT_R16_UINT: set(GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, 16); r.Integer = true; break;
		case DXGI_FORMAT_R8G8_TYPELESS:
		case DXGI_FORMAT_R8G8_UNORM: set(GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 16); break;
		case DXGI_FORMAT_R8_TYPELESS:
		case DXGI_FORMAT_R8_UNORM: set(GL_R8, GL_RED, GL_UNSIGNED_BYTE, 8); break;
		case DXGI_FORMAT_R8_UINT: set(GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, 8); r.Integer = true; break;
		case DXGI_FORMAT_R11G11B10_FLOAT: set(GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV, 32); break;
		case DXGI_FORMAT_R9G9B9E5_SHAREDEXP: set(0x8C3D /* GL_RGB9_E5 */, GL_RGB, 0x8C3E /* GL_UNSIGNED_INT_5_9_9_9_REV */, 32); break;   // HDR 하늘 큐브맵
		case DXGI_FORMAT_R8G8B8A8_SNORM: set(0x8F97 /* GL_RGBA8_SNORM */, GL_RGBA, GL_BYTE, 32); break;
		case DXGI_FORMAT_R8G8_SNORM: set(0x8F95 /* GL_RG8_SNORM */, GL_RG, GL_BYTE, 16); break;
		case DXGI_FORMAT_R16G16B16A16_SNORM: set(0x8F9B /* GL_RGBA16_SNORM */, GL_RGBA, GL_SHORT, 64); break;
		case DXGI_FORMAT_R32G32_TYPELESS: set(GL_RG32F, GL_RG, GL_FLOAT, 64); break;
		case DXGI_FORMAT_R16G16_SNORM: set(0x8F99 /* GL_RG16_SNORM */, GL_RG, GL_SHORT, 32); break;
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
			else set(GL_R16, GL_RED, GL_UNSIGNED_SHORT, 16);
			break;
		case DXGI_FORMAT_D16_UNORM: set(GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, 16); r.Depth = true; break;
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT: set(GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 32); r.Depth = r.Stencil = true; break;
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: set(GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 64); r.Depth = r.Stencil = true; break;
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
		case DXGI_FORMAT_BC4_UNORM: bc(GL_COMPRESSED_RED_RGTC1, 8); break;
		case DXGI_FORMAT_BC4_SNORM: bc(GL_COMPRESSED_SIGNED_RED_RGTC1, 8); break;
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM: bc(GL_COMPRESSED_RG_RGTC2, 16); break;
		case DXGI_FORMAT_BC5_SNORM: bc(GL_COMPRESSED_SIGNED_RG_RGTC2, 16); break;
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16: bc(GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT, 16); break;
		case DXGI_FORMAT_BC6H_SF16: bc(GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT, 16); break;
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM: bc(GL_COMPRESSED_RGBA_BPTC_UNORM, 16); break;
		case DXGI_FORMAT_BC7_UNORM_SRGB: bc(GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM, 16); break;
		default: break;
		}
		return r;
	}

	UINT64 RowBytes(const Format& f, UINT width)
	{
		if (f.BlockBytes) return (UINT64)(std::max)(1u, (width + 3) / 4) * f.BlockBytes;
		return (UINT64)width * f.Bits / 8;
	}

	UINT64 SliceBytes(const Format& f, UINT width, UINT height)
	{
		if (f.BlockBytes) return RowBytes(f, width) * (std::max)(1u, (height + 3) / 4);
		return RowBytes(f, width) * height;
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
		case DXGI_FORMAT_B8G8R8A8_UNORM: size = GL_BGRA; type = GL_UNSIGNED_BYTE; normalized = GL_TRUE; bytes = 4; return true;   // GL 이 B,G,R,A 순서로 읽음
		case DXGI_FORMAT_R8G8B8A8_UINT: size = 4; type = GL_UNSIGNED_BYTE; integer = true; bytes = 4; return true;
		case DXGI_FORMAT_R16G16_SINT: size = 2; type = GL_SHORT; integer = true; bytes = 4; return true;
		case DXGI_FORMAT_R32_UINT: size = 1; type = GL_UNSIGNED_INT; integer = true; bytes = 4; return true;
		case DXGI_FORMAT_R32_SINT: size = 1; type = GL_INT; integer = true; bytes = 4; return true;
		case DXGI_FORMAT_R32G32_UINT: size = 2; type = GL_UNSIGNED_INT; integer = true; bytes = 8; return true;
		case DXGI_FORMAT_R32G32B32A32_UINT: size = 4; type = GL_UNSIGNED_INT; integer = true; bytes = 16; return true;
		case DXGI_FORMAT_R32G32B32A32_SINT: size = 4; type = GL_INT; integer = true; bytes = 16; return true;
		case DXGI_FORMAT_R16G16B16A16_UINT: size = 4; type = GL_UNSIGNED_SHORT; integer = true; bytes = 8; return true;
		default: return false;
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

	GLenum CompareFunc(D3D11_COMPARISON_FUNC f) { return GLCompare(f); }

	void InstallDebugOutput(bool synchronous)
	{
		glEnable(GL_DEBUG_OUTPUT);
		if (synchronous)
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(DebugCallback, nullptr);
	}

	// ---- 묶기 캐시 (메인 스레드 하나만 GL 을 부른다 — GfxGL::Check)
	namespace
	{
		constexpr GLuint kUnknown = ~0u;
		struct Bindings
		{
			HGLRC Context = nullptr;
			GLuint Program = kUnknown, Vao = kUnknown;
			GLuint Textures[96], Samplers[96], Ubos[48];
			uint64_t Generation = 1;
			void Reset()
			{
				Program = Vao = kUnknown;
				for (GLuint& t : Textures) t = kUnknown;
				for (GLuint& s : Samplers) s = kUnknown;
				for (GLuint& u : Ubos) u = kUnknown;
				++Generation;
			}
		};
		Bindings& B()
		{
			static Bindings s_B = [] { Bindings b; b.Reset(); return b; }();
			const HGLRC current = ::wglGetCurrentContext();
			if (current != s_B.Context)   // 다른 컨텍스트 (검사용 장치 등): 그 컨텍스트의 묶기는 모른다
			{
				s_B.Context = current;
				s_B.Reset();
			}
			return s_B;
		}
	}

	void InvalidateBindings() { B().Reset(); }
	uint64_t BindingGeneration() { return B().Generation; }

	void UseProgram(GLuint program)
	{
		Bindings& b = B();
		if (b.Program == program) return;
		b.Program = program;
		glUseProgram(program);
	}

	void BindTextureUnit(GLuint unit, GLuint texture)
	{
		Bindings& b = B();
		if (unit < 96)
		{
			if (b.Textures[unit] == texture) return;
			b.Textures[unit] = texture;
		}
		glBindTextureUnit(unit, texture);
	}

	void BindSampler(GLuint unit, GLuint sampler)
	{
		Bindings& b = B();
		if (unit < 96)
		{
			if (b.Samplers[unit] == sampler) return;
			b.Samplers[unit] = sampler;
		}
		glBindSampler(unit, sampler);
	}

	void BindUniformBuffer(GLuint binding, GLuint buffer)
	{
		Bindings& b = B();
		if (binding < 48)
		{
			if (b.Ubos[binding] == buffer) return;
			b.Ubos[binding] = buffer;
		}
		glBindBufferBase(GL_UNIFORM_BUFFER, binding, buffer);
	}

	void BindVertexArray(GLuint vao)
	{
		Bindings& b = B();
		if (b.Vao == vao) return;
		b.Vao = vao;
		glBindVertexArray(vao);
	}
}
