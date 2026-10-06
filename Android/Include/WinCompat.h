#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <atomic>
#include <pthread.h>
#include <utility>
#include <memory>

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
typedef int16_t SHORT;
typedef uint16_t USHORT;
typedef uint16_t WORD;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef char CHAR;
typedef wchar_t WCHAR;
typedef const char* LPCSTR;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef int16_t INT16;
typedef int8_t INT8;
typedef uintptr_t UINT_PTR;
typedef intptr_t INT_PTR;
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
#define E_POINTER ((HRESULT)0x80004003)
#define E_NOINTERFACE ((HRESULT)0x80004002)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)

struct RECT { LONG left, top, right, bottom; };

#define ZeroMemory(p, n) memset((p), 0, (n))

// MSVC 의 정수 키워드 (엔진 Types.h 의 int8 … uint64 가 쓴다)
#define __int8 char
#define __int16 short
#define __int32 int
#define __int64 long long

// 그 밖의 Windows 타입 · 상수 · CRT 이름 (엔진 코드가 쓰는 것만)
typedef void* HINSTANCE;
typedef void* HMODULE;
typedef void* HANDLE;
typedef void* HDC;
typedef void* HGLRC;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef wchar_t* LPWSTR;
typedef const wchar_t* LPCWSTR;
typedef char* LPSTR;
struct POINT { LONG x, y; };
union LARGE_INTEGER { struct { DWORD LowPart; LONG HighPart; }; LONGLONG QuadPart; };
#define MAX_PATH 260
#define CALLBACK
#define WINAPI
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _wcsicmp NovaWcsicmp   // 아래: path::c_str() 가 char* 인 안드로이드에서도 (Windows 는 wchar_t*)
#define _wcsnicmp wcsncasecmp
#include <strings.h>
#include <wchar.h>
#include <cwctype>

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
enum D3D11_COLOR_WRITE_ENABLE { D3D11_COLOR_WRITE_ENABLE_RED = 1, D3D11_COLOR_WRITE_ENABLE_GREEN = 2, D3D11_COLOR_WRITE_ENABLE_BLUE = 4, D3D11_COLOR_WRITE_ENABLE_ALPHA = 8, D3D11_COLOR_WRITE_ENABLE_ALL = 15 };

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

// ---- COM 의 기본 (Gfx 층이 IUnknown 모양: AddRef · Release · QueryInterface) — 안드로이드에는 COM 이 없다.
//  __declspec(uuid(...)) 는 지우고, __uuidof(형식) 은 형식마다 하나인 번호를 준다 (같은 형식이면 같은 GUID)
struct GUID
{
	uint32_t Data1;
	uint16_t Data2, Data3;
	uint8_t Data4[8];
};
inline bool operator==(const GUID& a, const GUID& b) { return memcmp(&a, &b, sizeof(GUID)) == 0; }
inline bool operator!=(const GUID& a, const GUID& b) { return !(a == b); }
typedef GUID IID;
typedef const GUID& REFIID;
typedef const GUID& REFGUID;
inline GUID NovaNextGuid()
{
	static std::atomic<uint32_t> next{ 1 };
	GUID g = {};
	g.Data1 = next++;
	return g;
}
template <class T> const GUID& NovaUuidOf()
{
	static const GUID g = NovaNextGuid();
	return g;
}
#define __uuidof(T) NovaUuidOf<T>()
#define __declspec(x)
#define STDMETHODCALLTYPE

struct IUnknown
{
	virtual HRESULT QueryInterface(REFIID riid, void** out) = 0;
	virtual ULONG AddRef() = 0;
	virtual ULONG Release() = 0;
};

// Microsoft::WRL::ComPtr 와 같은 쓰임 (엔진이 쓰는 것만): & = 비우고 주소, As = QueryInterface
namespace Microsoft { namespace WRL
{
	template <class T> class ComPtr;
	namespace Details
	{
		template <class C>
		class ComPtrRef
		{
		public:
			explicit ComPtrRef(C* c) : _c(c) {}
			operator typename C::InterfaceType**() { return _c->ReleaseAndGetAddressOf(); }
			operator void**() { return reinterpret_cast<void**>(_c->ReleaseAndGetAddressOf()); }
			operator C*() { return _c; }
			typename C::InterfaceType* operator*() { return _c->Get(); }
		private:
			C* _c;
		};
	}

	template <class T>
	class ComPtr
	{
	public:
		typedef T InterfaceType;
		ComPtr() = default;
		ComPtr(std::nullptr_t) {}
		ComPtr(T* p) : _p(p) { if (_p) _p->AddRef(); }
		ComPtr(const ComPtr& o) : _p(o._p) { if (_p) _p->AddRef(); }
		ComPtr(ComPtr&& o) noexcept : _p(o._p) { o._p = nullptr; }
		template <class U> ComPtr(const ComPtr<U>& o) : _p(o.Get()) { if (_p) _p->AddRef(); }
		~ComPtr() { Reset(); }

