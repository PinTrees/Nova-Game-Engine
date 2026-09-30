#include "pch.h"
#include "CodeEditor.h"
#include "imgui_internal.h"

using CSharpLanguage::IsWordByte;
using CSharpLanguage::TokenKind;

namespace
{
	// ---------------------------------------------------------------- 색 (VS Code Dark+)
	const ImU32 kBg          = IM_COL32(30, 30, 30, 255);
	const ImU32 kCurLine     = IM_COL32(42, 42, 42, 255);
	const ImU32 kSelection   = IM_COL32(38, 79, 120, 255);
	const ImU32 kFindMatch   = IM_COL32(98, 78, 32, 255);
	const ImU32 kGutterNum   = IM_COL32(110, 118, 129, 255);
	const ImU32 kGutterCur   = IM_COL32(198, 198, 198, 255);
	const ImU32 kText        = IM_COL32(212, 212, 212, 255);
	const ImU32 kErrorLine   = IM_COL32(244, 71, 71, 255);
	const ImU32 kWarnLine    = IM_COL32(204, 167, 0, 255);

	ImU32 ColorOf(TokenKind k)
	{
		switch (k)
		{
		case TokenKind::Identifier:   return IM_COL32(156, 220, 254, 255);
		case TokenKind::Keyword:      return IM_COL32(86, 156, 214, 255);
		case TokenKind::Control:      return IM_COL32(197, 134, 192, 255);
		case TokenKind::Type:         return IM_COL32(78, 201, 176, 255);
		case TokenKind::Method:       return IM_COL32(220, 220, 170, 255);
		case TokenKind::String:       return IM_COL32(206, 145, 120, 255);
		case TokenKind::Number:       return IM_COL32(181, 206, 168, 255);
		case TokenKind::Comment:      return IM_COL32(106, 153, 85, 255);
		case TokenKind::Preprocessor: return IM_COL32(155, 155, 155, 255);
		default:                      return kText;
		}
	}

	int s_FocusFrame = -100;

	unsigned int Decode(const std::string& s, int i, int* len)
	{
		const unsigned char c = (unsigned char)s[i];
		if (c < 0x80) { *len = 1; return c; }
		if ((c >> 5) == 6 && i + 1 < (int)s.size()) { *len = 2; return ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); }
		if ((c >> 4) == 14 && i + 2 < (int)s.size()) { *len = 3; return ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); }
		if ((c >> 3) == 30 && i + 3 < (int)s.size()) { *len = 4; return ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F); }
		*len = 1;
		return '?';
	}

	std::string Encode(unsigned int c)
	{
		std::string out;
		if (c < 0x80) out += (char)c;
		else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
		else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
		else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
		return out;
	}

	std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
		return s;
	}

	int LeadingSpaces(const std::string& s)
	{
		int n = 0;
		while (n < (int)s.size() && (s[n] == ' ' || s[n] == '\t')) ++n;
		return n;
	}
}

bool CodeEditor::IsAnyEditorFocused()
{
	return s_FocusFrame >= ImGui::GetFrameCount() - 1;
}

// ==================================================================== 텍스트
CodeEditor::CodeEditor()
{
	m_LineStartInComment.assign(1, 0);
}

void CodeEditor::SetText(const std::string& text)
{
	m_CRLF = text.find("\r\n") != std::string::npos || text.find('\n') == std::string::npos;
	m_Lines.clear();
	std::string cur;
	for (char c : text)
	{
		if (c == '\r') continue;
		if (c == '\n') { m_Lines.push_back(cur); cur.clear(); }
		else cur += c;
	}
	m_Lines.push_back(cur);
	// UTF-8 BOM 은 보이지 않게 뗀다 (저장할 때도 쓰지 않음)
	if (m_Lines[0].size() >= 3 && (unsigned char)m_Lines[0][0] == 0xEF && (unsigned char)m_Lines[0][1] == 0xBB && (unsigned char)m_Lines[0][2] == 0xBF)
		m_Lines[0].erase(0, 3);
	m_Cursor = m_Anchor = Pos{};
	m_UndoStack.clear();
	m_RedoStack.clear();
	m_Version = m_SavedVersion = 0;
	m_WidthDirty = true;
	m_CompletionOpen = false;
	RebuildLineStates();
}

std::string CodeEditor::GetText() const
{
	std::string out;
	const char* nl = m_CRLF ? "\r\n" : "\n";
	for (size_t i = 0; i < m_Lines.size(); ++i)
	{
		out += m_Lines[i];
		if (i + 1 < m_Lines.size()) out += nl;
	}
	return out;
}

int CodeEditor::CursorColumn() const
{
	const std::string& s = m_Lines[m_Cursor.Line];
	int col = 1;
	for (int i = 0; i < m_Cursor.Col && i < (int)s.size();)
	{
		int len = 1;
		const unsigned int c = Decode(s, i, &len);
		col += c == '\t' ? TabSize - ((col - 1) % TabSize) : 1;
		i += len;
	}
	return col;
}

void CodeEditor::GoToLine(int line1, int column1)
{
	m_Cursor.Line = std::clamp(line1 - 1, 0, (int)m_Lines.size() - 1);
	m_Cursor.Col = std::clamp(column1 - 1, 0, (int)m_Lines[m_Cursor.Line].size());
	m_Cursor = Clamp(m_Cursor);
	m_Anchor = m_Cursor;
	m_GoToLineRequest = m_Cursor.Line;
	m_LastEdit = EditKind::None;
	m_WantFocus = true;
}

void CodeEditor::RebuildLineStates()
{
	m_LineStartInComment.resize(m_Lines.size());
	bool in = false;
	for (size_t i = 0; i < m_Lines.size(); ++i)
	{
		m_LineStartInComment[i] = in;
		in = CSharpLanguage::EndsInBlockComment(m_Lines[i], in);
	}
}

void CodeEditor::Changed()
{
	++m_Version;
	m_WidthDirty = true;
	m_PreferredX = -1.0f;
	m_ScrollToCursor = true;
	RebuildLineStates();
}

// 연속 입력/삭제는 한 번의 실행 취소로 묶는다 (1.5초 이상 쉬면 새로)
void CodeEditor::BeginEdit(EditKind kind)
{
	const double now = ImGui::GetTime();
	if (kind != m_LastEdit || kind == EditKind::Other || now - m_LastEditTime > 1.5)
	{
		m_UndoStack.push_back({ m_Lines, m_Cursor, m_Anchor });
		if (m_UndoStack.size() > 500)
			m_UndoStack.erase(m_UndoStack.begin());
		m_RedoStack.clear();
	}
	m_LastEdit = kind;
	m_LastEditTime = now;
}

