#include "pch.h"
#include "CSharpLanguage.h"
#include <unordered_set>
#include <unordered_map>

namespace
{
	const char* kKeywords[] = {
		"abstract", "as", "base", "bool", "byte", "char", "checked", "class", "const", "decimal", "default", "delegate",
		"double", "enum", "event", "explicit", "extern", "false", "fixed", "float", "implicit", "in", "int", "interface",
		"internal", "is", "lock", "long", "namespace", "new", "null", "object", "operator", "out", "override", "params",
		"private", "protected", "public", "readonly", "ref", "sbyte", "sealed", "short", "sizeof", "stackalloc", "static",
		"string", "struct", "this", "true", "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort", "using", "virtual",
		"void", "volatile", "var", "async", "get", "set", "value", "nameof", "where", "record", "init", "dynamic", "partial" };
	const char* kControlWords[] = {
		"if", "else", "for", "foreach", "while", "do", "switch", "case", "break", "continue", "return", "yield", "try",
		"catch", "finally", "throw", "goto", "await", "when" };
	const char* kCommonTypes[] = {
		"List", "Dictionary", "HashSet", "Queue", "Stack", "IEnumerator", "IEnumerable", "Action", "Func", "Math", "MathF",
		"String", "Exception", "Array", "Enum", "DateTime", "TimeSpan", "StringBuilder", "Tuple", "Type", "Console", "System" };
	// Unity 식 메시지 메서드 (MonoBehaviour 안에서 바로 쓰는 이름)
	const char* kMessages[] = {
		"Awake", "Start", "Update", "LateUpdate", "FixedUpdate", "OnEnable", "OnDisable", "OnDestroy",
		"OnCollisionEnter", "OnCollisionStay", "OnCollisionExit", "OnTriggerEnter", "OnTriggerStay", "OnTriggerExit" };

	std::unordered_set<std::string> s_Keywords, s_Control, s_Types;
	std::unordered_map<std::string, std::vector<CSharpLanguage::ApiMember>> s_Members;   // 타입 → 멤버
	std::unordered_map<std::string, std::string> s_MemberType;                          // 멤버 이름 → 타입 (처음 본 것)
	std::vector<std::string> s_AllMembers, s_TypeNames;
	bool s_HasApi = false;

	void InitWords()
	{
		if (!s_Keywords.empty())
			return;
		for (const char* k : kKeywords) s_Keywords.insert(k);
		for (const char* k : kControlWords) s_Control.insert(k);
		for (const char* k : kCommonTypes) s_Types.insert(k);
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

	// "Type name" / "Type name =" / "Type name;" 선언에서 name 의 타입
	std::string DeclaredType(const std::string& name, const std::vector<std::string>& lines)
	{
		using CSharpLanguage::IsWordByte;
		for (const std::string& s : lines)
		{
			for (size_t at = s.find(name); at != std::string::npos; at = s.find(name, at + 1))
			{
				const size_t end = at + name.size();
				if ((at > 0 && IsWordByte((unsigned char)s[at - 1])) || (end < s.size() && IsWordByte((unsigned char)s[end])))
					continue;
				size_t k = end;
				while (k < s.size() && s[k] == ' ') ++k;
				if (k < s.size() && s[k] != '=' && s[k] != ';' && s[k] != ',' && s[k] != ')')
					continue;
				// 앞 단어 (제네릭/배열 표기는 떼고)
				int e = (int)at - 1;
				while (e >= 0 && s[e] == ' ') --e;
				while (e >= 0 && (s[e] == ']' || s[e] == '[' || s[e] == '>')) --e;
				int b = e;
				while (b >= 0 && IsWordByte((unsigned char)s[b])) --b;
				if (e > b)
				{
					const std::string type = s.substr(b + 1, e - b);
					if (type != "var" && type != "return" && type != "new")
						return type;
				}
			}
		}
		return std::string();
	}
}

namespace CSharpLanguage
{
	bool IsWordByte(unsigned char c) { return isalnum(c) || c == '_' || c >= 0x80; }
	bool IsIdentStart(unsigned char c) { return isalpha(c) || c == '_' || c >= 0x80; }

