#pragma once
#include "GLLoader.h"
#include "FxParser.h"
#include <d3d11.h>

// OpenGL 공용: D3D11 설명 구조체 ↔ GL 상태, .fx 상태 블록 → D3D11 설명, DXGI 형식 → GL 형식.
//  Gfx(D3D11 모양) 층의 GL 구현과 GL 효과(GLRhi)가 같이 쓴다 — 상태의 뜻은 늘 D3D11 기준.
//  좌표 규칙(파일 GLRhi.cpp 맨 위): 창 y = D3D 행 번호 → 뷰포트·가위 숫자 그대로, D3D 앞면(시계) = GL_CCW
namespace GLState
{
	// ---- D3D11 기본값 (상태 객체가 nullptr 일 때)
	D3D11_RASTERIZER_DESC DefaultRasterizer();
	D3D11_BLEND_DESC DefaultBlend();
	D3D11_DEPTH_STENCIL_DESC DefaultDepthStencil();
	D3D11_SAMPLER_DESC DefaultSampler();

	// ---- .fx 상태 블록(FxParser) → D3D11 설명 (기본값에서 블록에 적힌 것만 바꿈 = Effects11)
	D3D11_RASTERIZER_DESC FxRasterizer(const FxParser::StateBlock& block);
	D3D11_BLEND_DESC FxBlend(const FxParser::StateBlock& block);
	D3D11_DEPTH_STENCIL_DESC FxDepthStencil(const FxParser::StateBlock& block);
	D3D11_SAMPLER_DESC FxSampler(const FxParser::StateBlock& block);

	// ---- D3D11 설명 → GL 상태
	void ApplyRasterizer(const D3D11_RASTERIZER_DESC& d);
	void ApplyBlend(const D3D11_BLEND_DESC& d, const float factor[4], UINT sampleMask);
	void ApplyDepthStencil(const D3D11_DEPTH_STENCIL_DESC& d, UINT stencilRef);
	GLuint CreateSampler(const D3D11_SAMPLER_DESC& d);
	void ApplyDefaults();   // D3D11 기본 상태 + 이 엔진의 고정 규칙 (첫 정점이 provoking, 큐브 경계 이음, 0xFFFF 끊기)

	// ---- 형식
	struct Format
	{
		GLenum Internal = 0;     // 0 = 지원하지 않음
		GLenum Upload = 0;       // glTexSubImage 의 format
		GLenum Type = 0;         //               type
		UINT Bits = 0;           // 화소당 비트 (압축 = 0)
		UINT BlockBytes = 0;     // 압축: 4x4 블록당 바이트
		bool Depth = false, Stencil = false, Integer = false;
	};
	// depthBind = D3D11_BIND_DEPTH_STENCIL 로 만든 텍스처 (R32_TYPELESS 같은 타입 없는 형식을 깊이로)
	Format FromDxgi(DXGI_FORMAT format, bool depthBind = false);
	// 화소 데이터 크기 (압축 블록 포함)
	UINT64 RowBytes(const Format& f, UINT width);
	UINT64 SliceBytes(const Format& f, UINT width, UINT height);

	// 정점 입력 형식 (D3D11_INPUT_ELEMENT_DESC::Format) → GL (성분 수, 타입, 정규화, 정수)
	bool VertexFormat(DXGI_FORMAT format, GLint& size, GLenum& type, GLboolean& normalized, bool& integer, UINT& bytes);

	GLenum Topology(D3D11_PRIMITIVE_TOPOLOGY t, GLint& patchVertices);
	GLenum CompareFunc(D3D11_COMPARISON_FUNC f);

	// GL 디버그 출력 → Editor.log [OpenGL] (같은 글은 한 번)
	void InstallDebugOutput();
}
