#include "pch.h"
#include "UIText.h"
#include "UIFont.h"
#include "UIRenderer.h"
#include "UISprites.h"
#include "RectTransform.h"
#include "UnityGUI.h"
#include "ObjectPicker.h"

namespace
{
	// ------------------------------------------------------------------ Rich Text 해석
	enum CharFlags : uint8 { FBold = 1, FItalic = 2, FUnderline = 4, FStrike = 8, FSup = 16, FSub = 32 };

	struct PChar
	{
		char32_t C = 0;
		int Source = 0;
		uint8 Flags = 0;
		float Scale = 1.0f;       // 글자 크기 배율 (기본 크기 기준)
		uint32 Color = 0;
		bool HasColor = false;
		uint32 Mark = 0;          // 0 = 없음
		int Link = -1;
		int Sprite = -1;
		float CSpace = 0.0f;      // px
		float VOffset = 0.0f;     // px
		int Align = -1;           // 줄 정렬 (0 왼, 1 가운데, 2 오른, -1 = 컴포넌트)
	};

	std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return s;
	}

	bool ParseColor(std::string v, uint32& out)
	{
		v = Lower(v);
		if (!v.empty() && v.front() == '"') v = v.substr(1, v.size() >= 2 ? v.size() - 2 : 0);
		static const std::pair<const char*, uint32> kNames[] = {
			{ "black", 0xFF000000 }, { "white", 0xFFFFFFFF }, { "red", 0xFF0000FF }, { "green", 0xFF00FF00 }, { "blue", 0xFFFF0000 },
			{ "yellow", 0xFF00FFFF }, { "orange", 0xFF00A5FF }, { "purple", 0xFF800080 }, { "cyan", 0xFFFFFF00 }, { "magenta", 0xFFFF00FF },
			{ "grey", 0xFF808080 }, { "gray", 0xFF808080 }, { "lightblue", 0xFFE6D8AD }, { "brown", 0xFF2A2AA5 } };
		for (const auto& n : kNames)
			if (v == n.first) { out = n.second; return true; }
		if (v.empty() || v[0] != '#')
			return false;
		std::string h = v.substr(1);
		if (h.size() == 3 || h.size() == 4)
		{
			std::string e;
			for (char c : h) { e.push_back(c); e.push_back(c); }
			h = e;
		}
		if (h.size() != 6 && h.size() != 8)
			return false;
		for (char c : h)
			if (!isxdigit((unsigned char)c)) return false;
		const uint32 r = std::stoul(h.substr(0, 2), nullptr, 16), g = std::stoul(h.substr(2, 2), nullptr, 16), b = std::stoul(h.substr(4, 2), nullptr, 16);
		const uint32 a = h.size() == 8 ? std::stoul(h.substr(6, 2), nullptr, 16) : 255;
		out = r | (g << 8) | (b << 16) | (a << 24);
		return true;
	}

	std::string Unquote(std::string v)
	{
		while (!v.empty() && (v.front() == ' ')) v.erase(0, 1);
		while (!v.empty() && (v.back() == ' ')) v.pop_back();
		if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front())
			v = v.substr(1, v.size() - 2);
		return v;
	}

	struct ParseResult
	{
		std::vector<PChar> Chars;
		std::vector<Text::LinkInfo> Links;
		std::vector<std::string> Sprites;
	};

	// 원문 → 글자 목록 (태그는 상태로). 모르는 태그는 글자 그대로 (TMP 와 같음)
	void Parse(const std::u32string& src, bool rich, int styleBits, int fontSize, ParseResult& out)
	{
		out.Chars.clear();
		out.Links.clear();
		out.Sprites.clear();
		PChar cur;
		if (styleBits & Text::StyleBold) cur.Flags |= FBold;
		if (styleBits & Text::StyleItalic) cur.Flags |= FItalic;
		if (styleBits & Text::StyleUnderline) cur.Flags |= FUnderline;
		if (styleBits & Text::StyleStrikethrough) cur.Flags |= FStrike;
		int caseMode = (styleBits & Text::StyleUpperCase) ? 2 : ((styleBits & Text::StyleLowerCase) ? 1 : 0);
		std::vector<uint32> colors, marks;
		std::vector<float> sizes, cspaces, voffs;
		std::vector<int> aligns, cases;
		int bold = 0, italic = 0, under = 0, strike = 0, sup = 0, sub = 0;
		bool noparse = false;
		auto push = [&](char32_t c, int source) {
			PChar p = cur;
			if (caseMode == 2 && c < 128) c = (char32_t)toupper((int)c);
			else if (caseMode == 1 && c < 128) c = (char32_t)tolower((int)c);
			p.C = c;
			p.Source = source;
			if (bold) p.Flags |= FBold;
			if (italic) p.Flags |= FItalic;
			if (under) p.Flags |= FUnderline;
			if (strike) p.Flags |= FStrike;
			if (sup) p.Flags |= FSup;
			if (sub) p.Flags |= FSub;
			if (p.Link >= 0)
			{
				Text::LinkInfo& l = out.Links[p.Link];
				if (l.Count == 0) l.First = (int)out.Chars.size();
				++l.Count;
			}
			out.Chars.push_back(p);
		};
		for (size_t i = 0; i < src.size(); ++i)
		{
			const char32_t c = src[i];
			if (!rich || c != '<')
			{
				push(c, (int)i);
				continue;
			}
			// 태그 찾기 (같은 줄, 128 글자 안)
			size_t close = std::u32string::npos;
			for (size_t k = i + 1; k < src.size() && k < i + 128; ++k)
			{
				if (src[k] == '>') { close = k; break; }
				if (src[k] == '<' || src[k] == '\n') break;
			}
			if (close == std::u32string::npos)
			{
				push(c, (int)i);
				continue;
			}
			const std::string tag = UIFont::EncodeUtf8(src.substr(i + 1, close - i - 1));
			const bool closing = !tag.empty() && tag[0] == '/';
			std::string body = closing ? tag.substr(1) : tag;
			std::string name = body, value;
			const size_t eq = body.find('=');
			const size_t sp = body.find(' ');
			if (eq != std::string::npos && (sp == std::string::npos || eq < sp))
			{
				name = body.substr(0, eq);
				value = Unquote(body.substr(eq + 1));
			}
			else if (sp != std::string::npos)
			{
				name = body.substr(0, sp);
				value = body.substr(sp + 1);
			}
			name = Lower(Unquote(name));
			if (noparse)
			{
				if (closing && name == "noparse") { noparse = false; i = close; }
				else push(c, (int)i);
				continue;
			}
			bool ok = true;
			if (name == "b") bold += closing ? (bold > 0 ? -1 : 0) : 1;
			else if (name == "i") italic += closing ? (italic > 0 ? -1 : 0) : 1;
			else if (name == "u") under += closing ? (under > 0 ? -1 : 0) : 1;
			else if (name == "s") strike += closing ? (strike > 0 ? -1 : 0) : 1;
			else if (name == "sup") sup += closing ? (sup > 0 ? -1 : 0) : 1;
			else if (name == "sub") sub += closing ? (sub > 0 ? -1 : 0) : 1;
			else if (name == "noparse" && !closing) noparse = true;
			else if (name == "br" && !closing) push('\n', (int)i);
			else if (name == "nobr") {}   // 받기만 (줄바꿈 금지는 아직)
			else if (name == "color" || name == "alpha")
			{
				if (closing)
				{
					if (!colors.empty()) { cur.Color = colors.back(); colors.pop_back(); cur.HasColor = cur.Color != 0 || !colors.empty(); }
					else cur.HasColor = false;
				}
				else
				{
					uint32 col = 0;
					if (name == "alpha")
					{
						const std::string h = Lower(value);
						if (h.size() == 3 && h[0] == '#' && isxdigit((unsigned char)h[1]) && isxdigit((unsigned char)h[2]))
						{
							const uint32 base = cur.HasColor ? cur.Color : 0xFFFFFFFF;
							col = (base & 0x00FFFFFF) | (std::stoul(h.substr(1), nullptr, 16) << 24);
						}
						else ok = false;
					}
					else ok = ParseColor(value, col);
					if (ok)
					{
						colors.push_back(cur.HasColor ? cur.Color : 0);
						cur.Color = col;
						cur.HasColor = true;
					}
				}
			}
			else if (name == "mark")
			{
				if (closing) { cur.Mark = marks.empty() ? 0 : marks.back(); if (!marks.empty()) marks.pop_back(); }
				else
				{
					uint32 col = 0;
					ok = ParseColor(value, col);
					if (ok) { marks.push_back(cur.Mark); cur.Mark = col; }
				}
			}
			else if (name == "size")
			{
				if (closing) { cur.Scale = sizes.empty() ? 1.0f : sizes.back(); if (!sizes.empty()) sizes.pop_back(); }
				else
				{
					float s = cur.Scale;
					try
					{
						if (!value.empty() && value.back() == '%') s = std::stof(value.substr(0, value.size() - 1)) / 100.0f;
						else if (value.size() > 2 && Lower(value.substr(value.size() - 2)) == "em") s = std::stof(value.substr(0, value.size() - 2));
						else if (!value.empty() && (value[0] == '+' || value[0] == '-')) s = cur.Scale + std::stof(value) / (float)fontSize;
						else s = std::stof(value) / (float)fontSize;
					}
					catch (...) { ok = false; }
					if (ok) { sizes.push_back(cur.Scale); cur.Scale = std::clamp(s, 0.05f, 20.0f); }
				}
			}
			else if (name == "cspace")
			{
				if (closing) { cur.CSpace = cspaces.empty() ? 0.0f : cspaces.back(); if (!cspaces.empty()) cspaces.pop_back(); }
				else { try { cspaces.push_back(cur.CSpace); cur.CSpace = std::stof(value); } catch (...) { ok = false; } }
			}
			else if (name == "voffset")
			{
				if (closing) { cur.VOffset = voffs.empty() ? 0.0f : voffs.back(); if (!voffs.empty()) voffs.pop_back(); }
				else { try { voffs.push_back(cur.VOffset); cur.VOffset = std::stof(value); } catch (...) { ok = false; } }
			}
			else if (name == "align")
			{
				if (closing) { cur.Align = aligns.empty() ? -1 : aligns.back(); if (!aligns.empty()) aligns.pop_back(); }
				else
				{
					const std::string a = Lower(value);
					aligns.push_back(cur.Align);
					cur.Align = a == "center" ? 1 : (a == "right" ? 2 : 0);
				}
			}
			else if (name == "lowercase" || name == "uppercase" || name == "allcaps")
			{
				if (closing) { caseMode = cases.empty() ? 0 : cases.back(); if (!cases.empty()) cases.pop_back(); }
				else { cases.push_back(caseMode); caseMode = name == "lowercase" ? 1 : 2; }
			}
			else if (name == "link")
			{
				if (closing) cur.Link = -1;
				else
				{
					Text::LinkInfo l;
					l.Id = value;
					cur.Link = (int)out.Links.size();
					out.Links.push_back(l);
				}
			}
			else if (name == "sprite" && !closing)
			{
				// <sprite name="coin">, <sprite="coin">, <sprite=coin>
				std::string spriteName = value;
				const size_t np = Lower(body).find("name=");
				if (np != std::string::npos)
				{
					std::string rest = body.substr(np + 5);
					if (!rest.empty() && (rest[0] == '"' || rest[0] == '\''))
					{
						const size_t endq = rest.find(rest[0], 1);
						spriteName = rest.substr(1, endq == std::string::npos ? std::string::npos : endq - 1);
					}
					else
						spriteName = rest.substr(0, rest.find(' '));
				}
				if (spriteName.empty()) ok = false;
				else
				{
					PChar p = cur;
					p.C = 0xFFFC;
					p.Source = (int)i;
					p.Sprite = (int)out.Sprites.size();
					out.Sprites.push_back(spriteName);
					if (p.Link >= 0) { Text::LinkInfo& l = out.Links[p.Link]; if (l.Count == 0) l.First = (int)out.Chars.size(); ++l.Count; }
					out.Chars.push_back(p);
				}
			}
			else ok = false;
			if (!ok)
			{
				push(c, (int)i);   // 모르는 태그: 글자 그대로
				continue;
			}
			i = close;
		}
		for (Text::LinkInfo& l : out.Links)
			for (int k = l.First; k < l.First + l.Count && k < (int)out.Chars.size(); ++k)
				if (out.Chars[k].Sprite < 0)
					l.Text += UIFont::EncodeUtf8(std::u32string(1, out.Chars[k].C));
	}

	// 한자/가나는 글자 사이에서 줄을 바꿀 수 있다 (한글은 띄어쓰기 기준)
	bool IsCjkBreakable(char32_t c)
	{
		return (c >= 0x3040 && c <= 0x30FF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF);
	}

	// 스프라이트 이름 → 경로 (Assets 이미지의 파일 이름). 이름이 경로면 그대로
	std::string SpritePath(const std::string& name)
	{
		if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos || name.find('.') != std::string::npos)
			return name;
		static std::map<std::string, std::string> s_Map;
		static double s_Built = -100.0;
		auto it = s_Map.find(Lower(name));
		if (it == s_Map.end() && ImGui::GetTime() - s_Built > 3.0)
		{
			s_Map.clear();
			for (const std::string& p : UISprites::FindAll())
				s_Map[Lower(std::filesystem::path(p).stem().string())] = p;
			s_Built = ImGui::GetTime();
			it = s_Map.find(Lower(name));
		}
		return it == s_Map.end() ? std::string() : it->second;
	}

	uint64 Fnv(uint64 h, const void* data, size_t n)
	{
		const unsigned char* p = static_cast<const unsigned char*>(data);
		for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
		return h;
	}

	// 필드 열 위치 (UnityGUI 행과 같은 비율)
	float FieldX(float x, float w) { return x + std::clamp(w * 0.408f, 90.0f, (std::max)(90.0f, w - 90.0f)); }
}

