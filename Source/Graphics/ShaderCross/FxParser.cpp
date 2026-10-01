#include "pch.h"
#include "FxParser.h"
#include <cctype>
#include <set>

namespace
{
	using namespace FxParser;

	bool IsIdent(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

	std::string Trim(const std::string& s)
	{
		size_t a = 0, b = s.size();
		while (a < b && std::isspace((unsigned char)s[a])) ++a;
		while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
		return s.substr(a, b - a);
	}

	std::string Lower(std::string s)
	{
		for (char& c : s) c = (char)std::tolower((unsigned char)c);
		return s;
	}

	// open = '{' / '(' 의 위치 → 짝이 맞는 닫는 괄호 위치 (없으면 npos)
	size_t Match(const std::string& s, size_t open)
	{
		const char o = s[open], c = o == '{' ? '}' : (o == '(' ? ')' : ']');
		int depth = 0;
		for (size_t i = open; i < s.size(); ++i)
		{
			if (s[i] == o) ++depth;
			else if (s[i] == c && --depth == 0) return i;
		}
		return std::string::npos;
	}

	size_t SkipSpace(const std::string& s, size_t i)
	{
		while (i < s.size())
		{
			if (std::isspace((unsigned char)s[i])) { ++i; continue; }
			// 전처리 결과의 #line 같은 줄
			if (s[i] == '#' && (i == 0 || s[i - 1] == '\n'))
			{
				while (i < s.size() && s[i] != '\n') ++i;
				continue;
			}
			break;
		}
		return i;
	}

	std::string ReadIdent(const std::string& s, size_t& i)
	{
		const size_t a = i;
		while (i < s.size() && IsIdent(s[i])) ++i;
		return s.substr(a, i - a);
	}

	// 최상위 쉼표로 나누기 (괄호 안 쉼표는 무시)
	std::vector<std::string> SplitArgs(const std::string& s)
	{
		std::vector<std::string> out;
		int depth = 0;
		size_t start = 0;
		for (size_t i = 0; i < s.size(); ++i)
		{
			if (s[i] == '(' || s[i] == '{' || s[i] == '[') ++depth;
			else if (s[i] == ')' || s[i] == '}' || s[i] == ']') --depth;
			else if (s[i] == ',' && depth == 0)
			{
				out.push_back(Trim(s.substr(start, i - start)));
				start = i + 1;
			}
		}
		const std::string last = Trim(s.substr(start));
		if (!last.empty() || !out.empty())
			out.push_back(last);
		return out;
	}

	// 최상위 ';' 로 문장 나누기
	std::vector<std::string> SplitStatements(const std::string& s)
	{
		std::vector<std::string> out;
		int depth = 0;
		size_t start = 0;
		for (size_t i = 0; i < s.size(); ++i)
		{
			if (s[i] == '(' || s[i] == '{' || s[i] == '[') ++depth;
			else if (s[i] == ')' || s[i] == '}' || s[i] == ']') --depth;
			else if (s[i] == ';' && depth == 0)
			{
				const std::string st = Trim(s.substr(start, i - start));
				if (!st.empty()) out.push_back(st);
				start = i + 1;
			}
		}
		const std::string last = Trim(s.substr(start));
		if (!last.empty()) out.push_back(last);
		return out;
	}

	bool StageFromSetter(const std::string& setter, Stage& stage)
	{
		static const std::pair<const char*, Stage> map[] = {
			{ "SetVertexShader", Stage::Vertex }, { "SetHullShader", Stage::Hull }, { "SetDomainShader", Stage::Domain },
			{ "SetGeometryShader", Stage::Geometry }, { "SetPixelShader", Stage::Pixel }, { "SetComputeShader", Stage::Compute } };
		for (auto& m : map)
			if (setter == m.first) { stage = m.second; return true; }
		return false;
	}

	struct FuncSig
	{
		std::string Return, ReturnSemantic, Attributes;
		std::vector<std::string> Params;   // 매개변수 글 그대로
	};

	// 최상위 함수 선언 찾기: [속성] 반환형 이름 ( 매개변수 ) [: 의미] {
	void ParamInfo(const std::string& param, bool& isUniform, std::string& name);

	// uniformCount >= 0 이면 uniform 매개변수가 그 수인 정의(오버로드)를 고른다. count = 찾은 정의 수
	bool FindFunction(const std::string& src, const std::string& name, FuncSig& sig, int uniformCount = -1, int* count = nullptr)
	{
		size_t pos = 0;
		bool found = false;
		if (count) *count = 0;
		while ((pos = src.find(name, pos)) != std::string::npos)
		{
			const size_t start = pos;
			pos += name.size();
			if ((start > 0 && IsIdent(src[start - 1])) || (pos < src.size() && IsIdent(src[pos])))
				continue;
			size_t p = SkipSpace(src, pos);
			if (p >= src.size() || src[p] != '(')
				continue;
			const size_t close = Match(src, p);
			if (close == std::string::npos)
				continue;
			size_t q = SkipSpace(src, close + 1);
			std::string semantic;
			if (q < src.size() && src[q] == ':')
			{
				q = SkipSpace(src, q + 1);
				semantic = ReadIdent(src, q);
				q = SkipSpace(src, q);
			}
			if (q >= src.size() || src[q] != '{')
				continue;   // 선언이 아니라 호출
			// 반환형: 이름 앞의 단어 (줄 시작까지 거슬러). 속성 [..] 은 그 앞 줄에
			size_t r = start;
			while (r > 0 && std::isspace((unsigned char)src[r - 1])) --r;
			size_t r0 = r;
			while (r0 > 0 && (IsIdent(src[r0 - 1]))) --r0;
			// 속성: 반환형 앞의 [ ... ] 들
			size_t a = r0;
			while (a > 0 && std::isspace((unsigned char)src[a - 1])) --a;
			std::string attrs;
			while (a > 0 && src[a - 1] == ']')
			{
				int depth = 0;
				size_t b = a - 1;
				for (; b > 0; --b)
				{
					if (src[b] == ']') ++depth;
					else if (src[b] == '[' && --depth == 0) break;
				}
				attrs = src.substr(b, a - b) + " " + attrs;
				a = b;
				while (a > 0 && std::isspace((unsigned char)src[a - 1])) --a;
			}
			FuncSig cand;
			cand.Return = src.substr(r0, r - r0);
			cand.Attributes = attrs;
			cand.ReturnSemantic = semantic;
			cand.Params = SplitArgs(src.substr(p + 1, close - p - 1));
			if (cand.Params.size() == 1 && cand.Params[0].empty())
				cand.Params.clear();
			if (count) ++*count;
			int uniforms = 0;
			for (const std::string& prm : cand.Params)
			{
				bool u;
				std::string n;
				ParamInfo(prm, u, n);
				uniforms += u ? 1 : 0;
			}
			if (!found && (uniformCount < 0 || uniforms == uniformCount))
			{
				sig = cand;
				found = true;
				if (!count) return true;
			}
		}
		return found;
	}

	// 매개변수 글 → (uniform 인지, 이름)
	void ParamInfo(const std::string& param, bool& isUniform, std::string& name)
	{
		std::string decl = param;
		const size_t colon = decl.find(':');
		if (colon != std::string::npos) decl = decl.substr(0, colon);
		const size_t eq = decl.find('=');
		if (eq != std::string::npos) decl = decl.substr(0, eq);
		decl = Trim(decl);
		// 배열 크기 [3] 떼기
		const size_t br = decl.find('[');
		if (br != std::string::npos) decl = Trim(decl.substr(0, br));
		size_t e = decl.size();
		size_t b = e;
		while (b > 0 && IsIdent(decl[b - 1])) --b;
		name = decl.substr(b, e - b);
		isUniform = (" " + decl + " ").find(" uniform ") != std::string::npos;
	}
}

namespace FxParser
{
	const char* StageName(Stage stage)
	{
		static const char* names[] = { "vertex", "hull", "domain", "geometry", "pixel", "compute" };
		return names[(int)stage];
	}