void CodeEditor::Undo()
{
	if (m_UndoStack.empty()) return;
	m_RedoStack.push_back({ m_Lines, m_Cursor, m_Anchor });
	Snapshot s = std::move(m_UndoStack.back());
	m_UndoStack.pop_back();
	m_Lines = std::move(s.Lines);
	m_Cursor = s.Cursor;
	m_Anchor = s.Anchor;
	m_LastEdit = EditKind::None;
	Changed();
}

void CodeEditor::Redo()
{
	if (m_RedoStack.empty()) return;
	m_UndoStack.push_back({ m_Lines, m_Cursor, m_Anchor });
	Snapshot s = std::move(m_RedoStack.back());
	m_RedoStack.pop_back();
	m_Lines = std::move(s.Lines);
	m_Cursor = s.Cursor;
	m_Anchor = s.Anchor;
	m_LastEdit = EditKind::None;
	Changed();
}

std::string CodeEditor::SelectedText() const
{
	const Pos a = SelStart(), b = SelEnd();
	if (a.Line == b.Line)
		return m_Lines[a.Line].substr(a.Col, b.Col - a.Col);
	std::string out = m_Lines[a.Line].substr(a.Col) + "\n";
	for (int l = a.Line + 1; l < b.Line; ++l) out += m_Lines[l] + "\n";
	out += m_Lines[b.Line].substr(0, b.Col);
	return out;
}

void CodeEditor::DeleteSelection()
{
	if (!HasSelection()) return;
	const Pos a = SelStart(), b = SelEnd();
	m_Lines[a.Line] = m_Lines[a.Line].substr(0, a.Col) + m_Lines[b.Line].substr(b.Col);
	m_Lines.erase(m_Lines.begin() + a.Line + 1, m_Lines.begin() + b.Line + 1);
	m_Cursor = m_Anchor = a;
}

void CodeEditor::Insert(const std::string& text)
{
	DeleteSelection();
	std::vector<std::string> parts;
	std::string cur;
	for (char c : text)
	{
		if (c == '\r') continue;
		if (c == '\n') { parts.push_back(cur); cur.clear(); }
		else cur += c;
	}
	parts.push_back(cur);
	std::string& line = m_Lines[m_Cursor.Line];
	const std::string tail = line.substr(m_Cursor.Col);
	line = line.substr(0, m_Cursor.Col) + parts[0];
	if (parts.size() == 1)
	{
		m_Cursor.Col = (int)line.size();
		line += tail;
	}
	else
	{
		for (size_t i = 1; i < parts.size(); ++i)
			m_Lines.insert(m_Lines.begin() + m_Cursor.Line + i, parts[i]);
		m_Cursor.Line += (int)parts.size() - 1;
		m_Cursor.Col = (int)m_Lines[m_Cursor.Line].size();
		m_Lines[m_Cursor.Line] += tail;
	}
	m_Anchor = m_Cursor;
	Changed();
}

void CodeEditor::Backspace(bool word)
{
	if (HasSelection()) { BeginEdit(EditKind::Other); DeleteSelection(); Changed(); return; }
	if (m_Cursor.Line == 0 && m_Cursor.Col == 0) return;
	BeginEdit(EditKind::Deleting);
	std::string& line = m_Lines[m_Cursor.Line];
	// 빈 괄호/따옴표 쌍 사이면 둘 다 지운다
	if (!word && m_Cursor.Col > 0 && m_Cursor.Col < (int)line.size())
	{
		const char a = line[m_Cursor.Col - 1], b = line[m_Cursor.Col];
		if ((a == '(' && b == ')') || (a == '[' && b == ']') || (a == '{' && b == '}') || (a == '"' && b == '"'))
		{
			line.erase(m_Cursor.Col - 1, 2);
			m_Cursor.Col--;
			m_Anchor = m_Cursor;
			Changed();
			return;
		}
	}
	// 들여쓰기 공백 안이면 탭 단위로 지운다
	if (!word && m_Cursor.Col > 0 && LeadingSpaces(line) >= m_Cursor.Col && line.substr(0, m_Cursor.Col).find('\t') == std::string::npos)
	{
		const int remove = ((m_Cursor.Col - 1) % TabSize) + 1;
		line.erase(m_Cursor.Col - remove, remove);
		m_Cursor.Col -= remove;
		m_Anchor = m_Cursor;
		Changed();
		return;
	}
	m_Anchor = m_Cursor;
	m_Cursor = Left(m_Cursor, word);
	DeleteSelection();
	Changed();
}

void CodeEditor::DeleteForward(bool word)
{
	if (HasSelection()) { BeginEdit(EditKind::Other); DeleteSelection(); Changed(); return; }
	if (m_Cursor.Line == (int)m_Lines.size() - 1 && m_Cursor.Col == (int)m_Lines.back().size()) return;
	BeginEdit(EditKind::Deleting);
	m_Anchor = Right(m_Cursor, word);
	DeleteSelection();
	Changed();
}

void CodeEditor::NewLine()
{
	BeginEdit(EditKind::Other);
	DeleteSelection();
	const std::string& line = m_Lines[m_Cursor.Line];
	const std::string indent = line.substr(0, (std::min)(LeadingSpaces(line), m_Cursor.Col));
	// 커서 앞(공백 제외)이 '{' 면 한 단계 더 들여쓴다
	int k = m_Cursor.Col - 1;
	while (k >= 0 && (line[k] == ' ' || line[k] == '\t')) --k;
	const bool openBrace = k >= 0 && line[k] == '{';
	const bool closeNext = m_Cursor.Col < (int)line.size() && line[m_Cursor.Col] == '}';
	const std::string tab(TabSize, ' ');
	if (openBrace && closeNext)
	{
		// {|} → {\n    |\n}
		Insert("\n" + indent + tab + "\n" + indent);
		m_Cursor.Line--;
		m_Cursor.Col = (int)(indent.size() + tab.size());
		m_Anchor = m_Cursor;
	}
	else
		Insert("\n" + indent + (openBrace ? tab : std::string()));
}

void CodeEditor::Indent(bool outdent)
{
	BeginEdit(EditKind::Other);
	const Pos a = SelStart(), b = SelEnd();
	const bool multi = HasSelection() && a.Line != b.Line;
	if (!multi && !outdent)
	{
		const int col = CursorColumn() - 1;
		Insert(std::string(TabSize - (col % TabSize), ' '));
		return;
	}
	const int last = multi && b.Col == 0 ? b.Line - 1 : b.Line;
	for (int l = a.Line; l <= last; ++l)
	{
		std::string& s = m_Lines[l];
		int delta = 0;
		if (outdent)
		{
			int n = 0;
			while (n < TabSize && n < (int)s.size() && s[n] == ' ') ++n;
			if (n == 0 && !s.empty() && s[0] == '\t') n = 1;
			s.erase(0, n);
			delta = -n;
		}
		else if (!s.empty())
		{
			s.insert(0, std::string(TabSize, ' '));
			delta = TabSize;
		}
		if (l == m_Cursor.Line) m_Cursor.Col = (std::max)(0, m_Cursor.Col + delta);
		if (l == m_Anchor.Line) m_Anchor.Col = (std::max)(0, m_Anchor.Col + delta);
	}
	if (!multi) m_Anchor = m_Cursor;
	Changed();
}