Text::Text()
{
	m_InspectorTitleName = "Text";
	m_Color[0] = m_Color[1] = m_Color[2] = 50.0f / 255.0f;   // Unity Text 기본 색 (50, 50, 50)
	m_VOverflow = VOverflow::Overflow;
}

// ------------------------------------------------------------------ 레이아웃
uint64 Text::LayoutKey() const
{
	uint64 h = 1469598103934665603ull;
	h = Fnv(h, m_Text.data(), m_Text.size());
	h = Fnv(h, m_Font.data(), m_Font.size());
	const bool rich = m_RichText && !m_ForcePlain;
	struct { int a, b, c, d, e, f, g, h; float i, j, k, l; Vec4 m; Vec2 n; bool o, p; } k = {
		m_StyleBits, m_FontSize, m_Alignment, (int)m_HOverflow, (int)m_VOverflow, m_MinSize, m_MaxSize, 0,
		m_LineSpacing, CharacterSpacing, WordSpacing, ParagraphSpacing, Margin, Vec2(0, 0), m_BestFit, rich };
	if (RectTransform* rt = const_cast<Text*>(this)->GetRect())
	{
		k.n = rt->GetRectSize();
		const Vec2 mn = rt->GetRectMin();
		h = Fnv(h, &mn, sizeof(mn));
	}
	return Fnv(h, &k, sizeof(k));
}

