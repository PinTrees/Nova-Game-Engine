#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

// 안드로이드에서 엔진 코드를 컴파일하기 위한 Windows · D3D11 타입 대체 (엔진이 실제로 쓰는 것만).
//  값은 Windows SDK 의 d3d11.h 와 같다 (상태 설명 구조체 · 열거형 — .fx 상태 블록 변환(FxStates)과 렌더러 상태가 쓴다)
typedef uint8_t BYTE;
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT;
typedef uint32_t DWORD;
typedef uint32_t ULONG;
typedef uint64_t UINT64;
typedef int32_t INT;
typedef int32_t LONG;      // Windows 의 LONG = 32 비트 (안드로이드 long = 64 비트)
typedef int64_t INT64;
typedef int32_t BOOL;
typedef int32_t HRESULT;
typedef float FLOAT;
typedef size_t SIZE_T;
typedef void* HWND;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_FAIL ((HRESULT)0x80004005)
#define E_INVALIDARG ((HRESULT)0x80070057)
#define E_OUTOFMEMORY ((HRESULT)0x8007000E)
#define E_NOTIMPL ((HRESULT)0x80004001)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)

struct RECT { LONG left, top, right, bottom; };

#define ZeroMemory(p, n) memset((p), 0, (n))

// 엔진 Types.h 의 정수 이름
using int8 = int8_t;
using int16 = int16_t;
using int32 = int32_t;
using int64 = int64_t;
using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

#define D3D11_FLOAT32_MAX (3.402823466e+38f)

enum D3D11_COMPARISON_FUNC
{
	D3D11_COMPARISON_NEVER = 1, D3D11_COMPARISON_LESS = 2, D3D11_COMPARISON_EQUAL = 3, D3D11_COMPARISON_LESS_EQUAL = 4,
	D3D11_COMPARISON_GREATER = 5, D3D11_COMPARISON_NOT_EQUAL = 6, D3D11_COMPARISON_GREATER_EQUAL = 7, D3D11_COMPARISON_ALWAYS = 8
};
enum D3D11_BLEND
{
	D3D11_BLEND_ZERO = 1, D3D11_BLEND_ONE = 2, D3D11_BLEND_SRC_COLOR = 3, D3D11_BLEND_INV_SRC_COLOR = 4, D3D11_BLEND_SRC_ALPHA = 5,
	D3D11_BLEND_INV_SRC_ALPHA = 6, D3D11_BLEND_DEST_ALPHA = 7, D3D11_BLEND_INV_DEST_ALPHA = 8, D3D11_BLEND_DEST_COLOR = 9,
	D3D11_BLEND_INV_DEST_COLOR = 10, D3D11_BLEND_SRC_ALPHA_SAT = 11, D3D11_BLEND_BLEND_FACTOR = 14, D3D11_BLEND_INV_BLEND_FACTOR = 15,
	D3D11_BLEND_SRC1_COLOR = 16, D3D11_BLEND_INV_SRC1_COLOR = 17, D3D11_BLEND_SRC1_ALPHA = 18, D3D11_BLEND_INV_SRC1_ALPHA = 19
};
enum D3D11_BLEND_OP { D3D11_BLEND_OP_ADD = 1, D3D11_BLEND_OP_SUBTRACT = 2, D3D11_BLEND_OP_REV_SUBTRACT = 3, D3D11_BLEND_OP_MIN = 4, D3D11_BLEND_OP_MAX = 5 };
enum D3D11_STENCIL_OP
{
	D3D11_STENCIL_OP_KEEP = 1, D3D11_STENCIL_OP_ZERO = 2, D3D11_STENCIL_OP_REPLACE = 3, D3D11_STENCIL_OP_INCR_SAT = 4,
	D3D11_STENCIL_OP_DECR_SAT = 5, D3D11_STENCIL_OP_INVERT = 6, D3D11_STENCIL_OP_INCR = 7, D3D11_STENCIL_OP_DECR = 8
};
enum D3D11_DEPTH_WRITE_MASK { D3D11_DEPTH_WRITE_MASK_ZERO = 0, D3D11_DEPTH_WRITE_MASK_ALL = 1 };
enum D3D11_FILL_MODE { D3D11_FILL_WIREFRAME = 2, D3D11_FILL_SOLID = 3 };
enum D3D11_CULL_MODE { D3D11_CULL_NONE = 1, D3D11_CULL_FRONT = 2, D3D11_CULL_BACK = 3 };
enum D3D11_TEXTURE_ADDRESS_MODE
{
	D3D11_TEXTURE_ADDRESS_WRAP = 1, D3D11_TEXTURE_ADDRESS_MIRROR = 2, D3D11_TEXTURE_ADDRESS_CLAMP = 3, D3D11_TEXTURE_ADDRESS_BORDER = 4,
	D3D11_TEXTURE_ADDRESS_MIRROR_ONCE = 5
};
// 필터 비트: MIP 1 · MAG 4 · MIN 0x10 · 비등방 0x55 · 비교 0x80 (FxStates 가 비트로 만든다)
enum D3D11_FILTER
{
	D3D11_FILTER_MIN_MAG_MIP_POINT = 0, D3D11_FILTER_MIN_MAG_MIP_LINEAR = 0x15, D3D11_FILTER_ANISOTROPIC = 0x55,
	D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT = 0x94, D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR = 0x95
};
enum D3D11_COLOR_WRITE_ENABLE { D3D11_COLOR_WRITE_ENABLE_ALL = 15 };