void CodeEditor::ToggleComment()
{
	const Pos a = SelStart(), b = SelEnd();
	const int last = HasSelection() && b.Col == 0 && b.Line > a.Line ? b.Line - 1 : b.Line;
	bool allCommented = true;
	int minIndent = INT_MAX;
	for (int l = a.Line; l <= last; ++l)
	{
		const std::string& s = m_Lines[l];
		const int ind = LeadingSpaces(s);
		if (ind == (int)s.size()) continue;
		minIndent = (std::min)(minIndent, ind);
		if (s.compare(ind, 2, "//") != 0) allCommented = false;
	}
	if (minIndent == INT_MAX) return;
	BeginEdit(EditKind::Other);
	for (int l = a.Line; l <= last; ++l)
	{
		std::string& s = m_Lines[l];
		const int ind = LeadingSpaces(s);
		if (ind == (int)s.size()) continue;
		if (allCommented)
		{
			const int n = s.compare(ind, 3, "// ") == 0 ? 3 : 2;
			s.erase(ind, n);
			if (l == m_Cursor.Line) m_Cursor.Col = (std::max)(ind, m_Cursor.Col - n);
			if (l == m_Anchor.Line) m_Anchor.Col = (std::max)(ind, m_Anchor.Col - n);
		}
		else
		{
			s.insert(minIndent, "// ");
			if (l == m_Cursor.Line && m_Cursor.Col >= minIndent) m_Cursor.Col += 3;
			if (l == m_Anchor.Line && m_Anchor.Col >= minIndent) m_Anchor.Col += 3;
		}
	}
	Changed();
}

void CodeEditor::DuplicateLine()
{
	BeginEdit(EditKind::Other);
	m_Lines.insert(m_Lines.begin() + m_Cursor.Line + 1, m_Lines[m_Cursor.Line]);
	m_Cursor.Line++;
	m_Anchor = m_Cursor;
	Changed();
}

void CodeEditor::CutOrCopy(bool cut)
{
	if (HasSelection())
	{
		ImGui::SetClipboardText(SelectedText().c_str());
		if (cut) { BeginEdit(EditKind::Other); DeleteSelection(); Changed(); }
		return;
	}
	// 선택이 없으면 줄 전체 (VS Code 와 같음)
	ImGui::SetClipboardText((m_Lines[m_Cursor.Line] + "\n").c_str());
	if (cut)
	{
		BeginEdit(EditKind::Other);
		if (m_Lines.size() > 1) m_Lines.erase(m_Lines.begin() + m_Cursor.Line);
		else m_Lines[0].clear();
		m_Cursor = m_Anchor = Clamp({ m_Cursor.Line, 0 });
		Changed();
	}
}

void CodeEditor::Paste()
{
	const char* clip = ImGui::GetClipboardText();
	if (clip == nullptr || clip[0] == 0) return;
	BeginEdit(EditKind::Other);
	std::string text = clip;
	// 탭은 공백 4칸으로 (문서 전체를 공백 들여쓰기로 유지)
	for (size_t p = text.find('\t'); p != std::string::npos; p = text.find('\t', p + TabSize))
		text.replace(p, 1, std::string(TabSize, ' '));
	Insert(text);
}

void CodeEditor::SelectAll()
{
	m_Anchor = { 0, 0 };
	m_Cursor = { (int)m_Lines.size() - 1, (int)m_Lines.back().size() };
}

// ==================================================================== 이동
CodeEditor::Pos CodeEditor::Clamp(Pos p) const
{
	p.Line = std::clamp(p.Line, 0, (int)m_Lines.size() - 1);
	const std::string& s = m_Lines[p.Line];
	p.Col = std::clamp(p.Col, 0, (int)s.size());
	while (p.Col > 0 && p.Col < (int)s.size() && ((unsigned char)s[p.Col] & 0xC0) == 0x80) --p.Col;   // UTF-8 경계
	return p;
}

CodeEditor::Pos CodeEditor::Left(Pos p, bool word) const
{
	if (p.Col == 0)
	{
		if (p.Line > 0) { p.Line--; p.Col = (int)m_Lines[p.Line].size(); }
		return p;
	}
	const std::string& s = m_Lines[p.Line];
	auto prev = [&](int c) { --c; while (c > 0 && ((unsigned char)s[c] & 0xC0) == 0x80) --c; return c; };
	if (!word) { p.Col = prev(p.Col); return p; }
	int c = p.Col;
	while (c > 0 && (s[c - 1] == ' ' || s[c - 1] == '\t')) c = prev(c);
	if (c > 0 && IsWordByte((unsigned char)s[c - 1]))
		while (c > 0 && IsWordByte((unsigned char)s[c - 1])) c = prev(c);
	else if (c > 0)
		c = prev(c);
	p.Col = c;
	return p;
}

CodeEditor::Pos CodeEditor::Right(Pos p, bool word) const
{
	const std::string& s = m_Lines[p.Line];
	if (p.Col >= (int)s.size())
	{
		if (p.Line + 1 < (int)m_Lines.size()) { p.Line++; p.Col = 0; }
		return p;
	}
	auto next = [&](int c) { ++c; while (c < (int)s.size() && ((unsigned char)s[c] & 0xC0) == 0x80) ++c; return c; };
	if (!word) { p.Col = next(p.Col); return p; }
	int c = p.Col;
	if (IsWordByte((unsigned char)s[c]))
		while (c < (int)s.size() && IsWordByte((unsigned char)s[c])) c = next(c);
	else
		c = next(c);
	while (c < (int)s.size() && (s[c] == ' ' || s[c] == '\t')) c = next(c);
	p.Col = c;
	return p;
}

void CodeEditor::MoveTo(Pos p, bool select)
{
	m_Cursor = Clamp(p);
	if (!select) m_Anchor = m_Cursor;
	m_LastEdit = EditKind::None;
	m_ScrollToCursor = true;
	m_CompletionOpen = false;
}

void CodeEditor::SelectWordAt(Pos p)
{
	p = Clamp(p);
	const std::string& s = m_Lines[p.Line];
	int a = p.Col, b = p.Col;
	while (a > 0 && IsWordByte((unsigned char)s[a - 1])) --a;
	while (b < (int)s.size() && IsWordByte((unsigned char)s[b])) ++b;
	m_Anchor = { p.Line, a };
	m_Cursor = { p.Line, b };
}

