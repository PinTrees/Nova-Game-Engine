#include "pch.h"
#include "GfxWgpuInternal.h"
#include "GLShared.h"
#include <emscripten.h>
#include <emscripten/html5_webgpu.h>

// Gfx WebGPU 장치: 형식 · 자원 · 뷰 · 상태 객체 · 프로그램 · 링 · 더미 · 블릿 · Present
using namespace GfxWgpuImpl;

namespace GfxWgpuImpl
{
	uint64_t NextId()
	{
		static uint64_t n = 1;
		return n++;
	}

	uint64_t HashBytes(const void* data, size_t size, uint64_t seed)
	{
		const uint8_t* p = static_cast<const uint8_t*>(data);
		uint64_t h = seed;
		for (size_t i = 0; i < size; ++i)
		{
			h ^= p[i];
			h *= 1099511628211ull;
		}
		return h;
	}

	// ------------------------------------------------------------------ 형식
	WGPUTextureFormat TextureFormat(DXGI_FORMAT f, UINT bind)
	{
		const bool depth = (bind & D3D11_BIND_DEPTH_STENCIL) != 0;
		switch (f)
		{
		case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: return WGPUTextureFormat_RGBA8Unorm;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return WGPUTextureFormat_RGBA8UnormSrgb;
		case DXGI_FORMAT_R8G8B8A8_SNORM: return WGPUTextureFormat_RGBA8Snorm;
		case DXGI_FORMAT_R8G8B8A8_UINT: return WGPUTextureFormat_RGBA8Uint;
		case DXGI_FORMAT_R8G8B8A8_SINT: return WGPUTextureFormat_RGBA8Sint;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM: return WGPUTextureFormat_BGRA8Unorm;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return WGPUTextureFormat_BGRA8UnormSrgb;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT: return WGPUTextureFormat_RGBA16Float;
		case DXGI_FORMAT_R16G16B16A16_UINT: return WGPUTextureFormat_RGBA16Uint;
		case DXGI_FORMAT_R16G16B16A16_SINT: return WGPUTextureFormat_RGBA16Sint;
		case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32A32_FLOAT: return WGPUTextureFormat_RGBA32Float;
		case DXGI_FORMAT_R32G32B32A32_UINT: return WGPUTextureFormat_RGBA32Uint;
		case DXGI_FORMAT_R32G32B32A32_SINT: return WGPUTextureFormat_RGBA32Sint;
		case DXGI_FORMAT_R32G32_TYPELESS: case DXGI_FORMAT_R32G32_FLOAT: return WGPUTextureFormat_RG32Float;
		case DXGI_FORMAT_R32G32_UINT: return WGPUTextureFormat_RG32Uint;
		case DXGI_FORMAT_R16G16_TYPELESS: case DXGI_FORMAT_R16G16_FLOAT: return WGPUTextureFormat_RG16Float;
		case DXGI_FORMAT_R16G16_UINT: return WGPUTextureFormat_RG16Uint;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM: return WGPUTextureFormat_RGB10A2Unorm;
		case DXGI_FORMAT_R10G10B10A2_UINT: return WGPUTextureFormat_RGB10A2Uint;
		case DXGI_FORMAT_R11G11B10_FLOAT: return WGPUTextureFormat_RG11B10Ufloat;
		case DXGI_FORMAT_R9G9B9E5_SHAREDEXP: return WGPUTextureFormat_RGB9E5Ufloat;
		case DXGI_FORMAT_R8G8_TYPELESS: case DXGI_FORMAT_R8G8_UNORM: return WGPUTextureFormat_RG8Unorm;
		case DXGI_FORMAT_R8G8_SNORM: return WGPUTextureFormat_RG8Snorm;
		case DXGI_FORMAT_R8G8_UINT: return WGPUTextureFormat_RG8Uint;
		case DXGI_FORMAT_R8_TYPELESS: case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_A8_UNORM: return WGPUTextureFormat_R8Unorm;
		case DXGI_FORMAT_R8_SNORM: return WGPUTextureFormat_R8Snorm;
		case DXGI_FORMAT_R8_UINT: return WGPUTextureFormat_R8Uint;
		case DXGI_FORMAT_R8_SINT: return WGPUTextureFormat_R8Sint;
		case DXGI_FORMAT_R16_FLOAT: return WGPUTextureFormat_R16Float;
		case DXGI_FORMAT_R16_UINT: return WGPUTextureFormat_R16Uint;
		case DXGI_FORMAT_R16_SINT: return WGPUTextureFormat_R16Sint;
		case DXGI_FORMAT_R16_TYPELESS: return depth ? WGPUTextureFormat_Depth16Unorm : WGPUTextureFormat_R16Float;
		case DXGI_FORMAT_D16_UNORM: return WGPUTextureFormat_Depth16Unorm;
		case DXGI_FORMAT_R32_TYPELESS: return depth ? WGPUTextureFormat_Depth32Float : WGPUTextureFormat_R32Float;
		case DXGI_FORMAT_D32_FLOAT: return WGPUTextureFormat_Depth32Float;
		case DXGI_FORMAT_R32_FLOAT: return WGPUTextureFormat_R32Float;
		case DXGI_FORMAT_R32_UINT: return WGPUTextureFormat_R32Uint;
		case DXGI_FORMAT_R32_SINT: return WGPUTextureFormat_R32Sint;
		case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: return WGPUTextureFormat_Depth24PlusStencil8;
		case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return WGPUTextureFormat_Depth32FloatStencil8;
		case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: return WGPUTextureFormat_BC1RGBAUnorm;
		case DXGI_FORMAT_BC1_UNORM_SRGB: return WGPUTextureFormat_BC1RGBAUnormSrgb;
		case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: return WGPUTextureFormat_BC2RGBAUnorm;
		case DXGI_FORMAT_BC2_UNORM_SRGB: return WGPUTextureFormat_BC2RGBAUnormSrgb;
		case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: return WGPUTextureFormat_BC3RGBAUnorm;
		case DXGI_FORMAT_BC3_UNORM_SRGB: return WGPUTextureFormat_BC3RGBAUnormSrgb;
		case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC4_UNORM: return WGPUTextureFormat_BC4RUnorm;
		case DXGI_FORMAT_BC4_SNORM: return WGPUTextureFormat_BC4RSnorm;
		case DXGI_FORMAT_BC5_TYPELESS: case DXGI_FORMAT_BC5_UNORM: return WGPUTextureFormat_BC5RGUnorm;
		case DXGI_FORMAT_BC5_SNORM: return WGPUTextureFormat_BC5RGSnorm;
		case DXGI_FORMAT_BC6H_TYPELESS: case DXGI_FORMAT_BC6H_UF16: return WGPUTextureFormat_BC6HRGBUfloat;
		case DXGI_FORMAT_BC6H_SF16: return WGPUTextureFormat_BC6HRGBFloat;
		case DXGI_FORMAT_BC7_TYPELESS: case DXGI_FORMAT_BC7_UNORM: return WGPUTextureFormat_BC7RGBAUnorm;
		case DXGI_FORMAT_BC7_UNORM_SRGB: return WGPUTextureFormat_BC7RGBAUnormSrgb;
		default: return WGPUTextureFormat_Undefined;
		}
	}

	bool IsDepthFormat(WGPUTextureFormat f)
	{
		return f == WGPUTextureFormat_Depth16Unorm || f == WGPUTextureFormat_Depth24Plus || f == WGPUTextureFormat_Depth24PlusStencil8 ||
			f == WGPUTextureFormat_Depth32Float || f == WGPUTextureFormat_Depth32FloatStencil8 || f == WGPUTextureFormat_Stencil8;
	}

	bool HasStencil(WGPUTextureFormat f) { return f == WGPUTextureFormat_Depth24PlusStencil8 || f == WGPUTextureFormat_Depth32FloatStencil8 || f == WGPUTextureFormat_Stencil8; }

	bool IsCompressed(WGPUTextureFormat f) { return f >= WGPUTextureFormat_BC1RGBAUnorm && f <= WGPUTextureFormat_BC7RGBAUnormSrgb; }

	UINT BlockBytes(WGPUTextureFormat f, UINT& bw, UINT& bh)
	{
		bw = bh = 1;
		switch (f)
		{
		case WGPUTextureFormat_BC1RGBAUnorm: case WGPUTextureFormat_BC1RGBAUnormSrgb: case WGPUTextureFormat_BC4RUnorm: case WGPUTextureFormat_BC4RSnorm:
			bw = bh = 4; return 8;
		case WGPUTextureFormat_BC2RGBAUnorm: case WGPUTextureFormat_BC2RGBAUnormSrgb: case WGPUTextureFormat_BC3RGBAUnorm: case WGPUTextureFormat_BC3RGBAUnormSrgb:
		case WGPUTextureFormat_BC5RGUnorm: case WGPUTextureFormat_BC5RGSnorm: case WGPUTextureFormat_BC6HRGBUfloat: case WGPUTextureFormat_BC6HRGBFloat:
		case WGPUTextureFormat_BC7RGBAUnorm: case WGPUTextureFormat_BC7RGBAUnormSrgb:
			bw = bh = 4; return 16;
		case WGPUTextureFormat_R8Unorm: case WGPUTextureFormat_R8Snorm: case WGPUTextureFormat_R8Uint: case WGPUTextureFormat_R8Sint: case WGPUTextureFormat_Stencil8: return 1;
		case WGPUTextureFormat_RG8Unorm: case WGPUTextureFormat_RG8Snorm: case WGPUTextureFormat_RG8Uint: case WGPUTextureFormat_R16Float: case WGPUTextureFormat_R16Uint:
		case WGPUTextureFormat_R16Sint: case WGPUTextureFormat_Depth16Unorm: return 2;
		case WGPUTextureFormat_RGBA16Float: case WGPUTextureFormat_RGBA16Uint: case WGPUTextureFormat_RGBA16Sint: case WGPUTextureFormat_RG32Float:
		case WGPUTextureFormat_RG32Uint: case WGPUTextureFormat_RG32Sint: case WGPUTextureFormat_Depth32FloatStencil8: return 8;
		case WGPUTextureFormat_RGBA32Float: case WGPUTextureFormat_RGBA32Uint: case WGPUTextureFormat_RGBA32Sint: return 16;
		default: return 4;
		}
	}

