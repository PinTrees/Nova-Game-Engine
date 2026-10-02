#pragma once
#include "UIGraphic.h"
#include "UIEventList.h"
#include "UIRenderer.h"

// Unity 의 Text — TextMeshPro 기능을 합친 하나의 텍스트 컴포넌트 (TMP 를 따로 두지 않는다. C# TMPro.TextMeshProUGUI 도 이것).
//  - SDF 글꼴 (UIFont): 어떤 크기 · 배율 · 회전에서도 선명, Outline · Underlay(그림자) · Face Dilate · Softness
//  - Rich Text (TMP 태그): <b> <i> <u> <s> <color=#RRGGBBAA|이름> <alpha=#AA> <size=20|150%|+4> <mark=#..> <sup> <sub>
//    <link="id"> <sprite name="아이콘"> <cspace=2> <voffset=4> <align=center> <lowercase> <uppercase> <noparse> <br>
//  - 글자 · 단어 · 줄 · 문단 간격, 여백(Margins), Auto Size, 줄바꿈(단어 / 한자·가나는 글자), 정렬 3x3,
//    넘침 Overflow / Truncate / Ellipsis(…), maxVisibleCharacters (타자기 효과)
//  - 결과 정보 (textInfo: 글자 · 줄 · 링크 사각형), <link> 클릭 → On Link Clicked (string), 글자별 위치 · 색 · 크기 바꾸기 (스크립트 애니메이션)
class Text : public UIGraphic
{
public:
	// Font Style 비트 (TMP FontStyles). 예전 값 0~3 (Normal / Bold / Italic / Bold And Italic) 과 같다
	enum StyleBits { StyleBold = 1, StyleItalic = 2, StyleUnderline = 4, StyleStrikethrough = 8, StyleLowerCase = 16, StyleUpperCase = 32 };
	enum class Style { Normal = 0, Bold = 1, Italic = 2, BoldAndItalic = 3 };
	enum class HOverflow { Wrap = 0, Overflow = 1 };
	enum class VOverflow { Truncate = 0, Overflow = 1, Ellipsis = 2 };

	// 레이아웃 결과 (캔버스 로컬 = 피벗 원점)
	struct CharInfo
	{
		char32_t Char = 0;
		int Source = 0;          // 원문(태그 포함, 유니코드 문자 단위) 위치
		int Line = 0;
		bool Visible = false;    // 그리는 글자 (공백 · 줄바꿈 = false)
		Vec2 BottomLeft, TopRight;
		float PenX = 0.0f;       // 글자 시작 (커서 자리)
		float Advance = 0.0f;
		int Link = -1;
		float Size = 0.0f;       // 글자 크기 (px)
	};
	struct LineInfo { int First = 0, Count = 0; float Top = 0.0f, Height = 0.0f, Ascent = 0.0f, StartX = 0.0f, EndX = 0.0f; };
	struct LinkInfo { std::string Id, Text; int First = 0, Count = 0; };

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
	Style GetStyle() const { return (Style)(m_StyleBits & 3); }
	void SetStyle(Style s) { m_StyleBits = (m_StyleBits & ~3) | (int)s; }
	int GetStyleBits() const { return m_StyleBits; }
	void SetStyleBits(int bits) { m_StyleBits = bits & 63; }
	void SetOverflow(HOverflow h, VOverflow v) { m_HOverflow = h; m_VOverflow = v; }
	VOverflow GetVOverflow() const { return m_VOverflow; }
	bool GetWrap() const { return m_HOverflow == HOverflow::Wrap; }
	void SetWrap(bool w) { m_HOverflow = w ? HOverflow::Wrap : HOverflow::Overflow; }
	bool GetRichText() const { return m_RichText; }
	void SetRichText(bool v) { m_RichText = v; }
	bool GetAutoSize() const { return m_BestFit; }
	void SetAutoSize(bool v) { m_BestFit = v; }
	int GetMinSize() const { return m_MinSize; }
	int GetMaxSize() const { return m_MaxSize; }
	void SetMinMaxSize(int mn, int mx) { m_MinSize = std::clamp(mn, 1, 300); m_MaxSize = std::clamp(mx, m_MinSize, 300); }
	// 간격: 글자 · 단어 · 문단 = TMP 단위 (글자 크기의 1/100), 줄 = 배율 (Unity Text 의 Line Spacing)
	float CharacterSpacing = 0.0f, WordSpacing = 0.0f, ParagraphSpacing = 0.0f;
	Vec4 Margin = Vec4(0, 0, 0, 0);   // 왼쪽, 위, 오른쪽, 아래
	int MaxVisibleCharacters = -1;     // < 0 = 전부
	UIRenderer::TextMaterial Material;  // Face · Outline · Underlay (SpreadU/V 는 그릴 때 채운다)
	bool Underlay = false;
	UIEventList OnLinkClicked;