// ==================================================================== 좌표
float CodeEditor::CharAdvance(unsigned int c) const
{
	if (m_Font == nullptr) return 8.0f;
	return m_Font->GetCharAdvance((ImWchar)(c < 0x10000 ? c : '?')) * (m_FontSize / m_Font->FontSize);
}

float CodeEditor::XOf(int line, int col) const
{
	const std::string& s = m_Lines[line];
	float x = 0.0f;
	const float tabW = m_CharW * TabSize;
	for (int i = 0; i < col && i < (int)s.size();)
	{
		int len = 1;
		const unsigned int c = Decode(s, i, &len);
		x = c == '\t' ? (floorf(x / tabW) + 1.0f) * tabW : x + CharAdvance(c);
		i += len;
	}
	return x;
}

int CodeEditor::ColOfX(int line, float target) const
{
	const std::string& s = m_Lines[line];
	float x = 0.0f;
	const float tabW = m_CharW * TabSize;
	for (int i = 0; i < (int)s.size();)
	{
		int len = 1;
		const unsigned int c = Decode(s, i, &len);
		const float nx = c == '\t' ? (floorf(x / tabW) + 1.0f) * tabW : x + CharAdvance(c);
		if (target < (x + nx) * 0.5f) return i;
		x = nx;
		i += len;
	}
	return (int)s.size();
}

// ==================================================================== 구문 강조 그리기
void CodeEditor::DrawLine(ImDrawList* dl, int line, float x0, float y)
{
	const std::string& s = m_Lines[line];
	const bool inComment = line < (int)m_LineStartInComment.size() && m_LineStartInComment[line];
	CSharpLanguage::Tokenize(s, inComment, m_Tokens);
	for (const CSharpLanguage::Token& t : m_Tokens)
	{
		const ImU32 col = ColorOf(t.Kind);
		// 토큰 안의 탭은 건너뛰며 조각별로 그린다
		int start = t.Begin;
		for (int k = t.Begin; k <= t.End; ++k)
		{
			if (k == t.End || s[k] == '\t')
			{
				if (k > start)
					dl->AddText(m_Font, m_FontSize, ImVec2(floorf(x0 + XOf(line, start)), y), col, s.data() + start, s.data() + k);
				start = k + 1;
			}
		}
	}
}

// ==================================================================== 자동 완성
void CodeEditor::UpdateCompletion(bool force)
{
	const std::string& s = m_Lines[m_Cursor.Line];
	int start = m_Cursor.Col;
	while (start > 0 && IsWordByte((unsigned char)s[start - 1])) --start;
	const std::string prefix = s.substr(start, m_Cursor.Col - start);
	const bool member = start > 0 && s[start - 1] == '.';
	// 주석/문자열 안에서는 띄우지 않는다
	CSharpLanguage::Tokenize(s, m_LineStartInComment[m_Cursor.Line] != 0, m_Tokens);
	for (const CSharpLanguage::Token& t : m_Tokens)
		if ((t.Kind == TokenKind::Comment || t.Kind == TokenKind::String) && t.Begin < m_Cursor.Col && m_Cursor.Col <= t.End)
		{
			m_CompletionOpen = false;
			return;
		}
	if (!force && !member && prefix.size() < 2)
	{
		m_CompletionOpen = false;
		return;
	}
	std::string owner;
	if (member)
	{
		const int e = start - 1;
		int b = e;
		while (b > 0 && IsWordByte((unsigned char)s[b - 1])) --b;
		owner = s.substr(b, e - b);
	}
	m_Completions = CSharpLanguage::Complete(prefix, member, owner, m_Lines);
	m_CompletionOpen = !m_Completions.empty();
	m_CompletionIndex = 0;
	m_CompletionStart = { m_Cursor.Line, start };
}

void CodeEditor::AcceptCompletion()
{
	if (!m_CompletionOpen || m_Completions.empty()) return;
	BeginEdit(EditKind::Other);
	m_Anchor = m_CompletionStart;
	Insert(m_Completions[m_CompletionIndex].Text);
	m_CompletionOpen = false;
}

// ==================================================================== 찾기 / 바꾸기
void CodeEditor::OpenFind(bool replace)
{
	m_FindOpen = true;
	m_GotoOpen = false;
	m_ReplaceOpen = replace;
	m_FindFocus = true;
	if (HasSelection() && SelStart().Line == SelEnd().Line)
		strncpy_s(m_FindText, SelectedText().c_str(), _TRUNCATE);
}

void CodeEditor::OpenGoToLine()
{
	m_GotoOpen = true;
	m_FindOpen = false;
	m_GotoFocus = true;
	m_GotoText[0] = 0;
}

void CodeEditor::FindNext(bool backwards)
{
	const std::string needle = m_MatchCase ? std::string(m_FindText) : Lower(m_FindText);
	if (needle.empty()) return;
	const int count = (int)m_Lines.size();
	const Pos from = backwards ? SelStart() : SelEnd();
	for (int step = 0; step <= count; ++step)
	{
		const int l = ((backwards ? from.Line - step : from.Line + step) % count + count) % count;
		const std::string hay = m_MatchCase ? m_Lines[l] : Lower(m_Lines[l]);
		size_t pos;
		if (!backwards)
			pos = hay.find(needle, step == 0 ? (size_t)from.Col : 0);
		else
		{
			if (step == 0 && from.Col == 0) continue;
			pos = hay.rfind(needle, step == 0 ? (size_t)(from.Col - 1) : std::string::npos);
		}
		if (pos != std::string::npos)
		{
			m_Anchor = { l, (int)pos };
			m_Cursor = { l, (int)(pos + needle.size()) };
			m_ScrollToCursor = true;
			return;
		}
	}
}

void CodeEditor::ReplaceOne()
{
	const std::string sel = SelectedText();
	if (HasSelection() && (m_MatchCase ? sel == m_FindText : Lower(sel) == Lower(m_FindText)))
	{
		BeginEdit(EditKind::Other);
		Insert(m_ReplaceText);
	}
	FindNext(false);
}

void CodeEditor::ReplaceAll()
{
	const std::string needle = m_MatchCase ? std::string(m_FindText) : Lower(m_FindText);
	if (needle.empty()) return;
	BeginEdit(EditKind::Other);
	int count = 0;
	const size_t replLen = strlen(m_ReplaceText);
	for (std::string& line : m_Lines)
	{
		size_t pos = 0;
		while (true)
		{
			const std::string hay = m_MatchCase ? line : Lower(line);
			pos = hay.find(needle, pos);
			if (pos == std::string::npos) break;
			line.replace(pos, needle.size(), m_ReplaceText);
			pos += replLen;
			++count;
		}
	}
	m_Cursor = m_Anchor = Clamp(m_Cursor);
	if (count > 0) Changed();
}

