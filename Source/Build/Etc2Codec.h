#pragma once
#include <cstdint>

// ETC2 (OpenGL ES 3.0 의 기본 압축) 블록 인코더 · 디코더 — 4x4 화소, RGBA 8 비트 입력 (행 우선, 행 0 = 위).
//  - RGB: ETC1 의 개별 · 차분 모드 (두 방향) 와 ETC2 평면 모드 (부드러운 변화) 중 오차가 가장 작은 것 (T · H 모드는 아직 안 씀)
//  - 알파: EAC (RGBA8_ETC2_EAC 의 앞 8 바이트) — 16 표 × 곱 1..15 중 오차가 가장 작은 것
//  블록 바이트: RGB 8, RGBA 16 (EAC 알파 8 + RGB 8). 64 비트 블록은 큰 쪽 바이트가 먼저 (스펙)
namespace Etc2Codec
{
	void EncodeRgb(const uint8_t rgba[64], uint8_t out[8], bool perceptual = true);   // perceptual = 색 그림 (초록 가중), 아니면 성분 똑같이 (노멀맵 · 마스크)
	void EncodeAlpha(const uint8_t rgba[64], uint8_t out[8]);
	void DecodeRgb(const uint8_t in[8], uint8_t rgba[64]);       // RGB 만 채운다 (알파 255)
	void DecodeAlpha(const uint8_t in[8], uint8_t rgba[64]);     // 알파만 채운다
}