		ComPtr& operator=(std::nullptr_t) { Reset(); return *this; }
		ComPtr& operator=(T* p) { if (p) p->AddRef(); Reset(); _p = p; return *this; }
		ComPtr& operator=(const ComPtr& o) { return *this = o._p; }
		ComPtr& operator=(ComPtr&& o) noexcept { if (this != std::addressof(o)) { Reset(); _p = o._p; o._p = nullptr; } return *this; }
		template <class U> ComPtr& operator=(const ComPtr<U>& o) { return *this = static_cast<T*>(o.Get()); }

		T* Get() const { return _p; }
		T* operator->() const { return _p; }
		explicit operator bool() const { return _p != nullptr; }
		T* const* GetAddressOf() const { return &_p; }
		T** GetAddressOf() { return &_p; }
		T** ReleaseAndGetAddressOf() { Reset(); return &_p; }
		Details::ComPtrRef<ComPtr> operator&() { return Details::ComPtrRef<ComPtr>(this); }
		void Reset() { if (_p) { T* p = _p; _p = nullptr; p->Release(); } }
		void Attach(T* p) { Reset(); _p = p; }
		T* Detach() { T* p = _p; _p = nullptr; return p; }
		void Swap(ComPtr& o) { std::swap(_p, o._p); }
		HRESULT CopyTo(T** out) const { *out = _p; if (_p) _p->AddRef(); return S_OK; }
		template <class U> HRESULT As(Details::ComPtrRef<ComPtr<U>> out) const { return As(static_cast<ComPtr<U>*>(out)); }
		template <class U> HRESULT As(ComPtr<U>* out) const
		{
			if (!_p) { out->Reset(); return E_POINTER; }
			return _p->QueryInterface(__uuidof(U), reinterpret_cast<void**>(out->ReleaseAndGetAddressOf()));
		}

		bool operator==(const ComPtr& o) const { return _p == o._p; }
		bool operator!=(const ComPtr& o) const { return _p != o._p; }
		bool operator==(std::nullptr_t) const { return _p == nullptr; }
		bool operator!=(std::nullptr_t) const { return _p != nullptr; }
		bool operator==(const T* p) const { return _p == p; }
		bool operator!=(const T* p) const { return _p != p; }

	private:
		T* _p = nullptr;
	};
} }