void Text::ForceMeshUpdate()
{
	m_ForScript = true;
	UpdateLayout();
	m_ForScript = false;
}

void Text::UpdateLayout()
{
	const uint64 key = LayoutKey();
	bool fontsSame = m_Atlas[0] != nullptr;
	for (int f = 0; f < 2 && fontsSame; ++f)
		if (const auto* font = static_cast<const UIFont::Font*>(m_FontRef[f]))
			fontsSame = font->Generation == m_FontGen[f] && font->Texture == m_Atlas[f];
	if (m_FontPending)
		fontsSame = false;   // 못 구운 글자가 남았다: 매 프레임 예산만큼 더 굽고 다시 배치 (스크립트가 읽으면 끝까지)
	if (key == m_LayoutKey && fontsSame)
		return;
	m_LayoutKey = key;
	Layout();
}

void Text::Layout()
{
	m_Quads.clear();
	m_Chars.clear();
	m_Lines.clear();
	m_Links.clear();
	m_Sprites.clear();
	m_Atlas[0] = m_Atlas[1] = nullptr;
	m_FontRef[0] = m_FontRef[1] = nullptr;
	m_Overflowing = false;
	m_PreferredW = m_PreferredH = 0.0f;
	if (m_ModsText != m_Text)
	{
		m_Mods.clear();
		m_ModsText = m_Text;
	}
	RectTransform* rt = GetRect();
	if (rt == nullptr)
		return;

	const std::u32string src = UIFont::DecodeUtf8(m_Text);
	ParseResult pr;
	Parse(src, m_RichText && !m_ForcePlain, m_StyleBits, m_FontSize, pr);
	std::u32string all;
	bool anyBold = false;
	for (const PChar& p : pr.Chars)
	{
		all.push_back(p.C);
		anyBold |= (p.Flags & FBold) != 0;
	}
	all.push_back(U'…');
	UIFont::Font* fonts[2] = { UIFont::Get(m_Font, false, all, m_ForScript), anyBold ? UIFont::Get(m_Font, true, all, m_ForScript) : nullptr };
	m_FontPending = fonts[0] && (fonts[0]->Pending || (fonts[1] && fonts[1]->Pending));
	if (fonts[0] == nullptr)
		return;
	if (fonts[1] == nullptr)
		fonts[1] = fonts[0];
	for (int f = 0; f < 2; ++f)
	{
		m_FontRef[f] = fonts[f];
		m_FontGen[f] = fonts[f]->Generation;
		m_FontRev[f] = fonts[f]->Revision;
		m_Atlas[f] = fonts[f]->Texture;
		m_Spread[f][0] = fonts[f]->Spread / fonts[f]->AtlasW;
		m_Spread[f][1] = fonts[f]->Spread / fonts[f]->AtlasH;
	}
	m_Links = pr.Links;
	m_Sprites = pr.Sprites;

	const Vec2 rmin = rt->GetRectMin(), rsize = rt->GetRectSize();
	const float left = rmin.x + Margin.x, right = rmin.x + rsize.x - Margin.z;
	const float top = rmin.y + rsize.y - Margin.y, bottom = rmin.y + Margin.w;
	const float maxW = (std::max)(0.0f, right - left), maxH = (std::max)(0.0f, top - bottom);
	const bool wrap = m_HOverflow == HOverflow::Wrap;

	auto fontOf = [&](const PChar& p) { return (p.Flags & FBold) ? fonts[1] : fonts[0]; };
	// 글자 크기(px) — sup/sub 은 작게
	auto sizeOf = [&](const PChar& p, float base) { return base * p.Scale * ((p.Flags & (FSup | FSub)) ? 0.6f : 1.0f); };

	struct LL { int First, Count; float Width, Asc, Desc; bool Paragraph; int Align; };
	std::vector<LL> lines;
	std::vector<float> adv;   // 글자별 진행 폭

	// 기본 크기 size 로 줄을 나눈다 → 줄 목록 + 전체 폭 · 높이
	auto measure = [&](float size, float& widest, float& totalH) {
		lines.clear();
		adv.assign(pr.Chars.size(), 0.0f);
		for (size_t i = 0; i < pr.Chars.size(); ++i)
		{
			const PChar& p = pr.Chars[i];
			const float s = sizeOf(p, size);
			const UIFont::Font* f = fontOf(p);
			const float k = s / f->BasePx;
			float a = 0.0f;
			if (p.Sprite >= 0)
				a = s * 1.1f;
			else if (p.C == '\t')
			{
				const UIFont::Glyph* g = f->Find(' ');
				a = (g ? g->Advance : f->BasePx * 0.25f) * k * 4.0f;
			}
			else if (p.C != '\n' && p.C != '\r')
			{
				const UIFont::Glyph* g = f->Find(p.C);
				a = (g ? g->Advance : 0.0f) * k;
				if (i + 1 < pr.Chars.size() && pr.Chars[i + 1].Sprite < 0 && (pr.Chars[i + 1].Flags & FBold) == (p.Flags & FBold))
					a += f->Kerning(g, p.C, f->Find(pr.Chars[i + 1].C), pr.Chars[i + 1].C) * k;
			}
			if (p.C != '\n' && p.C != '\r')
				a += CharacterSpacing * s / 100.0f + p.CSpace;
			if (p.C == ' ')
				a += WordSpacing * s / 100.0f;
			adv[i] = a;
		}
		auto lineMetrics = [&](LL& l) {
			l.Asc = 0.0f; l.Desc = 0.0f;
			for (int i = l.First; i < l.First + l.Count; ++i)
			{
				const PChar& p = pr.Chars[i];
				const UIFont::Font* f = fontOf(p);
				const float s = size * p.Scale;
				l.Asc = (std::max)(l.Asc, f->Ascent * s / f->BasePx + (std::max)(0.0f, p.VOffset));
				l.Desc = (std::min)(l.Desc, f->Descent * s / f->BasePx + (std::min)(0.0f, p.VOffset));
			}
			if (l.Count == 0 || (l.Asc == 0.0f && l.Desc == 0.0f))
			{
				l.Asc = fonts[0]->Ascent * size / fonts[0]->BasePx;
				l.Desc = fonts[0]->Descent * size / fonts[0]->BasePx;
			}
			// 줄 끝 공백 제외 폭
			int e = l.First + l.Count;
			while (e > l.First && (pr.Chars[e - 1].C == ' ' || pr.Chars[e - 1].C == '\t' || pr.Chars[e - 1].C == '\n' || pr.Chars[e - 1].C == '\r'))
				--e;
			l.Width = 0.0f;
			for (int i = l.First; i < e; ++i)
				l.Width += adv[i];
			l.Align = l.Count > 0 ? pr.Chars[l.First].Align : -1;
		};
		int lineStart = 0, lastBreak = -1;
		float width = 0.0f;
		const int n = (int)pr.Chars.size();
		for (int i = 0; i < n; ++i)
		{
			const char32_t c = pr.Chars[i].C;
			if (c == '\n')
			{
				LL l{ lineStart, i + 1 - lineStart, 0, 0, 0, true, -1 };
				lineMetrics(l);
				lines.push_back(l);
				lineStart = i + 1;
				width = 0.0f;
				lastBreak = -1;
				continue;
			}
			if (wrap && c != ' ' && width + adv[i] > maxW + 0.01f && i > lineStart)
			{
				const int cut = (lastBreak > lineStart) ? lastBreak : i;
				LL l{ lineStart, cut - lineStart, 0, 0, 0, false, -1 };
				lineMetrics(l);
				lines.push_back(l);
				lineStart = cut;
				while (lineStart < n && pr.Chars[lineStart].C == ' ')   // 줄 앞 공백은 앞 줄에 붙인다
				{
					++lines.back().Count;
					++lineStart;
				}
				width = 0.0f;
				for (int k = lineStart; k < i; ++k)
					width += adv[k];
				lastBreak = -1;
			}
			if ((IsCjkBreakable(c) || pr.Chars[i].Sprite >= 0) && i > lineStart)
				lastBreak = i;
			width += adv[i];
			if (c == ' ')
				lastBreak = i + 1;
		}
		LL last{ lineStart, n - lineStart, 0, 0, 0, false, -1 };
		lineMetrics(last);
		lines.push_back(last);
		widest = 0.0f;
		totalH = 0.0f;
		for (size_t li = 0; li < lines.size(); ++li)
		{
			widest = (std::max)(widest, lines[li].Width);
			totalH += (lines[li].Asc - lines[li].Desc) * m_LineSpacing;
			if (lines[li].Paragraph && li + 1 < lines.size())
				totalH += ParagraphSpacing * size / 100.0f;
		}
	};

	// Auto Size: 넘치지 않는 가장 큰 크기 (거리장이라 크기마다 다시 굽지 않는다 — 재기만)
	float size = (float)m_FontSize;
	float widest = 0.0f, totalH = 0.0f;
	if (m_BestFit)
	{
		int lo = (std::max)(1, m_MinSize), hi = (std::max)(lo, m_MaxSize), best = lo;
		while (lo <= hi)
		{
			const int mid = (lo + hi) / 2;
			measure((float)mid, widest, totalH);
			if (totalH <= maxH + 0.01f && (wrap || widest <= maxW + 0.01f)) { best = mid; lo = mid + 1; }
			else hi = mid - 1;
		}
		size = (float)best;
	}
	measure(size, widest, totalH);
	m_UsedSize = (int)lroundf(size);
	m_PreferredW = widest + Margin.x + Margin.z;
	m_PreferredH = totalH + Margin.y + Margin.w;

	// 세로 넘침: Truncate / Ellipsis = 들어가는 줄까지
	size_t lineCount = lines.size();
	if (m_VOverflow != VOverflow::Overflow)
	{
		float h = 0.0f;
		size_t fit = 0;
		for (size_t li = 0; li < lines.size(); ++li)
		{
			h += (lines[li].Asc - lines[li].Desc) * m_LineSpacing;
			if (h > maxH + 0.01f)
				break;
			fit = li + 1;
			if (lines[li].Paragraph) h += ParagraphSpacing * size / 100.0f;
		}
		if (fit < lines.size())
		{
			m_Overflowing = true;
			lineCount = (std::max)((size_t)(m_VOverflow == VOverflow::Ellipsis ? 1 : 0), fit);
		}
	}
	if (!wrap && widest > maxW + 0.01f)
		m_Overflowing = true;

	// Ellipsis: 마지막 줄 끝을 … 로 (가로로 넘치는 줄도)
	std::vector<int> ellipsisAt(lineCount, -1);   // 이 위치부터 글자 대신 …
	if (m_VOverflow == VOverflow::Ellipsis && lineCount > 0)
	{
		const UIFont::Font* f = fonts[0];
		const UIFont::Glyph* eg = f->Find(U'…');
		for (size_t li = 0; li < lineCount; ++li)
		{
			const bool lastCut = li + 1 == lineCount && lineCount < lines.size();
			if (!lastCut && lines[li].Width <= maxW + 0.01f)
				continue;
			LL& l = lines[li];
			const PChar& ref = pr.Chars[(std::max)(l.First, l.First + l.Count - 1)];
			const float ew = (eg ? eg->Advance : f->BasePx * 0.5f) * sizeOf(ref, size) / f->BasePx;
			float w = 0.0f;
			int k = l.First;
			while (k < l.First + l.Count && w + adv[k] + ew <= maxW + 0.01f)
				w += adv[k++];
			while (k > l.First && pr.Chars[k - 1].C == ' ')
				w -= adv[--k];
			ellipsisAt[li] = k;
			l.Width = w + ew;
		}
	}

	// ---- 글자 배치 → 사각형 ----
	float drawH = 0.0f;
	for (size_t li = 0; li < lineCount; ++li)
	{
		drawH += (lines[li].Asc - lines[li].Desc) * m_LineSpacing;
		if (lines[li].Paragraph && li + 1 < lineCount) drawH += ParagraphSpacing * size / 100.0f;
	}
	const int hAlign = m_Alignment % 3, vAlign = m_Alignment / 3;
	float y = top;
	if (vAlign == 1) y = (top + bottom) * 0.5f + drawH * 0.5f;
	else if (vAlign == 2) y = bottom + drawH;

	m_Chars.resize(pr.Chars.size());
	for (size_t i = 0; i < pr.Chars.size(); ++i)
	{
		m_Chars[i].Char = pr.Chars[i].C;
		m_Chars[i].Source = pr.Chars[i].Source;
		m_Chars[i].Link = pr.Chars[i].Link;
		m_Chars[i].Line = -1;
		m_Chars[i].Size = sizeOf(pr.Chars[i], size);
		m_Chars[i].Advance = adv[i];
	}

	for (size_t li = 0; li < lineCount; ++li)
	{
		const LL& l = lines[li];
		const float lineH = (l.Asc - l.Desc) * m_LineSpacing;
		const float lineTop = y;
		const float baseline = lineTop - l.Asc;   // 줄 간격은 아래쪽에 (Unity Text 와 같음)
		const int a = l.Align >= 0 ? l.Align : hAlign;
		float x = left;
		if (a == 1) x = (left + right) * 0.5f - l.Width * 0.5f;
		else if (a == 2) x = right - l.Width;
		LineInfo info;
		info.First = l.First;
		info.Count = l.Count;
		info.Top = lineTop;
		info.Height = lineH;
		info.Ascent = l.Asc;
		info.StartX = x;

		// 밑줄 · 취소선 · mark 는 같은 모양이 이어지는 구간마다
		auto addSolid = [&](float x0, float x1, float y0, float y1, uint32 color, bool hasColor, int fontIdx, int layer, int charIdx) {
			Quad q;
			q.P[0] = Vec2(x0, y0); q.P[1] = Vec2(x0, y1); q.P[2] = Vec2(x1, y1); q.P[3] = Vec2(x1, y0);
			const Vec2 uv(fonts[fontIdx]->SolidU, fonts[fontIdx]->SolidV);
			for (Vec2& u : q.UV) u = uv;
			q.TagColor = color;
			q.HasTagColor = hasColor;
			q.Texture = fontIdx;
			q.Layer = layer;
			q.Char = charIdx;
			m_Quads.push_back(q);
		};

		const int end = ellipsisAt[li] >= 0 ? ellipsisAt[li] : l.First + l.Count;
		for (int i = l.First; i < end; ++i)
		{
			const PChar& p = pr.Chars[i];
			CharInfo& ci = m_Chars[i];
			ci.Line = (int)li;
			ci.PenX = x;
			const float s = sizeOf(p, size);
			const float yOff = p.VOffset + ((p.Flags & FSup) ? size * p.Scale * 0.35f : 0.0f) - ((p.Flags & FSub) ? size * p.Scale * 0.15f : 0.0f);
			const float by = baseline + yOff;
			const int fontIdx = (p.Flags & FBold) ? 1 : 0;
			const UIFont::Font* f = fonts[fontIdx];
			const float k = s / f->BasePx;
			ci.BottomLeft = Vec2(x, by + f->Descent * k);
			ci.TopRight = Vec2(x + adv[i], by + f->Ascent * k);
			if (p.Mark != 0)
				addSolid(x, x + adv[i], baseline + l.Desc, baseline + l.Asc, p.Mark, true, fontIdx, 0, -1);
			if (p.Sprite >= 0)
			{
				Quad q;
				const float h = s * 1.0f, x0 = x + s * 0.05f;
				const float y0 = by + f->Descent * k * 0.5f;
				q.P[0] = Vec2(x0, y0); q.P[1] = Vec2(x0, y0 + h); q.P[2] = Vec2(x0 + h, y0 + h); q.P[3] = Vec2(x0 + h, y0);
				q.UV[0] = Vec2(0, 1); q.UV[1] = Vec2(0, 0); q.UV[2] = Vec2(1, 0); q.UV[3] = Vec2(1, 1);
				q.TagColor = 0xFFFFFFFF;
				q.HasTagColor = !p.HasColor;   // 스프라이트는 흰색 (태그 색이 있으면 그 색)
				if (p.HasColor) { q.TagColor = p.Color; q.HasTagColor = true; }
				q.Texture = 2 + p.Sprite;
				q.Char = i;
				m_Quads.push_back(q);
				ci.Visible = true;
				ci.BottomLeft = Vec2(x0, y0);
				ci.TopRight = Vec2(x0 + h, y0 + h);
			}
			else if (const UIFont::Glyph* g = f->Find(p.C); g && g->Visible && p.C != ' ' && p.C != '\t')
			{
				const float x0 = x + g->X0 * k, x1 = x + g->X1 * k;
				const float yTop = by - g->Y0 * k, yBot = by - g->Y1 * k;
				const bool italic = (p.Flags & FItalic) != 0;
				const float skewT = italic ? (yTop - by) * 0.2f : 0.0f, skewB = italic ? (yBot - by) * 0.2f : 0.0f;
				Quad q;
				q.P[0] = Vec2(x0 + skewB, yBot); q.P[1] = Vec2(x0 + skewT, yTop); q.P[2] = Vec2(x1 + skewT, yTop); q.P[3] = Vec2(x1 + skewB, yBot);
				const float u0 = (float)g->AtlasX / f->AtlasW, v0 = (float)g->AtlasY / f->AtlasH;
				const float u1 = (float)(g->AtlasX + g->W) / f->AtlasW, v1 = (float)(g->AtlasY + g->H) / f->AtlasH;
				q.UV[0] = Vec2(u0, v1); q.UV[1] = Vec2(u0, v0); q.UV[2] = Vec2(u1, v0); q.UV[3] = Vec2(u1, v1);
				q.TagColor = p.Color;
				q.HasTagColor = p.HasColor;
				q.Texture = fontIdx;
				q.Char = i;
				m_Quads.push_back(q);
				ci.Visible = true;
			}
			// 밑줄 · 취소선 (글자 폭 그대로, 다음 글자와 이어진다)
			if ((p.Flags & (FUnderline | FStrike)) && p.C != '\n')
			{
				const float t = (std::max)(1.0f, s * 0.06f);
				if (p.Flags & FUnderline)
					addSolid(x, x + adv[i], by - s * 0.12f - t, by - s * 0.12f, p.Color, p.HasColor, fontIdx, 1, i);
				if (p.Flags & FStrike)
					addSolid(x, x + adv[i], by + s * 0.28f, by + s * 0.28f + t, p.Color, p.HasColor, fontIdx, 1, i);
			}
			x += adv[i];
		}
		if (ellipsisAt[li] >= 0)
		{
			// … (마지막 글자 모양으로)
			const PChar& ref = pr.Chars[(std::max)(l.First, (std::min)(end, l.First + l.Count) - 1)];
			const UIFont::Font* f = fonts[0];
			if (const UIFont::Glyph* g = f->Find(U'…'); g && g->Visible)
			{
				const float k = sizeOf(ref, size) / f->BasePx;
				Quad q;
				const float x0 = x + g->X0 * k, x1 = x + g->X1 * k, yTop = baseline - g->Y0 * k, yBot = baseline - g->Y1 * k;
				q.P[0] = Vec2(x0, yBot); q.P[1] = Vec2(x0, yTop); q.P[2] = Vec2(x1, yTop); q.P[3] = Vec2(x1, yBot);
				const float u0 = (float)g->AtlasX / f->AtlasW, v0 = (float)g->AtlasY / f->AtlasH;
				const float u1 = (float)(g->AtlasX + g->W) / f->AtlasW, v1 = (float)(g->AtlasY + g->H) / f->AtlasH;
				q.UV[0] = Vec2(u0, v1); q.UV[1] = Vec2(u0, v0); q.UV[2] = Vec2(u1, v0); q.UV[3] = Vec2(u1, v1);
				q.TagColor = ref.Color;
				q.HasTagColor = ref.HasColor;
				q.Texture = 0;
				q.Char = end > l.First ? end - 1 : -1;
				m_Quads.push_back(q);
				x += g->Advance * k;
			}
		}
		info.EndX = x;
		m_Lines.push_back(info);
		y -= lineH;
		if (l.Paragraph) y -= ParagraphSpacing * size / 100.0f;
	}
	// mark 를 글자 뒤로
	std::stable_sort(m_Quads.begin(), m_Quads.end(), [](const Quad& a, const Quad& b) { return a.Layer < b.Layer; });
}

