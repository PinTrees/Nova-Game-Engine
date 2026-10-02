#include "pch.h"
#include "UIInputField.h"
#include "UIText.h"
#include "UIFont.h"
#include "UISystem.h"
#include "UnityGUI.h"
#include "ScriptEngine.h"
#include "GameViewEditorWindow.h"
#include "imgui_internal.h"

namespace
{
	std::string EncodeUtf8(const std::u32string& s)
	{
		std::string out;
		for (char32_t c : s)
		{
			if (c < 0x80) out += (char)c;
			else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
			else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
			else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
		}
		return out;
	}

	bool IsLetter(char32_t c) { return (c < 0x80 && isalpha((int)c)) || (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0x3131 && c <= 0x318E) || (c >= 0xC0 && c < 0x2000); }
	bool IsDigit(char32_t c) { return c >= '0' && c <= '9'; }
}

InputField::InputField()
{
	m_InspectorTitleName = "Input Field";
}

Text* InputField::TextComponent()
{
	GameObject* go = FindObject(m_TextComponent);
	return go ? go->GetComponent<Text>() : nullptr;
}

void InputField::SetText(const std::string& text, bool notify)
{
	if (text == m_Text)
		return;
	std::u32string s = UIFont::DecodeUtf8(text);
	if (m_CharacterLimit > 0 && (int)s.size() > m_CharacterLimit)
		s.resize(m_CharacterLimit);
	m_Text = EncodeUtf8(s);
	m_Caret = (std::min)(m_Caret, s.size());
	if (notify)
		Changed();
}

void InputField::Changed()
{
	if (m_pGameObject == nullptr || !Application::IsPlaying())
		return;
	m_OnValueChanged.Invoke(m_pGameObject, "On Value Changed", m_Text);
	ScriptEngine::InvokeUIEvent(m_pGameObject->GetFileID(), 3, 0.0f, m_Text);
}

bool InputField::Accepts(char32_t c, const std::u32string& current, size_t caret) const
{
	if (c == '\n')
		return m_LineType == LineType::MultiLineNewline;
	if (c < 32 || c == 127)
		return false;
	if (m_CharacterLimit > 0 && (int)current.size() >= m_CharacterLimit)
		return false;
	switch (m_ContentType)
	{
	case ContentType::IntegerNumber:
		return IsDigit(c) || (c == '-' && caret == 0 && current.find(U'-') == std::u32string::npos);
	case ContentType::DecimalNumber:
		return IsDigit(c) || (c == '-' && caret == 0 && current.find(U'-') == std::u32string::npos) || (c == '.' && current.find(U'.') == std::u32string::npos);
	case ContentType::Pin:
		return IsDigit(c);
	case ContentType::Alphanumeric:
		return IsDigit(c) || (c < 0x80 && isalpha((int)c));
	case ContentType::Name:
		return IsLetter(c) || c == ' ' || c == '\'' || c == '-';
	case ContentType::EmailAddress:
		return c < 0x80 && (isalnum((int)c) || strchr("@.!#$%&'*+-/=?^_`{|}~", (int)c) != nullptr);
	default:
		return true;
	}
}

void InputField::Insert(const std::u32string& input)
{
	std::u32string s = UIFont::DecodeUtf8(m_Text);
	bool changed = false;
	for (char32_t c : input)
		if (Accepts(c, s, m_Caret))
		{
			s.insert(s.begin() + (std::min)(m_Caret, s.size()), c);
			++m_Caret;
			changed = true;
		}
	if (changed)
	{
		m_Text = EncodeUtf8(s);
		m_BlinkStart = ImGui::GetTime();
		Changed();
	}
}

void InputField::OnSelect()
{
	UISelectable::OnSelect();
	if (!m_Interactable)
		return;
	m_Caret = UIFont::DecodeUtf8(m_Text).size();   // 누르면 끝으로 (Unity 는 누른 위치 — 간단히)
	m_BlinkStart = ImGui::GetTime();
}

void InputField::OnDeselect()
{
	const bool was = m_Selected;
	UISelectable::OnDeselect();
	if (was)
		EndEdit();
}

void InputField::EndEdit()
{
	if (m_pGameObject == nullptr || !Application::IsPlaying())
		return;
	m_OnEndEdit.Invoke(m_pGameObject, "On End Edit", m_Text);
	ScriptEngine::InvokeUIEvent(m_pGameObject->GetFileID(), 4, 0.0f, m_Text);
}