// ---- DXGI 형식 (값 = DXGI 의 번호 — DDS 파일 머리에 이 숫자가 들어 있다)
enum DXGI_FORMAT
{
	DXGI_FORMAT_UNKNOWN = 0,
	DXGI_FORMAT_R32G32B32A32_TYPELESS = 1, DXGI_FORMAT_R32G32B32A32_FLOAT = 2, DXGI_FORMAT_R32G32B32A32_UINT = 3, DXGI_FORMAT_R32G32B32A32_SINT = 4,
	DXGI_FORMAT_R32G32B32_TYPELESS = 5, DXGI_FORMAT_R32G32B32_FLOAT = 6, DXGI_FORMAT_R32G32B32_UINT = 7, DXGI_FORMAT_R32G32B32_SINT = 8,
	DXGI_FORMAT_R16G16B16A16_TYPELESS = 9, DXGI_FORMAT_R16G16B16A16_FLOAT = 10, DXGI_FORMAT_R16G16B16A16_UNORM = 11, DXGI_FORMAT_R16G16B16A16_UINT = 12,
	DXGI_FORMAT_R16G16B16A16_SNORM = 13, DXGI_FORMAT_R16G16B16A16_SINT = 14,
	DXGI_FORMAT_R32G32_TYPELESS = 15, DXGI_FORMAT_R32G32_FLOAT = 16, DXGI_FORMAT_R32G32_UINT = 17, DXGI_FORMAT_R32G32_SINT = 18,
	DXGI_FORMAT_R32G8X24_TYPELESS = 19, DXGI_FORMAT_D32_FLOAT_S8X24_UINT = 20, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS = 21, DXGI_FORMAT_X32_TYPELESS_G8X24_UINT = 22,
	DXGI_FORMAT_R10G10B10A2_TYPELESS = 23, DXGI_FORMAT_R10G10B10A2_UNORM = 24, DXGI_FORMAT_R10G10B10A2_UINT = 25,
	DXGI_FORMAT_R11G11B10_FLOAT = 26,
	DXGI_FORMAT_R8G8B8A8_TYPELESS = 27, DXGI_FORMAT_R8G8B8A8_UNORM = 28, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29, DXGI_FORMAT_R8G8B8A8_UINT = 30,
	DXGI_FORMAT_R8G8B8A8_SNORM = 31, DXGI_FORMAT_R8G8B8A8_SINT = 32,
	DXGI_FORMAT_R16G16_TYPELESS = 33, DXGI_FORMAT_R16G16_FLOAT = 34, DXGI_FORMAT_R16G16_UNORM = 35, DXGI_FORMAT_R16G16_UINT = 36,
	DXGI_FORMAT_R16G16_SNORM = 37, DXGI_FORMAT_R16G16_SINT = 38,
	DXGI_FORMAT_R32_TYPELESS = 39, DXGI_FORMAT_D32_FLOAT = 40, DXGI_FORMAT_R32_FLOAT = 41, DXGI_FORMAT_R32_UINT = 42, DXGI_FORMAT_R32_SINT = 43,
	DXGI_FORMAT_R24G8_TYPELESS = 44, DXGI_FORMAT_D24_UNORM_S8_UINT = 45, DXGI_FORMAT_R24_UNORM_X8_TYPELESS = 46, DXGI_FORMAT_X24_TYPELESS_G8_UINT = 47,
	DXGI_FORMAT_R8G8_TYPELESS = 48, DXGI_FORMAT_R8G8_UNORM = 49, DXGI_FORMAT_R8G8_UINT = 50, DXGI_FORMAT_R8G8_SNORM = 51, DXGI_FORMAT_R8G8_SINT = 52,
	DXGI_FORMAT_R16_TYPELESS = 53, DXGI_FORMAT_R16_FLOAT = 54, DXGI_FORMAT_D16_UNORM = 55, DXGI_FORMAT_R16_UNORM = 56, DXGI_FORMAT_R16_UINT = 57,
	DXGI_FORMAT_R16_SNORM = 58, DXGI_FORMAT_R16_SINT = 59,
	DXGI_FORMAT_R8_TYPELESS = 60, DXGI_FORMAT_R8_UNORM = 61, DXGI_FORMAT_R8_UINT = 62, DXGI_FORMAT_R8_SNORM = 63, DXGI_FORMAT_R8_SINT = 64,
	DXGI_FORMAT_A8_UNORM = 65, DXGI_FORMAT_R1_UNORM = 66, DXGI_FORMAT_R9G9B9E5_SHAREDEXP = 67, DXGI_FORMAT_R8G8_B8G8_UNORM = 68, DXGI_FORMAT_G8R8_G8B8_UNORM = 69,
	DXGI_FORMAT_BC1_TYPELESS = 70, DXGI_FORMAT_BC1_UNORM = 71, DXGI_FORMAT_BC1_UNORM_SRGB = 72,
	DXGI_FORMAT_BC2_TYPELESS = 73, DXGI_FORMAT_BC2_UNORM = 74, DXGI_FORMAT_BC2_UNORM_SRGB = 75,
	DXGI_FORMAT_BC3_TYPELESS = 76, DXGI_FORMAT_BC3_UNORM = 77, DXGI_FORMAT_BC3_UNORM_SRGB = 78,
	DXGI_FORMAT_BC4_TYPELESS = 79, DXGI_FORMAT_BC4_UNORM = 80, DXGI_FORMAT_BC4_SNORM = 81,
	DXGI_FORMAT_BC5_TYPELESS = 82, DXGI_FORMAT_BC5_UNORM = 83, DXGI_FORMAT_BC5_SNORM = 84,
	DXGI_FORMAT_B5G6R5_UNORM = 85, DXGI_FORMAT_B5G5R5A1_UNORM = 86, DXGI_FORMAT_B8G8R8A8_UNORM = 87, DXGI_FORMAT_B8G8R8X8_UNORM = 88,
	DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM = 89, DXGI_FORMAT_B8G8R8A8_TYPELESS = 90, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91,
	DXGI_FORMAT_B8G8R8X8_TYPELESS = 92, DXGI_FORMAT_B8G8R8X8_UNORM_SRGB = 93,
	DXGI_FORMAT_BC6H_TYPELESS = 94, DXGI_FORMAT_BC6H_UF16 = 95, DXGI_FORMAT_BC6H_SF16 = 96,
	DXGI_FORMAT_BC7_TYPELESS = 97, DXGI_FORMAT_BC7_UNORM = 98, DXGI_FORMAT_BC7_UNORM_SRGB = 99,
	DXGI_FORMAT_B4G4R4A4_UNORM = 115,
	DXGI_FORMAT_FORCE_UINT = 0xffffffff
};
struct DXGI_SAMPLE_DESC { UINT Count; UINT Quality; };

// ---- D3D11 자원 · 뷰 설명 (Gfx 층의 함수 모양)
#define D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT 32
#define D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE 16
#define D3D11_APPEND_ALIGNED_ELEMENT 0xffffffff
#define D3D11_ASYNC_GETDATA_DONOTFLUSH 0x1
#define D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT 8
#define D3D11_DEFAULT_STENCIL_READ_MASK 0xff
#define D3D11_DEFAULT_STENCIL_WRITE_MASK 0xff