int CodeEditor::CountMatches() const
{
	if (m_FindText[0] == 0) return 0;
	const std::string needle = m_MatchCase ? std::string(m_FindText) : Lower(m_FindText);
	int total = 0;
	for (const std::string& l : m_Lines)
	{
		const std::string hay = m_MatchCase ? l : Lower(l);
		for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + needle.size())) ++total;
	}
	return total;
}

void CodeEditor::DrawFindBar()
{
	if (!m_FindOpen && !m_GotoOpen)
		return;
	ImGuiIO& io = ImGui::GetIO();
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 3));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3);
	if (m_GotoOpen)
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Go to line:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90);
		if (m_GotoFocus) { ImGui::SetKeyboardFocusHere(); m_GotoFocus = false; }
		if (ImGui::InputText("##goto", m_GotoText, sizeof(m_GotoText), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal))
		{
			if (m_GotoText[0]) GoToLine(atoi(m_GotoText));
			m_GotoOpen = false;
		}
		const bool active = ImGui::IsItemActive();
		ImGui::SameLine();
		ImGui::TextDisabled("(1 - %d)", (int)m_Lines.size());
		ImGui::SameLine();
		if (ImGui::SmallButton("x##gotoClose") || (active && ImGui::IsKeyPressed(ImGuiKey_Escape)))
		{
			m_GotoOpen = false;
			m_WantFocus = true;
		}
	}
	else
	{
		ImGui::SetNextItemWidth(260);
		if (m_FindFocus) { ImGui::SetKeyboardFocusHere(); m_FindFocus = false; }
		if (ImGui::InputTextWithHint("##find", "Find", m_FindText, sizeof(m_FindText), ImGuiInputTextFlags_EnterReturnsTrue))
		{
			FindNext(io.KeyShift);
			ImGui::SetKeyboardFocusHere(-1);   // Enter 를 계속 눌러 다음 찾기
		}
		const bool active = ImGui::IsItemActive();
		ImGui::SameLine();
		ImGui::Checkbox("Aa", &m_MatchCase);
		ImGui::SameLine();
		if (ImGui::SmallButton("Prev")) FindNext(true);
		ImGui::SameLine();
		if (ImGui::SmallButton("Next")) FindNext(false);
		ImGui::SameLine();
		const int total = CountMatches();
		if (total > 0) ImGui::TextDisabled("%d results", total);
		else ImGui::TextDisabled("No results");
		ImGui::SameLine();
		if (ImGui::SmallButton("x##findClose") || (active && ImGui::IsKeyPressed(ImGuiKey_Escape)))
		{
			m_FindOpen = false;
			m_WantFocus = true;
		}
		if (m_ReplaceOpen)
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4);
			ImGui::SetNextItemWidth(260);
			ImGui::InputTextWithHint("##replace", "Replace", m_ReplaceText, sizeof(m_ReplaceText));
			ImGui::SameLine();
			if (ImGui::SmallButton("Replace")) ReplaceOne();
			ImGui::SameLine();
			if (ImGui::SmallButton("Replace All")) ReplaceAll();
		}
	}
	ImGui::PopStyleVar(2);
}