	// 보기 형식: 깊이 텍스처를 읽는 SRV (R24_UNORM_X8 · R32_FLOAT · R16_UNORM) = 깊이 면, 스텐실 SRV = 스텐실 면
	WGPUTextureFormat ViewFormat(DXGI_FORMAT f, WGPUTextureFormat tex, WGPUTextureAspect& aspect)
	{
		aspect = WGPUTextureAspect_All;
		if (IsDepthFormat(tex))
		{
			if (f == DXGI_FORMAT_X24_TYPELESS_G8_UINT || f == DXGI_FORMAT_X32_TYPELESS_G8X24_UINT)
				aspect = WGPUTextureAspect_StencilOnly;
			else if (HasStencil(tex) && f != DXGI_FORMAT_D24_UNORM_S8_UINT && f != DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
				aspect = WGPUTextureAspect_DepthOnly;
			return tex;
		}
		if (f == DXGI_FORMAT_UNKNOWN)
			return tex;
		const WGPUTextureFormat v = TextureFormat(f, 0);
		return v == WGPUTextureFormat_Undefined ? tex : v;
	}

	// sRGB · UNORM 짝 (TYPELESS 텍스처를 두 형식으로 볼 때 viewFormats)
	WGPUTextureFormat SrgbPair(WGPUTextureFormat f)
	{
		switch (f)
		{
		case WGPUTextureFormat_RGBA8Unorm: return WGPUTextureFormat_RGBA8UnormSrgb;
		case WGPUTextureFormat_RGBA8UnormSrgb: return WGPUTextureFormat_RGBA8Unorm;
		case WGPUTextureFormat_BGRA8Unorm: return WGPUTextureFormat_BGRA8UnormSrgb;
		case WGPUTextureFormat_BGRA8UnormSrgb: return WGPUTextureFormat_BGRA8Unorm;
		case WGPUTextureFormat_BC1RGBAUnorm: return WGPUTextureFormat_BC1RGBAUnormSrgb;
		case WGPUTextureFormat_BC1RGBAUnormSrgb: return WGPUTextureFormat_BC1RGBAUnorm;
		case WGPUTextureFormat_BC2RGBAUnorm: return WGPUTextureFormat_BC2RGBAUnormSrgb;
		case WGPUTextureFormat_BC2RGBAUnormSrgb: return WGPUTextureFormat_BC2RGBAUnorm;
		case WGPUTextureFormat_BC3RGBAUnorm: return WGPUTextureFormat_BC3RGBAUnormSrgb;
		case WGPUTextureFormat_BC3RGBAUnormSrgb: return WGPUTextureFormat_BC3RGBAUnorm;
		case WGPUTextureFormat_BC7RGBAUnorm: return WGPUTextureFormat_BC7RGBAUnormSrgb;
		case WGPUTextureFormat_BC7RGBAUnormSrgb: return WGPUTextureFormat_BC7RGBAUnorm;
		default: return WGPUTextureFormat_Undefined;
		}
	}

	bool StorageCapable(WGPUTextureFormat f)
	{
		switch (f)
		{
		case WGPUTextureFormat_RGBA8Unorm: case WGPUTextureFormat_RGBA8Snorm: case WGPUTextureFormat_RGBA8Uint: case WGPUTextureFormat_RGBA8Sint:
		case WGPUTextureFormat_RGBA16Uint: case WGPUTextureFormat_RGBA16Sint: case WGPUTextureFormat_RGBA16Float: case WGPUTextureFormat_R32Uint:
		case WGPUTextureFormat_R32Sint: case WGPUTextureFormat_R32Float: case WGPUTextureFormat_RG32Uint: case WGPUTextureFormat_RG32Sint:
		case WGPUTextureFormat_RG32Float: case WGPUTextureFormat_RGBA32Uint: case WGPUTextureFormat_RGBA32Sint: case WGPUTextureFormat_RGBA32Float:
		case WGPUTextureFormat_R8Unorm: case WGPUTextureFormat_RG8Unorm: case WGPUTextureFormat_R16Float: case WGPUTextureFormat_RG16Float:
		case WGPUTextureFormat_RGB10A2Unorm: case WGPUTextureFormat_RG11B10Ufloat:
			return true;   // 마지막 줄 = texture-formats-tier1 (장치를 받을 때 켠다)
		default: return false;
		}
	}

	WGPUVertexFormat VertexFormat(DXGI_FORMAT f)
	{
		switch (f)
		{
		case DXGI_FORMAT_R32_FLOAT: return WGPUVertexFormat_Float32;
		case DXGI_FORMAT_R32G32_FLOAT: return WGPUVertexFormat_Float32x2;
		case DXGI_FORMAT_R32G32B32_FLOAT: return WGPUVertexFormat_Float32x3;
		case DXGI_FORMAT_R32G32B32A32_FLOAT: return WGPUVertexFormat_Float32x4;
		case DXGI_FORMAT_R32_UINT: return WGPUVertexFormat_Uint32;
		case DXGI_FORMAT_R32G32_UINT: return WGPUVertexFormat_Uint32x2;
		case DXGI_FORMAT_R32G32B32_UINT: return WGPUVertexFormat_Uint32x3;
		case DXGI_FORMAT_R32G32B32A32_UINT: return WGPUVertexFormat_Uint32x4;
		case DXGI_FORMAT_R32_SINT: return WGPUVertexFormat_Sint32;
		case DXGI_FORMAT_R32G32B32A32_SINT: return WGPUVertexFormat_Sint32x4;
		case DXGI_FORMAT_R8G8B8A8_UNORM: return WGPUVertexFormat_Unorm8x4;
		case DXGI_FORMAT_B8G8R8A8_UNORM: return WGPUVertexFormat_Unorm8x4;   // (빨강 · 파랑이 바뀐다 — 엔진 정점은 RGBA)
		case DXGI_FORMAT_R8G8B8A8_UINT: return WGPUVertexFormat_Uint8x4;
		case DXGI_FORMAT_R8G8B8A8_SNORM: return WGPUVertexFormat_Snorm8x4;
		case DXGI_FORMAT_R8G8B8A8_SINT: return WGPUVertexFormat_Sint8x4;
		case DXGI_FORMAT_R16G16_FLOAT: return WGPUVertexFormat_Float16x2;
		case DXGI_FORMAT_R16G16B16A16_FLOAT: return WGPUVertexFormat_Float16x4;
		case DXGI_FORMAT_R16G16_UNORM: return WGPUVertexFormat_Unorm16x2;
		case DXGI_FORMAT_R16G16B16A16_UNORM: return WGPUVertexFormat_Unorm16x4;
		case DXGI_FORMAT_R16G16_SNORM: return WGPUVertexFormat_Snorm16x2;
		case DXGI_FORMAT_R16G16B16A16_SNORM: return WGPUVertexFormat_Snorm16x4;
		case DXGI_FORMAT_R16G16_UINT: return WGPUVertexFormat_Uint16x2;
		case DXGI_FORMAT_R16G16B16A16_UINT: return WGPUVertexFormat_Uint16x4;
		case DXGI_FORMAT_R16G16_SINT: return WGPUVertexFormat_Sint16x2;
		case DXGI_FORMAT_R16G16B16A16_SINT: return WGPUVertexFormat_Sint16x4;
		default: return WGPUVertexFormat_Undefined;
		}
	}

	WGPUCompareFunction Compare(D3D11_COMPARISON_FUNC f)
	{
		switch (f)
		{
		case D3D11_COMPARISON_NEVER: return WGPUCompareFunction_Never;
		case D3D11_COMPARISON_LESS: return WGPUCompareFunction_Less;
		case D3D11_COMPARISON_EQUAL: return WGPUCompareFunction_Equal;
		case D3D11_COMPARISON_LESS_EQUAL: return WGPUCompareFunction_LessEqual;
		case D3D11_COMPARISON_GREATER: return WGPUCompareFunction_Greater;
		case D3D11_COMPARISON_NOT_EQUAL: return WGPUCompareFunction_NotEqual;
		case D3D11_COMPARISON_GREATER_EQUAL: return WGPUCompareFunction_GreaterEqual;
		default: return WGPUCompareFunction_Always;
		}
	}

	D3D11_RASTERIZER_DESC DefaultRasterizer()
	{
		D3D11_RASTERIZER_DESC d = {};
		d.FillMode = D3D11_FILL_SOLID;
		d.CullMode = D3D11_CULL_BACK;
		d.DepthClipEnable = TRUE;
		return d;
	}

	D3D11_BLEND_DESC DefaultBlend()
	{
		D3D11_BLEND_DESC d = {};
		for (auto& rt : d.RenderTarget)
		{
			rt.SrcBlend = D3D11_BLEND_ONE; rt.DestBlend = D3D11_BLEND_ZERO; rt.BlendOp = D3D11_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D11_BLEND_ONE; rt.DestBlendAlpha = D3D11_BLEND_ZERO; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		}
		return d;
	}

	D3D11_DEPTH_STENCIL_DESC DefaultDepthStencil()
	{
		D3D11_DEPTH_STENCIL_DESC d = {};
		d.DepthEnable = TRUE;
		d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		d.DepthFunc = D3D11_COMPARISON_LESS;
		d.StencilReadMask = d.StencilWriteMask = 0xFF;
		d.FrontFace = d.BackFace = { D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS };
		return d;
	}

	TexInfo* TexOf(GfxResource* r)
	{
		if (!r || r->Api() != GfxApi::WebGPU) return nullptr;
		D3D11_RESOURCE_DIMENSION dim;
		r->GetType(&dim);
		switch (dim)
		{
		case D3D11_RESOURCE_DIMENSION_TEXTURE1D: return &static_cast<Tex1D*>(r)->I;
		case D3D11_RESOURCE_DIMENSION_TEXTURE2D: return &static_cast<Tex2D*>(r)->I;
		case D3D11_RESOURCE_DIMENSION_TEXTURE3D: return &static_cast<Tex3D*>(r)->I;
		default: return nullptr;
		}
	}

	Buf* BufOf(GfxResource* r)
	{
		if (!r || r->Api() != GfxApi::WebGPU) return nullptr;
		D3D11_RESOURCE_DIMENSION dim;
		r->GetType(&dim);
		return dim == D3D11_RESOURCE_DIMENSION_BUFFER ? static_cast<Buf*>(r) : nullptr;
	}

	ViewInfo* ViewOf(GfxView* v)
	{
		if (!v || v->Api() != GfxApi::WebGPU) return nullptr;
		// 네 뷰 모두 같은 배치 (View<Iface, Desc>::V) — 종류를 물어 맞는 형으로
		ComPtr<GfxShaderResourceView> srv;
		if (SUCCEEDED(v->QueryInterface(__uuidof(GfxShaderResourceView), (void**)srv.GetAddressOf()))) return &static_cast<Srv*>(srv.Get())->V;
		ComPtr<GfxRenderTargetView> rtv;
		if (SUCCEEDED(v->QueryInterface(__uuidof(GfxRenderTargetView), (void**)rtv.GetAddressOf()))) return &static_cast<Rtv*>(rtv.Get())->V;
		ComPtr<GfxDepthStencilView> dsv;
		if (SUCCEEDED(v->QueryInterface(__uuidof(GfxDepthStencilView), (void**)dsv.GetAddressOf()))) return &static_cast<Dsv*>(dsv.Get())->V;
		ComPtr<GfxUnorderedAccessView> uav;
		if (SUCCEEDED(v->QueryInterface(__uuidof(GfxUnorderedAccessView), (void**)uav.GetAddressOf()))) return &static_cast<Uav*>(uav.Get())->V;
		return nullptr;
	}

	// ------------------------------------------------------------------ 소멸
	Buf::~Buf()
	{
		if (Handle) { wgpuBufferDestroy(Handle); wgpuBufferRelease(Handle); }
	}

	static void ReleaseTex(TexInfo& t)
	{
		if (t.Handle) { wgpuTextureDestroy(t.Handle); wgpuTextureRelease(t.Handle); t.Handle = nullptr; }
		if (t.Staging) { wgpuBufferDestroy(t.Staging); wgpuBufferRelease(t.Staging); t.Staging = nullptr; }
	}
	Tex1D::~Tex1D() { ReleaseTex(I); }
	Tex2D::~Tex2D() { ReleaseTex(I); }
	Tex3D::~Tex3D() { ReleaseTex(I); }

	template <class Iface, class Desc>
	View<Iface, Desc>::~View()
	{
		if (V.View) wgpuTextureViewRelease(V.View);
	}
	template class View<GfxShaderResourceView, D3D11_SHADER_RESOURCE_VIEW_DESC>;
	template class View<GfxRenderTargetView, D3D11_RENDER_TARGET_VIEW_DESC>;
	template class View<GfxDepthStencilView, D3D11_DEPTH_STENCIL_VIEW_DESC>;
	template class View<GfxUnorderedAccessView, D3D11_UNORDERED_ACCESS_VIEW_DESC>;

	Sampler::~Sampler()
	{
		if (Handle) wgpuSamplerRelease(Handle);
		if (Nearest) wgpuSamplerRelease(Nearest);
	}

	Program::~Program()
	{
		for (Stage& s : Stages)
			if (s.Module) wgpuShaderModuleRelease(s.Module);
		for (auto& [k, l] : Layouts)
		{
			if (l.ComputePipe) wgpuComputePipelineRelease(l.ComputePipe);
			if (l.Pipeline) wgpuPipelineLayoutRelease(l.Pipeline);
			if (l.Group) wgpuBindGroupLayoutRelease(l.Group);
		}
		// 이 프로그램으로 만든 렌더 파이프라인 · 바인드 그룹은 장치 캐시에 남는다 (키의 Program id 는 다시 쓰이지 않는다)
	}

	Dev::~Dev()
	{
		for (auto& [k, p] : Pipelines) wgpuRenderPipelineRelease(p);
		for (auto& [k, g] : BindGroups) wgpuBindGroupRelease(g);
		for (auto& [k, v] : Dummies) wgpuTextureViewRelease(v);
		for (Chunk& c : Chunks) { wgpuBufferDestroy(c.Buffer); wgpuBufferRelease(c.Buffer); }
	}

	void Dev::Once(const std::string& key, const char* fmt, const char* arg)
	{
		if (!Reported.insert(key).second) return;
		char buf[512];
		snprintf(buf, sizeof(buf), fmt, arg);
		EditorLog::Write("WebGPU", "%s", buf);
	}

	// ------------------------------------------------------------------ 명령
	WGPUCommandEncoder Dev::Enc()
	{
		if (!Encoder)
			Encoder = wgpuDeviceCreateCommandEncoder(Device, nullptr);
		return Encoder;
	}

	void Dev::Submit()
	{
		if (Immediate)
			Immediate->EndPass();
		if (!Encoder)
			return;
		WGPUCommandBuffer cb = wgpuCommandEncoderFinish(Encoder, nullptr);
		wgpuCommandEncoderRelease(Encoder);
		Encoder = nullptr;
		wgpuQueueSubmit(Queue, 1, &cb);
		wgpuCommandBufferRelease(cb);
		++EncoderSerial;
	}

	// ------------------------------------------------------------------ 링
	bool Dev::RingWrite(const void* data, uint64_t size, uint64_t align, RingLoc& loc)
	{
		const uint64_t padded = (size + 3) & ~3ull;
		for (;;)
		{
			if (CurrentChunk >= Chunks.size())
			{
				Chunk c;
				c.Size = (std::max<uint64_t>)(4ull << 20, (padded + 255) & ~255ull);
				WGPUBufferDescriptor bd = {};
				bd.size = c.Size;
				bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_Index | WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst |
					WGPUBufferUsage_CopySrc | WGPUBufferUsage_Indirect;
				c.Buffer = wgpuDeviceCreateBuffer(Device, &bd);
				c.Id = NextId();
				if (!c.Buffer) return false;
				Chunks.push_back(c);
			}
			Chunk& c = Chunks[CurrentChunk];
			const uint64_t at = (c.Used + align - 1) / align * align;
			if (at + padded <= c.Size)
			{
				if (padded == size)
					wgpuQueueWriteBuffer(Queue, c.Buffer, at, data, size);
				else
				{
					std::vector<uint8_t> tmp(padded, 0);
					memcpy(tmp.data(), data, size);
					wgpuQueueWriteBuffer(Queue, c.Buffer, at, tmp.data(), padded);
				}
				c.Used = at + padded;
				loc.Buffer = c.Buffer;
				loc.BufferId = c.Id;
				loc.Offset = at;
				loc.Frame = Frame;
				return true;
			}
			++CurrentChunk;
		}
	}

	void Dev::RingReset()
	{
		for (Chunk& c : Chunks) c.Used = 0;
		CurrentChunk = 0;
	}

	bool WriteConstants(GfxDevice* device, const void* data, uint32_t size, RingLoc& loc)
	{
		return static_cast<Dev*>(device)->RingWrite(data, size, 256, loc);
	}

	bool IsCurrent(GfxDevice* device, const RingLoc& loc) { return loc.Buffer && loc.Frame == static_cast<Dev*>(device)->Frame; }

	// ------------------------------------------------------------------ 더미
	WGPUTextureView Dev::DummyView(WGPUTextureViewDimension dim, WGPUTextureSampleType sample)
	{
		const int key = (int)dim | ((int)sample << 8);
		auto it = Dummies.find(key);
		if (it != Dummies.end()) return it->second;
		WGPUTextureDescriptor td = {};
		td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
		td.dimension = dim == WGPUTextureViewDimension_3D ? WGPUTextureDimension_3D : dim == WGPUTextureViewDimension_1D ? WGPUTextureDimension_1D : WGPUTextureDimension_2D;
		const bool cube = dim == WGPUTextureViewDimension_Cube || dim == WGPUTextureViewDimension_CubeArray;
		td.size = { 1, 1, cube ? 6u : 1u };
		td.format = sample == WGPUTextureSampleType_Depth ? WGPUTextureFormat_Depth32Float : sample == WGPUTextureSampleType_Uint ? WGPUTextureFormat_RGBA8Uint :
			sample == WGPUTextureSampleType_Sint ? WGPUTextureFormat_RGBA8Sint : WGPUTextureFormat_RGBA8Unorm;
		if (sample == WGPUTextureSampleType_Depth) td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
		td.mipLevelCount = 1;
		td.sampleCount = 1;
		WGPUTexture tex = wgpuDeviceCreateTexture(Device, &td);
		if (sample != WGPUTextureSampleType_Depth && td.format == WGPUTextureFormat_RGBA8Unorm)
		{
			// 빈 칸 = 검정 (알파 1) — D3D 는 0 을 읽지만 엔진이 기대하는 값은 '영향 없음'
			const uint8_t px[4] = { 0, 0, 0, 255 };
			for (uint32_t l = 0; l < td.size.depthOrArrayLayers; ++l)
			{
				WGPUImageCopyTexture dst = {};
				dst.texture = tex;
				dst.origin = { 0, 0, l };
				dst.aspect = WGPUTextureAspect_All;
				WGPUTextureDataLayout layout = {};
				layout.bytesPerRow = 4;
				layout.rowsPerImage = 1;
				const WGPUExtent3D ext = { 1, 1, 1 };
				wgpuQueueWriteTexture(Queue, &dst, px, 4, &layout, &ext);
			}
		}
		else if (sample == WGPUTextureSampleType_Depth)
		{
			// 깊이 더미 = 1 (그림자 없음): 지우기만 하는 패스
			WGPUTextureViewDescriptor vd = {};
			vd.format = td.format; vd.dimension = WGPUTextureViewDimension_2D; vd.mipLevelCount = 1; vd.arrayLayerCount = 1; vd.aspect = WGPUTextureAspect_All;
			for (uint32_t l = 0; l < td.size.depthOrArrayLayers; ++l)
			{
				vd.baseArrayLayer = l;
				WGPUTextureView v = wgpuTextureCreateView(tex, &vd);
				WGPURenderPassDepthStencilAttachment ds = {};
				ds.view = v;
				ds.depthLoadOp = WGPULoadOp_Clear;
				ds.depthStoreOp = WGPUStoreOp_Store;
				ds.depthClearValue = 1.0f;
				ds.stencilLoadOp = WGPULoadOp_Undefined;
				ds.stencilStoreOp = WGPUStoreOp_Undefined;
				WGPURenderPassDescriptor rp = {};
				rp.depthStencilAttachment = &ds;
				// 따로 제출하는 인코더 (그리기 도중에 불린다 — 지금 열린 패스를 닫지 않게. 큐 순서상 본 인코더보다 먼저 실행된다)
				WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(Device, nullptr);
				WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
				wgpuRenderPassEncoderEnd(pass);
				wgpuRenderPassEncoderRelease(pass);
				WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, nullptr);
				wgpuQueueSubmit(Queue, 1, &cb);
				wgpuCommandBufferRelease(cb);
				wgpuCommandEncoderRelease(enc);
				wgpuTextureViewRelease(v);
			}
		}
		WGPUTextureViewDescriptor vd = {};
		vd.format = td.format;
		vd.dimension = dim;
		vd.mipLevelCount = 1;
		vd.arrayLayerCount = cube && dim == WGPUTextureViewDimension_Cube ? 6 : td.size.depthOrArrayLayers;
		vd.aspect = WGPUTextureAspect_All;
		WGPUTextureView view = wgpuTextureCreateView(tex, &vd);
		wgpuTextureRelease(tex);   // 뷰가 텍스처를 잡는다
		Dummies[key] = view;
		return view;
	}