int Text::FindLinkAt(const Vec2& canvasWorld)
{
	ForceMeshUpdate();
	if (m_Links.empty() || m_pGameObject == nullptr)
		return -1;
	const Matrix inv = m_pGameObject->GetTransform()->GetWorldMatrix().Invert();
	const Vec3 local = Vec3::Transform(Vec3(canvasWorld.x, canvasWorld.y, 0.0f), inv);
	for (const CharInfo& c : m_Chars)
		if (c.Link >= 0 && c.Line >= 0 && local.x >= c.PenX && local.x <= c.PenX + (std::max)(c.Advance, 1.0f))
		{
			const LineInfo& l = m_Lines[c.Line];
			if (local.y <= l.Top && local.y >= l.Top - l.Height)
				return c.Link;
		}
	return -1;
}

void Text::SetCharacterOffset(int index, const Vec2& offset) { if (index >= 0) { m_ModsText = m_Text; m_Mods[index].Offset = offset; } }
void Text::SetCharacterScale(int index, float scale) { if (index >= 0) { m_ModsText = m_Text; m_Mods[index].Scale = scale; } }
void Text::SetCharacterColor(int index, const float rgba[4])
{
	if (index < 0 || rgba == nullptr)
		return;
	m_ModsText = m_Text;
	Mod& m = m_Mods[index];
	memcpy(m.Color, rgba, sizeof(m.Color));
	m.HasColor = true;
}