// ==================================================================== 입력
void CodeEditor::HandleKeyboard()
{
	ImGuiIO& io = ImGui::GetIO();
	const bool ctrl = io.KeyCtrl, shift = io.KeyShift;
	auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };

	if (m_CompletionOpen && !m_Completions.empty())
	{
		const int n = (int)m_Completions.size();
		if (pressed(ImGuiKey_DownArrow)) { m_CompletionIndex = (m_CompletionIndex + 1) % n; return; }
		if (pressed(ImGuiKey_UpArrow)) { m_CompletionIndex = (m_CompletionIndex + n - 1) % n; return; }
		if (pressed(ImGuiKey_Tab) || pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) { AcceptCompletion(); return; }
		if (pressed(ImGuiKey_Escape)) { m_CompletionOpen = false; return; }
	}

	// ---- 이동
	if (pressed(ImGuiKey_LeftArrow))
	{
		if (HasSelection() && !shift) MoveTo(SelStart(), false);
		else MoveTo(Left(m_Cursor, ctrl), shift);
		m_PreferredX = -1.0f;
	}
	if (pressed(ImGuiKey_RightArrow))
	{
		if (HasSelection() && !shift) MoveTo(SelEnd(), false);
		else MoveTo(Right(m_Cursor, ctrl), shift);
		m_PreferredX = -1.0f;
	}
	auto vertical = [&](int delta) {
		if (m_PreferredX < 0.0f) m_PreferredX = XOf(m_Cursor.Line, m_Cursor.Col);
		const float keepX = m_PreferredX;
		Pos p = m_Cursor;
		p.Line = std::clamp(p.Line + delta, 0, (int)m_Lines.size() - 1);
		p.Col = ColOfX(p.Line, keepX);
		MoveTo(p, shift);
		m_PreferredX = keepX;   // 위아래로 움직일 때는 처음 가로 위치를 기억
	};
	if (pressed(ImGuiKey_UpArrow)) { if (ctrl) ImGui::SetScrollY(ImGui::GetScrollY() - m_LineH); else vertical(-1); }
	if (pressed(ImGuiKey_DownArrow)) { if (ctrl) ImGui::SetScrollY(ImGui::GetScrollY() + m_LineH); else vertical(1); }
	const int page = (std::max)(1, (int)(ImGui::GetWindowHeight() / m_LineH) - 1);
	if (pressed(ImGuiKey_PageUp)) vertical(-page);
	if (pressed(ImGuiKey_PageDown)) vertical(page);
	if (pressed(ImGuiKey_Home))
	{
		if (ctrl) MoveTo({ 0, 0 }, shift);
		else
		{
			const int ind = LeadingSpaces(m_Lines[m_Cursor.Line]);
			MoveTo({ m_Cursor.Line, m_Cursor.Col == ind ? 0 : ind }, shift);   // 스마트 Home: 들여쓰기 끝 ↔ 줄 처음
		}
	}
	if (pressed(ImGuiKey_End))
	{
		if (ctrl) MoveTo({ (int)m_Lines.size() - 1, (int)m_Lines.back().size() }, shift);
		else MoveTo({ m_Cursor.Line, (int)m_Lines[m_Cursor.Line].size() }, shift);
	}

	// ---- 단축키
	if (ctrl && !io.KeyAlt)
	{
		if (pressed(ImGuiKey_A)) SelectAll();
		if (pressed(ImGuiKey_C)) CutOrCopy(false);
		if (pressed(ImGuiKey_X)) CutOrCopy(true);
		if (pressed(ImGuiKey_V)) Paste();
		if (!shift && pressed(ImGuiKey_Z)) Undo();
		if (pressed(ImGuiKey_Y) || (shift && pressed(ImGuiKey_Z))) Redo();
		if (pressed(ImGuiKey_F)) OpenFind(false);
		if (pressed(ImGuiKey_H)) OpenFind(true);
		if (pressed(ImGuiKey_G)) OpenGoToLine();
		if (pressed(ImGuiKey_Slash)) ToggleComment();
		if (pressed(ImGuiKey_D)) DuplicateLine();
		if (pressed(ImGuiKey_Space)) UpdateCompletion(true);
	}
	if (pressed(ImGuiKey_F3)) FindNext(shift);
	if (pressed(ImGuiKey_Escape))
	{
		m_CompletionOpen = false;
		if (m_FindOpen || m_GotoOpen) { m_FindOpen = false; m_GotoOpen = false; }
		else m_Anchor = m_Cursor;
	}

	// ---- 편집 키
	if (pressed(ImGuiKey_Backspace)) { Backspace(ctrl); if (m_CompletionOpen) UpdateCompletion(false); }
	if (pressed(ImGuiKey_Delete)) DeleteForward(ctrl);
	if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) NewLine();
	if (pressed(ImGuiKey_Tab)) Indent(shift);

	// ---- 글자 입력 (한글은 IME 조합이 끝난 글자가 들어온다)
	if (ctrl && !io.KeyAlt)
		return;
	for (int qi = 0; qi < io.InputQueueCharacters.Size; ++qi)
	{
		const unsigned int c = io.InputQueueCharacters[qi];
		if (c < 32 || c == 127) continue;
		std::string& line = m_Lines[m_Cursor.Line];
		const char nextCh = m_Cursor.Col < (int)line.size() ? line[m_Cursor.Col] : 0;
		// 자동으로 닫힌 괄호/따옴표 위에 같은 글자를 치면 건너뛰기만
		if (AutoCloseBrackets && !HasSelection() && (c == ')' || c == ']' || c == '}' || c == '"') && nextCh == (char)c)
		{
			MoveTo(Right(m_Cursor, false), false);
			continue;
		}
		BeginEdit(EditKind::Typing);
		// 빈 줄에서 '}' 를 치면 한 단계 내어쓰기
		if (c == '}' && !HasSelection() && LeadingSpaces(line) == m_Cursor.Col && m_Cursor.Col >= TabSize)
		{
			line.erase(m_Cursor.Col - TabSize, TabSize);
			m_Cursor.Col -= TabSize;
			m_Anchor = m_Cursor;
		}
		const bool atGap = nextCh == 0 || nextCh == ' ' || nextCh == ')' || nextCh == ']' || nextCh == '}' || nextCh == ';' || nextCh == ',';
		const char prevCh = m_Cursor.Col > 0 ? line[m_Cursor.Col - 1] : 0;
		std::string pair;
		if (AutoCloseBrackets && !HasSelection() && atGap)
		{
			if (c == '(') pair = ")";
			else if (c == '[') pair = "]";
			else if (c == '{') pair = "}";
			else if (c == '"' && !IsWordByte((unsigned char)prevCh)) pair = "\"";
		}
		Insert(Encode(c) + pair);
		if (!pair.empty()) { m_Cursor.Col -= (int)pair.size(); m_Anchor = m_Cursor; }
		if (!AutoComplete && !m_CompletionOpen) continue;
		if (c == '.') UpdateCompletion(true);
		else if (c >= 0x80 || IsWordByte((unsigned char)c)) UpdateCompletion(false);
		else m_CompletionOpen = false;
	}
}

void CodeEditor::HandleMouse(float originX, float originY, float gutterW)
{
	ImGuiIO& io = ImGui::GetIO();
	const ImVec2 m = io.MousePos;
	auto posAt = [&](ImVec2 mp) {
		Pos p;
		p.Line = std::clamp((int)floorf((mp.y - originY) / m_LineH), 0, (int)m_Lines.size() - 1);
		p.Col = ColOfX(p.Line, mp.x - originX);
		return Clamp(p);
	};
	const bool hovered = ImGui::IsWindowHovered();
	const float winX = ImGui::GetWindowPos().x;
	if (hovered)
		ImGui::SetMouseCursor(m.x < winX + gutterW ? ImGuiMouseCursor_Arrow : ImGuiMouseCursor_TextInput);
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		const Pos p = posAt(m);
		if (m.x < winX + gutterW)
		{
			// 줄 번호 클릭 = 줄 선택
			m_Anchor = { p.Line, 0 };
			m_Cursor = p.Line + 1 < (int)m_Lines.size() ? Pos{ p.Line + 1, 0 } : Pos{ p.Line, (int)m_Lines[p.Line].size() };
		}
		else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			SelectWordAt(p);
		else
		{
			MoveTo(p, io.KeyShift);
			m_Selecting = true;
		}
		m_PreferredX = -1.0f;
		m_CompletionOpen = false;
		m_LastEditTime = ImGui::GetTime();   // 커서 깜빡임을 보이는 상태부터
	}
	if (m_Selecting)
	{
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			m_Cursor = posAt(m);
			const float top = ImGui::GetWindowPos().y, bottom = top + ImGui::GetWindowHeight();
			if (m.y < top + 4) ImGui::SetScrollY(ImGui::GetScrollY() - m_LineH);
			if (m.y > bottom - 4) ImGui::SetScrollY(ImGui::GetScrollY() + m_LineH);
		}
		else
			m_Selecting = false;
	}
	// 우클릭 메뉴
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		if (!HasSelection()) MoveTo(posAt(m), false);
		ImGui::OpenPopup("##codeContext");
	}
	if (ImGui::BeginPopup("##codeContext"))
	{
		if (ImGui::MenuItem("Cut", "Ctrl+X")) CutOrCopy(true);
		if (ImGui::MenuItem("Copy", "Ctrl+C")) CutOrCopy(false);
		if (ImGui::MenuItem("Paste", "Ctrl+V")) Paste();
		ImGui::Separator();
		if (ImGui::MenuItem("Select All", "Ctrl+A")) SelectAll();
		if (ImGui::MenuItem("Toggle Line Comment", "Ctrl+/")) ToggleComment();
		if (ImGui::MenuItem("Duplicate Line", "Ctrl+D")) DuplicateLine();
		ImGui::Separator();
		if (ImGui::MenuItem("Find", "Ctrl+F")) OpenFind(false);
		if (ImGui::MenuItem("Replace", "Ctrl+H")) OpenFind(true);
		if (ImGui::MenuItem("Go to Line...", "Ctrl+G")) OpenGoToLine();
		ImGui::EndPopup();
	}
}

