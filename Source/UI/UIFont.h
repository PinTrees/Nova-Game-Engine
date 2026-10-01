#pragma once
#include <string>

// UI 글꼴 (Unity 의 동적 Font 와 같은 방식): (글꼴 파일, 픽셀 크기) 마다 글자 아틀라스를 만들고,
// 처음 보는 글자가 나오면 그 글자를 넣어 다시 만든다 → 한글 등 쓰는 글자만 들어가 가볍다.
// 래스터화는 ImGui 의 ImFontAtlas(stb_truetype) 를 에디터 UI 와 별개로 쓴다. .ttf / .otf 지원.
struct ImFont;
struct ImFontGlyph;

namespace UIFont
{
	// 기본 글꼴 (Pretendard, 한글 포함) 경로 표시 이름
	const char* DefaultFontName();
	// 글꼴 경로 → 실제 파일 (빈 문자열/"builtin:Default" = 기본 글꼴, Bold 면 굵은 파일)
	std::wstring ResolveFile(const std::string& fontPath, bool bold);

	struct Atlas
	{
		ImFont* Font = nullptr;
		GfxShaderResourceView* Texture = nullptr;
		float PixelSize = 16.0f;
		float Ascent = 0.0f, Descent = 0.0f;   // 픽셀
		const ImFontGlyph* Glyph(unsigned int codepoint) const;
	};

	// 이 글자들이 들어 있는 아틀라스 (없던 글자가 있으면 다시 만든다). 실패하면 nullptr
	const Atlas* Get(const std::string& fontPath, bool bold, int pixelSize, const std::u32string& text);

	// Project 창/Object Picker 용: Assets 의 글꼴 파일 목록 + 기본 글꼴
	std::vector<std::string> FindAll();
	bool IsFontPath(const std::string& path);

	std::u32string DecodeUtf8(const std::string& s);
}
