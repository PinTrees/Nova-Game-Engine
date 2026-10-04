#pragma once

// 모바일 GPU 의 텍스처 압축 형식 (ASTC · ETC2) — DXGI 에 없는 형식이라 DDS (DX10 머리) 의 dxgiFormat 자리에 이 번호를 쓴다.
//  - ASTC: 옛 Windows SDK 의 DXGI_FORMAT_ASTC_* 번호 (133 + 4 × 블록 순서: +0 typeless, +1 UNORM, +2 UNORM_SRGB).
//    블록 순서 = GL 번호 순서 (GL_COMPRESSED_RGBA_ASTC_4x4_KHR 0x93B0 + i, sRGB 0x93D0 + i)
//  - ETC2: 엔진 번호 240 ~ 243 (RGB · sRGB · RGBA (EAC 알파) · sRGB + EAC 알파). 블록 4x4
//  PC (nova android export) 가 쓰고, 안드로이드 (GfxGLES · DirectXTexLite) 가 읽는다
namespace MobileTex
{
	struct Block { int X, Y; };
	constexpr Block kAstcBlocks[] = { { 4, 4 }, { 5, 4 }, { 5, 5 }, { 6, 5 }, { 6, 6 }, { 8, 5 }, { 8, 6 }, { 8, 8 }, { 10, 5 }, { 10, 6 }, { 10, 8 }, { 10, 10 }, { 12, 10 }, { 12, 12 } };
	constexpr int kAstcCount = 14;
	constexpr unsigned kAstcFirst = 133, kAstcEnd = 133 + 4 * kAstcCount;
	constexpr unsigned kEtc2RGB8 = 240, kEtc2SRGB8 = 241, kEtc2RGBA8 = 242, kEtc2SRGB8A8 = 243;

	constexpr unsigned AstcFormat(int blockIndex, bool srgb) { return kAstcFirst + 1 + 4 * (unsigned)blockIndex + (srgb ? 1u : 0u); }
	inline int AstcBlockIndex(unsigned f) { return f >= kAstcFirst && f < kAstcEnd ? (int)((f - kAstcFirst) / 4) : -1; }
	inline bool AstcSrgb(unsigned f) { return f >= kAstcFirst && f < kAstcEnd && (f - kAstcFirst) % 4 == 2; }
	inline int FindAstcBlock(int x, int y)
	{
		for (int i = 0; i < kAstcCount; ++i)
			if (kAstcBlocks[i].X == x && kAstcBlocks[i].Y == y) return i;
		return -1;
	}
	inline bool IsEtc2(unsigned f) { return f >= kEtc2RGB8 && f <= kEtc2SRGB8A8; }
	inline bool IsMobile(unsigned f) { return AstcBlockIndex(f) >= 0 || IsEtc2(f); }

	// 블록 크기 (픽셀) 와 블록당 바이트. 모바일 형식이 아니면 false
	inline bool BlockInfo(unsigned f, int& bx, int& by, int& bytes)
	{
		if (const int i = AstcBlockIndex(f); i >= 0) { bx = kAstcBlocks[i].X; by = kAstcBlocks[i].Y; bytes = 16; return true; }
		if (IsEtc2(f)) { bx = by = 4; bytes = f >= kEtc2RGBA8 ? 16 : 8; return true; }
		return false;
	}

	// 사람이 읽는 이름 (Inspector · 로그)
	inline const char* Name(unsigned f)
	{
		static const char* astc[] = { "ASTC 4x4", "ASTC 5x4", "ASTC 5x5", "ASTC 6x5", "ASTC 6x6", "ASTC 8x5", "ASTC 8x6", "ASTC 8x8", "ASTC 10x5", "ASTC 10x6",
			"ASTC 10x8", "ASTC 10x10", "ASTC 12x10", "ASTC 12x12" };
		if (const int i = AstcBlockIndex(f); i >= 0) return astc[i];
		switch (f)
		{
		case kEtc2RGB8: case kEtc2SRGB8: return "ETC2 RGB";
		case kEtc2RGBA8: case kEtc2SRGB8A8: return "ETC2 RGBA";
		default: return "";
		}
	}
}