// ------------------------------------------------------------------ 그리기
void Text::Populate(UIRenderer& r, float canvasScale)
{
	RectTransform* rt = GetRect();
	const bool caret = m_CaretIndex >= 0;
	if (rt == nullptr || m_pGameObject == nullptr || (m_Text.empty() && !caret))
		return;
	m_ForScript = false;
	UpdateLayout();
	if (m_Atlas[0] == nullptr)
		return;

	const Matrix world = m_pGameObject->GetTransform()->GetWorldMatrix();
	float base[4];
	FinalColor(base);
	const uint32 baseColor = UIRenderer::PackColor(base);
	UIRenderer::TextMaterial mats[2] = { Material, Material };
	for (int f = 0; f < 2; ++f)
	{
		mats[f].SpreadU = m_Spread[f][0];
		mats[f].SpreadV = m_Spread[f][1];
		if (!Underlay)
			mats[f].UnderlayColor[3] = 0.0f;
	}
	const int maxVisible = MaxVisibleCharacters < 0 ? INT_MAX : MaxVisibleCharacters;
	for (const Quad& q : m_Quads)
	{
		if (q.Char >= maxVisible)
			continue;
		// 색: 태그 색이면 그 색 (Button 색 전환 · 컴포넌트 알파는 곱한다), 아니면 컴포넌트 색
		uint32 color = baseColor;
		if (q.HasTagColor)
		{
			const float c[4] = { (q.TagColor & 255) / 255.0f * m_Tint[0], ((q.TagColor >> 8) & 255) / 255.0f * m_Tint[1],
				((q.TagColor >> 16) & 255) / 255.0f * m_Tint[2], ((q.TagColor >> 24) & 255) / 255.0f * base[3] };
			color = UIRenderer::PackColor(c);
		}
		Vec2 p[4] = { q.P[0], q.P[1], q.P[2], q.P[3] };
		if (q.Char >= 0)
			if (auto it = m_Mods.find(q.Char); it != m_Mods.end())
			{
				const Mod& m = it->second;
				const Vec2 c = (q.P[0] + q.P[2]) * 0.5f;
				for (Vec2& v : p)
					v = c + (v - c) * m.Scale + m.Offset;
				if (m.HasColor)
				{
					const float mc[4] = { m.Color[0] * m_Tint[0], m.Color[1] * m_Tint[1], m.Color[2] * m_Tint[2], m.Color[3] * base[3] };
					color = UIRenderer::PackColor(mc);
				}
			}
		const Vec3 w[4] = { ToWorld(world, p[0].x, p[0].y), ToWorld(world, p[1].x, p[1].y), ToWorld(world, p[2].x, p[2].y), ToWorld(world, p[3].x, p[3].y) };
		if (q.Texture >= 2)
		{
			UISprites::Info info;
			const std::string path = SpritePath(m_Sprites[q.Texture - 2]);
			if (!path.empty() && UISprites::Get(path, info))
				r.AddQuad(w, q.UV, color, info.Texture);
			continue;
		}
		const uint32 colors[4] = { color, color, color, color };
		r.AddTextQuad(w, q.UV, colors, m_Atlas[q.Texture], mats[q.Texture]);
	}

	// 입력 커서 (InputField): 원문 순번 = 글자 순번 (입력 글자는 태그로 읽지 않는다)
	if (caret)
	{
		float cx = 0.0f, lineTop = 0.0f, lineH = 0.0f, asc = 0.0f;
		bool found = false;
		for (const CharInfo& c : m_Chars)
			if (c.Source == m_CaretIndex && c.Line >= 0)
			{
				cx = c.PenX;
				lineTop = m_Lines[c.Line].Top;
				lineH = m_Lines[c.Line].Height;
				asc = m_Lines[c.Line].Ascent;
				found = true;
				break;
			}
		if (!found && !m_Lines.empty())
		{
			// 끝 (또는 빈 글): 마지막 줄 끝 — 줄바꿈으로 끝나면 다음 줄 처음
			const LineInfo& l = m_Lines.back();
			cx = l.EndX;
			lineTop = l.Top;
			lineH = l.Height;
			asc = l.Ascent;
			if (!m_Chars.empty() && m_Chars.back().Char == '\n')
			{
				cx = l.StartX;
				lineTop = l.Top - l.Height;
			}
		}
		const float h = lineH / (std::max)(0.01f, m_LineSpacing);
		m_CaretWorld = ToWorld(world, cx, lineTop - h);
		m_CaretHeight = h;
		(void)asc;
		if (m_CaretVisible)
		{
			const float w = (std::max)(1.0f, 1.5f / (std::max)(0.05f, canvasScale));
			const Vec3 p[4] = { ToWorld(world, cx, lineTop - h), ToWorld(world, cx, lineTop), ToWorld(world, cx + w, lineTop), ToWorld(world, cx + w, lineTop - h) };
			const Vec2 uv[4] = { Vec2(0, 0), Vec2(0, 0), Vec2(0, 0), Vec2(0, 0) };
			r.AddQuad(p, uv, UIRenderer::PackColor(m_CaretColor), r.WhiteTexture());
		}
	}
}