void CodeEditor::EnsureCursorVisible()
{
	const float viewH = ImGui::GetWindowHeight(), viewW = ImGui::GetWindowWidth();
	const float cy = m_Cursor.Line * m_LineH;
	const float sy = ImGui::GetScrollY();
	if (m_GoToLineRequest >= 0)
	{
		ImGui::SetScrollY((std::max)(0.0f, m_GoToLineRequest * m_LineH - viewH * 0.35f));   // 줄로 이동하면 화면 위쪽 1/3 쯤
		m_GoToLineRequest = -1;
	}
	else
	{
		if (cy < sy) ImGui::SetScrollY(cy);
		else if (cy + m_LineH * 2.0f > sy + viewH) ImGui::SetScrollY(cy + m_LineH * 2.0f - viewH);
	}
	const float cx = XOf(m_Cursor.Line, m_Cursor.Col);
	const float sx = ImGui::GetScrollX();
	const float textW = viewW - m_GutterW - 20.0f;
	if (cx < sx) ImGui::SetScrollX((std::max)(0.0f, cx - 40.0f));
	else if (cx > sx + textW) ImGui::SetScrollX(cx - textW + 40.0f);
}

// ==================================================================== 그리기
void CodeEditor::DrawOverlays(ImDrawList* dl, float originX, float originY, int first, int last)
{
	ImGuiIO& io = ImGui::GetIO();
	const ImVec2 winPos = ImGui::GetWindowPos();
	const ImVec2 winSize = ImGui::GetWindowSize();
	const bool hovered = ImGui::IsWindowHovered();
	std::string hoverMessage;

	// 오류/경고: 물결 밑줄 (마우스를 올리면 메시지)
	for (const Marker& mk : m_Markers)
	{
		const int l = mk.Line - 1;
		if (l < 0 || l >= (int)m_Lines.size()) continue;
		const std::string& s = m_Lines[l];
		int a = mk.Column > 0 ? (std::min)(mk.Column - 1, (int)s.size()) : LeadingSpaces(s);
		if (a >= (int)s.size())
		{
			// 줄 끝 오류 (예: ; 없음): 마지막 단어에 밑줄
			a = (int)s.size();
			while (a > 0 && (s[a - 1] == ' ' || s[a - 1] == '\t')) --a;
			const int end = a;
			while (a > 0 && IsWordByte((unsigned char)s[a - 1])) --a;
			if (a == end && a > 0) --a;   // 마지막이 기호면 그 글자 (예: ')')
		}
		int b = a;
		while (b < (int)s.size() && IsWordByte((unsigned char)s[b])) ++b;
		if (b == a) b = (std::min)(a + 1, (int)s.size());
		if (b == a) { a = 0; b = (int)s.size(); }
		const float x0 = originX + XOf(l, a), x1 = (std::max)(x0 + 8.0f, originX + XOf(l, b));
		const float ly = originY + l * m_LineH;
		const ImU32 col = mk.Error ? kErrorLine : kWarnLine;
		if (l >= first && l <= last)
		{
			const float y = ly + m_LineH - 3.0f;
			int seg = 0;
			for (float x = x0; x < x1; x += 3.0f, ++seg)
				dl->AddLine(ImVec2(x, y + (seg % 2 ? 2.0f : 0.0f)), ImVec2((std::min)(x + 3.0f, x1), y + (seg % 2 ? 0.0f : 2.0f)), col, 1.0f);
		}
		if (hovered && io.MousePos.y >= ly && io.MousePos.y < ly + m_LineH && io.MousePos.x >= x0 - 4 && io.MousePos.x <= x1 + 4)
			hoverMessage += (hoverMessage.empty() ? "" : "\n") + mk.Message;
	}

	// 커서 (깜빡임) + 한글 IME 조합 창 위치
	if (m_Focused)
	{
		const float cx = originX + XOf(m_Cursor.Line, m_Cursor.Col);
		const float cy = originY + m_Cursor.Line * m_LineH;
		const bool blinkOn = fmodf((float)(ImGui::GetTime() - m_LastEditTime), 1.0f) < 0.6f;
		if (blinkOn)
			dl->AddRectFilled(ImVec2(floorf(cx), cy + 1), ImVec2(floorf(cx) + 2.0f, cy + m_LineH - 1), IM_COL32(230, 230, 230, 255));
		ImGuiContext& g = *GImGui;
		g.PlatformImeData.WantVisible = true;
		g.PlatformImeData.InputPos = ImVec2(cx, cy);
		g.PlatformImeData.InputLineHeight = m_LineH;
		g.PlatformImeViewport = ImGui::GetWindowViewport()->ID;
		// 다른 창의 단축키(씬 Undo, 도구 전환 등)가 "글자 입력 중" 으로 보고 쉬도록
		g.WantTextInputNextFrame = 1;
		ImGui::SetNextFrameWantCaptureKeyboard(true);
	}

	// 여백: 줄 번호 + 오류 점 (가로 스크롤과 무관하게 왼쪽 고정)
	dl->AddRectFilled(winPos, ImVec2(winPos.x + m_GutterW, winPos.y + winSize.y), kBg);
	const float textOffY = floorf((m_LineH - m_FontSize) * 0.5f);
	for (int l = first; l <= last; ++l)
	{
		const float y = originY + l * m_LineH + textOffY;
		char num[16];
		snprintf(num, sizeof(num), "%d", l + 1);
		const float w = m_Font->CalcTextSizeA(m_FontSize, FLT_MAX, 0.0f, num).x;
		dl->AddText(m_Font, m_FontSize, ImVec2(winPos.x + m_GutterW - 12.0f - w, y), l == m_Cursor.Line ? kGutterCur : kGutterNum, num);
	}
	for (const Marker& mk : m_Markers)
	{
		const int l = mk.Line - 1;
		if (l < first || l > last) continue;
		const float ly = originY + l * m_LineH;
		dl->AddCircleFilled(ImVec2(winPos.x + 10.0f, ly + m_LineH * 0.5f), 4.5f, mk.Error ? kErrorLine : kWarnLine);
		if (hovered && io.MousePos.x < winPos.x + m_GutterW && io.MousePos.y >= ly && io.MousePos.y < ly + m_LineH)
			hoverMessage += (hoverMessage.empty() ? "" : "\n") + mk.Message;
	}
	if (!hoverMessage.empty())
	{
		// 창 스타일의 투명한 팝업 배경 대신 VS Code 식 불투명 hover 상자
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.145f, 0.145f, 0.149f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.27f, 0.27f, 0.27f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
		ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
		ImGui::SetTooltip("%s", hoverMessage.c_str());
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(3);
	}

	// 자동 완성 목록 (커서 아래, 다른 창보다 위에)
	if (m_CompletionOpen && m_Focused && !m_Completions.empty())
	{
		ImDrawList* fg = ImGui::GetForegroundDrawList(ImGui::GetWindowViewport());
		const int total = (int)m_Completions.size();
		const int visible = (std::min)(10, total);
		const int top = std::clamp(m_CompletionIndex - visible / 2, 0, total - visible);
		float w = 200.0f;
		for (int i = top; i < top + visible; ++i)
			w = (std::max)(w, ImGui::CalcTextSize(m_Completions[i].Text.c_str()).x + 44.0f);
		const float rowH = ImGui::GetFontSize() + 5.0f;
		ImVec2 p(originX + XOf(m_CompletionStart.Line, m_CompletionStart.Col) - 26.0f, originY + (m_Cursor.Line + 1) * m_LineH + 2.0f);
		if (p.y + visible * rowH > winPos.y + winSize.y)
			p.y = originY + m_Cursor.Line * m_LineH - visible * rowH - 6.0f;   // 아래가 모자라면 위로
		fg->AddRectFilled(p, ImVec2(p.x + w, p.y + visible * rowH + 4), IM_COL32(37, 37, 38, 250), 3.0f);
		fg->AddRect(p, ImVec2(p.x + w, p.y + visible * rowH + 4), IM_COL32(69, 69, 69, 255), 3.0f);
		for (int i = top; i < top + visible; ++i)
		{
			const float y = p.y + 2 + (i - top) * rowH;
			if (i == m_CompletionIndex)
				fg->AddRectFilled(ImVec2(p.x + 2, y), ImVec2(p.x + w - 2, y + rowH), IM_COL32(4, 57, 94, 255), 2.0f);
			const CSharpLanguage::Completion& c = m_Completions[i];
			const char* icon = c.Kind == CSharpLanguage::CompletionKind::Type ? "T" : (c.Kind == CSharpLanguage::CompletionKind::Keyword ? "K" : "M");
			const ImU32 iconCol = ColorOf(c.Kind == CSharpLanguage::CompletionKind::Type ? TokenKind::Type : (c.Kind == CSharpLanguage::CompletionKind::Keyword ? TokenKind::Keyword : TokenKind::Method));
			fg->AddText(ImVec2(p.x + 9, y + 2), iconCol, icon);
			fg->AddText(ImVec2(p.x + 26, y + 2), kText, c.Text.c_str());
		}
	}
}