enum D3D11_USAGE { D3D11_USAGE_DEFAULT = 0, D3D11_USAGE_IMMUTABLE = 1, D3D11_USAGE_DYNAMIC = 2, D3D11_USAGE_STAGING = 3 };
enum D3D11_BIND_FLAG
{
	D3D11_BIND_VERTEX_BUFFER = 0x1, D3D11_BIND_INDEX_BUFFER = 0x2, D3D11_BIND_CONSTANT_BUFFER = 0x4, D3D11_BIND_SHADER_RESOURCE = 0x8,
	D3D11_BIND_STREAM_OUTPUT = 0x10, D3D11_BIND_RENDER_TARGET = 0x20, D3D11_BIND_DEPTH_STENCIL = 0x40, D3D11_BIND_UNORDERED_ACCESS = 0x80
};
enum D3D11_CPU_ACCESS_FLAG { D3D11_CPU_ACCESS_WRITE = 0x10000, D3D11_CPU_ACCESS_READ = 0x20000 };
enum D3D11_RESOURCE_MISC_FLAG
{
	D3D11_RESOURCE_MISC_GENERATE_MIPS = 0x1, D3D11_RESOURCE_MISC_SHARED = 0x2, D3D11_RESOURCE_MISC_TEXTURECUBE = 0x4,
	D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS = 0x10, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS = 0x20, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED = 0x40
};
enum D3D11_RESOURCE_DIMENSION
{
	D3D11_RESOURCE_DIMENSION_UNKNOWN = 0, D3D11_RESOURCE_DIMENSION_BUFFER = 1, D3D11_RESOURCE_DIMENSION_TEXTURE1D = 2,
	D3D11_RESOURCE_DIMENSION_TEXTURE2D = 3, D3D11_RESOURCE_DIMENSION_TEXTURE3D = 4
};
enum D3D11_MAP { D3D11_MAP_READ = 1, D3D11_MAP_WRITE = 2, D3D11_MAP_READ_WRITE = 3, D3D11_MAP_WRITE_DISCARD = 4, D3D11_MAP_WRITE_NO_OVERWRITE = 5 };
enum D3D11_MAP_FLAG { D3D11_MAP_FLAG_DO_NOT_WAIT = 0x100000 };
// 버퍼 뷰 (오클루전 컬링의 compute — GfxGLES 의 SSBO)
enum D3D11_BUFFER_UAV_FLAG { D3D11_BUFFER_UAV_FLAG_RAW = 0x1, D3D11_BUFFER_UAV_FLAG_APPEND = 0x2, D3D11_BUFFER_UAV_FLAG_COUNTER = 0x4 };
enum D3D11_BUFFEREX_SRV_FLAG { D3D11_BUFFEREX_SRV_FLAG_RAW = 0x1 };
enum D3D11_CLEAR_FLAG { D3D11_CLEAR_DEPTH = 0x1, D3D11_CLEAR_STENCIL = 0x2 };
enum D3D11_INPUT_CLASSIFICATION { D3D11_INPUT_PER_VERTEX_DATA = 0, D3D11_INPUT_PER_INSTANCE_DATA = 1 };
enum D3D11_DSV_FLAG { D3D11_DSV_READ_ONLY_DEPTH = 0x1, D3D11_DSV_READ_ONLY_STENCIL = 0x2 };

enum D3D11_PRIMITIVE_TOPOLOGY
{
	D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED = 0, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST = 1, D3D11_PRIMITIVE_TOPOLOGY_LINELIST = 2,
	D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP = 3, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST = 4, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP = 5,
	D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ = 10, D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ = 11, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ = 12,
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ = 13,
	D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST = 33, D3D11_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST = 34,
	D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST = 35, D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST = 36,
	D3D11_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST = 48, D3D11_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST = 64
};

enum D3D11_SRV_DIMENSION
{
	D3D11_SRV_DIMENSION_UNKNOWN = 0, D3D11_SRV_DIMENSION_BUFFER = 1, D3D11_SRV_DIMENSION_TEXTURE1D = 2, D3D11_SRV_DIMENSION_TEXTURE1DARRAY = 3,
	D3D11_SRV_DIMENSION_TEXTURE2D = 4, D3D11_SRV_DIMENSION_TEXTURE2DARRAY = 5, D3D11_SRV_DIMENSION_TEXTURE2DMS = 6,
	D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY = 7, D3D11_SRV_DIMENSION_TEXTURE3D = 8, D3D11_SRV_DIMENSION_TEXTURECUBE = 9,
	D3D11_SRV_DIMENSION_TEXTURECUBEARRAY = 10, D3D11_SRV_DIMENSION_BUFFEREX = 11
};
enum D3D11_RTV_DIMENSION
{
	D3D11_RTV_DIMENSION_UNKNOWN = 0, D3D11_RTV_DIMENSION_BUFFER = 1, D3D11_RTV_DIMENSION_TEXTURE1D = 2, D3D11_RTV_DIMENSION_TEXTURE1DARRAY = 3,
	D3D11_RTV_DIMENSION_TEXTURE2D = 4, D3D11_RTV_DIMENSION_TEXTURE2DARRAY = 5, D3D11_RTV_DIMENSION_TEXTURE2DMS = 6,
	D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY = 7, D3D11_RTV_DIMENSION_TEXTURE3D = 8
};
enum D3D11_DSV_DIMENSION
{
	D3D11_DSV_DIMENSION_UNKNOWN = 0, D3D11_DSV_DIMENSION_TEXTURE1D = 1, D3D11_DSV_DIMENSION_TEXTURE1DARRAY = 2, D3D11_DSV_DIMENSION_TEXTURE2D = 3,
	D3D11_DSV_DIMENSION_TEXTURE2DARRAY = 4, D3D11_DSV_DIMENSION_TEXTURE2DMS = 5, D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY = 6
};
enum D3D11_UAV_DIMENSION
{
	D3D11_UAV_DIMENSION_UNKNOWN = 0, D3D11_UAV_DIMENSION_BUFFER = 1, D3D11_UAV_DIMENSION_TEXTURE1D = 2, D3D11_UAV_DIMENSION_TEXTURE1DARRAY = 3,
	D3D11_UAV_DIMENSION_TEXTURE2D = 4, D3D11_UAV_DIMENSION_TEXTURE2DARRAY = 5, D3D11_UAV_DIMENSION_TEXTURE3D = 8
};

