#pragma once
#include <string>
#include <vector>

// UI 글꼴 (TextMeshPro 와 같은 SDF 방식): 글꼴(파일 + 굵게)마다 SDF(Signed Distance Field) 아틀라스 하나.
//  - 글자를 기준 크기(BasePx)에서 거리장으로 구워 두고, 어떤 크기 · 배율 · 회전으로 그려도 셰이더가 가장자리를 다시 만든다 → 늘 선명
//  - 같은 거리장으로 Outline · Underlay(그림자) · Face Dilate(굵기) 를 셰이더에서
//  - 처음 보는 글자가 나오면 그 글자만 구워 넣는다 (한글도 쓰는 글자만). 아틀라스가 차면 두 배로 (최대 4096)
//  - 글꼴에 없는 글자는 대체 글꼴(Fallback)에서: 기본 글꼴(Pretendard) → 맑은 고딕 → Segoe UI Symbol
// 래스터화 = stb_truetype (stbtt_GetGlyphSDF). .ttf / .otf 지원.
namespace UIFont
{
	struct Glyph
	{
		float Advance = 0.0f;                  // 기준 크기 픽셀
		float X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;  // 펜(기준선) 기준 사각형, 기준 크기 픽셀 (y 아래로 +, 거리장 여백 포함)
		int AtlasX = 0, AtlasY = 0, W = 0, H = 0;
		bool Visible = false;                  // 공백 · 그릴 것 없는 글자는 false
		int FontIndex = 0;                     // 대체 글꼴 순번 (0 = 고른 글꼴) — 짝 간격(kerning) 은 같은 글꼴끼리만
	};

	struct Font
	{
		float BasePx = 48.0f;                  // 이 크기로 구웠다 (글자 크기 / BasePx = 배율)
		float Spread = 7.0f;                   // 거리장 여백 (기준 크기 픽셀) — Outline · Underlay 의 최대 두께
		float Ascent = 0.0f, Descent = 0.0f;   // 기준 크기 픽셀 (Descent 는 음수)
		int AtlasW = 0, AtlasH = 0;
		GfxShaderResourceView* Texture = nullptr;   // R8: 0.5 = 가장자리, 클수록 안쪽
		float SolidU = 0.0f, SolidV = 0.0f;         // 꽉 찬 칸 (밑줄 · 취소선 · <mark>)
		int Generation = 0;                         // 아틀라스를 키울 때마다 +1 (그 전에 계산한 UV 는 틀린다 → Text 가 다시 배치)
		int Revision = 0;                           // 글자를 넣을 때마다 +1 (아직 못 구운 글자가 있던 Text 가 다시 배치)
		bool Pending = false;                       // 마지막 Get 에서 시간이 모자라 못 구운 글자가 있다

		const Glyph* Find(char32_t c) const;
		float Kerning(const Glyph* a, char32_t ca, const Glyph* b, char32_t cb) const;   // 기준 크기 픽셀
		void* Impl = nullptr;
	};

	// 기본 글꼴 (Pretendard, 한글 포함) 표시 이름
	const char* DefaultFontName();
	// 글꼴 경로 → 실제 파일 (빈 문자열/"builtin:Default" = 기본 글꼴, Bold 면 굵은 파일)
	std::wstring ResolveFile(const std::string& fontPath, bool bold);

	// 이 글자들이 들어 있는 SDF 글꼴 (없던 글자는 구워 넣는다). 실패하면 nullptr
	// complete = false 면 한 프레임 예산(약 6 ms)까지만 굽고 나머지는 다음 호출에 (긴 한글 글이 처음 나올 때 멈추지 않게) → Font::Pending
	Font* Get(const std::string& fontPath, bool bold, const std::u32string& text, bool complete = true);

	// Project 창/Object Picker 용: Assets 의 글꼴 파일 목록 + 기본 글꼴
	std::vector<std::string> FindAll();
	bool IsFontPath(const std::string& path);

	std::u32string DecodeUtf8(const std::string& s);
	std::string EncodeUtf8(const std::u32string& s);
}