	WGPUTextureView Dev::DummyStorage(WGPUTextureFormat format, WGPUTextureViewDimension dim)
	{
		const auto key = std::make_pair((int)format, (int)dim);
		auto it = StorageDummies.find(key);
		if (it != StorageDummies.end()) return it->second;
		WGPUTextureDescriptor td = {};
		td.usage = WGPUTextureUsage_StorageBinding;
		td.dimension = dim == WGPUTextureViewDimension_3D ? WGPUTextureDimension_3D : WGPUTextureDimension_2D;
		td.size = { 1, 1, 1 };
		td.format = format;
		td.mipLevelCount = 1;
		td.sampleCount = 1;
		WGPUTexture tex = wgpuDeviceCreateTexture(Device, &td);
		WGPUTextureViewDescriptor vd = {};
		vd.format = format; vd.dimension = dim; vd.mipLevelCount = 1; vd.arrayLayerCount = 1; vd.aspect = WGPUTextureAspect_All;
		WGPUTextureView v = wgpuTextureCreateView(tex, &vd);
		wgpuTextureRelease(tex);
		StorageDummies[key] = v;
		return v;
	}

	// ------------------------------------------------------------------ 블릿 (Present · GenerateMips)
	static const char* kBlitWgsl = R"(
@group(0) @binding(0) var src : texture_2d<f32>;
@group(0) @binding(1) var smp : sampler;
struct VO { @builtin(position) pos : vec4f, @location(0) uv : vec2f };
@vertex fn vs(@builtin(vertex_index) i : u32) -> VO {
  var o : VO;
  let uv = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
  o.pos = vec4f(uv * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0), 0.0, 1.0);
  o.uv = uv;
  return o;
}
@fragment fn fs(v : VO) -> @location(0) vec4f { return textureSampleLevel(src, smp, v.uv, 0.0); }
)";

	WGPURenderPipeline Dev::BlitPipeline(WGPUTextureFormat format)
	{
		auto it = BlitPipes.find(format);
		if (it != BlitPipes.end()) return it->second;
		if (!BlitModule)
		{
			WGPUShaderModuleWGSLDescriptor w = {};
			w.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
			w.code = kBlitWgsl;
			WGPUShaderModuleDescriptor md = {};
			md.nextInChain = &w.chain;
			BlitModule = wgpuDeviceCreateShaderModule(Device, &md);
			WGPUBindGroupLayoutEntry e[2] = {};
			e[0].binding = 0;
			e[0].visibility = WGPUShaderStage_Fragment;
			e[0].texture.sampleType = WGPUTextureSampleType_Float;
			e[0].texture.viewDimension = WGPUTextureViewDimension_2D;
			e[1].binding = 1;
			e[1].visibility = WGPUShaderStage_Fragment;
			e[1].sampler.type = WGPUSamplerBindingType_Filtering;
			WGPUBindGroupLayoutDescriptor gd = {};
			gd.entryCount = 2;
			gd.entries = e;
			BlitGroup = wgpuDeviceCreateBindGroupLayout(Device, &gd);
			WGPUPipelineLayoutDescriptor pd = {};
			pd.bindGroupLayoutCount = 1;
			pd.bindGroupLayouts = &BlitGroup;
			BlitLayout = wgpuDeviceCreatePipelineLayout(Device, &pd);
			WGPUSamplerDescriptor sd = {};
			sd.addressModeU = sd.addressModeV = sd.addressModeW = WGPUAddressMode_ClampToEdge;
			sd.magFilter = sd.minFilter = WGPUFilterMode_Linear;
			sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
			sd.lodMaxClamp = 32.0f;
			sd.maxAnisotropy = 1;
			BlitSampler = wgpuDeviceCreateSampler(Device, &sd);
		}
		WGPUColorTargetState ct = {};
		ct.format = format;
		ct.writeMask = WGPUColorWriteMask_All;
		WGPUFragmentState fs = {};
		fs.module = BlitModule;
		fs.entryPoint = "fs";
		fs.targetCount = 1;
		fs.targets = &ct;
		WGPURenderPipelineDescriptor rd = {};
		rd.layout = BlitLayout;
		rd.vertex.module = BlitModule;
		rd.vertex.entryPoint = "vs";
		rd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
		rd.primitive.cullMode = WGPUCullMode_None;
		rd.multisample.count = 1;
		rd.multisample.mask = 0xFFFFFFFF;
		rd.fragment = &fs;
		WGPURenderPipeline p = wgpuDeviceCreateRenderPipeline(Device, &rd);
		BlitPipes[format] = p;
		return p;
	}

	void Dev::Blit(WGPUTextureView src, WGPUTextureView dst, WGPUTextureFormat dstFormat)
	{
		WGPURenderPipeline pipe = BlitPipeline(dstFormat);
		WGPUBindGroupEntry e[2] = {};
		e[0].binding = 0;
		e[0].textureView = src;
		e[1].binding = 1;
		e[1].sampler = BlitSampler;
		WGPUBindGroupDescriptor gd = {};
		gd.layout = BlitGroup;
		gd.entryCount = 2;
		gd.entries = e;
		WGPUBindGroup g = wgpuDeviceCreateBindGroup(Device, &gd);
		WGPURenderPassColorAttachment ca = {};
		ca.view = dst;
		ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
		ca.loadOp = WGPULoadOp_Clear;
		ca.storeOp = WGPUStoreOp_Store;
		WGPURenderPassDescriptor rp = {};
		rp.colorAttachmentCount = 1;
		rp.colorAttachments = &ca;
		if (Immediate) Immediate->EndPass();
		WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(Enc(), &rp);
		wgpuRenderPassEncoderSetPipeline(pass, pipe);
		wgpuRenderPassEncoderSetBindGroup(pass, 0, g, 0, nullptr);
		wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
		wgpuRenderPassEncoderEnd(pass);
		wgpuRenderPassEncoderRelease(pass);
		wgpuBindGroupRelease(g);
	}

	// ------------------------------------------------------------------ 장치 준비
	bool Dev::Init(std::string& error)
	{
		Device = emscripten_webgpu_get_device();
		if (!Device)
		{
			error = "no WebGPU device (the page did not set Module.preinitializedWebGPUDevice)";
			return false;
		}
		Queue = wgpuDeviceGetQueue(Device);
		Instance = wgpuCreateInstance(nullptr);
		WGPUSurfaceDescriptorFromCanvasHTMLSelector cs = {};
		cs.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
		cs.selector = "#canvas";
		WGPUSurfaceDescriptor sd = {};
		sd.nextInChain = &cs.chain;
		Surface = wgpuInstanceCreateSurface(Instance, &sd);
		Name = "WebGPU (browser)";
		return true;
	}

	// ------------------------------------------------------------------ 프로그램
	static WGPUTextureViewDimension DimOf(const std::string& d)
	{
		if (d == "1d") return WGPUTextureViewDimension_1D;
		if (d == "2dArray") return WGPUTextureViewDimension_2DArray;
		if (d == "Cube") return WGPUTextureViewDimension_Cube;
		if (d == "CubeArray") return WGPUTextureViewDimension_CubeArray;
		if (d == "3d") return WGPUTextureViewDimension_3D;
		return WGPUTextureViewDimension_2D;
	}

	static WGPUTextureFormat StorageFormatOf(const std::string& f)
	{
		static const std::pair<const char*, WGPUTextureFormat> table[] = {
			{ "R32Uint", WGPUTextureFormat_R32Uint }, { "R32Sint", WGPUTextureFormat_R32Sint }, { "R32Float", WGPUTextureFormat_R32Float },
			{ "Bgra8Unorm", WGPUTextureFormat_BGRA8Unorm }, { "Rgba8Unorm", WGPUTextureFormat_RGBA8Unorm }, { "Rgba8Snorm", WGPUTextureFormat_RGBA8Snorm },
			{ "Rgba8Uint", WGPUTextureFormat_RGBA8Uint }, { "Rgba8Sint", WGPUTextureFormat_RGBA8Sint }, { "Rg32Uint", WGPUTextureFormat_RG32Uint },
			{ "Rg32Sint", WGPUTextureFormat_RG32Sint }, { "Rg32Float", WGPUTextureFormat_RG32Float }, { "Rgba16Uint", WGPUTextureFormat_RGBA16Uint },
			{ "Rgba16Sint", WGPUTextureFormat_RGBA16Sint }, { "Rgba16Float", WGPUTextureFormat_RGBA16Float }, { "Rgba32Uint", WGPUTextureFormat_RGBA32Uint },
			{ "Rgba32Sint", WGPUTextureFormat_RGBA32Sint }, { "Rgba32Float", WGPUTextureFormat_RGBA32Float }, { "R8Unorm", WGPUTextureFormat_R8Unorm },
			{ "Rg8Unorm", WGPUTextureFormat_RG8Unorm }, { "R16Float", WGPUTextureFormat_R16Float }, { "Rg16Float", WGPUTextureFormat_RG16Float },
			{ "Rgb10A2Unorm", WGPUTextureFormat_RGB10A2Unorm }, { "Rg11B10Ufloat", WGPUTextureFormat_RG11B10Ufloat } };
		for (const auto& [n, v] : table)
			if (f == n) return v;
		return WGPUTextureFormat_Undefined;
	}

	HRESULT CreateProgram(GfxDevice* device, const StageSource* stages, uint32_t count, const std::vector<std::pair<std::string, int>>& vertexInputs,
		uint32_t pixelOutputs, const std::vector<std::pair<int, int>>& samplerPairs, const std::string& name, GfxObject** out, std::string& error)
	{
		auto* d = static_cast<Dev*>(device);
		ComPtr<Program> p;
		p.Attach(new Program(d));
		p->Id = NextId();
		p->Name = name;
		p->VertexInputs = vertexInputs;
		p->PixelOutputs = pixelOutputs;
		p->SamplerPairs = samplerPairs;
		std::map<int, ProgramBinding> bindings;
		for (uint32_t i = 0; i < count; ++i)
		{
			const StageSource& s = stages[i];
			Program::Stage st;
			switch (s.Stage)
			{
			case FxParser::Stage::Vertex: st.Flag = WGPUShaderStage_Vertex; break;
			case FxParser::Stage::Pixel: st.Flag = WGPUShaderStage_Fragment; break;
			case FxParser::Stage::Compute: st.Flag = WGPUShaderStage_Compute; p->Compute = true; break;
			default: error = "WebGPU has no tessellation / geometry stage"; return E_NOTIMPL;
			}
			WGPUShaderModuleWGSLDescriptor w = {};
			w.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
			w.code = s.Wgsl->c_str();
			WGPUShaderModuleDescriptor md = {};
			md.nextInChain = &w.chain;
			md.label = name.c_str();
			st.Module = wgpuDeviceCreateShaderModule(d->Device, &md);
			st.Entry = s.Entry;
			if (!st.Module) { error = "createShaderModule failed"; return E_FAIL; }
			p->Stages.push_back(st);
			if (s.Stage == FxParser::Stage::Vertex && s.Wgsl->find("instance_index") != std::string::npos)
				p->UsesInstanceIndex = true;
			for (const ShaderCross::WgslBinding& b : *s.Bindings)
			{
				ProgramBinding& pb = bindings[b.Binding];
				pb.Binding = b.Binding;
				pb.Visibility |= st.Flag;
				if (b.Type == "UniformBuffer") pb.Type = ProgramBinding::Kind::Uniform;
				else if (b.Type == "StorageBuffer") pb.Type = ProgramBinding::Kind::Storage;
				else if (b.Type == "ReadOnlyStorageBuffer") { if (pb.Type != ProgramBinding::Kind::Storage) pb.Type = ProgramBinding::Kind::ReadOnlyStorage; }
				else if (b.Type == "Sampler") pb.Type = b.SamplerType == "comparison" ? ProgramBinding::Kind::ComparisonSampler : ProgramBinding::Kind::Sampler;
				else if (b.Type == "SampledTexture" || b.Type == "MultisampledTexture")
				{
					pb.Type = ProgramBinding::Kind::Texture;
					pb.Dim = DimOf(b.Dim);
					pb.Sample = b.Sampled == "UInt" ? WGPUTextureSampleType_Uint : b.Sampled == "SInt" ? WGPUTextureSampleType_Sint : WGPUTextureSampleType_Float;
				}
				else if (b.Type == "DepthTexture" || b.Type == "DepthMultisampledTexture")
				{
					pb.Type = ProgramBinding::Kind::DepthTexture;
					pb.Dim = DimOf(b.Dim);
					pb.Sample = WGPUTextureSampleType_Depth;
				}
				else if (b.Type == "WriteOnlyStorageTexture" || b.Type == "ReadOnlyStorageTexture" || b.Type == "ReadWriteStorageTexture")
				{
					pb.Type = ProgramBinding::Kind::StorageTexture;
					pb.Dim = DimOf(b.Dim);
					pb.Access = b.Type == "WriteOnlyStorageTexture" ? WGPUStorageTextureAccess_WriteOnly :
						b.Type == "ReadOnlyStorageTexture" ? WGPUStorageTextureAccess_ReadOnly : WGPUStorageTextureAccess_ReadWrite;
					pb.StorageFormat = StorageFormatOf(b.Format);
				}
				else
				{
					error = "unsupported binding " + b.Type;
					return E_NOTIMPL;
				}
			}
		}
		for (auto& [b, pb] : bindings)
			p->Bindings.push_back(pb);
		int uniforms = 0;
		for (const ProgramBinding& b : p->Bindings)
			uniforms += b.Type == ProgramBinding::Kind::Uniform ? 1 : 0;
		p->DynamicUniforms = uniforms <= 8 ? uniforms : 0;   // 그 이상이면 오프셋을 바인드 그룹에 (maxDynamicUniformBuffersPerPipelineLayout)
		*out = p.Detach();
		return S_OK;
	}
}