void Text::SetCaret(int index, bool visible, const float color[4])
{
	m_CaretIndex = index;
	m_CaretVisible = visible;
	if (color)
		memcpy(m_CaretColor, color, sizeof(m_CaretColor));
}

// ------------------------------------------------------------------ Inspector
void Text::OnInspectorGUI()
{
	UnityGUI::TextArea("Text", &m_Text, 60.0f);

	if (UnityGUI::FoldoutPlain("Character", 0, true))
	{
		// Font [ 이름 ⊙ ]
		const std::string fontText = m_Font.empty() ? UIFont::DefaultFontName() : std::filesystem::path(m_Font).stem().string();
		ImVec2 fmin, fmax;
		const int pressed = UnityGUI::ObjectFieldButtons("Font", fontText.c_str(), "font", nullptr, 0, &fmin, &fmax, 1);
		const ImVec2 after = ImGui::GetCursorScreenPos();
		const std::string key = "uitext:" + std::to_string((uintptr_t)this);
		if (pressed == -1)
		{
			ObjectPicker::Options opt;
			opt.TypeName = "Font";
			opt.Icon = "font";
			opt.Items = UIFont::FindAll();
			opt.Current = m_Font;
			ObjectPicker::Open(key, std::move(opt));
		}
		std::string picked;
		if (ObjectPicker::Poll(key, picked))
			m_Font = picked;   // None = 기본 글꼴
		ImGui::SetCursorScreenPos(fmin);
		ImGui::InvisibleButton("##fontDrop", ImVec2((std::max)(1.0f, fmax.x - fmin.x - 22.0f), fmax.y - fmin.y));
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
			{
				const std::string dropped(static_cast<const char*>(payload->Data));
				if (UIFont::IsFontPath(dropped))
					m_Font = dropped;
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::SetCursorScreenPos(after);

		// Font Style: [B] [I] [U] [S] [ab] [AB] (TMP 와 같은 토글 줄)
		{
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float w = ImGui::GetContentRegionAvail().x;
			ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + UnityGUI::kBaseIndent + UnityGUI::kNestIndent, p.y + 2.0f), IM_COL32(196, 196, 196, 255), "Font Style");
			float x = FieldX(p.x, w);
			const char* labels[6] = { "B", "I", "U", "S", "ab", "AB" };
			const char* tips[6] = { "Bold", "Italic", "Underline", "Strikethrough", "Lowercase", "Uppercase" };
			const int bits[6] = { StyleBold, StyleItalic, StyleUnderline, StyleStrikethrough, StyleLowerCase, StyleUpperCase };
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 1));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
			for (int i = 0; i < 6; ++i)
			{
				const bool on = (m_StyleBits & bits[i]) != 0;
				ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.27f, 0.38f, 0.5f, 1.0f) : ImVec4(0.32f, 0.32f, 0.32f, 1.0f));
				ImGui::SetCursorScreenPos(ImVec2(x, p.y));
				ImGui::PushID(100 + i);
				if (ImGui::Button(labels[i], ImVec2(26.0f, 18.0f)))
				{
					m_StyleBits ^= bits[i];
					if (bits[i] == StyleLowerCase && (m_StyleBits & StyleLowerCase)) m_StyleBits &= ~StyleUpperCase;
					if (bits[i] == StyleUpperCase && (m_StyleBits & StyleUpperCase)) m_StyleBits &= ~StyleLowerCase;
				}
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
					ImGui::SetTooltip("%s", tips[i]);
				ImGui::PopID();
				ImGui::PopStyleColor();
				x += 27.0f;
			}
			ImGui::PopStyleVar(2);
			ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + UnityGUI::kRowStep));
			ImGui::Dummy(ImVec2(w, 0));
		}
		if (UnityGUI::Int("Font Size", &m_FontSize, 1))
			m_FontSize = std::clamp(m_FontSize, 1, 300);
		UnityGUI::Toggle("Auto Size", &m_BestFit, 1);
		if (m_BestFit)
		{
			UnityGUI::Int("Min Size", &m_MinSize, 2);
			UnityGUI::Int("Max Size", &m_MaxSize, 2);
			m_MinSize = std::clamp(m_MinSize, 1, 300);
			m_MaxSize = std::clamp(m_MaxSize, m_MinSize, 300);
			char buf[32];
			snprintf(buf, sizeof(buf), "%d", m_UsedSize);
			UnityGUI::ValueLabel("Used Size", buf, 2);
		}
		UnityGUI::Toggle("Rich Text", &m_RichText, 1);
	}

	if (UnityGUI::FoldoutPlain("Spacing", 0, true))
	{
		UnityGUI::Float("Character", &CharacterSpacing, 1);
		UnityGUI::Float("Word", &WordSpacing, 1);
		UnityGUI::Float("Line", &m_LineSpacing, 1);
		UnityGUI::Float("Paragraph", &ParagraphSpacing, 1);
	}

	if (UnityGUI::FoldoutPlain("Paragraph", 0, true))
	{
		// Alignment: [왼][가운데][오른]  [위][가운데][아래]
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + UnityGUI::kBaseIndent + UnityGUI::kNestIndent, p.y + 2.0f), IM_COL32(196, 196, 196, 255), "Alignment");
		float x = FieldX(p.x, w);
		const char* hIcons[3] = { ICON_FA_ALIGN_LEFT, ICON_FA_ALIGN_CENTER, ICON_FA_ALIGN_RIGHT };
		const char* vIcons[3] = { ICON_FA_ARROW_UP, ICON_FA_MINUS, ICON_FA_ARROW_DOWN };
		const char* hTips[3] = { "Left", "Center", "Right" };
		const char* vTips[3] = { "Top", "Middle", "Bottom" };
		int h = m_Alignment % 3, v = m_Alignment / 3;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 1));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
		for (int group = 0; group < 2; ++group)
		{
			for (int i = 0; i < 3; ++i)
			{
				const bool on = group == 0 ? h == i : v == i;
				ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.27f, 0.38f, 0.5f, 1.0f) : ImVec4(0.32f, 0.32f, 0.32f, 1.0f));
				ImGui::SetCursorScreenPos(ImVec2(x, p.y));
				ImGui::PushID(group * 3 + i);
				if (ImGui::Button(group == 0 ? hIcons[i] : vIcons[i], ImVec2(26.0f, 18.0f)))
				{
					if (group == 0) h = i; else v = i;
					m_Alignment = v * 3 + h;
				}
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
					ImGui::SetTooltip("%s", group == 0 ? hTips[i] : vTips[i]);
				ImGui::PopID();
				ImGui::PopStyleColor();
				x += 27.0f;
			}
			x += 10.0f;
		}
		ImGui::PopStyleVar(2);
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + UnityGUI::kRowStep));
		ImGui::Dummy(ImVec2(w, 0));

		static const char* kWrap[] = { "Enabled", "Disabled" };
		static const char* kV[] = { "Truncate", "Overflow", "Ellipsis" };
		int ho = (int)m_HOverflow, vo = (int)m_VOverflow;
		if (UnityGUI::Dropdown("Wrapping", &ho, kWrap, 2, 1)) m_HOverflow = (HOverflow)ho;
		if (UnityGUI::Dropdown("Overflow", &vo, kV, 3, 1)) m_VOverflow = (VOverflow)vo;
		float margin[4] = { Margin.x, Margin.y, Margin.z, Margin.w };
		UnityGUI::Label("Margins", 1);
		if (UnityGUI::Float("Left", &margin[0], 2) | UnityGUI::Float("Top", &margin[1], 2) | UnityGUI::Float("Right", &margin[2], 2) | UnityGUI::Float("Bottom", &margin[3], 2))
			Margin = Vec4(margin[0], margin[1], margin[2], margin[3]);
		if (UnityGUI::Int("Max Visible Characters", &MaxVisibleCharacters, 1))
			MaxVisibleCharacters = (std::max)(-1, MaxVisibleCharacters);
	}

	// TMP 의 머티리얼 (Face · Outline · Underlay)
	if (UnityGUI::FoldoutPlain("Material", 0, false))
	{
		UnityGUI::Slider("Face Dilate", &Material.FaceDilate, -1.0f, 1.0f, 1);
		UnityGUI::Slider("Softness", &Material.Softness, 0.0f, 1.0f, 1);
		UnityGUI::Label("Outline", 1, true);
		UnityGUI::Color("Color", Material.OutlineColor, 2);
		UnityGUI::Slider("Thickness", &Material.OutlineWidth, 0.0f, 1.0f, 2);
		UnityGUI::Toggle("Underlay", &Underlay, 1);
		if (Underlay)
		{
			UnityGUI::Color("Color##underlay", Material.UnderlayColor, 2);
			UnityGUI::Slider("Offset X", &Material.UnderlayOffsetX, -1.0f, 1.0f, 2);
			UnityGUI::Slider("Offset Y", &Material.UnderlayOffsetY, -1.0f, 1.0f, 2);
			UnityGUI::Slider("Dilate", &Material.UnderlayDilate, -1.0f, 1.0f, 2);
			UnityGUI::Slider("Softness##underlay", &Material.UnderlaySoftness, 0.0f, 1.0f, 2);
		}
	}

	DrawColorAndRaycast();
	if (!m_Links.empty() || !OnLinkClicked.Calls.empty())
		OnLinkClicked.Draw("On Link Clicked (String)", "string");
}

