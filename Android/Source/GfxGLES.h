#pragma once
#include "Gfx.h"
#include <memory>
#include <string>

namespace Rhi { class Device; }

// Gfx(D3D11 모양) 층의 OpenGL ES 3.2 구현 (안드로이드). 데스크톱 GfxGL 과 같은 규칙을 바인딩 방식(DSA 없음)으로.
//  - 지금 현재인 EGL 컨텍스트 위에 만든다 (AndroidPlatform::Egl). 만든 스레드에서만 쓴다
//  - 화면 표시: 엔진은 늘 텍스처(백버퍼)에 그리고 Present 가 기본 프레임버퍼로 위아래를 뒤집어 복사 (eglSwapBuffers 는 부르는 쪽)
namespace GfxGLES
{
	bool CreateDevice(GfxDevice** device, GfxContext** context, std::string& error);

	// 같은 컨텍스트 위의 RHI 장치 (효과 FxEffect 를 이 장치로 불러온다. pass 상태는 이 Gfx 컨텍스트로)
	std::unique_ptr<Rhi::Device> CreateRhiDevice(GfxDevice* device, GfxContext* context, std::string& error);

	// backBuffer → 기본 프레임버퍼 (위아래 뒤집기). 그 뒤 eglSwapBuffers
	void Present(GfxDevice* device, GfxTexture2D* backBuffer, int windowWidth, int windowHeight);

	// GL 을 직접 만진 뒤: Gfx 컨텍스트가 알고 있는 상태를 다시 GL 에
	void RestoreState(GfxContext* context);

	bool IsFormatSupported(DXGI_FORMAT format);
}

// GLESRhi 가 효과의 텍스처 유닛을 묶을 때: 뷰를 셰이더가 읽을 GL 텍스처로 (부분 뷰면 사본을 새로 고침). 0 = GLES 뷰가 아님
unsigned int GfxGLES_ResolveView(GfxShaderResourceView* view, unsigned int* target);

// 그리기 CPU 진단 (scene 검사의 -e profile on → NOVA_TEST 의 gl): GL 호출 종류별 수 (늘 센다 — 더하기 하나씩)
namespace GlesCounters
{
	inline uint64_t Applies = 0;       // 효과 pass 적용 (ESEffect::Apply)
	inline uint64_t Programs = 0;      // glUseProgram
	inline uint64_t UboUploads = 0;    // 상수 블록 올리기 (glBufferSubData)
	inline uint64_t UboBytes = 0;
	inline uint64_t UboBinds = 0;      // glBindBufferBase (UNIFORM)
	inline uint64_t TexBinds = 0;      // glBindTexture (유닛)
	inline uint64_t SamplerBinds = 0;  // glBindSampler
	inline uint64_t ComputeBinds = 0;  // SSBO · image
	inline uint64_t Draws = 0;         // 그리기 (간접 포함)
	inline uint64_t VertexBinds = 0;   // VAO · 정점 · 인덱스 버퍼
	inline uint64_t States = 0;        // 래스터 · 블렌드 · 깊이 상태 적용
	inline void Reset() { Applies = Programs = UboUploads = UboBytes = UboBinds = TexBinds = SamplerBinds = ComputeBinds = Draws = VertexBinds = States = 0; }
}
