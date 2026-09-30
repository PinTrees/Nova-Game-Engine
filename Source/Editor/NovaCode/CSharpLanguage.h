#pragma once
#include <string>
#include <vector>

// NOVA Code 의 C# 언어 규칙: 구문 강조용 토큰 분리, 블록 주석 상태, 자동 완성 후보.
// 편집 위젯(CodeEditor)은 글자 편집만 알고, 언어에 관한 것은 전부 여기에 둔다.
namespace CSharpLanguage
{
	enum class TokenKind { Text, Identifier, Keyword, Control, Type, Method, String, Number, Comment, Preprocessor };

	struct Token
	{
		int Begin = 0;   // 바이트 위치 [Begin, End)
		int End = 0;
		TokenKind Kind = TokenKind::Text;
	};

	// 한 줄을 토큰으로 나눈다 (공백은 토큰을 만들지 않음). inComment = 줄 시작이 /* */ 안인지
	void Tokenize(const std::string& line, bool inComment, std::vector<Token>& out);
	// 줄 끝에서 /* */ 안인지 (다음 줄의 inComment)
	bool EndsInBlockComment(const std::string& line, bool inComment);

	bool IsWordByte(unsigned char c);    // 식별자 글자 (영숫자, '_', UTF-8 바이트)
	bool IsIdentStart(unsigned char c);

	// 엔진 API (NovaScriptCore 리플렉션)
	struct ApiMember
	{
		std::string Name;
		std::string Type;   // 필드/속성/메서드 반환 타입 이름 (transform. → Transform 멤버를 보이려고)
	};
	struct ApiType
	{
		std::string Name;
		std::vector<ApiMember> Members;
	};
	void SetApi(std::vector<ApiType> types, const std::vector<std::string>& projectClasses);
	bool HasApi();

	enum class CompletionKind { Keyword, Type, Member };
	struct Completion
	{
		std::string Text;
		CompletionKind Kind = CompletionKind::Member;
	};
	// 자동 완성 후보
	//  prefix = 커서 앞에서 치는 중인 단어, member = '.' 뒤인지, owner = '.' 앞의 단어
	//  lines = 문서 전체 (변수 선언에서 owner 의 타입을 찾고, 문서 안 단어도 후보로)
	std::vector<Completion> Complete(const std::string& prefix, bool member, const std::string& owner,
		const std::vector<std::string>& lines, size_t maxCount = 60);
	CompletionKind KindOf(const std::string& word);
}