	bool Parse(const std::string& input, Effect& out, std::string& error)
	{
		std::string src = input;
		out = Effect();
		static const char* stateTypes[] = { "DepthStencilState", "RasterizerState", "BlendState", "SamplerState", "SamplerComparisonState" };

		// ---- 1) 최상위 효과 문법 걷어내기 (technique, 상태 블록, cbuffer 초기값)
		std::vector<std::pair<std::string, std::string>> techniqueBlocks;   // 이름, 본문
		int depth = 0;
		size_t i = 0;
		while (i < src.size())
		{
			const char c = src[i];
			if (c == '#' && (i == 0 || src[i - 1] == '\n'))
			{
				while (i < src.size() && src[i] != '\n') ++i;
				continue;
			}
			if (c == '{') { ++depth; ++i; continue; }
			if (c == '}') { --depth; ++i; continue; }
			if (depth != 0 || !IsIdent(c) || (i > 0 && IsIdent(src[i - 1])))
			{
				++i;
				continue;
			}
			const size_t wordStart = i;
			const std::string word = ReadIdent(src, i);

			if (word == "technique11" || word == "technique10" || word == "technique")
			{
				size_t p = SkipSpace(src, i);
				const std::string name = ReadIdent(src, p);
				p = SkipSpace(src, p);
				if (p >= src.size() || src[p] != '{') { error = "technique '" + name + "': '{' expected"; return false; }
				const size_t close = Match(src, p);
				if (close == std::string::npos) { error = "technique '" + name + "': unbalanced braces"; return false; }
				techniqueBlocks.push_back({ name, src.substr(p + 1, close - p - 1) });
				size_t endPos = close + 1;
				const size_t semi = SkipSpace(src, endPos);
				if (semi < src.size() && src[semi] == ';') endPos = semi + 1;
				src.erase(wordStart, endPos - wordStart);
				i = wordStart;
				continue;
			}

			bool isState = false;
			for (const char* t : stateTypes)
				if (word == t) isState = true;
			if (isState)
			{
				size_t p = SkipSpace(src, i);
				const size_t nameStart = p;
				const std::string name = ReadIdent(src, p);
				std::string arraySuffix;
				p = SkipSpace(src, p);
				if (p < src.size() && src[p] == '[')
				{
					const size_t close = Match(src, p);
					arraySuffix = src.substr(p, close - p + 1);
					p = SkipSpace(src, close + 1);
				}
				(void)nameStart;
				if (p < src.size() && src[p] == '{')
				{
					const size_t close = Match(src, p);
					StateBlock block;
					block.Type = word;
					for (const std::string& st : SplitStatements(src.substr(p + 1, close - p - 1)))
					{
						const size_t eq = st.find('=');
						if (eq == std::string::npos) continue;
						std::string key = Lower(Trim(st.substr(0, eq)));
						key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
						block.Fields[key] = Trim(st.substr(eq + 1));
					}
					out.States[name] = block;
					size_t endPos = close + 1;
					const size_t semi = SkipSpace(src, endPos);
					if (semi < src.size() && src[semi] == ';') endPos = semi + 1;
					// 샘플러는 HLSL 에 남기고(선언만), 나머지 상태는 지운다
					const std::string replacement = (word == "SamplerState" || word == "SamplerComparisonState") ? word + " " + name + arraySuffix + ";" : "";
					src.replace(wordStart, endPos - wordStart, replacement);
					i = wordStart + replacement.size();
				}
				else if (word == "SamplerState" || word == "SamplerComparisonState")
				{
					out.States[name].Type = word;   // 상태 없는 샘플러 선언
				}
				continue;
			}

			if (word == "shared")
			{
				src.erase(wordStart, i - wordStart);
				i = wordStart;
				continue;
			}

			if (word == "cbuffer" || word == "tbuffer")
			{
				size_t p = SkipSpace(src, i);
				ReadIdent(src, p);
				while (p < src.size() && src[p] != '{' && src[p] != ';') ++p;
				if (p >= src.size() || src[p] != '{') continue;
				const size_t close = Match(src, p);
				const std::string body = src.substr(p + 1, close - p - 1);
				// 문장마다 최상위 '=' 이 있으면 초기값을 떼어 기록
				std::string rebuilt, hoisted;
				int d = 0;
				size_t stStart = 0;
				for (size_t k = 0; k <= body.size(); ++k)
				{
					const char ch = k < body.size() ? body[k] : ';';
					if (ch == '(' || ch == '{' || ch == '[') ++d;
					else if (ch == ')' || ch == '}' || ch == ']') --d;
					if ((ch == ';' && d == 0) || k == body.size())
					{
						std::string st = body.substr(stStart, k - stStart);
						int dd = 0;
						size_t eqPos = std::string::npos;
						for (size_t m = 0; m < st.size(); ++m)
						{
							if (st[m] == '(' || st[m] == '{' || st[m] == '[') ++dd;
							else if (st[m] == ')' || st[m] == '}' || st[m] == ']') --dd;
							else if (st[m] == '=' && dd == 0) { eqPos = m; break; }
						}
						if (Trim(st).rfind("static", 0) == 0 && (Trim(st).size() == 6 || !IsIdent(Trim(st)[6])))
						{
							hoisted += Trim(st) + ";\n";
							stStart = k + 1;
							continue;
						}
						if (eqPos != std::string::npos)
						{
							std::string decl = Trim(st.substr(0, eqPos));
							bool uniform;
							std::string varName;
							ParamInfo(decl, uniform, varName);
							out.Defaults[varName] = Trim(st.substr(eqPos + 1));
							st = st.substr(0, eqPos);
						}
						rebuilt += st;
						if (k < body.size()) rebuilt += ';';
						stStart = k + 1;
					}
				}
				src.replace(p + 1, close - p - 1, rebuilt);
				i = p + 1 + rebuilt.size() + 1;
				if (!hoisted.empty())
				{
					src.insert(wordStart, hoisted);
					i += hoisted.size();
				}
				continue;
			}
		}

		// ---- 2) technique 본문 → pass → 셰이더·상태
		std::map<std::string, std::string> wrappers;   // "PS(3, true)" → 만든 함수 이름
		std::string wrapperCode;
		for (const auto& [techName, body] : techniqueBlocks)
		{
			Technique tech;
			tech.Name = techName;
			size_t p = 0;
			while ((p = body.find("pass", p)) != std::string::npos)
			{
				if ((p > 0 && IsIdent(body[p - 1])) || (p + 4 < body.size() && IsIdent(body[p + 4]))) { p += 4; continue; }
				size_t q = SkipSpace(body, p + 4);
				Pass pass;
				pass.Name = ReadIdent(body, q);
				q = SkipSpace(body, q);
				if (q >= body.size() || body[q] != '{') { p = q; continue; }
				const size_t close = Match(body, q);
				for (const std::string& st : SplitStatements(body.substr(q + 1, close - q - 1)))
				{
					const size_t paren = st.find('(');
					if (paren == std::string::npos) continue;
					const std::string fn = Trim(st.substr(0, paren));
					const size_t pclose = Match(st, paren);
					if (pclose == std::string::npos) continue;
					const std::vector<std::string> args = SplitArgs(st.substr(paren + 1, pclose - paren - 1));
					Stage stage;
					if (StageFromSetter(fn, stage))
					{
						if (args.empty() || args[0] == "NULL") continue;
						const std::string& a0 = args[0];
						const size_t cp = a0.find("CompileShader");
						if (cp == std::string::npos) { out.Warnings.push_back(tech.Name + "/" + pass.Name + ": unsupported shader expression " + a0); continue; }
						const size_t cpo = a0.find('(', cp);
						const std::vector<std::string> cargs = SplitArgs(a0.substr(cpo + 1, Match(a0, cpo) - cpo - 1));
						if (cargs.size() < 2) continue;
						ShaderRef ref;
						ref.StageType = stage;
						ref.Profile = cargs[0];
						const std::string call = cargs[1];
						const size_t eo = call.find('(');
						ref.Original = Trim(call.substr(0, eo));
						std::vector<std::string> callArgs = eo == std::string::npos ? std::vector<std::string>() : SplitArgs(call.substr(eo + 1, Match(call, eo) - eo - 1));
						if (callArgs.size() == 1 && callArgs[0].empty()) callArgs.clear();
						int definitions = 0;
						{
							FuncSig probe;
							FindFunction(src, ref.Original, probe, -1, &definitions);
						}
						if (callArgs.empty() && definitions <= 1)
							ref.Entry = ref.Original;
						else
						{
							std::string key = ref.Original + "(";
							for (auto& a : callArgs) key += a + ",";
							auto it = wrappers.find(key);
							if (it != wrappers.end())
								ref.Entry = it->second;
							else
							{
								FuncSig sig;
								if (!FindFunction(src, ref.Original, sig, definitions > 1 ? (int)callArgs.size() : -1))
								{
									error = "entry point '" + ref.Original + "(" + std::to_string(callArgs.size()) + " uniform args)' not found";
									return false;
								}
								const std::string wname = "__nova_" + std::to_string(wrappers.size()) + "_" + ref.Original;
								std::string params, callList;
								size_t argIndex = 0;
								for (const std::string& prm : sig.Params)
								{
									bool uniform;
									std::string pname;
									ParamInfo(prm, uniform, pname);
									if (!callList.empty()) callList += ", ";
									if (uniform && argIndex < callArgs.size())
										callList += callArgs[argIndex++];
									else
									{
										if (!params.empty()) params += ", ";
										params += prm;
										callList += pname;
									}
								}
								wrapperCode += "\n" + sig.Attributes + " " + sig.Return + " " + wname + "(" + params + ")" + (sig.ReturnSemantic.empty() ? "" : " : " + sig.ReturnSemantic) +
									"\n{\n    " + (sig.Return == "void" ? "" : "return ") + ref.Original + "(" + callList + ");\n}\n";
								wrappers[key] = wname;
								ref.Entry = wname;
							}
						}
						pass.Shaders.push_back(ref);
					}
					else if (fn == "SetDepthStencilState" && !args.empty())
					{
						pass.DepthStencilState = args[0] == "NULL" ? "" : args[0];
						if (args.size() > 1) pass.StencilRef = atoi(args[1].c_str());
					}
					else if (fn == "SetRasterizerState" && !args.empty())
						pass.RasterizerState = args[0] == "NULL" ? "" : args[0];
					else if (fn == "SetBlendState" && !args.empty())
					{
						pass.BlendState = args[0] == "NULL" ? "" : args[0];
						if (args.size() > 1)
						{
							const size_t bo = args[1].find('(');
							if (bo != std::string::npos)
							{
								const auto f = SplitArgs(args[1].substr(bo + 1, Match(args[1], bo) - bo - 1));
								for (size_t k = 0; k < f.size() && k < 4; ++k) pass.BlendFactor[k] = (float)atof(f[k].c_str());
							}
						}
						if (args.size() > 2) pass.SampleMask = (unsigned)strtoul(args[2].c_str(), nullptr, 0);
					}
				}
				tech.Passes.push_back(pass);
				p = close + 1;
			}
			out.Techniques.push_back(tech);
		}
		// ---- 3) tex.Length (옛 문법, DXC 미지원) → __nova_len_tex() (GetDimensions)
		{
			std::string lengthHelpers;
			std::set<std::string> done;
			size_t p = 0;
			while ((p = src.find(".Length", p)) != std::string::npos)
			{
				size_t b = p;
				while (b > 0 && IsIdent(src[b - 1])) --b;
				const std::string var = src.substr(b, p - b);
				if (var.empty() || (p + 7 < src.size() && IsIdent(src[p + 7]))) { p += 7; continue; }
				// 선언 찾기: Texture2D / RWTexture2D (<..>)? var
				int dims = 0;
				for (const char* t : { "Texture2D", "RWTexture2D", "Texture1D", "RWTexture1D", "Texture3D", "RWTexture3D" })
				{
					const std::string ty = t;
					size_t d = 0;
					while ((d = src.find(ty, d)) != std::string::npos)
					{
						if (d > 0 && IsIdent(src[d - 1])) { d += ty.size(); continue; }
						size_t q = d + ty.size();
						if (q < src.size() && src[q] == '<') q = src.find('>', q) + 1;
						else if (q < src.size() && IsIdent(src[q])) { d = q; continue; }
						q = SkipSpace(src, q);
						size_t q2 = q;
						if (ReadIdent(src, q2) == var) { dims = ty.find("1D") != std::string::npos ? 1 : ty.find("3D") != std::string::npos ? 3 : 2; break; }
						d = q;
					}
					if (dims) break;
				}
				if (!dims) { p += 7; continue; }
				const std::string fn = "__nova_len_" + var;
				if (done.insert(var).second)
				{
					if (dims == 1)
						lengthHelpers += "uint " + fn + "() { uint w; " + var + ".GetDimensions(w); return w; }\n";
					else if (dims == 2)
						lengthHelpers += "uint2 " + fn + "() { uint w, h; " + var + ".GetDimensions(w, h); return uint2(w, h); }\n";
					else
						lengthHelpers += "uint3 " + fn + "() { uint w, h, d; " + var + ".GetDimensions(w, h, d); return uint3(w, h, d); }\n";
				}
				src.replace(b, p + 7 - b, fn + "()");
				p = b + fn.size() + 2;
			}
			if (!lengthHelpers.empty())
			{
				// 첫 함수 정의(진입점보다 앞) 자리: 마지막 텍스처 선언 뒤면 충분 → 첫 '[' 속성 또는 첫 사용 위치 앞의 줄 시작
				const size_t first = src.find("__nova_len_");
				size_t lineStart = src.rfind('\n', first);
				// 사용하는 함수 정의의 시작까지 거슬러 올라간다 (최상위 깊이)
				int d = 0;
				size_t top = 0;
				for (size_t k = 0; k < first; ++k)
				{
					if (src[k] == '{') { if (d == 0) top = k; ++d; }
					else if (src[k] == '}') --d;
				}
				if (d > 0)
				{
					// top = 사용하는 함수 본문 '{'. 그 앞의 마지막 ';' 또는 '}' 다음 줄로
					size_t s = top;
					while (s > 0 && src[s - 1] != ';' && src[s - 1] != '}') --s;
					lineStart = s;
				}
				src.insert(lineStart == std::string::npos ? 0 : lineStart, "\n" + lengthHelpers);
			}
		}

		out.Source = src + wrapperCode;
		return true;
	}
}
