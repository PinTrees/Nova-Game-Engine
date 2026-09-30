#pragma once
#include "UISelectable.h"
#include "UIEventList.h"

// Unity 의 Input Field (UI): 누르면 입력 중(커서 깜빡임), 글자는 Text Component 에 보이고, 비어 있으면 Placeholder.
//  - 키보드: 글자(한글은 IME 조합이 끝난 글자, 조합 창은 커서 위치), Backspace/Delete, ←/→, Home/End, Ctrl+V,
//    Enter = 끝내기(Single Line) 또는 줄바꿈(Multi Line Newline), Esc = 끝내기
//  - Content Type: Standard / Integer / Decimal / Alphanumeric / Name / Email / Password(*) / Pin
//  - On Value Changed (String), On End Edit (String): Dynamic 이면 현재 글자가 인자로
class InputField : public UISelectable
{
public:
	enum class ContentType { Standard = 0, IntegerNumber, DecimalNumber, Alphanumeric, Name, EmailAddress, Password, Pin };
	enum class LineType { SingleLine = 0, MultiLineSubmit, MultiLineNewline };

	InputField();

	const std::string& GetText() const { return m_Text; }
	void SetText(const std::string& text, bool notify = true);
	int GetCharacterLimit() const { return m_CharacterLimit; }
	void SetCharacterLimit(int n) { m_CharacterLimit = (std::max)(0, n); }
	bool IsFocused() const { return m_Selected; }
	void SetTextComponents(uint64 text, uint64 placeholder) { m_TextComponent = text; m_Placeholder = placeholder; }

	virtual void OnSelect() override;
	virtual void OnDeselect() override;
	virtual void OnUpdateSelected() override;
	virtual void UpdateBeforeLayout(float dt, bool playing) override;

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "ui_input_field"; }

	GENERATE_COMPONENT_BODY(InputField)

private:
	bool Accepts(char32_t c, const std::u32string& current, size_t caret) const;
	void Insert(const std::u32string& s);
	void EndEdit();
	void Changed();
	class Text* TextComponent();

	uint64 m_TextComponent = 0, m_Placeholder = 0;
	std::string m_Text;
	int m_CharacterLimit = 0;
	ContentType m_ContentType = ContentType::Standard;
	LineType m_LineType = LineType::SingleLine;
	float m_CaretBlinkRate = 0.85f;
	int m_CaretWidth = 1;
	float m_SelectionColor[4] = { 0.659f, 0.808f, 1.0f, 0.753f };
	bool m_ReadOnly = false;
	UIEventList m_OnValueChanged, m_OnEndEdit;

	// 실행 중
	size_t m_Caret = 0;          // 유니코드 문자 순번
	double m_BlinkStart = 0.0;
};
REGISTER_COMPONENT(InputField)
