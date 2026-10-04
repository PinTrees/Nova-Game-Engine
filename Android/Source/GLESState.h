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

	// ---- 기억: 같은 값이면 GL 을 부르지 않는다 (에뮬레이터 · 드라이버는 GL 호출마다 비용 — 효과 Apply 가 유닛 수십 개를 매번 다시 묶었다).
	//  이 함수들 밖에서 그 GL 상태 · 바인딩을 바꾼 곳은 Invalidate · Forget 으로 알린다
	void InvalidateStates();          // ApplyRasterizer · ApplyBlend · ApplyDepthStencil 의 기억
	void InvalidateBindings();        // 프로그램 · 상수 블록 · 텍스처 유닛 · 샘플러 · SSBO · image · 지금 유닛
	void ForgetUnit(GLuint unit);     // 그 유닛의 텍스처를 밖에서 바꿨다
	void NoteActiveUnit(GLuint unit); // 밖에서 glActiveTexture 했다
	void UseProgram(GLuint program);
	void BindUniformBuffer(GLuint binding, GLuint buffer);
	void BindTexture(GLuint unit, GLenum target, GLuint texture);   // target 0 = 유닛 비우기 (2D · 배열 · 큐브 모두 0)
	void BindSampler(GLuint unit, GLuint sampler);
	void BindStorage(GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);   // size 0 = 풀기
	void BindImage(GLuint unit, GLuint texture, GLint level, GLenum format);         // texture 0 = 풀기
	// GL 객체 (텍스처 · 버퍼 · 샘플러 · 프로그램 · 정점 배열) 를 지웠다: 이름이 다시 쓰일 수 있어 기억을 모두 버린다 (Epoch 도 하나 올린다)
	void Deleted();
	uint64_t Epoch();                 // 정점 배열 안의 버퍼 기억 (GfxGLES 의 GLLayout) 이 이것과 같을 때만 믿는다
	void BindVertexArray(GLuint vao);
	void ForgetVertexArray();         // 밖에서 glBindVertexArray 했다
}