// ============================================================================ GfxDevice
HRESULT Dev::CreateBuffer(const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxBuffer** out)
{
	*out = nullptr;
	if (!desc || desc->ByteWidth == 0) return E_INVALIDARG;
	ComPtr<Buf> b;
	b.Attach(new Buf(this));
	b->Desc = *desc;
	b->Id = NextId();
	b->Size = ((uint64_t)desc->ByteWidth + 3) & ~3ull;
	if (desc->Usage == D3D11_USAGE_DYNAMIC)
	{
		b->Shadow.assign(b->Size, 0);
		if (data && data->pSysMem)
		{
			memcpy(b->Shadow.data(), data->pSysMem, desc->ByteWidth);
			b->HasLoc = RingWrite(b->Shadow.data(), b->Size, 256, b->Loc);
		}
		*out = b.Detach();
		return S_OK;
	}
	WGPUBufferDescriptor bd = {};
	bd.size = b->Size;
	if (desc->Usage == D3D11_USAGE_STAGING && (desc->CPUAccessFlags & D3D11_CPU_ACCESS_READ))
		bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
	else
	{
		bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
		if (desc->BindFlags & D3D11_BIND_VERTEX_BUFFER) bd.usage |= WGPUBufferUsage_Vertex;
		if (desc->BindFlags & D3D11_BIND_INDEX_BUFFER) bd.usage |= WGPUBufferUsage_Index;
		if (desc->BindFlags & D3D11_BIND_CONSTANT_BUFFER) bd.usage |= WGPUBufferUsage_Uniform;
		if (desc->BindFlags & (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS)) bd.usage |= WGPUBufferUsage_Storage;
		if (desc->MiscFlags & D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS) bd.usage |= WGPUBufferUsage_Indirect;
		if (desc->Usage == D3D11_USAGE_STAGING) b->Shadow.assign(b->Size, 0);   // 올리기용 STAGING: Map(WRITE) = 사본
	}
	b->Handle = wgpuDeviceCreateBuffer(Device, &bd);
	if (!b->Handle) return E_OUTOFMEMORY;
	if (data && data->pSysMem && !(bd.usage & WGPUBufferUsage_MapRead))
	{
		if (b->Size == desc->ByteWidth)
			wgpuQueueWriteBuffer(Queue, b->Handle, 0, data->pSysMem, b->Size);
		else
		{
			std::vector<uint8_t> tmp(b->Size, 0);
			memcpy(tmp.data(), data->pSysMem, desc->ByteWidth);
			wgpuQueueWriteBuffer(Queue, b->Handle, 0, tmp.data(), b->Size);
		}
	}
	*out = b.Detach();
	return S_OK;
}