void InputField::OnUpdateSelected()
{
	if (!m_Interactable || m_ReadOnly)
		return;
	ImGuiIO& io = ImGui::GetIO();
	std::u32string s = UIFont::DecodeUtf8(m_Text);
	m_Caret = (std::min)(m_Caret, s.size());
	auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
	bool changed = false;

	// 글자 (Ctrl 조합 제외, AltGr 는 허용)
	if (!io.KeyCtrl || io.KeyAlt)
	{
		std::u32string typed;
		for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
			typed.push_back((char32_t)io.InputQueueCharacters[i]);
		if (!typed.empty())
		{
			Insert(typed);
			s = UIFont::DecodeUtf8(m_Text);
		}
	}
	if (pressed(ImGuiKey_Backspace) && m_Caret > 0)
	{
		s.erase(m_Caret - 1, 1);
		--m_Caret;
		changed = true;
	}
	if (pressed(ImGuiKey_Delete) && m_Caret < s.size())
	{
		s.erase(m_Caret, 1);
		changed = true;
	}
	if (pressed(ImGuiKey_LeftArrow) && m_Caret > 0) { --m_Caret; m_BlinkStart = ImGui::GetTime(); }
	if (pressed(ImGuiKey_RightArrow) && m_Caret < s.size()) { ++m_Caret; m_BlinkStart = ImGui::GetTime(); }
	if (pressed(ImGuiKey_Home)) m_Caret = 0;
	if (pressed(ImGuiKey_End)) m_Caret = s.size();
	if (changed)
	{
		m_Text = EncodeUtf8(s);
		m_BlinkStart = ImGui::GetTime();
		Changed();
	}
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
		if (const char* clip = ImGui::GetClipboardText())
			Insert(UIFont::DecodeUtf8(clip));
	if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter))
	{
		if (m_LineType == LineType::MultiLineNewline)
			Insert(U"\n");
		else
		{
			UISystem::ClearSelection();   // → OnDeselect → On End Edit
			return;
		}
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
	{
		UISystem::ClearSelection();
		return;
	}

	// 한글 IME 조합 창을 커서 위치에, 다른 창 단축키는 쉬게
	if (Text* text = TextComponent())
	{
		const Vec3 cw = text->GetCaretWorld();
		const ImVec2 bottom = GameViewEditorWindow::GameToScreen(cw.x, cw.y);
		const ImVec2 top = GameViewEditorWindow::GameToScreen(cw.x, cw.y + text->GetCaretHeight());
		ImGuiContext& g = *GImGui;
		g.PlatformImeData.WantVisible = true;
		g.PlatformImeData.InputPos = top;
		g.PlatformImeData.InputLineHeight = (std::max)(8.0f, bottom.y - top.y);
		if (ImGuiViewport* vp = ImGui::GetMainViewport())
			g.PlatformImeViewport = vp->ID;
		g.WantTextInputNextFrame = 1;
	}
}

void InputField::UpdateBeforeLayout(float, bool playing)
{
	// 보이는 글자 (Password / Pin 은 *), Placeholder 는 비어 있을 때만, 입력 중이면 커서
	if (Text* text = TextComponent())
	{
		std::string display = m_Text;
		if (m_ContentType == ContentType::Password || m_ContentType == ContentType::Pin)
			display.assign(UIFont::DecodeUtf8(m_Text).size(), '*');
		text->SetForcePlain(true);   // 입력한 < > 는 글자 그대로 (태그로 읽지 않는다)
		if (text->GetText() != display)
			text->SetText(display);
		if (playing && m_Selected && m_Interactable)
		{
			const double period = 1.0 / (std::max)(0.01f, m_CaretBlinkRate);
			const bool on = m_CaretBlinkRate <= 0.0f || fmod(ImGui::GetTime() - m_BlinkStart, period) < period * 0.5;
			text->SetCaret((int)m_Caret, on, text->GetColor());
		}
		else
			text->SetCaret(-1, false, nullptr);
	}
	if (GameObject* ph = FindObject(m_Placeholder))
		if (UIGraphic* g = ph->GetComponent<UIGraphic>())
			g->SetEnabled(m_Text.empty());
}