bool CodeEditor::Render(const char* id, float fontSize)
{
	ImGuiIO& io = ImGui::GetIO();
	m_Font = io.Fonts->Fonts.Size > 2 ? io.Fonts->Fonts[2] : ImGui::GetFont();   // Fonts[2] = 고정폭 코드 글꼴 (Consolas)
	m_FontSize = fontSize;
	m_LineH = floorf(fontSize * 1.4f);
	m_CharW = CharAdvance(' ');
	const unsigned long long versionBefore = m_Version;

	DrawFindBar();

	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(kBg));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNav);
	if (m_WantFocus)
	{
		ImGui::SetWindowFocus();
		m_WantFocus = false;
	}
	m_Focused = ImGui::IsWindowFocused();
	if (m_Focused)
		s_FocusFrame = ImGui::GetFrameCount();

	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 winPos = ImGui::GetWindowPos();
	const ImVec2 winSize = ImGui::GetWindowSize();
	const float scrollX = ImGui::GetScrollX(), scrollY = ImGui::GetScrollY();
	const int digits = (int)std::to_string(m_Lines.size()).size();
	m_GutterW = (std::max)(3, digits) * m_CharW + 34.0f;
	const float originX = winPos.x + m_GutterW - scrollX + 6.0f;
	const float originY = winPos.y - scrollY + 4.0f;

	if (m_Focused)
		HandleKeyboard();
	HandleMouse(originX, originY, m_GutterW);

	if (m_WidthDirty)
	{
		m_MaxWidth = 0.0f;
		for (int l = 0; l < (int)m_Lines.size(); ++l)
			m_MaxWidth = (std::max)(m_MaxWidth, XOf(l, (int)m_Lines[l].size()));
		m_WidthDirty = false;
	}

	const int first = (std::max)(0, (int)floorf((scrollY - 4.0f) / m_LineH));
	const int last = (std::min)((int)m_Lines.size() - 1, first + (int)(winSize.y / m_LineH) + 2);
	const Pos selA = SelStart(), selB = SelEnd();

	// 현재 줄
	if (!HasSelection())
	{
		const float y = originY + m_Cursor.Line * m_LineH;
		dl->AddRectFilled(ImVec2(winPos.x, y), ImVec2(winPos.x + winSize.x, y + m_LineH), kCurLine);
	}
	// 찾기 결과
	if (m_FindOpen && m_FindText[0])
	{
		const std::string needle = m_MatchCase ? std::string(m_FindText) : Lower(m_FindText);
		for (int l = first; l <= last; ++l)
		{
			const std::string hay = m_MatchCase ? m_Lines[l] : Lower(m_Lines[l]);
			for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + needle.size()))
			{
				const float y = originY + l * m_LineH;
				dl->AddRectFilled(ImVec2(originX + XOf(l, (int)p), y), ImVec2(originX + XOf(l, (int)(p + needle.size())), y + m_LineH), kFindMatch);
			}
		}
	}
	// 선택
	if (HasSelection())
		for (int l = (std::max)(first, selA.Line); l <= (std::min)(last, selB.Line); ++l)
		{
			const float y = originY + l * m_LineH;
			const float x0 = originX + (l == selA.Line ? XOf(l, selA.Col) : 0.0f);
			const float x1 = originX + (l == selB.Line ? XOf(l, selB.Col) : XOf(l, (int)m_Lines[l].size()) + m_CharW * 0.6f);
			dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + m_LineH), kSelection);
		}
	// 글자
	const float textOffY = floorf((m_LineH - m_FontSize) * 0.5f);
	for (int l = first; l <= last; ++l)
		DrawLine(dl, l, originX, originY + l * m_LineH + textOffY);

	DrawOverlays(dl, originX, originY, first, last);

	if (m_ScrollToCursor || m_GoToLineRequest >= 0)
	{
		EnsureCursorVisible();
		m_ScrollToCursor = false;
	}

	// 스크롤 영역 크기 (마지막 줄 아래로 반 화면 여유)
	ImGui::SetCursorPos(ImVec2(0, 0));
	ImGui::Dummy(ImVec2(m_GutterW + m_MaxWidth + 80.0f, m_Lines.size() * m_LineH + winSize.y * 0.5f));
	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
	return m_Version != versionBefore;
}
