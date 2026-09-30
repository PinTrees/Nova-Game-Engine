#pragma once
#include "UIGraphic.h"

// Unity 의 Text (UI). 글꼴은 기본 Pretendard(한글 포함) 또는 Assets 의 .ttf/.otf.
//  - 글자는 화면에 보이는 픽셀 크기로 구워서 그린다 (Canvas Scaler 배율 반영 → 선명)
//  - 줄바꿈(Wrap: 단어 단위, 한글/한자는 글자 단위), 정렬 3x3, 줄 간격, 넘침(Overflow/Truncate), Best Fit, Bold/Italic
// C# 에서는 NovaEngine.UI.Text (TMPro.TextMeshProUGUI 로도 쓸 수 있음).
class Text : public UIGraphic
{
public:
	enum class Style { Normal = 0, Bold = 1, Italic = 2, BoldAndItalic = 3 };
	enum class HOverflow { Wrap = 0, Overflow = 1 };
	enum class VOverflow { Truncate = 0, Overflow = 1 };

	Text();

	const std::string& GetText() const { return m_Text; }
	void SetText(const std::string& text) { m_Text = text; }
	int GetFontSize() const { return m_FontSize; }
	void SetFontSize(int size) { m_FontSize = std::clamp(size, 1, 300); }
	const std::string& GetFont() const { return m_Font; }
	void SetFont(const std::string& font) { m_Font = font; }
	int GetAlignment() const { return m_Alignment; }   // 0 UpperLeft .. 8 LowerRight
	void SetAlignment(int a) { m_Alignment = std::clamp(a, 0, 8); }
	float GetLineSpacing() const { return m_LineSpacing; }
	void SetLineSpacing(float v) { m_LineSpacing = v; }
	Style GetStyle() const { return m_Style; }
	void SetStyle(Style s) { m_Style = s; }
	void SetOverflow(HOverflow h, VOverflow v) { m_HOverflow = h; m_VOverflow = v; }

	// InputField 의 입력 커서: 글자 순번(유니코드 문자 기준) 앞에 세로 막대. index < 0 이면 없음
	void SetCaret(int index, bool visible, const float color[4]);
	// 마지막으로 그린 커서 위치 (캔버스 월드, 커서 아래쪽) / 높이 — 한글 IME 조합 창 위치용
	Vec3 GetCaretWorld() const { return m_CaretWorld; }
	float GetCaretHeight() const { return m_CaretHeight; }

	virtual void Populate(UIRenderer& renderer, float canvasScale) override;

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "ui_text"; }

	GENERATE_COMPONENT_BODY(Text)

private:
	std::string m_Text = "New Text";
	std::string m_Font;   // 빈 문자열 = 기본 글꼴
	Style m_Style = Style::Normal;
	int m_FontSize = 14;
	float m_LineSpacing = 1.0f;
	bool m_RichText = true;
	int m_Alignment = 0;
	HOverflow m_HOverflow = HOverflow::Wrap;
	VOverflow m_VOverflow = VOverflow::Truncate;
	bool m_BestFit = false;
	int m_MinSize = 10, m_MaxSize = 40;
	bool m_CharacterOpen = true, m_ParagraphOpen = true;

	// 실행 중 (저장하지 않음)
	int m_CaretIndex = -1;
	bool m_CaretVisible = false;
	float m_CaretColor[4] = { 0.196f, 0.196f, 0.196f, 1.0f };
	Vec3 m_CaretWorld = Vec3(0, 0, 0);
	float m_CaretHeight = 0.0f;
};
REGISTER_COMPONENT(Text)
