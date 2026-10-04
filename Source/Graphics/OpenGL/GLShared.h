#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class GfxShaderResourceView;
class GfxUnorderedAccessView;
class GfxDevice;
class GfxContext;

// GL 효과(GLRhi.cpp)와 Gfx GL 구현(GfxGL.cpp)이 주고받는 것.
//  - 입력 서명: FxPass::GetDesc 의 pIAInputSignature 가 GL 에서는 이 구조체 (의미 → location 표).
//    GfxDevice::CreateInputLayout 이 받아 정점 형식을 location 에 맞춘다
struct GLInputSignature
{
	static constexpr uint32_t kMagic = 0x4C474E56;   // "VNGL"
	uint32_t Magic = kMagic;
	const std::vector<std::pair<std::string, int>>* Inputs = nullptr;   // NormSemantic(TEXCOORD0 …) → location
};

// Gfx GL 의 셰이더 자원 뷰 → GL 텍스처 이름 (뷰가 텍스처 뷰면 그 이름). GL 이 아닌 뷰 = 0
unsigned int GfxGL_TextureName(GfxShaderResourceView* view);
// 버퍼 뷰 (구조 · raw 버퍼 SRV · UAV) → GL 버퍼 · 바이트 범위 (SSBO), 텍스처 UAV → 텍스처 · 밉 · 내부 형식 (image). 아니면 false
bool GfxGL_BufferRange(GfxShaderResourceView* view, unsigned& buffer, unsigned& offset, unsigned& size);
bool GfxGL_UavBuffer(GfxUnorderedAccessView* view, unsigned& buffer, unsigned& offset, unsigned& size);
bool GfxGL_UavImage(GfxUnorderedAccessView* view, unsigned& texture, unsigned& level, unsigned& format);