HRESULT Dev::MakeTexture(TexInfo& t, const D3D11_SUBRESOURCE_DATA* data, const char* what)
{
	t.Id = NextId();
	if (t.Format == WGPUTextureFormat_Undefined)
	{
		Once(std::string("fmt") + std::to_string((int)t.Dxgi), "texture format DXGI %s is not supported on WebGPU", std::to_string((int)t.Dxgi).c_str());
		return E_NOTIMPL;
	}
	if (t.Mips == 0)
	{
		UINT m = 1, s = (std::max)(t.Width, (std::max)(t.Height, t.Depth));
		while (s > 1) { s >>= 1; ++m; }
		t.Mips = m;
	}
	const bool depth = IsDepthFormat(t.Format);
	if (t.Usage == D3D11_USAGE_STAGING)
	{
		// 읽기 버퍼: 서브리소스마다 256 바이트 정렬 행
		UINT bw, bh;
		const UINT bpp = BlockBytes(t.Format, bw, bh);
		uint64_t at = 0;
		for (UINT l = 0; l < t.Layers; ++l)
			for (UINT m = 0; m < t.Mips; ++m)
			{
				const UINT w = (t.MipW(m) + bw - 1) / bw, h = (t.MipH(m) + bh - 1) / bh;
				const uint32_t pitch = (w * bpp + 255) & ~255u;
				t.SubOffset.push_back(at);
				t.SubRowPitch.push_back(pitch);
				at += (uint64_t)pitch * h * t.MipD(m);
			}
		t.StagingBytes = at;
		WGPUBufferDescriptor bd = {};
		bd.size = (at + 3) & ~3ull;
		bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
		t.Staging = wgpuDeviceCreateBuffer(Device, &bd);
		return t.Staging ? S_OK : E_OUTOFMEMORY;
	}
	WGPUTextureDescriptor td = {};
	td.label = what;
	td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc;
	if (!depth || t.Format == WGPUTextureFormat_Depth16Unorm) td.usage |= WGPUTextureUsage_CopyDst;
	if ((t.Bind & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL)) || ((t.Misc & D3D11_RESOURCE_MISC_GENERATE_MIPS) && !IsCompressed(t.Format)))
		td.usage |= WGPUTextureUsage_RenderAttachment;
	if (t.Bind & D3D11_BIND_UNORDERED_ACCESS)
	{
		if (StorageCapable(t.Format)) td.usage |= WGPUTextureUsage_StorageBinding;
		else Once("uav" + std::to_string((int)t.Format), "storage texture format %s not supported - UAV dropped", std::to_string((int)t.Dxgi).c_str());
	}
	td.dimension = t.Dim;
	td.size = { t.Width, t.Height, t.Dim == WGPUTextureDimension_3D ? t.Depth : t.Layers };
	td.format = t.Format;
	td.mipLevelCount = t.Mips;
	td.sampleCount = t.Samples > 1 ? 4 : 1;
	t.Samples = td.sampleCount;
	if (td.sampleCount > 1) td.usage &= ~(WGPUTextureUsage_CopyDst | WGPUTextureUsage_StorageBinding);
	WGPUTextureFormat viewFormats[1];
	const WGPUTextureFormat pair = SrgbPair(t.Format);
	if (pair != WGPUTextureFormat_Undefined)
	{
		viewFormats[0] = pair;
		td.viewFormatCount = 1;
		td.viewFormats = viewFormats;
	}
	t.Handle = wgpuDeviceCreateTexture(Device, &td);
	if (!t.Handle) return E_OUTOFMEMORY;
	if (data)
	{
		if (t.Dim == WGPUTextureDimension_3D)
		{
			for (UINT m = 0; m < t.Mips; ++m)
				if (data[m].pSysMem)
					UploadSub(t, m, nullptr, data[m].pSysMem, data[m].SysMemPitch, data[m].SysMemSlicePitch);
		}
		else
			for (UINT l = 0; l < t.Layers; ++l)
				for (UINT m = 0; m < t.Mips; ++m)
				{
					const D3D11_SUBRESOURCE_DATA& s = data[l * t.Mips + m];
					if (s.pSysMem)
						UploadSub(t, l * t.Mips + m, nullptr, s.pSysMem, s.SysMemPitch, s.SysMemSlicePitch);
				}
	}
	return S_OK;
}