	// 결과 (지금 값으로 다시 계산해 둔다 — 스크립트가 text 를 바꾼 직후 읽어도 맞게)
	void ForceMeshUpdate();   // (스크립트용: 글자를 끝까지 구운 결과)
	const std::vector<CharInfo>& GetCharacters() { ForceMeshUpdate(); return m_Chars; }
	const std::vector<LineInfo>& GetLines() { ForceMeshUpdate(); return m_Lines; }
	const std::vector<LinkInfo>& GetLinks() { ForceMeshUpdate(); return m_Links; }
	float GetPreferredWidth() { ForceMeshUpdate(); return m_PreferredW; }
	float GetPreferredHeight() { ForceMeshUpdate(); return m_PreferredH; }
	bool IsOverflowing() { ForceMeshUpdate(); return m_Overflowing; }
	int GetUsedFontSize() { ForceMeshUpdate(); return m_UsedSize; }
	// 캔버스 월드 점 → 그 자리의 <link> 번호 (없으면 -1)
	int FindLinkAt(const Vec2& canvasWorld);

	// 스크립트 글자 애니메이션 (글자 번호 = CharInfo 순번, 텍스트가 바뀌면 지워진다)
	void SetCharacterOffset(int index, const Vec2& offset);
	void SetCharacterScale(int index, float scale);
	void SetCharacterColor(int index, const float rgba[4]);
	void ClearCharacterModifiers() { m_Mods.clear(); }

	// InputField: 입력 글자는 태그로 읽지 않는다
	void SetForcePlain(bool v) { m_ForcePlain = v; }

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
	int m_StyleBits = 0;
	int m_FontSize = 14;
	float m_LineSpacing = 1.0f;
	bool m_RichText = true;
	int m_Alignment = 0;
	HOverflow m_HOverflow = HOverflow::Wrap;
	VOverflow m_VOverflow = VOverflow::Truncate;
	bool m_BestFit = false;
	int m_MinSize = 10, m_MaxSize = 40;
	bool m_ForcePlain = false;

	// ---- 레이아웃 결과 (입력이 같으면 다시 계산하지 않는다) ----
	struct Quad
	{
		Vec2 P[4];        // 왼쪽 아래, 왼쪽 위, 오른쪽 위, 오른쪽 아래 (캔버스 로컬)
		Vec2 UV[4];
		uint32 TagColor = 0;
		bool HasTagColor = false;
		int Texture = 0;  // 0 보통 글꼴, 1 굵은 글꼴, 2+ = 스프라이트 (m_Sprites[n - 2])
		int Char = -1;    // 글자 순번 (maxVisibleCharacters · 글자 애니메이션), -1 = 글자와 무관 (mark)
		int Layer = 1;    // 0 mark (뒤), 1 글자 · 밑줄
	};
	std::vector<Quad> m_Quads;
	std::vector<CharInfo> m_Chars;
	std::vector<LineInfo> m_Lines;
	std::vector<LinkInfo> m_Links;
	std::vector<std::string> m_Sprites;
	GfxShaderResourceView* m_Atlas[2] = { nullptr, nullptr };
	const void* m_FontRef[2] = { nullptr, nullptr };   // UIFont::Font (캐시에 계속 산다)
	int m_FontGen[2] = { 0, 0 }, m_FontRev[2] = { 0, 0 };
	bool m_FontPending = false;   // 다 못 구운 글자가 있다 → 글꼴이 바뀌면 다시 배치
	bool m_ForScript = false;     // 스크립트가 결과를 읽는다 → 글자를 끝까지 굽는다
	float m_Spread[2][2] = { { 0, 0 }, { 0, 0 } };   // 글꼴별 여백 UV (Underlay)
	float m_PreferredW = 0.0f, m_PreferredH = 0.0f;
	bool m_Overflowing = false;
	int m_UsedSize = 14;
	uint64 m_LayoutKey = 0;
	uint64 LayoutKey() const;
	void Layout();
	void UpdateLayout();   // 입력이 바뀌었으면 Layout (그릴 때는 굽는 시간 예산 안에서)

	struct Mod { Vec2 Offset = Vec2(0, 0); float Scale = 1.0f; float Color[4] = { 1, 1, 1, 1 }; bool HasColor = false; };
	std::unordered_map<int, Mod> m_Mods;
	std::string m_ModsText;   // 이 글에 대한 수정 (글이 바뀌면 지운다)

	bool m_CharacterOpen = true, m_ParagraphOpen = true, m_MaterialOpen = false, m_EventsOpen = false;

	// 실행 중 (저장하지 않음)
	int m_CaretIndex = -1;
	bool m_CaretVisible = false;
	float m_CaretColor[4] = { 0.196f, 0.196f, 0.196f, 1.0f };
	Vec3 m_CaretWorld = Vec3(0, 0, 0);
	float m_CaretHeight = 0.0f;
};
REGISTER_COMPONENT(Text)