// ------------------------------------------------------------------ 저장
GENERATE_COMPONENT_FUNC_TOJSON(Text)
{
	json j;
	SERIALIZE_TYPE(j, Text);
	GraphicToJson(j);
	j["text"] = m_Text;
	j["font"] = m_Font;
	j["fontStyle"] = m_StyleBits;
	j["fontSize"] = m_FontSize;
	j["lineSpacing"] = m_LineSpacing;
	j["richText"] = m_RichText;
	j["alignment"] = m_Alignment;
	j["horizontalOverflow"] = (int)m_HOverflow;
	j["verticalOverflow"] = (int)m_VOverflow;
	j["bestFit"] = m_BestFit;
	j["minSize"] = m_MinSize;
	j["maxSize"] = m_MaxSize;
	// TMP 기능 (기본값이면 저장하지 않는다 — 예전 씬과 같은 JSON)
	if (CharacterSpacing != 0.0f) j["characterSpacing"] = CharacterSpacing;
	if (WordSpacing != 0.0f) j["wordSpacing"] = WordSpacing;
	if (ParagraphSpacing != 0.0f) j["paragraphSpacing"] = ParagraphSpacing;
	if (Margin != Vec4(0, 0, 0, 0)) j["margin"] = { Margin.x, Margin.y, Margin.z, Margin.w };
	if (MaxVisibleCharacters >= 0) j["maxVisibleCharacters"] = MaxVisibleCharacters;
	const UIRenderer::TextMaterial d;
	if (Material.FaceDilate != d.FaceDilate || Material.Softness != d.Softness || Material.OutlineWidth != d.OutlineWidth || Underlay)
	{
		const UIRenderer::TextMaterial& m = Material;
		j["material"] = { { "faceDilate", m.FaceDilate }, { "softness", m.Softness }, { "outlineWidth", m.OutlineWidth },
			{ "outlineColor", { m.OutlineColor[0], m.OutlineColor[1], m.OutlineColor[2], m.OutlineColor[3] } },
			{ "underlay", Underlay }, { "underlayColor", { m.UnderlayColor[0], m.UnderlayColor[1], m.UnderlayColor[2], m.UnderlayColor[3] } },
			{ "underlayOffset", { m.UnderlayOffsetX, m.UnderlayOffsetY } }, { "underlayDilate", m.UnderlayDilate }, { "underlaySoftness", m.UnderlaySoftness } };
	}
	if (!OnLinkClicked.Calls.empty())
		j["onLinkClicked"] = OnLinkClicked.ToJson();
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Text)
{
	GraphicFromJson(j);
	m_Text = j.value("text", std::string());
	m_Font = j.value("font", std::string());
	m_StyleBits = j.value("fontStyle", 0) & 63;
	m_FontSize = j.value("fontSize", 14);
	m_LineSpacing = j.value("lineSpacing", 1.0f);
	m_RichText = j.value("richText", true);
	m_Alignment = j.value("alignment", 0);
	m_HOverflow = (HOverflow)std::clamp(j.value("horizontalOverflow", 0), 0, 1);
	m_VOverflow = (VOverflow)std::clamp(j.value("verticalOverflow", 1), 0, 2);
	m_BestFit = j.value("bestFit", false);
	m_MinSize = j.value("minSize", 10);
	m_MaxSize = j.value("maxSize", 40);
	CharacterSpacing = j.value("characterSpacing", 0.0f);
	WordSpacing = j.value("wordSpacing", 0.0f);
	ParagraphSpacing = j.value("paragraphSpacing", 0.0f);
	Margin = Vec4(0, 0, 0, 0);
	if (j.contains("margin") && j["margin"].is_array() && j["margin"].size() == 4)
		Margin = Vec4(j["margin"][0].get<float>(), j["margin"][1].get<float>(), j["margin"][2].get<float>(), j["margin"][3].get<float>());
	MaxVisibleCharacters = j.value("maxVisibleCharacters", -1);
	Material = UIRenderer::TextMaterial();
	Underlay = false;
	if (j.contains("material") && j["material"].is_object())
	{
		const json& m = j["material"];
		Material.FaceDilate = m.value("faceDilate", 0.0f);
		Material.Softness = m.value("softness", 0.0f);
		Material.OutlineWidth = m.value("outlineWidth", 0.0f);
		auto col = [&](const char* key, float* out) {
			if (m.contains(key) && m[key].is_array() && m[key].size() == 4)
				for (int i = 0; i < 4; ++i) out[i] = m[key][i].get<float>();
		};
		col("outlineColor", Material.OutlineColor);
		col("underlayColor", Material.UnderlayColor);
		Underlay = m.value("underlay", false);
		if (m.contains("underlayOffset") && m["underlayOffset"].is_array() && m["underlayOffset"].size() == 2)
		{
			Material.UnderlayOffsetX = m["underlayOffset"][0].get<float>();
			Material.UnderlayOffsetY = m["underlayOffset"][1].get<float>();
		}
		Material.UnderlayDilate = m.value("underlayDilate", 0.0f);
		Material.UnderlaySoftness = m.value("underlaySoftness", 0.0f);
	}
	OnLinkClicked = UIEventList();
	if (j.contains("onLinkClicked"))
		OnLinkClicked.FromJson(j["onLinkClicked"]);
	m_LayoutKey = 0;
}