struct D3D11_BUFFER_DESC { UINT ByteWidth; D3D11_USAGE Usage; UINT BindFlags; UINT CPUAccessFlags; UINT MiscFlags; UINT StructureByteStride; };
struct D3D11_TEXTURE1D_DESC { UINT Width; UINT MipLevels; UINT ArraySize; DXGI_FORMAT Format; D3D11_USAGE Usage; UINT BindFlags; UINT CPUAccessFlags; UINT MiscFlags; };
struct D3D11_TEXTURE2D_DESC
{
	UINT Width; UINT Height; UINT MipLevels; UINT ArraySize; DXGI_FORMAT Format; DXGI_SAMPLE_DESC SampleDesc;
	D3D11_USAGE Usage; UINT BindFlags; UINT CPUAccessFlags; UINT MiscFlags;
};
struct D3D11_TEXTURE3D_DESC { UINT Width; UINT Height; UINT Depth; UINT MipLevels; DXGI_FORMAT Format; D3D11_USAGE Usage; UINT BindFlags; UINT CPUAccessFlags; UINT MiscFlags; };
struct D3D11_SUBRESOURCE_DATA { const void* pSysMem; UINT SysMemPitch; UINT SysMemSlicePitch; };
struct D3D11_MAPPED_SUBRESOURCE { void* pData; UINT RowPitch; UINT DepthPitch; };
struct D3D11_BOX { UINT left; UINT top; UINT front; UINT right; UINT bottom; UINT back; };
struct D3D11_VIEWPORT { FLOAT TopLeftX; FLOAT TopLeftY; FLOAT Width; FLOAT Height; FLOAT MinDepth; FLOAT MaxDepth; };
typedef RECT D3D11_RECT;

struct D3D11_INPUT_ELEMENT_DESC
{
	LPCSTR SemanticName; UINT SemanticIndex; DXGI_FORMAT Format; UINT InputSlot; UINT AlignedByteOffset;
	D3D11_INPUT_CLASSIFICATION InputSlotClass; UINT InstanceDataStepRate;
};

struct D3D11_BUFFER_SRV { union { UINT FirstElement; UINT ElementOffset; }; union { UINT NumElements; UINT ElementWidth; }; };
struct D3D11_BUFFEREX_SRV { UINT FirstElement; UINT NumElements; UINT Flags; };
struct D3D11_TEX1D_SRV { UINT MostDetailedMip; UINT MipLevels; };
struct D3D11_TEX1D_ARRAY_SRV { UINT MostDetailedMip; UINT MipLevels; UINT FirstArraySlice; UINT ArraySize; };
struct D3D11_TEX2D_SRV { UINT MostDetailedMip; UINT MipLevels; };
struct D3D11_TEX2D_ARRAY_SRV { UINT MostDetailedMip; UINT MipLevels; UINT FirstArraySlice; UINT ArraySize; };
struct D3D11_TEX2DMS_SRV { UINT UnusedField_NothingToDefine; };
struct D3D11_TEX2DMS_ARRAY_SRV { UINT FirstArraySlice; UINT ArraySize; };
struct D3D11_TEX3D_SRV { UINT MostDetailedMip; UINT MipLevels; };
struct D3D11_TEXCUBE_SRV { UINT MostDetailedMip; UINT MipLevels; };
struct D3D11_TEXCUBE_ARRAY_SRV { UINT MostDetailedMip; UINT MipLevels; UINT First2DArrayFace; UINT NumCubes; };
struct D3D11_SHADER_RESOURCE_VIEW_DESC
{
	DXGI_FORMAT Format;
	D3D11_SRV_DIMENSION ViewDimension;
	union
	{
		D3D11_BUFFER_SRV Buffer; D3D11_TEX1D_SRV Texture1D; D3D11_TEX1D_ARRAY_SRV Texture1DArray; D3D11_TEX2D_SRV Texture2D;
		D3D11_TEX2D_ARRAY_SRV Texture2DArray; D3D11_TEX2DMS_SRV Texture2DMS; D3D11_TEX2DMS_ARRAY_SRV Texture2DMSArray; D3D11_TEX3D_SRV Texture3D;
		D3D11_TEXCUBE_SRV TextureCube; D3D11_TEXCUBE_ARRAY_SRV TextureCubeArray; D3D11_BUFFEREX_SRV BufferEx;
	};
};