void Dev::UploadSub(TexInfo& t, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch)
{
	const UINT mip = sub % t.Mips, layer = sub / t.Mips;
	UINT bw, bh;
	const UINT bpp = BlockBytes(t.Format, bw, bh);
	UINT x = 0, y = 0, z = 0, w = t.MipW(mip), h = t.MipH(mip), d = t.Dim == WGPUTextureDimension_3D ? t.MipD(mip) : 1;
	if (box)
	{
		x = box->left; y = box->top; z = box->front;
		w = box->right - box->left; h = box->bottom - box->top; d = (std::max)(1u, box->back - box->front);
	}
	// 압축 형식: 블록 단위 (작은 밉의 물리 크기 = 블록으로 올림)
	w = (w + bw - 1) / bw * bw;
	h = (h + bh - 1) / bh * bh;
	const UINT rows = h / bh;
	if (rowPitch == 0) rowPitch = (w / bw) * bpp;
	WGPUImageCopyTexture dst = {};
	dst.texture = t.Handle;
	dst.mipLevel = mip;
	dst.origin = { x, y, t.Dim == WGPUTextureDimension_3D ? z : layer };
	dst.aspect = WGPUTextureAspect_All;
	WGPUTextureDataLayout layout = {};
	layout.bytesPerRow = rowPitch;
	layout.rowsPerImage = depthPitch && rowPitch ? depthPitch / rowPitch : rows;
	const WGPUExtent3D ext = { w, h, d };
	const size_t bytes = (size_t)rowPitch * (rows - 1) + (size_t)(w / bw) * bpp + (size_t)(d - 1) * layout.rowsPerImage * rowPitch;
	wgpuQueueWriteTexture(Queue, &dst, data, bytes, &layout, &ext);
}

HRESULT Dev::CreateTexture1D(const D3D11_TEXTURE1D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture1D** out)
{
	*out = nullptr;
	ComPtr<Tex1D> t;
	t.Attach(new Tex1D(this));
	t->Desc = *desc;
	TexInfo& i = t->I;
	i.Dxgi = desc->Format;
	i.Format = TextureFormat(desc->Format, desc->BindFlags);
	i.Dim = desc->MipLevels == 1 && !(desc->BindFlags & D3D11_BIND_RENDER_TARGET) ? WGPUTextureDimension_1D : WGPUTextureDimension_2D;
	i.Width = desc->Width;
	i.Layers = (std::max)(1u, desc->ArraySize);
	i.Mips = desc->MipLevels;
	i.Usage = desc->Usage; i.Bind = desc->BindFlags; i.CpuAccess = desc->CPUAccessFlags; i.Misc = desc->MiscFlags;
	const HRESULT hr = MakeTexture(i, data, "Texture1D");
	if (FAILED(hr)) return hr;
	t->Desc.MipLevels = i.Mips;
	*out = t.Detach();
	return S_OK;
}

