#include "pch.h"
#include "UIText.h"
#include "UIFont.h"
#include "UIRenderer.h"
#include "RectTransform.h"
#include "UnityGUI.h"
#include "ObjectPicker.h"

namespace
{
	struct Line
	{
		size_t Begin = 0, End = 0;   // [Begin, End) 글자 위치
		float Width = 0.0f;          // 끝 공백 제외 폭
	};

	// 한자/가나는 글자 사이에서 줄을 바꿀 수 있다 (한글은 띄어쓰기 기준)
	bool IsCjkBreakable(char32_t c)
	{
		return (c >= 0x3040 && c <= 0x30FF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF);
	}

	float Advance(const UIFont::Atlas& a, char32_t c, float unit)
	{
		if (c == '\t')
			return Advance(a, ' ', unit) * 4.0f;
		const ImFontGlyph* g = a.Glyph((unsigned int)c);
		return g ? g->AdvanceX * unit : 0.0f;
	}

	float TrimmedWidth(const std::u32string& s, size_t b, size_t e, const UIFont::Atlas& a, float unit)
	{
		while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t'))
			--e;
		float w = 0.0f;
		for (size_t i = b; i < e; ++i)
			w += Advance(a, s[i], unit);
		return w;
	}

	void BreakLines(const std::u32string& s, const UIFont::Atlas& a, float unit, float maxWidth, bool wrap, std::vector<Line>& out)
	{
		out.clear();
		size_t lineStart = 0, lastBreak = std::string::npos;
		float width = 0.0f;
		for (size_t i = 0; i < s.size(); ++i)
		{
			const char32_t c = s[i];
			if (c == '\n')
			{
				out.push_back({ lineStart, i, TrimmedWidth(s, lineStart, i, a, unit) });
				lineStart = i + 1;
				width = 0.0f;
				lastBreak = std::string::npos;
				continue;
			}
			if (c == '\r')
				continue;
			const float adv = Advance(a, c, unit);
			if (wrap && c != ' ' && width + adv > maxWidth + 0.01f && i > lineStart)
			{
				size_t cut = (lastBreak != std::string::npos && lastBreak > lineStart) ? lastBreak : i;
				out.push_back({ lineStart, cut, TrimmedWidth(s, lineStart, cut, a, unit) });
				lineStart = cut;
				while (lineStart < s.size() && s[lineStart] == ' ')   // 줄 앞 공백은 버린다
					++lineStart;
				width = 0.0f;
				for (size_t k = lineStart; k < i; ++k)
					width += Advance(a, s[k], unit);
				lastBreak = std::string::npos;
			}
			if (IsCjkBreakable(c) && i > lineStart)
				lastBreak = i;
			width += adv;
			if (c == ' ')
				lastBreak = i + 1;
		}
		out.push_back({ lineStart, s.size(), TrimmedWidth(s, lineStart, s.size(), a, unit) });
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

// ------------------------------------------------------------------ 그리기
void Text::Populate(UIRenderer& r, float canvasScale)
{
	RectTransform* rt = GetRect();
	const bool caret = m_CaretIndex >= 0;
	if (rt == nullptr || m_pGameObject == nullptr || (m_Text.empty() && !caret))
		return;
	const std::u32string text = UIFont::DecodeUtf8(m_Text);
	const bool bold = m_Style == Style::Bold || m_Style == Style::BoldAndItalic;
	const bool italic = m_Style == Style::Italic || m_Style == Style::BoldAndItalic;
	const Vec2 rmin = rt->GetRectMin(), rsize = rt->GetRectSize();
	const bool wrap = m_HOverflow == HOverflow::Wrap;

	// Best Fit: 넘치지 않는 가장 큰 크기 (32px 아틀라스로 재고 비례로 판단)
	int fontSize = m_FontSize;
	std::vector<Line> lines;
	if (m_BestFit)
	{
		const UIFont::Atlas* probe = UIFont::Get(m_Font, bold, 32, text);
		if (probe)
		{
			fontSize = (std::max)(1, m_MinSize);
			for (int size = (std::max)(m_MaxSize, m_MinSize); size >= m_MinSize; --size)
			{
				const float unit = (float)size / 32.0f;
				BreakLines(text, *probe, unit, rsize.x, wrap, lines);
				float widest = 0.0f;
				for (const Line& l : lines) widest = (std::max)(widest, l.Width);
				const float totalH = lines.size() * size * m_LineSpacing;
				if (totalH <= rsize.y + 0.01f && (wrap || widest <= rsize.x + 0.01f))
				{
					fontSize = size;
					break;
				}
			}
		}
	}

	const int px = (std::max)(4, (int)lroundf(fontSize * (std::max)(0.05f, canvasScale)));
	const UIFont::Atlas* atlas = UIFont::Get(m_Font, bold, px, text);
	if (atlas == nullptr)
		return;
	const float unit = (float)fontSize / (float)px;   // 아틀라스 1 픽셀 = 캔버스 단위
	BreakLines(text, *atlas, unit, rsize.x, wrap, lines);
	const float lineH = (atlas->Ascent - atlas->Descent) * unit * m_LineSpacing;

	size_t lineCount = lines.size();
	if (m_VOverflow == VOverflow::Truncate)
		lineCount = (std::min)(lineCount, (size_t)floorf((rsize.y + 0.01f) / (std::max)(1e-3f, lineH)));
	if (lineCount == 0)
		return;
	const float totalH = lineCount * lineH;

	const int h = m_Alignment % 3, v = m_Alignment / 3;   // 0 왼/위, 1 가운데, 2 오른/아래
	const float rectTop = rmin.y + rsize.y;
	float top = rectTop;
	if (v == 1) top = rmin.y + rsize.y * 0.5f + totalH * 0.5f;
	else if (v == 2) top = rmin.y + totalH;

	const Matrix world = m_pGameObject->GetTransform()->GetWorldMatrix();
	const uint32 color = PackedColor();
	const size_t caretIndex = caret ? (std::min)((size_t)m_CaretIndex, text.size()) : std::string::npos;
	bool caretPlaced = false;
	auto placeCaret = [&](float cx, float lineTop) {
		caretPlaced = true;
		m_CaretWorld = ToWorld(world, cx, lineTop - lineH / (std::max)(0.01f, m_LineSpacing));
		m_CaretHeight = lineH / (std::max)(0.01f, m_LineSpacing);
		if (!m_CaretVisible)
			return;
		// 1 화면 픽셀 남짓한 세로 막대
		const float w = (std::max)(1.0f, 1.5f / (std::max)(0.05f, canvasScale));
		const float yTop = lineTop, yBot = lineTop - m_CaretHeight;
		const Vec3 p[4] = { ToWorld(world, cx, yBot), ToWorld(world, cx, yTop), ToWorld(world, cx + w, yTop), ToWorld(world, cx + w, yBot) };
		const Vec2 uv[4] = { Vec2(0, 0), Vec2(0, 0), Vec2(0, 0), Vec2(0, 0) };
		r.AddQuad(p, uv, UIRenderer::PackColor(m_CaretColor), r.WhiteTexture());
	};
	for (size_t li = 0; li < lineCount; ++li)
	{
		const Line& line = lines[li];
		float x = rmin.x;
		if (h == 1) x = rmin.x + (rsize.x - line.Width) * 0.5f;
		else if (h == 2) x = rmin.x + rsize.x - line.Width;
		const float lineTop = top - li * lineH;
		const float baseline = lineTop - atlas->Ascent * unit;
		for (size_t i = line.Begin; i < line.End; ++i)
		{
			const char32_t c = text[i];
			if (i == caretIndex && !caretPlaced)
				placeCaret(x, lineTop);
			if (c == '\r')
				continue;
			const ImFontGlyph* g = atlas->Glyph((unsigned int)c);
			if (g == nullptr)
				continue;
			if (c != ' ' && c != '\t' && g->Visible)
			{
				const float x0 = x + g->X0 * unit, x1 = x + g->X1 * unit;
				const float yTop = lineTop - g->Y0 * unit, yBot = lineTop - g->Y1 * unit;
				const float skewT = italic ? (yTop - baseline) * 0.2f : 0.0f;
				const float skewB = italic ? (yBot - baseline) * 0.2f : 0.0f;
				const Vec3 p[4] = { ToWorld(world, x0 + skewB, yBot), ToWorld(world, x0 + skewT, yTop), ToWorld(world, x1 + skewT, yTop), ToWorld(world, x1 + skewB, yBot) };
				const Vec2 uv[4] = { Vec2(g->U0, g->V1), Vec2(g->U0, g->V0), Vec2(g->U1, g->V0), Vec2(g->U1, g->V1) };
				r.AddQuad(p, uv, color, atlas->Texture);
			}
			x += Advance(*atlas, c, unit);
		}
		// 줄 끝의 커서 (다음 줄이 이어지는 자리면 다음 줄 처음에 둔다)
		const bool lastLine = li + 1 == lineCount;
		const bool breakAfter = !lastLine && lines[li + 1].Begin > line.End;   // 줄바꿈 문자 / 공백으로 끝남
		if (!caretPlaced && caretIndex != std::string::npos && caretIndex == line.End && (lastLine || breakAfter))
			placeCaret(x, lineTop);
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

		static const char* kStyles[] = { "Normal", "Bold", "Italic", "Bold And Italic" };
		int style = (int)m_Style;
		if (UnityGUI::Dropdown("Font Style", &style, kStyles, 4, 1))
			m_Style = (Style)style;
		if (UnityGUI::Int("Font Size", &m_FontSize, 1))
			m_FontSize = std::clamp(m_FontSize, 1, 300);
		UnityGUI::Float("Line Spacing", &m_LineSpacing, 1);
		UnityGUI::Toggle("Rich Text", &m_RichText, 1);
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

		static const char* kH[] = { "Wrap", "Overflow" };
		static const char* kV[] = { "Truncate", "Overflow" };
		int ho = (int)m_HOverflow, vo = (int)m_VOverflow;
		if (UnityGUI::Dropdown("Horizontal Overflow", &ho, kH, 2, 1)) m_HOverflow = (HOverflow)ho;
		if (UnityGUI::Dropdown("Vertical Overflow", &vo, kV, 2, 1)) m_VOverflow = (VOverflow)vo;
		UnityGUI::Toggle("Best Fit", &m_BestFit, 1);
		if (m_BestFit)
		{
			UnityGUI::Int("Min Size", &m_MinSize, 2);
			UnityGUI::Int("Max Size", &m_MaxSize, 2);
			m_MinSize = std::clamp(m_MinSize, 1, 300);
			m_MaxSize = std::clamp(m_MaxSize, m_MinSize, 300);
		}
	}

	DrawColorAndRaycast();
}

// ------------------------------------------------------------------ 저장
GENERATE_COMPONENT_FUNC_TOJSON(Text)
{
	json j;
	SERIALIZE_TYPE(j, Text);
	GraphicToJson(j);
	j["text"] = m_Text;
	j["font"] = m_Font;
	j["fontStyle"] = (int)m_Style;
	j["fontSize"] = m_FontSize;
	j["lineSpacing"] = m_LineSpacing;
	j["richText"] = m_RichText;
	j["alignment"] = m_Alignment;
	j["horizontalOverflow"] = (int)m_HOverflow;
	j["verticalOverflow"] = (int)m_VOverflow;
	j["bestFit"] = m_BestFit;
	j["minSize"] = m_MinSize;
	j["maxSize"] = m_MaxSize;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Text)
{
	GraphicFromJson(j);
	m_Text = j.value("text", std::string());
	m_Font = j.value("font", std::string());
	m_Style = (Style)j.value("fontStyle", 0);
	m_FontSize = j.value("fontSize", 14);
	m_LineSpacing = j.value("lineSpacing", 1.0f);
	m_RichText = j.value("richText", true);
	m_Alignment = j.value("alignment", 0);
	m_HOverflow = (HOverflow)j.value("horizontalOverflow", 0);
	m_VOverflow = (VOverflow)j.value("verticalOverflow", 1);
	m_BestFit = j.value("bestFit", false);
	m_MinSize = j.value("minSize", 10);
	m_MaxSize = j.value("maxSize", 40);
}