	bool EndsInBlockComment(const std::string& s, bool inComment)
	{
		for (size_t i = 0; i < s.size(); ++i)
		{
			if (inComment)
			{
				if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '/') { inComment = false; ++i; }
				continue;
			}
			if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') return false;
			if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') { inComment = true; ++i; continue; }
			if (s[i] == '"')
			{
				// 문자열 안의 /* 는 주석이 아니다
				const bool verbatim = i > 0 && s[i - 1] == '@';
				for (++i; i < s.size(); ++i)
				{
					if (!verbatim && s[i] == '\\') { ++i; continue; }
					if (s[i] == '"') break;
				}
			}
			else if (s[i] == '\'')
			{
				for (++i; i < s.size(); ++i)
				{
					if (s[i] == '\\') { ++i; continue; }
					if (s[i] == '\'') break;
				}
			}
		}
		return inComment;
	}

	void Tokenize(const std::string& s, bool inComment, std::vector<Token>& out)
	{
		InitWords();
		out.clear();
		const int n = (int)s.size();
		const int firstNonSpace = LeadingSpaces(s);
		auto push = [&](int a, int b, TokenKind k) { if (b > a) out.push_back({ a, b, k }); };
		int i = 0;
		while (i < n)
		{
			if (inComment)
			{
				const size_t end = s.find("*/", i);
				const int e = end == std::string::npos ? n : (int)end + 2;
				push(i, e, TokenKind::Comment);
				i = e;
				inComment = end == std::string::npos;
				continue;
			}
			const unsigned char c = (unsigned char)s[i];
			const char next = i + 1 < n ? s[i + 1] : 0;
			if (c == ' ' || c == '\t') { ++i; continue; }
			if (c == '/' && next == '/') { push(i, n, TokenKind::Comment); break; }
			if (c == '/' && next == '*') { inComment = true; continue; }
			if (c == '#' && i == firstNonSpace) { push(i, n, TokenKind::Preprocessor); break; }
			// 문자열: "..", @"..", $"..", $@"..", @$".."
			if (c == '"' || ((c == '@' || c == '$') && (next == '"' || next == '@' || next == '$')))
			{
				int k = i;
				bool verbatim = false;
				while (k < n && s[k] != '"') { if (s[k] == '@') verbatim = true; ++k; }
				for (++k; k < n; ++k)
				{
					if (!verbatim && s[k] == '\\') { ++k; continue; }
					if (s[k] == '"') { ++k; break; }
				}
				k = (std::min)(k, n);
				push(i, k, TokenKind::String);
				i = k;
				continue;
			}
			if (c == '\'')
			{
				int k = i + 1;
				for (; k < n; ++k)
				{
					if (s[k] == '\\') { ++k; continue; }
					if (s[k] == '\'') { ++k; break; }
				}
				k = (std::min)(k, n);
				push(i, k, TokenKind::String);
				i = k;
				continue;
			}
			if (isdigit(c) || (c == '.' && isdigit((unsigned char)next)))
			{
				int k = i;
				while (k < n && (isalnum((unsigned char)s[k]) || s[k] == '.' || s[k] == '_')) ++k;
				push(i, k, TokenKind::Number);
				i = k;
				continue;
			}
			if (IsIdentStart(c))
			{
				int k = i;
				while (k < n && IsWordByte((unsigned char)s[k])) ++k;
				const std::string word = s.substr(i, k - i);
				int after = k;
				while (after < n && s[after] == ' ') ++after;
				int before = i - 1;
				while (before >= 0 && s[before] == ' ') --before;
				TokenKind kind = TokenKind::Identifier;
				if (s_Control.count(word)) kind = TokenKind::Control;
				else if (s_Keywords.count(word)) kind = TokenKind::Keyword;
				else if (after < n && s[after] == '(') kind = TokenKind::Method;
				else if (s_Types.count(word)) kind = TokenKind::Type;
				else if (before >= 0 && s[before] == '[') kind = TokenKind::Type;   // [SerializeField] 등 속성
				else if (after < n && s[after] == '<' && isupper((unsigned char)word[0])) kind = TokenKind::Type;   // GetComponent<T> 가 아닌 List<T>
				push(i, k, kind);
				i = k;
				continue;
			}
			// 기호: 다음 단어/공백/문자열/주석 전까지 한 덩어리
			int k = i + 1;
			while (k < n && !IsWordByte((unsigned char)s[k]) && s[k] != ' ' && s[k] != '\t' && s[k] != '"' && s[k] != '\'' && s[k] != '/' && s[k] != '@' && s[k] != '$') ++k;
			push(i, k, TokenKind::Text);
			i = k;
		}
	}