void InputField::OnInspectorGUI()
{
	DrawSelectableInspector();
	auto refField = [&](const char* label, uint64& id, const char* key) {
		GameObject* g = FindObject(id);
		const std::string text = g ? g->GetName() + " (Text)" : "None (Text)";
		ImVec2 fmin, fmax;
		const int pressed = UnityGUI::ObjectFieldButtons(label, text.c_str(), "ui_text", nullptr, 0, &fmin, &fmax);
		const ImVec2 after = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(fmin);
		ImGui::InvisibleButton(key, ImVec2((std::max)(1.0f, fmax.x - fmin.x - 22.0f), fmax.y - fmin.y));
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAME_OBJECT"))
				if (GameObject* go = *(GameObject**)payload->Data)
					id = go->GetFileID();
			ImGui::EndDragDropTarget();
		}
		if (pressed == -1)
			id = 0;
		ImGui::SetCursorScreenPos(after);
	};
	refField("Text Component", m_TextComponent, "##textComp");
	std::string text = m_Text;
	if (UnityGUI::TextArea("Text", &text, 40.0f))
		SetText(text, false);
	if (UnityGUI::Int("Character Limit", &m_CharacterLimit))
		m_CharacterLimit = (std::max)(0, m_CharacterLimit);
	static const char* kContent[] = { "Standard", "Integer Number", "Decimal Number", "Alphanumeric", "Name", "Email Address", "Password", "Pin" };
	int ct = (int)m_ContentType;
	if (UnityGUI::Dropdown("Content Type", &ct, kContent, 8))
		m_ContentType = (ContentType)ct;
	static const char* kLines[] = { "Single Line", "Multi Line Submit", "Multi Line Newline" };
	int lt = (int)m_LineType;
	if (UnityGUI::Dropdown("Line Type", &lt, kLines, 3))
		m_LineType = (LineType)lt;
	refField("Placeholder", m_Placeholder, "##placeholder");
	UnityGUI::Slider("Caret Blink Rate", &m_CaretBlinkRate, 0.0f, 4.0f);
	UnityGUI::Int("Caret Width", &m_CaretWidth);
	UnityGUI::Color("Selection Color", m_SelectionColor);
	UnityGUI::Toggle("Read Only", &m_ReadOnly);
	UnityGUI::Spacing(6.0f);
	m_OnValueChanged.Draw("On Value Changed (String)", "string");
	UnityGUI::Spacing(4.0f);
	m_OnEndEdit.Draw("On End Edit (String)", "string");
}

GENERATE_COMPONENT_FUNC_TOJSON(InputField)
{
	json j;
	SERIALIZE_TYPE(j, InputField);
	SelectableToJson(j);
	j["textComponent"] = m_TextComponent;
	j["placeholder"] = m_Placeholder;
	j["text"] = m_Text;
	j["characterLimit"] = m_CharacterLimit;
	j["contentType"] = (int)m_ContentType;
	j["lineType"] = (int)m_LineType;
	j["caretBlinkRate"] = m_CaretBlinkRate;
	j["caretWidth"] = m_CaretWidth;
	j["selectionColor"] = { m_SelectionColor[0], m_SelectionColor[1], m_SelectionColor[2], m_SelectionColor[3] };
	j["readOnly"] = m_ReadOnly;
	j["onValueChanged"] = m_OnValueChanged.ToJson();
	j["onEndEdit"] = m_OnEndEdit.ToJson();
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(InputField)
{
	SelectableFromJson(j);
	m_TextComponent = j.value("textComponent", (uint64)0);
	m_Placeholder = j.value("placeholder", (uint64)0);
	m_Text = j.value("text", std::string());
	m_CharacterLimit = j.value("characterLimit", 0);
	m_ContentType = (ContentType)j.value("contentType", 0);
	m_LineType = (LineType)j.value("lineType", 0);
	m_CaretBlinkRate = j.value("caretBlinkRate", 0.85f);
	m_CaretWidth = j.value("caretWidth", 1);
	if (j.contains("selectionColor") && j["selectionColor"].is_array() && j["selectionColor"].size() == 4)
		for (int i = 0; i < 4; ++i)
			m_SelectionColor[i] = j["selectionColor"][i].get<float>();
	m_ReadOnly = j.value("readOnly", false);
	m_OnValueChanged.FromJson(j.contains("onValueChanged") ? j["onValueChanged"] : json::array());
	m_OnEndEdit.FromJson(j.contains("onEndEdit") ? j["onEndEdit"] : json::array());
}

void InputField::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	UISelectable::RemapFileIDs(map);
	if (auto it = map.find(m_TextComponent); it != map.end()) m_TextComponent = it->second;
	if (auto it = map.find(m_Placeholder); it != map.end()) m_Placeholder = it->second;
}