HRESULT Dev::CreateTexture2D(const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture2D** out)
{
	*out = nullptr;
	ComPtr<Tex2D> t;
	t.Attach(new Tex2D(this));
	t->Desc = *desc;
	TexInfo& i = t->I;
	i.Dxgi = desc->Format;
	i.Format = TextureFormat(desc->Format, desc->BindFlags);
	i.Width = desc->Width;
	i.Height = desc->Height;
	i.Layers = (std::max)(1u, desc->ArraySize);
	i.Mips = desc->MipLevels;
	i.Samples = desc->SampleDesc.Count;
	i.Cube = (desc->MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0;
	i.Usage = desc->Usage; i.Bind = desc->BindFlags; i.CpuAccess = desc->CPUAccessFlags; i.Misc = desc->MiscFlags;
	const HRESULT hr = MakeTexture(i, data, "Texture2D");
	if (FAILED(hr)) return hr;
	t->Desc.MipLevels = i.Mips;
	t->Desc.SampleDesc.Count = i.Samples;
	*out = t.Detach();
	return S_OK;
}

HRESULT Dev::CreateTexture3D(const D3D11_TEXTURE3D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, GfxTexture3D** out)
{
	*out = nullptr;
	ComPtr<Tex3D> t;
	t.Attach(new Tex3D(this));
	t->Desc = *desc;
	TexInfo& i = t->I;
	i.Dxgi = desc->Format;
	i.Format = TextureFormat(desc->Format, desc->BindFlags);
	i.Dim = WGPUTextureDimension_3D;
	i.Width = desc->Width;
	i.Height = desc->Height;
	i.Depth = desc->Depth;
	i.Mips = desc->MipLevels;
	i.Usage = desc->Usage; i.Bind = desc->BindFlags; i.CpuAccess = desc->CPUAccessFlags; i.Misc = desc->MiscFlags;
	const HRESULT hr = MakeTexture(i, data, "Texture3D");
	if (FAILED(hr)) return hr;
	t->Desc.MipLevels = i.Mips;
	*out = t.Detach();
	return S_OK;
}

WGPUTextureView Dev::MakeView(TexInfo& t, WGPUTextureViewDimension dim, WGPUTextureFormat format, WGPUTextureAspect aspect, UINT baseMip, UINT mips, UINT baseLayer, UINT layers)
{
	if (!t.Handle) return nullptr;
	WGPUTextureViewDescriptor vd = {};
	vd.format = format;
	if (aspect == WGPUTextureAspect_DepthOnly)
		vd.format = format == WGPUTextureFormat_Depth24PlusStencil8 ? WGPUTextureFormat_Depth24Plus : format == WGPUTextureFormat_Depth32FloatStencil8 ? WGPUTextureFormat_Depth32Float : format;
	else if (aspect == WGPUTextureAspect_StencilOnly)
		vd.format = WGPUTextureFormat_Stencil8;
	vd.dimension = dim;
	vd.baseMipLevel = baseMip;
	vd.mipLevelCount = (std::min)(mips, t.Mips - baseMip);
	vd.baseArrayLayer = t.Dim == WGPUTextureDimension_3D ? 0 : baseLayer;
	vd.arrayLayerCount = t.Dim == WGPUTextureDimension_3D ? 1 : (std::min)(layers, t.Layers - baseLayer);
	vd.aspect = aspect;
	return wgpuTextureCreateView(t.Handle, &vd);
}

HRESULT Dev::CreateShaderResourceView(GfxResource* r, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc, GfxShaderResourceView** out)
{
	*out = nullptr;
	ComPtr<Srv> v;
	v.Attach(new Srv(this));
	v->V.Res = r;
	v->V.Id = NextId();
	if (Buf* b = BufOf(r))
	{
		// 구조 · raw 버퍼 SRV → 스토리지 버퍼 범위
		v->V.Buffer = b;
		UINT stride = b->Desc.StructureByteStride ? b->Desc.StructureByteStride : 4;
		UINT first = 0, num = b->Desc.ByteWidth / stride;
		if (desc)
		{
			v->Dsc = *desc;
			if (desc->ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX) { first = desc->BufferEx.FirstElement; num = desc->BufferEx.NumElements; }
			else { first = desc->Buffer.FirstElement; num = desc->Buffer.NumElements; }
			if (desc->Format == DXGI_FORMAT_R32_TYPELESS) stride = 4;
		}
		v->V.BufOffset = (uint64_t)first * stride;
		v->V.BufSize = ((uint64_t)num * stride + 3) & ~3ull;
		*out = v.Detach();
		return S_OK;
	}
	TexInfo* t = TexOf(r);
	if (!t) return E_INVALIDARG;
	v->V.Tex = t;
	WGPUTextureAspect aspect;
	const WGPUTextureFormat fmt = ViewFormat(desc ? desc->Format : DXGI_FORMAT_UNKNOWN, t->Format, aspect);
	v->V.Format = fmt;
	v->V.DepthFormat = IsDepthFormat(t->Format);
	if (v->V.DepthFormat && aspect == WGPUTextureAspect_All && HasStencil(t->Format)) aspect = WGPUTextureAspect_DepthOnly;
	v->V.Aspect = aspect;
	UINT baseMip = 0, mips = t->Mips, baseLayer = 0, layers = t->Layers;
	WGPUTextureViewDimension dim = t->Dim == WGPUTextureDimension_3D ? WGPUTextureViewDimension_3D : t->Dim == WGPUTextureDimension_1D ? WGPUTextureViewDimension_1D :
		t->Cube ? (t->Layers > 6 ? WGPUTextureViewDimension_CubeArray : WGPUTextureViewDimension_Cube) : t->Layers > 1 ? WGPUTextureViewDimension_2DArray : WGPUTextureViewDimension_2D;
	if (desc)
	{
		v->Dsc = *desc;
		switch (desc->ViewDimension)
		{
		case D3D11_SRV_DIMENSION_TEXTURE1D: dim = t->Dim == WGPUTextureDimension_1D ? WGPUTextureViewDimension_1D : WGPUTextureViewDimension_2D; baseMip = desc->Texture1D.MostDetailedMip; mips = desc->Texture1D.MipLevels; layers = 1; break;
		case D3D11_SRV_DIMENSION_TEXTURE2D: dim = WGPUTextureViewDimension_2D; baseMip = desc->Texture2D.MostDetailedMip; mips = desc->Texture2D.MipLevels; layers = 1; break;
		case D3D11_SRV_DIMENSION_TEXTURE2DMS: dim = WGPUTextureViewDimension_2D; mips = 1; layers = 1; break;
		case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
			dim = WGPUTextureViewDimension_2DArray; baseMip = desc->Texture2DArray.MostDetailedMip; mips = desc->Texture2DArray.MipLevels;
			baseLayer = desc->Texture2DArray.FirstArraySlice; layers = desc->Texture2DArray.ArraySize; break;
		case D3D11_SRV_DIMENSION_TEXTURE3D: dim = WGPUTextureViewDimension_3D; baseMip = desc->Texture3D.MostDetailedMip; mips = desc->Texture3D.MipLevels; break;
		case D3D11_SRV_DIMENSION_TEXTURECUBE: dim = WGPUTextureViewDimension_Cube; baseMip = desc->TextureCube.MostDetailedMip; mips = desc->TextureCube.MipLevels; layers = 6; break;
		case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
			dim = WGPUTextureViewDimension_CubeArray; baseMip = desc->TextureCubeArray.MostDetailedMip; mips = desc->TextureCubeArray.MipLevels;
			baseLayer = desc->TextureCubeArray.First2DArrayFace; layers = desc->TextureCubeArray.NumCubes * 6; break;
		default: break;
		}
	}
	if (mips == (UINT)-1 || mips == 0) mips = t->Mips - baseMip;
	v->V.Dim = dim; v->V.BaseMip = baseMip; v->V.Mips = mips; v->V.BaseLayer = baseLayer; v->V.Layers = layers;
	v->V.View = MakeView(*t, dim, fmt, aspect, baseMip, mips, baseLayer, layers);
	if (!v->V.View) return E_FAIL;
	*out = v.Detach();
	return S_OK;
}

HRESULT Dev::CreateRenderTargetView(GfxResource* r, const D3D11_RENDER_TARGET_VIEW_DESC* desc, GfxRenderTargetView** out)
{
	*out = nullptr;
	TexInfo* t = TexOf(r);
	if (!t) return E_INVALIDARG;
	ComPtr<Rtv> v;
	v.Attach(new Rtv(this));
	v->V.Res = r;
	v->V.Tex = t;
	v->V.Id = NextId();
	WGPUTextureAspect aspect;
	v->V.Format = ViewFormat(desc ? desc->Format : DXGI_FORMAT_UNKNOWN, t->Format, aspect);
	UINT mip = 0, layer = 0;
	if (desc)
	{
		v->Dsc = *desc;
		switch (desc->ViewDimension)
		{
		case D3D11_RTV_DIMENSION_TEXTURE2D: mip = desc->Texture2D.MipSlice; break;
		case D3D11_RTV_DIMENSION_TEXTURE2DARRAY: mip = desc->Texture2DArray.MipSlice; layer = desc->Texture2DArray.FirstArraySlice; break;
		case D3D11_RTV_DIMENSION_TEXTURE3D: mip = desc->Texture3D.MipSlice; layer = desc->Texture3D.FirstWSlice; break;
		default: break;
		}
	}
	v->V.BaseMip = mip; v->V.BaseLayer = layer;
	// 렌더 타깃 = 2D 보기 한 장 (3D 는 depthSlice 로 — 지금은 첫 조각)
	v->V.View = MakeView(*t, t->Dim == WGPUTextureDimension_3D ? WGPUTextureViewDimension_3D : WGPUTextureViewDimension_2D, v->V.Format, WGPUTextureAspect_All, mip, 1,
		t->Dim == WGPUTextureDimension_3D ? 0 : layer, 1);
	if (!v->V.View) return E_FAIL;
	*out = v.Detach();
	return S_OK;
}

HRESULT Dev::CreateDepthStencilView(GfxResource* r, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, GfxDepthStencilView** out)
{
	*out = nullptr;
	TexInfo* t = TexOf(r);
	if (!t) return E_INVALIDARG;
	ComPtr<Dsv> v;
	v.Attach(new Dsv(this));
	v->V.Res = r;
	v->V.Tex = t;
	v->V.Id = NextId();
	v->V.Format = t->Format;
	v->V.DepthFormat = true;
	UINT mip = 0, layer = 0, layers = 1;
	if (desc)
	{
		v->Dsc = *desc;
		switch (desc->ViewDimension)
		{
		case D3D11_DSV_DIMENSION_TEXTURE2D: mip = desc->Texture2D.MipSlice; break;
		case D3D11_DSV_DIMENSION_TEXTURE2DARRAY: mip = desc->Texture2DArray.MipSlice; layer = desc->Texture2DArray.FirstArraySlice; layers = desc->Texture2DArray.ArraySize; break;
		default: break;
		}
	}
	v->V.BaseMip = mip; v->V.BaseLayer = layer; v->V.Layers = (std::max)(1u, layers);
	// WebGPU 의 깊이 타깃은 조각 하나 — 여러 조각 DSV 는 지우기 (ClearDepthStencilView 가 조각마다) 에만 쓴다
	v->V.View = MakeView(*t, WGPUTextureViewDimension_2D, t->Format, WGPUTextureAspect_All, mip, 1, layer, 1);
	if (!v->V.View) return E_FAIL;
	*out = v.Detach();
	return S_OK;
}

HRESULT Dev::CreateUnorderedAccessView(GfxResource* r, const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc, GfxUnorderedAccessView** out)
{
	*out = nullptr;
	ComPtr<Uav> v;
	v.Attach(new Uav(this));
	v->V.Res = r;
	v->V.Id = NextId();
	if (Buf* b = BufOf(r))
	{
		v->V.Buffer = b;
		UINT stride = b->Desc.StructureByteStride ? b->Desc.StructureByteStride : 4;
		UINT first = 0, num = b->Desc.ByteWidth / stride;
		if (desc)
		{
			v->Dsc = *desc;
			first = desc->Buffer.FirstElement;
			num = desc->Buffer.NumElements;
			if (desc->Format == DXGI_FORMAT_R32_TYPELESS) stride = 4;
		}
		v->V.BufOffset = (uint64_t)first * stride;
		v->V.BufSize = ((uint64_t)num * stride + 3) & ~3ull;
		*out = v.Detach();
		return S_OK;
	}
	TexInfo* t = TexOf(r);
	if (!t) return E_INVALIDARG;
	v->V.Tex = t;
	WGPUTextureAspect aspect;
	v->V.Format = ViewFormat(desc ? desc->Format : DXGI_FORMAT_UNKNOWN, t->Format, aspect);
	UINT mip = 0, layer = 0, layers = 1;
	WGPUTextureViewDimension dim = t->Dim == WGPUTextureDimension_3D ? WGPUTextureViewDimension_3D : t->Layers > 1 ? WGPUTextureViewDimension_2DArray : WGPUTextureViewDimension_2D;
	if (desc)
	{
		v->Dsc = *desc;
		switch (desc->ViewDimension)
		{
		case D3D11_UAV_DIMENSION_TEXTURE2D: mip = desc->Texture2D.MipSlice; dim = WGPUTextureViewDimension_2D; break;
		case D3D11_UAV_DIMENSION_TEXTURE2DARRAY: mip = desc->Texture2DArray.MipSlice; layer = desc->Texture2DArray.FirstArraySlice; layers = desc->Texture2DArray.ArraySize; dim = WGPUTextureViewDimension_2DArray; break;
		case D3D11_UAV_DIMENSION_TEXTURE3D: mip = desc->Texture3D.MipSlice; dim = WGPUTextureViewDimension_3D; break;
		default: break;
		}
	}
	else if (t->Dim != WGPUTextureDimension_3D)
		layers = t->Layers;
	v->V.Dim = dim; v->V.BaseMip = mip; v->V.Mips = 1; v->V.BaseLayer = layer; v->V.Layers = layers;
	v->V.View = MakeView(*t, dim, v->V.Format, WGPUTextureAspect_All, mip, 1, layer, layers);
	if (!v->V.View) return E_FAIL;
	*out = v.Detach();
	return S_OK;
}

HRESULT Dev::CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* elements, UINT count, const void* signature, SIZE_T signatureSize, GfxInputLayout** out)
{
	*out = nullptr;
	ComPtr<InputLayout> l;
	l.Attach(new InputLayout(this));
	l->Id = NextId();
	UINT offsets[16] = {};
	for (UINT i = 0; i < count; ++i)
	{
		const D3D11_INPUT_ELEMENT_DESC& e = elements[i];
		InputLayout::Element el;
		std::string sem = e.SemanticName;
		for (char& c : sem) c = (char)toupper((unsigned char)c);
		el.Semantic = sem + std::to_string(e.SemanticIndex);
		el.Slot = e.InputSlot & 15;
		el.Format = VertexFormat(e.Format);
		el.Offset = e.AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT ? offsets[el.Slot] : e.AlignedByteOffset;
		UINT bw, bh;
		const WGPUTextureFormat tf = TextureFormat(e.Format, 0);
		const UINT size = e.Format == DXGI_FORMAT_R32G32B32_FLOAT || e.Format == DXGI_FORMAT_R32G32B32_UINT ? 12 : tf != WGPUTextureFormat_Undefined ? BlockBytes(tf, bw, bh) : 16;
		offsets[el.Slot] = el.Offset + size;
		if (e.InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA) l->PerInstance[el.Slot] = true;
		if (el.Format == WGPUVertexFormat_Undefined)
			Once("vf" + std::to_string((int)e.Format), ("vertex format DXGI %s is not supported on WebGPU (" + el.Semantic + ")").c_str(), std::to_string((int)e.Format).c_str());
		l->Elements.push_back(el);
	}
	(void)signature; (void)signatureSize;
	*out = l.Detach();
	return S_OK;
}

HRESULT Dev::CreateRasterizerState(const D3D11_RASTERIZER_DESC* desc, GfxRasterizerState** out)
{
	ComPtr<Rasterizer> s;
	s.Attach(new Rasterizer(this));
	s->Dsc = desc ? *desc : DefaultRasterizer();
	s->Hash = HashBytes(&s->Dsc, sizeof(s->Dsc));
	*out = s.Detach();
	return S_OK;
}

HRESULT Dev::CreateBlendState(const D3D11_BLEND_DESC* desc, GfxBlendState** out)
{
	ComPtr<Blend> s;
	s.Attach(new Blend(this));
	s->Dsc = desc ? *desc : DefaultBlend();
	s->Hash = HashBytes(&s->Dsc, sizeof(s->Dsc));
	*out = s.Detach();
	return S_OK;
}