	void SetApi(std::vector<ApiType> types, const std::vector<std::string>& projectClasses)
	{
		InitWords();
		s_Members.clear();
		s_MemberType.clear();
		s_TypeNames.clear();
		std::unordered_set<std::string> allMembers;
		for (ApiType& t : types)
		{
			s_Types.insert(t.Name);
			s_TypeNames.push_back(t.Name);
			for (const ApiMember& m : t.Members)
			{
				allMembers.insert(m.Name);
				if (!m.Type.empty())
					s_MemberType.emplace(m.Name, m.Type);
			}
			s_Members[t.Name] = std::move(t.Members);
		}
		for (const std::string& c : projectClasses)
		{
			s_Types.insert(c);
			s_TypeNames.push_back(c);
		}
		s_AllMembers.assign(allMembers.begin(), allMembers.end());
		std::sort(s_AllMembers.begin(), s_AllMembers.end());
		s_HasApi = !types.empty();
	}

	bool HasApi() { return s_HasApi; }

	CompletionKind KindOf(const std::string& word)
	{
		InitWords();
		if (s_Keywords.count(word) || s_Control.count(word)) return CompletionKind::Keyword;
		if (s_Types.count(word)) return CompletionKind::Type;
		return CompletionKind::Member;
	}

	std::vector<Completion> Complete(const std::string& prefix, bool member, const std::string& owner,
		const std::vector<std::string>& lines, size_t maxCount)
	{
		InitWords();
		std::vector<std::string> pool;
		if (member)
		{
			// owner 의 타입: 타입 이름 그 자체 → 문서의 변수 선언 → 엔진 멤버(transform 등)의 타입 → 모름(전체 멤버)
			std::string type = s_Members.count(owner) ? owner : std::string();
			if (type.empty()) type = DeclaredType(owner, lines);
			if (type.empty() || !s_Members.count(type))
			{
				auto it = s_MemberType.find(owner);
				if (it != s_MemberType.end()) type = it->second;
			}
			auto it = s_Members.find(type);
			if (it != s_Members.end())
				for (const ApiMember& m : it->second) pool.push_back(m.Name);
			else
				pool = s_AllMembers;
		}
		else
		{
			for (const char* k : kKeywords) pool.push_back(k);
			for (const char* k : kControlWords) pool.push_back(k);
			for (const char* k : kMessages) pool.push_back(k);
			pool.insert(pool.end(), s_TypeNames.begin(), s_TypeNames.end());
			for (const char* k : kCommonTypes) pool.push_back(k);
			// MonoBehaviour 안이면 상속 멤버(transform, gameObject, GetComponent ...)도 바로 쓴다
			auto mb = s_Members.find("MonoBehaviour");
			if (mb != s_Members.end())
				for (const ApiMember& m : mb->second) pool.push_back(m.Name);
			// 문서 안의 단어
			for (const std::string& line : lines)
				for (size_t i = 0; i < line.size();)
				{
					if (IsIdentStart((unsigned char)line[i]) && (i == 0 || !IsWordByte((unsigned char)line[i - 1])))
					{
						size_t k = i;
						while (k < line.size() && IsWordByte((unsigned char)line[k])) ++k;
						if (k - i >= 3) pool.push_back(line.substr(i, k - i));
						i = k;
					}
					else ++i;
				}
		}

		// 앞부분이 같은 것 먼저, 그다음 가운데에 들어 있는 것
		const std::string lp = Lower(prefix);
		std::vector<std::string> starts, contains;
		std::unordered_set<std::string> seen;
		for (const std::string& w : pool)
		{
			if (w == prefix || !seen.insert(w).second)
				continue;
			const std::string lw = Lower(w);
			if (lw.compare(0, lp.size(), lp) == 0) starts.push_back(w);
			else if (!lp.empty() && lw.find(lp) != std::string::npos) contains.push_back(w);
		}
		// 대소문자까지 맞는 것을 앞으로
		std::stable_sort(starts.begin(), starts.end(), [&](const std::string& a, const std::string& b) {
			const bool ea = a.compare(0, prefix.size(), prefix) == 0, eb = b.compare(0, prefix.size(), prefix) == 0;
			if (ea != eb) return ea;
			return a < b;
		});
		std::sort(contains.begin(), contains.end());
		std::vector<Completion> out;
		for (const std::vector<std::string>* list : { &starts, &contains })
			for (const std::string& w : *list)
			{
				if (out.size() >= maxCount) break;
				out.push_back({ w, member ? CompletionKind::Member : KindOf(w) });
			}
		return out;
	}
}
