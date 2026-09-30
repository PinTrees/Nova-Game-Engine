#pragma once
#include <string>
#include <vector>
#include "CSharpLanguage.h"

struct ImDrawList;
struct ImFont;

// NOVA Code 의 코드 편집 위젯 (ImGui 로 직접 그림, 외부 라이브러리 없음).
//  - 구문 강조(VS Code Dark+ 색, 언어 규칙은 CSharpLanguage), 줄 번호, 현재 줄 표시, 탭은 4칸으로 표시
//  - 커서/선택(마우스 클릭·드래그·더블클릭 단어·Shift), Ctrl+화살표 단어 이동, Home(스마트)/End, PageUp/Down
//  - 입력: 자동 들여쓰기, '}' 자동 내어쓰기, 괄호·따옴표 자동 닫기, Tab/Shift+Tab(선택 들여쓰기), Ctrl+/ 주석, Ctrl+D 줄 복제
//  - Ctrl+Z/Y 실행 취소, Ctrl+C/X/V, Ctrl+A, Ctrl+F 찾기, Ctrl+H 바꾸기, Ctrl+G 줄로 이동
//  - 자동 완성(Ctrl+Space 또는 입력 중): C# 키워드 + 엔진 API + 문서 안의 단어, '.' 뒤는 그 타입의 멤버
//  - 컴파일 오류 표시: 빨간 물결 밑줄 + 여백 아이콘 + 마우스를 올리면 메시지
class CodeEditor
{
public:
	struct Marker
	{
		int Line = 0;          // 1부터
		int Column = 0;        // 1부터 (0 = 줄 전체)
		std::string Message;
		bool Error = true;
	};

	CodeEditor();

	void SetText(const std::string& text);
	std::string GetText() const;
	bool IsDirty() const { return m_Version != m_SavedVersion; }
	void MarkSaved() { m_SavedVersion = m_Version; }
	bool UsesCRLF() const { return m_CRLF; }

	void SetMarkers(std::vector<Marker> markers) { m_Markers = std::move(markers); }
	const std::vector<Marker>& Markers() const { return m_Markers; }
	void GoToLine(int line1, int column1 = 1);
	int CursorLine() const { return m_Cursor.Line + 1; }
	int CursorColumn() const;
	int LineCount() const { return (int)m_Lines.size(); }

	// 그린다. 글자가 바뀐 프레임이면 true
	bool Render(const char* id, float fontSize);

	void Undo();
	void Redo();
	bool CanUndo() const { return !m_UndoStack.empty(); }
	bool CanRedo() const { return !m_RedoStack.empty(); }
	void OpenFind(bool replace);
	void OpenGoToLine();
	void Focus() { m_WantFocus = true; }
	bool HasFocus() const { return m_Focused; }
	int TabSize = 4;
	bool AutoComplete = true;        // 입력 중 자동 완성 (Ctrl+Space 는 항상)
	bool AutoCloseBrackets = true;   // ( [ { " 를 치면 닫는 것도

	// 에디터 전역 단축키(씬 Undo, Ctrl+S 씬 저장 등)가 코드 입력 중에 동작하지 않도록
	static bool IsAnyEditorFocused();

private:
	struct Pos
	{
		int Line = 0;
		int Col = 0;   // 바이트 위치 (UTF-8 경계)
		bool operator==(const Pos& o) const { return Line == o.Line && Col == o.Col; }
		bool operator!=(const Pos& o) const { return !(*this == o); }
		bool operator<(const Pos& o) const { return Line < o.Line || (Line == o.Line && Col < o.Col); }
	};
	struct Snapshot { std::vector<std::string> Lines; Pos Cursor, Anchor; };
	enum class EditKind { None, Typing, Deleting, Other };

	// 편집
	void Insert(const std::string& text);
	void DeleteSelection();
	void Backspace(bool word);
	void DeleteForward(bool word);
	void NewLine();
	void Indent(bool outdent);
	void ToggleComment();
	void DuplicateLine();
	void CutOrCopy(bool cut);
	void Paste();
	void SelectAll();
	void BeginEdit(EditKind kind);
	void Changed();
	bool HasSelection() const { return m_Cursor != m_Anchor; }
	Pos SelStart() const { return m_Cursor < m_Anchor ? m_Cursor : m_Anchor; }
	Pos SelEnd() const { return m_Cursor < m_Anchor ? m_Anchor : m_Cursor; }
	std::string SelectedText() const;
	void SelectWordAt(Pos p);

	// 이동
	Pos Clamp(Pos p) const;
	Pos Left(Pos p, bool word) const;
	Pos Right(Pos p, bool word) const;
	void MoveTo(Pos p, bool select);

	// 좌표
	float XOf(int line, int col) const;
	int ColOfX(int line, float x) const;
	float CharAdvance(unsigned int c) const;

	// 구문 강조
	void RebuildLineStates();
	void DrawLine(ImDrawList* dl, int line, float x, float y);

	// 자동 완성 / 찾기
	void UpdateCompletion(bool force);
	void AcceptCompletion();
	void FindNext(bool backwards);
	void ReplaceOne();
	void ReplaceAll();
	int CountMatches() const;
	void DrawFindBar();

	void HandleKeyboard();
	void HandleMouse(float originX, float originY, float gutterW);
	void DrawOverlays(ImDrawList* dl, float originX, float originY, int first, int last);
	void EnsureCursorVisible();

	std::vector<std::string> m_Lines = { std::string() };
	std::vector<unsigned char> m_LineStartInComment;   // 줄 시작이 /* */ 안인지
	Pos m_Cursor, m_Anchor;
	float m_PreferredX = -1.0f;
	bool m_CRLF = true;
	unsigned long long m_Version = 0, m_SavedVersion = 0;
	std::vector<Snapshot> m_UndoStack, m_RedoStack;
	EditKind m_LastEdit = EditKind::None;
	double m_LastEditTime = 0.0;
	std::vector<Marker> m_Markers;

	// 그리기 상태
	ImFont* m_Font = nullptr;
	float m_FontSize = 14.0f;
	float m_LineH = 18.0f;
	float m_CharW = 8.0f;
	float m_MaxWidth = 0.0f;
	bool m_WidthDirty = true;
	bool m_Focused = false;
	bool m_WantFocus = false;
	bool m_ScrollToCursor = false;
	int m_GoToLineRequest = -1;
	bool m_Selecting = false;
	float m_GutterW = 0;
	std::vector<CSharpLanguage::Token> m_Tokens;   // 줄 그리기용 (재사용)

	// 자동 완성
	bool m_CompletionOpen = false;
	std::vector<CSharpLanguage::Completion> m_Completions;
	int m_CompletionIndex = 0;
	Pos m_CompletionStart;

	// 찾기 / 바꾸기 / 줄 이동
	bool m_FindOpen = false, m_ReplaceOpen = false, m_FindFocus = false, m_MatchCase = false;
	char m_FindText[256] = {};
	char m_ReplaceText[256] = {};
	bool m_GotoOpen = false, m_GotoFocus = false;
	char m_GotoText[16] = {};
};