HRESULT Dev::CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC* desc, GfxDepthStencilState** out)
{
	ComPtr<DepthStencil> s;
	s.Attach(new DepthStencil(this));
	s->Dsc = desc ? *desc : DefaultDepthStencil();
	s->Hash = HashBytes(&s->Dsc, sizeof(s->Dsc));
	*out = s.Detach();
	return S_OK;
}

static WGPUAddressMode Address(D3D11_TEXTURE_ADDRESS_MODE m)
{
	switch (m)
	{
	case D3D11_TEXTURE_ADDRESS_WRAP: return WGPUAddressMode_Repeat;
	case D3D11_TEXTURE_ADDRESS_MIRROR: case D3D11_TEXTURE_ADDRESS_MIRROR_ONCE: return WGPUAddressMode_MirrorRepeat;
	default: return WGPUAddressMode_ClampToEdge;   // BORDER 도 (WebGPU 에 테두리 색 없음)
	}
}

HRESULT Dev::CreateSamplerState(const D3D11_SAMPLER_DESC* desc, GfxSamplerState** out)
{
	*out = nullptr;
	ComPtr<Sampler> s;
	s.Attach(new Sampler(this));
	D3D11_SAMPLER_DESC d = desc ? *desc : D3D11_SAMPLER_DESC{ D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, 0, 1, D3D11_COMPARISON_NEVER, {}, 0, D3D11_FLOAT32_MAX };
	s->Dsc = d;
	s->Id = NextId();
	WGPUSamplerDescriptor sd = {};
	sd.addressModeU = Address(d.AddressU);
	sd.addressModeV = Address(d.AddressV);
	sd.addressModeW = Address(d.AddressW);
	const int f = (int)d.Filter;
	const bool aniso = (f & 0x40) != 0;   // D3D11_FILTER_ANISOTROPIC 계열
	sd.minFilter = aniso || (f & 0x10) ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
	sd.magFilter = aniso || (f & 0x04) ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
	sd.mipmapFilter = aniso || (f & 0x01) ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
	sd.lodMinClamp = (std::max)(0.0f, d.MinLOD);
	sd.lodMaxClamp = (std::min)(32.0f, (std::max)(sd.lodMinClamp, d.MaxLOD));
	s->Comparison = (f & 0x80) != 0;   // D3D11_FILTER_COMPARISON_*
	sd.compare = s->Comparison ? Compare(d.ComparisonFunc) : WGPUCompareFunction_Undefined;
	sd.maxAnisotropy = aniso && sd.minFilter == WGPUFilterMode_Linear && sd.magFilter == WGPUFilterMode_Linear && sd.mipmapFilter == WGPUMipmapFilterMode_Linear ?
		(uint16_t)std::clamp(d.MaxAnisotropy, 1u, 16u) : 1;
	s->Handle = wgpuDeviceCreateSampler(Device, &sd);
	if (!s->Handle) return E_FAIL;
	*out = s.Detach();
	return S_OK;
}

HRESULT Dev::CreateQuery(const D3D11_QUERY_DESC* desc, GfxQuery** out)
{
	ComPtr<Query> q;
	q.Attach(new Query(this));
	q->Dsc = desc ? *desc : D3D11_QUERY_DESC{};
	*out = q.Detach();
	return S_OK;
}

void Dev::GetImmediateContext(GfxContext** out)
{
	*out = Immediate;
	if (*out) (*out)->AddRef();
}

// ============================================================================ 공개
namespace GfxWgpu
{
	bool CreateDevice(GfxDevice** device, GfxContext** context, std::string& error)
	{
		ComPtr<Dev> d;
		d.Attach(new Dev());
		if (!d->Init(error))
			return false;
		auto* c = new Ctx();
		c->D = d;
		d->Immediate = c;
		*device = d.Detach();
		*context = c;
		return true;
	}

	bool IsWgpu(GfxObject* object) { return object && object->Api() == GfxApi::WebGPU; }

	std::string Description(GfxDevice* device) { return IsWgpu(device) ? static_cast<Dev*>(device)->Name : std::string(); }

	void Present(GfxDevice* device, GfxTexture2D* backBuffer, int canvasWidth, int canvasHeight)
	{
		auto* d = static_cast<Dev*>(device);
		if (!d || !d->Surface) return;
		canvasWidth = (std::max)(1, canvasWidth);
		canvasHeight = (std::max)(1, canvasHeight);
		if (!d->Swap || d->SwapW != canvasWidth || d->SwapH != canvasHeight)
		{
			if (d->Swap) wgpuSwapChainRelease(d->Swap);
			WGPUSwapChainDescriptor sd = {};
			sd.usage = WGPUTextureUsage_RenderAttachment;
			sd.format = d->SwapFormat;
			sd.width = (uint32_t)canvasWidth;
			sd.height = (uint32_t)canvasHeight;
			sd.presentMode = WGPUPresentMode_Fifo;
			d->Swap = wgpuDeviceCreateSwapChain(d->Device, d->Surface, &sd);
			d->SwapW = canvasWidth;
			d->SwapH = canvasHeight;
		}
		if (d->Immediate) d->Immediate->FlushClears();
		if (TexInfo* t = TexOf(backBuffer); t && t->Handle && d->Swap)
		{
			WGPUTextureView src = d->MakeView(*t, WGPUTextureViewDimension_2D, t->Format, WGPUTextureAspect_All, 0, 1, 0, 1);
			WGPUTextureView dst = wgpuSwapChainGetCurrentTextureView(d->Swap);
			if (src && dst)
				d->Blit(src, dst, d->SwapFormat);
			if (src) wgpuTextureViewRelease(src);
			if (dst) wgpuTextureViewRelease(dst);
		}
		d->Submit();
		// 프레임 끝: 링을 처음부터 (다음 프레임의 writeBuffer 는 이 제출 뒤에 실행된다), 바인드 그룹 캐시 비우기
		d->RingReset();
		++d->Frame;
		LastStats = FrameStats;
		FrameStats = Stats();
		for (auto& [k, g] : d->BindGroups) wgpuBindGroupRelease(g);
		d->BindGroups.clear();
	}
}

// ============================================================================ Gfx 공통 (DirectXTex 이미지 → 텍스처 — GfxGLES 와 같은 규칙)
namespace
{
	ComPtr<GfxDevice> s_Device;
	ComPtr<GfxContext> s_Context;
}

namespace Gfx
{
	GfxDevice* Device() { return s_Device.Get(); }
	GfxContext* Context() { return s_Context.Get(); }
	void SetMain(GfxDevice* device, GfxContext* context)
	{
		s_Device = device;
		s_Context = context;
	}

	GfxObject* WrapD3D11(IUnknown*) { return nullptr; }

	HRESULT CreateTexture(GfxDevice* device, const DirectX::Image* images, size_t count, const DirectX::TexMetadata& meta,
		D3D11_USAGE usage, UINT bindFlags, UINT cpuAccess, UINT miscFlags, GfxResource** out)
	{
		*out = nullptr;
		std::vector<D3D11_SUBRESOURCE_DATA> data;
		if (meta.dimension == DirectX::TEX_DIMENSION_TEXTURE3D)
		{
			size_t index = 0;
			for (size_t m = 0; m < meta.mipLevels && index < count; ++m)
			{
				const DirectX::Image& img = images[index];
				data.push_back({ img.pixels, (UINT)img.rowPitch, (UINT)img.slicePitch });
				index += (std::max<size_t>)(1, meta.depth >> m);
			}
		}
		else
		{
			const size_t subs = meta.arraySize * meta.mipLevels;
			if (count < subs) return E_INVALIDARG;
			for (size_t i = 0; i < subs; ++i)
				data.push_back({ images[i].pixels, (UINT)images[i].rowPitch, (UINT)images[i].slicePitch });
		}
		switch (meta.dimension)
		{
		case DirectX::TEX_DIMENSION_TEXTURE1D:
		{
			D3D11_TEXTURE1D_DESC d = { (UINT)meta.width, (UINT)meta.mipLevels, (UINT)meta.arraySize, meta.format, usage, bindFlags, cpuAccess, miscFlags };
			GfxTexture1D* t = nullptr;
			const HRESULT hr = device->CreateTexture1D(&d, data.data(), &t);
			*out = t;
			return hr;
		}
		case DirectX::TEX_DIMENSION_TEXTURE3D:
		{
			D3D11_TEXTURE3D_DESC d = { (UINT)meta.width, (UINT)meta.height, (UINT)meta.depth, (UINT)meta.mipLevels, meta.format, usage, bindFlags, cpuAccess, miscFlags };
			GfxTexture3D* t = nullptr;
			const HRESULT hr = device->CreateTexture3D(&d, data.data(), &t);
			*out = t;
			return hr;
		}
		default:
		{
			D3D11_TEXTURE2D_DESC d = {};
			d.Width = (UINT)meta.width;
			d.Height = (UINT)meta.height;
			d.MipLevels = (UINT)meta.mipLevels;
			d.ArraySize = (UINT)meta.arraySize;
			d.Format = meta.format;
			d.SampleDesc.Count = 1;
			d.Usage = usage;
			d.BindFlags = bindFlags;
			d.CPUAccessFlags = cpuAccess;
			d.MiscFlags = miscFlags | (meta.IsCubemap() ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0);
			GfxTexture2D* t = nullptr;
			const HRESULT hr = device->CreateTexture2D(&d, data.data(), &t);
			*out = t;
			return hr;
		}
		}
	}

	HRESULT CreateShaderResourceView(GfxDevice* device, const DirectX::Image* images, size_t count, const DirectX::TexMetadata& meta, GfxShaderResourceView** out)
	{
		*out = nullptr;
		ComPtr<GfxResource> tex;
		HRESULT hr = CreateTexture(device, images, count, meta, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, tex.GetAddressOf());
		if (FAILED(hr)) return hr;
		return device->CreateShaderResourceView(tex.Get(), nullptr, out);
	}

	// 웹: GPU 읽기는 비동기뿐 — 화면 캡처는 캔버스에서 (Web/Shell 의 검사 함수)
	HRESULT CaptureTexture(GfxContext*, GfxResource*, DirectX::ScratchImage&) { return E_NOTIMPL; }
}