struct D3D11_RASTERIZER_DESC
{
	D3D11_FILL_MODE FillMode;
	D3D11_CULL_MODE CullMode;
	BOOL FrontCounterClockwise;
	INT DepthBias;
	FLOAT DepthBiasClamp;
	FLOAT SlopeScaledDepthBias;
	BOOL DepthClipEnable;
	BOOL ScissorEnable;
	BOOL MultisampleEnable;
	BOOL AntialiasedLineEnable;
};

struct D3D11_RENDER_TARGET_BLEND_DESC
{
	BOOL BlendEnable;
	D3D11_BLEND SrcBlend;
	D3D11_BLEND DestBlend;
	D3D11_BLEND_OP BlendOp;
	D3D11_BLEND SrcBlendAlpha;
	D3D11_BLEND DestBlendAlpha;
	D3D11_BLEND_OP BlendOpAlpha;
	UINT8 RenderTargetWriteMask;
};

struct D3D11_BLEND_DESC
{
	BOOL AlphaToCoverageEnable;
	BOOL IndependentBlendEnable;
	D3D11_RENDER_TARGET_BLEND_DESC RenderTarget[8];
};

struct D3D11_DEPTH_STENCILOP_DESC
{
	D3D11_STENCIL_OP StencilFailOp;
	D3D11_STENCIL_OP StencilDepthFailOp;
	D3D11_STENCIL_OP StencilPassOp;
	D3D11_COMPARISON_FUNC StencilFunc;
};

struct D3D11_DEPTH_STENCIL_DESC
{
	BOOL DepthEnable;
	D3D11_DEPTH_WRITE_MASK DepthWriteMask;
	D3D11_COMPARISON_FUNC DepthFunc;
	BOOL StencilEnable;
	UINT8 StencilReadMask;
	UINT8 StencilWriteMask;
	D3D11_DEPTH_STENCILOP_DESC FrontFace;
	D3D11_DEPTH_STENCILOP_DESC BackFace;
};

struct D3D11_SAMPLER_DESC
{
	D3D11_FILTER Filter;
	D3D11_TEXTURE_ADDRESS_MODE AddressU;
	D3D11_TEXTURE_ADDRESS_MODE AddressV;
	D3D11_TEXTURE_ADDRESS_MODE AddressW;
	FLOAT MipLODBias;
	UINT MaxAnisotropy;
	D3D11_COMPARISON_FUNC ComparisonFunc;
	FLOAT BorderColor[4];
	FLOAT MinLOD;
	FLOAT MaxLOD;
};

// MSVC 의 _countof
template <typename T, size_t N> constexpr size_t NovaCountOf(T (&)[N]) { return N; }
#define _countof(a) NovaCountOf(a)