struct D3D11_BUFFER_RTV { union { UINT FirstElement; UINT ElementOffset; }; union { UINT NumElements; UINT ElementWidth; }; };
struct D3D11_TEX1D_RTV { UINT MipSlice; };
struct D3D11_TEX1D_ARRAY_RTV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; };
struct D3D11_TEX2D_RTV { UINT MipSlice; };
struct D3D11_TEX2D_ARRAY_RTV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; };
struct D3D11_TEX2DMS_RTV { UINT UnusedField_NothingToDefine; };
struct D3D11_TEX2DMS_ARRAY_RTV { UINT FirstArraySlice; UINT ArraySize; };
struct D3D11_TEX3D_RTV { UINT MipSlice; UINT FirstWSlice; UINT WSize; };
struct D3D11_RENDER_TARGET_VIEW_DESC
{
	DXGI_FORMAT Format;
	D3D11_RTV_DIMENSION ViewDimension;
	union
	{
		D3D11_BUFFER_RTV Buffer; D3D11_TEX1D_RTV Texture1D; D3D11_TEX1D_ARRAY_RTV Texture1DArray; D3D11_TEX2D_RTV Texture2D;
		D3D11_TEX2D_ARRAY_RTV Texture2DArray; D3D11_TEX2DMS_RTV Texture2DMS; D3D11_TEX2DMS_ARRAY_RTV Texture2DMSArray; D3D11_TEX3D_RTV Texture3D;
	};
};

struct D3D11_TEX1D_DSV { UINT MipSlice; };
struct D3D11_TEX1D_ARRAY_DSV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; };
struct D3D11_TEX2D_DSV { UINT MipSlice; };
struct D3D11_TEX2D_ARRAY_DSV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; };
struct D3D11_TEX2DMS_DSV { UINT UnusedField_NothingToDefine; };
struct D3D11_TEX2DMS_ARRAY_DSV { UINT FirstArraySlice; UINT ArraySize; };
struct D3D11_DEPTH_STENCIL_VIEW_DESC
{
	DXGI_FORMAT Format;
	D3D11_DSV_DIMENSION ViewDimension;
	UINT Flags;
	union
	{
		D3D11_TEX1D_DSV Texture1D; D3D11_TEX1D_ARRAY_DSV Texture1DArray; D3D11_TEX2D_DSV Texture2D; D3D11_TEX2D_ARRAY_DSV Texture2DArray;
		D3D11_TEX2DMS_DSV Texture2DMS; D3D11_TEX2DMS_ARRAY_DSV Texture2DMSArray;
	};
};

struct D3D11_BUFFER_UAV { UINT FirstElement; UINT NumElements; UINT Flags; };
struct D3D11_TEX1D_UAV { UINT MipSlice; };
struct D3D11_TEX1D_ARRAY_UAV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; };
struct D3D11_TEX2D_UAV { UINT MipSlice; };
struct D3D11_TEX2D_ARRAY_UAV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; };
struct D3D11_TEX3D_UAV { UINT MipSlice; UINT FirstWSlice; UINT WSize; };
struct D3D11_UNORDERED_ACCESS_VIEW_DESC
{
	DXGI_FORMAT Format;
	D3D11_UAV_DIMENSION ViewDimension;
	union
	{
		D3D11_BUFFER_UAV Buffer; D3D11_TEX1D_UAV Texture1D; D3D11_TEX1D_ARRAY_UAV Texture1DArray; D3D11_TEX2D_UAV Texture2D;
		D3D11_TEX2D_ARRAY_UAV Texture2DArray; D3D11_TEX3D_UAV Texture3D;
	};
};

enum D3D11_QUERY
{
	D3D11_QUERY_EVENT = 0, D3D11_QUERY_OCCLUSION = 1, D3D11_QUERY_TIMESTAMP = 2, D3D11_QUERY_TIMESTAMP_DISJOINT = 3,
	D3D11_QUERY_PIPELINE_STATISTICS = 4, D3D11_QUERY_OCCLUSION_PREDICATE = 5
};
struct D3D11_QUERY_DESC { D3D11_QUERY Query; UINT MiscFlags; };
struct D3D11_QUERY_DATA_TIMESTAMP_DISJOINT { UINT64 Frequency; BOOL Disjoint; };
struct D3D11_QUERY_DATA_PIPELINE_STATISTICS
{
	UINT64 IAVertices, IAPrimitives, VSInvocations, GSInvocations, GSPrimitives, CInvocations, CPrimitives, PSInvocations,
		HSInvocations, DSInvocations, CSInvocations;
};

