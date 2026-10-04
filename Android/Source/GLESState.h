#pragma once
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>

// OpenGL ES 3.2 의 D3D11 상태 · 형식 변환 (GfxGLES · GLESRhi 공용, 데스크톱 GLState 의 ES 판).
//  - 상태: D3D 앞면(시계) = GL_CCW (창 y = D3D 행 번호라 감김이 뒤집혀 보인다), 깊이 0..1 (셰이더가 -1..1 로)
//  - 형식: ES 에 없는 것은 다른 형식 + 올릴 때 바꾸기 (BGRA → RGBA 자리 바꿈, 16 비트 UNORM → 32 비트 float)
namespace GLESState
{
	enum class Convert { None, SwapRB, Unorm16ToFloat };

	struct Format
	{
		GLenum Internal = 0;     // 0 = 지원하지 않음
		GLenum Upload = 0;       // glTexSubImage 의 format
		GLenum Type = 0;         //               type
		UINT Bits = 0;           // 원본(DXGI) 화소당 비트 (압축 = 0)
		UINT BlockBytes = 0;     // 압축: 블록당 바이트
		UINT BlockW = 4, BlockH = 4;   // 압축 블록 크기 (BC · ETC2 = 4x4, ASTC = 4x4 ~ 12x12)
		Convert Conv = Convert::None;
		UINT Channels = 4;       // Unorm16ToFloat 의 성분 수
		bool Depth = false, Stencil = false, Integer = false;
	};
	// depthBind = D3D11_BIND_DEPTH_STENCIL 로 만든 텍스처 (R32_TYPELESS 같은 타입 없는 형식을 깊이로)
	Format FromDxgi(DXGI_FORMAT format, bool depthBind = false);
	UINT64 RowBytes(const Format& f, UINT width);              // 원본(DXGI) 데이터 기준
	UINT64 SliceBytes(const Format& f, UINT width, UINT height);
	UINT GLBytesPerPixel(const Format& f);                     // GL 에 올리는 데이터 기준 (바꾼 뒤)

	bool VertexFormat(DXGI_FORMAT format, GLint& size, GLenum& type, GLboolean& normalized, bool& integer, UINT& bytes);
	GLenum Topology(D3D11_PRIMITIVE_TOPOLOGY t, GLint& patchVertices);

	GLenum Compare(D3D11_COMPARISON_FUNC f);
	void ApplyRasterizer(const D3D11_RASTERIZER_DESC& d);
	void ApplyBlend(const D3D11_BLEND_DESC& d, const float factor[4], UINT sampleMask);
	void ApplyDepthStencil(const D3D11_DEPTH_STENCIL_DESC& d, UINT ref);
	GLuint CreateSampler(const D3D11_SAMPLER_DESC& d);
	void ApplyDefaults();   // D3D11 기본 상태 + 원시 끊기 (0xFFFF / 0xFFFFFFFF)
}
