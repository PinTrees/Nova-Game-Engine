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

	// GL 디버그 출력 → Editor.log [OpenGL] (같은 글은 한 번). synchronous = 부른 자리에서 바로 (디버그 컨텍스트, 느림)
	void InstallDebugOutput(bool synchronous = true);

	// ---- 묶기 캐시: 같은 것을 다시 묶지 않는다 (효과 Apply 마다 유닛 수십 개를 다시 묶던 CPU 비용).
	//  GL 은 지운 객체의 이름을 새 객체에 다시 주므로 **객체를 지울 때마다 InvalidateBindings** (GfxGL·GLRhi·ImGuiGL 의 소멸자).
	//  캐시를 거치지 않고 직접 묶는 코드(ImGuiGL)도 끝나면 InvalidateBindings. 현재 컨텍스트가 바뀌면 저절로 비운다
	void InvalidateBindings();
	uint64_t BindingGeneration();   // InvalidateBindings 마다 +1 (VAO 의 정점·인덱스 버퍼 캐시가 비교)
	void UseProgram(GLuint program);
	void BindTextureUnit(GLuint unit, GLuint texture);
	void BindSampler(GLuint unit, GLuint sampler);
	void BindUniformBuffer(GLuint binding, GLuint buffer);
	void BindVertexArray(GLuint vao);

	// VAO 하나의 정점·인덱스 버퍼 지정 캐시 (그리기마다 glVertexArrayVertexBuffer 를 다시 부르지 않게)
	struct VaoCache
	{
		uint64_t Generation = ~0ull;
		GLuint Buffer[16] = {};
		GLintptr Offset[16] = {};
		GLsizei Stride[16] = {};
		GLuint Elements = 0;
		bool Valid() const { return Generation == BindingGeneration(); }
		void Reset() { Generation = BindingGeneration(); for (int i = 0; i < 16; ++i) { Buffer[i] = ~0u; Offset[i] = -1; Stride[i] = -1; } Elements = ~0u; }
		void VertexBuffer(GLuint vao, GLuint slot, GLuint buffer, GLintptr offset, GLsizei stride)
		{
			if (!Valid()) Reset();
			if (Buffer[slot] == buffer && Offset[slot] == offset && Stride[slot] == stride) return;
			Buffer[slot] = buffer; Offset[slot] = offset; Stride[slot] = stride;
			glVertexArrayVertexBuffer(vao, slot, buffer, offset, stride);
		}
		void ElementBuffer(GLuint vao, GLuint buffer)
		{
			if (!Valid()) Reset();
			if (Elements == buffer) return;
			Elements = buffer;
			glVertexArrayElementBuffer(vao, buffer);
		}
	};
}