// ---- Effects11 의 설명 구조체 (RhiFx 의 FxPass · FxTechnique · FxEffect::GetDesc)
struct D3DX11_PASS_DESC { LPCSTR Name; UINT Annotations; BYTE* pIAInputSignature; SIZE_T IAInputSignatureSize; UINT StencilRef; UINT SampleMask; FLOAT BlendFactor[4]; };
struct D3DX11_TECHNIQUE_DESC { LPCSTR Name; UINT Passes; UINT Annotations; };
struct D3DX11_EFFECT_DESC { UINT ConstantBuffers; UINT GlobalVariables; UINT InterfaceVariables; UINT Techniques; UINT Groups; };

// ---- Windows API · MSVC CRT 의 안드로이드 판 (엔진 코드가 쓰는 것만, 같은 뜻)
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
typedef const void* LPCVOID;
#define CP_UTF8 65001
#define _TRUNCATE ((size_t)-1)
#define OUT
#define D3D10_CPU_ACCESS_WRITE 0x10000
#define D3D10_CPU_ACCESS_READ 0x20000

inline ULONGLONG GetTickCount64()
{
	return (ULONGLONG)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* f) { f->QuadPart = 1000000000LL; return TRUE; }
inline BOOL QueryPerformanceCounter(LARGE_INTEGER* c)
{
	c->QuadPart = (LONGLONG)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	return TRUE;
}
inline DWORD GetEnvironmentVariableA(const char* name, char* buffer, DWORD size)
{
	const char* v = getenv(name);
	if (!v) return 0;
	const size_t n = strlen(v);
	if (!buffer || n + 1 > size) return (DWORD)(n + 1);
	memcpy(buffer, v, n + 1);
	return (DWORD)n;
}
inline DWORD GetCurrentThreadId() { return (DWORD)(uintptr_t)pthread_self(); }

template <size_t N, class... A> int sprintf_s(char (&buffer)[N], const char* format, A... args) { return snprintf(buffer, N, format, args...); }
template <class... A> int sprintf_s(char* buffer, size_t size, const char* format, A... args) { return snprintf(buffer, size, format, args...); }
template <size_t N> int strcpy_s(char (&dst)[N], const char* src) { snprintf(dst, N, "%s", src); return 0; }
inline int strcpy_s(char* dst, size_t size, const char* src) { snprintf(dst, size, "%s", src); return 0; }
template <size_t N> int strncpy_s(char (&dst)[N], const char* src, size_t count)
{
	const size_t n = (std::min)(count == _TRUNCATE ? N - 1 : count, N - 1);
	strncpy(dst, src, n);
	dst[n] = 0;
	return 0;
}
inline int strncpy_s(char* dst, size_t size, const char* src, size_t count)
{
	if (!size) return 0;
	const size_t n = (std::min)(count == _TRUNCATE ? size - 1 : count, size - 1);
	strncpy(dst, src, n);
	dst[n] = 0;
	return 0;
}
template <size_t N> int wcscpy_s(wchar_t (&dst)[N], const wchar_t* src) { wcsncpy(dst, src, N - 1); dst[N - 1] = 0; return 0; }
inline int wcscpy_s(wchar_t* dst, size_t size, const wchar_t* src) { if (size) { wcsncpy(dst, src, size - 1); dst[size - 1] = 0; } return 0; }
inline int fopen_s(FILE** f, const char* name, const char* mode) { *f = fopen(name, mode); return *f ? 0 : 1; }
inline int _wfopen_s(FILE** f, const wchar_t* name, const wchar_t* mode)
{
	std::string n, m;
	for (const wchar_t* p = name; *p; ++p) n += (char)*p;   // 경로는 UTF-8 로 (아래 MultiByte 변환과 같은 규칙은 엔진 Utils 가)
	for (const wchar_t* p = mode; *p; ++p) m += (char)*p;
	*f = fopen(n.c_str(), m.c_str());
	return *f ? 0 : 1;
}

// std::execution::par — 안드로이드 libc++ 에 병렬 알고리즘이 없다 → 차례로 (결과는 같다)
#include <algorithm>
#include <execution>
namespace NovaPstl { struct Par {}; }
namespace std
{
	namespace execution { inline constexpr NovaPstl::Par par{}, par_unseq{}; }
	template <class It, class F> void for_each(NovaPstl::Par, It first, It last, F f) { std::for_each(first, last, f); }
	template <class It, class F> void sort(NovaPstl::Par, It first, It last, F f) { std::sort(first, last, f); }
	template <class It> void sort(NovaPstl::Par, It first, It last) { std::sort(first, last); }
}

// windows.h 의 min · max 매크로 (엔진 코드는 이것을 전제로 쓴다: min(float, int) 처럼 타입이 섞여도 된다.
//  std 쪽은 (std::max)(a, b) 처럼 괄호로 감싸 쓰고, libc++ 헤더는 이 매크로를 스스로 피한다)
#ifndef NOMINMAX
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#endif

#include <ctime>
inline int localtime_s(struct tm* out, const time_t* t) { return localtime_r(t, out) ? 0 : 1; }
inline int gmtime_s(struct tm* out, const time_t* t) { return gmtime_r(t, out) ? 0 : 1; }
#if defined(__ANDROID__)
#include <android/log.h>
inline void OutputDebugStringA(const char* text) { __android_log_print(ANDROID_LOG_DEBUG, "NOVA", "%s", text); }
#else
// 웹 (Emscripten): 브라우저 콘솔 (stderr)
inline void OutputDebugStringA(const char* text) { fputs(text, stderr); }
#endif
inline LPWSTR* CommandLineToArgvW(LPCWSTR, int* argc) { *argc = 0; return nullptr; }   // 안드로이드: 명령줄 없음 (인텐트 값은 AndroidMain)
inline LPCWSTR GetCommandLineW() { return L""; }
inline void* LocalFree(void*) { return nullptr; }
inline DWORD GetModuleFileNameW(HMODULE, wchar_t* buffer, DWORD size) { if (size) buffer[0] = 0; return 0; }
inline int NovaWcsicmp(const wchar_t* a, const wchar_t* b) { return wcscasecmp(a, b); }
inline int NovaWcsicmp(const char* a, const wchar_t* b)   // std::filesystem::path::c_str() (UTF-8) 와 L"..." 비교
{
	std::wstring w;
	for (const unsigned char* p = (const unsigned char*)a; *p; ++p) w.push_back((wchar_t)*p);   // 확장자 비교용 (ASCII)
	return wcscasecmp(w.c_str(), b);
}
// 모듈 핸들 (패키지 DLL 이 자기 위치를 찾을 때 — 안드로이드는 패키지를 엔진에 함께 넣어 위치가 없다)
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x4
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x2
inline BOOL GetModuleHandleExW(DWORD, LPCWSTR, HMODULE* out) { if (out) *out = nullptr; return FALSE; }

// DXGI 스왑 체인 · 드라이버 종류 (App.h 의 멤버 타입 — 안드로이드에서는 쓰지 않는다)
struct IDXGISwapChain : IUnknown {};
enum D3D_DRIVER_TYPE { D3D_DRIVER_TYPE_UNKNOWN = 0, D3D_DRIVER_TYPE_HARDWARE = 1, D3D_DRIVER_TYPE_REFERENCE = 2, D3D_DRIVER_TYPE_NULL = 3, D3D_DRIVER_TYPE_SOFTWARE = 4, D3D_DRIVER_TYPE_WARP = 5 };

// ---- 입력 (Win32 이름 그대로 — InputManager · 단축키 코드가 그대로 돈다). 구현 = Android/Source/Engine/AndroidWin32.cpp:
//  터치 첫 손가락 = 마우스 왼쪽 단추 + 커서, 키보드 이벤트 = 가상 키
enum
{
	VK_LBUTTON = 0x01, VK_RBUTTON = 0x02, VK_MBUTTON = 0x04, VK_BACK = 0x08, VK_TAB = 0x09, VK_RETURN = 0x0D, VK_SHIFT = 0x10, VK_CONTROL = 0x11,
	VK_MENU = 0x12, VK_ESCAPE = 0x1B, VK_SPACE = 0x20, VK_PRIOR = 0x21, VK_NEXT = 0x22, VK_END = 0x23, VK_HOME = 0x24, VK_LEFT = 0x25, VK_UP = 0x26,
	VK_RIGHT = 0x27, VK_DOWN = 0x28, VK_INSERT = 0x2D, VK_DELETE = 0x2E, VK_F1 = 0x70, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10,
	VK_F11, VK_F12, VK_LSHIFT = 0xA0, VK_RSHIFT = 0xA1, VK_LCONTROL = 0xA2, VK_RCONTROL = 0xA3, VK_LMENU = 0xA4, VK_RMENU = 0xA5
};
SHORT GetAsyncKeyState(int vk);
SHORT GetKeyState(int vk);
BOOL GetCursorPos(POINT* p);
BOOL ScreenToClient(HWND, POINT* p);
HWND GetFocus();
HWND GetForegroundWindow();

#define sscanf_s sscanf
inline UINT D3D11CalcSubresource(UINT mip, UINT arraySlice, UINT mipLevels) { return mip + arraySlice * mipLevels; }
int MultiByteToWideChar(UINT codePage, DWORD flags, const char* src, int srcLen, wchar_t* dst, int dstLen);   // UTF-8 → wchar_t (AndroidWin32.cpp)
int WideCharToMultiByte(UINT codePage, DWORD flags, const wchar_t* src, int srcLen, char* dst, int dstLen, const char* defaultChar, BOOL* usedDefault);
inline DWORD GetModuleFileNameA(HMODULE, char* buffer, DWORD size) { if (size) buffer[0] = 0; return 0; }

// 마우스 붙잡기 (Win32 — EditorApp 의 마우스 처리, 안드로이드는 의미 없음)
inline HWND SetCapture(HWND h) { return h; }
inline BOOL ReleaseCapture() { return TRUE; }
#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_MBUTTON 0x0010
