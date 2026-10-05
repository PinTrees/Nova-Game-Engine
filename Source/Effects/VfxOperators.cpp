#include "pch.h"
#include "VfxAsset.h"
#include <functional>
#include <set>

// 연산 노드 (Unity VFX Graph 의 Operator): 정의표 · JSON · 스택 명령으로 바꾸기.
//  명령 = float4 (번호, a, b, c). 번호는 58. VFX.fx 의 Eval 과 같다. 값은 늘 float4, 스칼라는 네 칸에 같은 값
namespace Vfx
{
	namespace
	{
		using K = ParamKind;
		using F4 = std::array<float, 4>;

		std::string Simplify(const std::string& s)
		{
			std::string o;
			for (char c : s)
				if (isalnum((unsigned char)c))
					o += (char)tolower((unsigned char)c);
			return o;
		}

		// 명령 번호 (58. VFX.fx 의 Eval)
		enum Op
		{
			OpConst = 1, OpTime = 2, OpDeltaTime = 3, OpAge = 4, OpLifetime = 5, OpAge01 = 6, OpPosition = 7, OpVelocity = 8, OpColor = 9,
			OpSize = 10, OpSpeed = 11, OpRandParticle = 12, OpRandFrame = 13,
			OpAdd = 20, OpSub = 21, OpMul = 22, OpDiv = 23, OpMin = 24, OpMax = 25, OpPow = 26, OpStep = 27, OpDot = 28, OpCross = 29, OpDistance = 30, OpMod = 31,
			OpLerp = 40, OpClamp = 41, OpSmoothstep = 42, OpRemap = 43,
			OpAbs = 50, OpSin = 51, OpCos = 52, OpFrac = 53, OpSaturate = 54, OpOneMinus = 55, OpNegate = 56, OpLength = 57, OpNormalize = 58,
			OpFloor = 59, OpSqrt = 60, OpRound = 61, OpHsv = 62,
			OpSplit = 70, OpCombine = 71,
			OpCurve = 80, OpGradient = 81, OpNoise = 82, OpNoiseVec = 83,
		};
		constexpr int kMaxStack = 10;   // 58. VFX.fx 의 Eval 스택 크기

		std::vector<OperatorDesc> MakeOperators()
		{
			const F4 zero = { 0, 0, 0, 0 }, one = { 1, 1, 1, 1 };
			const char* comps = "X|Y|Z|W";
			return {
				// ---------------------------------------------------------------- Inputs
				{ "Float", "Float", "Inputs", { { "Value", K::Float, { 1 } } }, {}, K::Float, "상수 (Inline Float)" },
				{ "Vector3", "Vector3", "Inputs", { { "Value", K::Vector3, { 0, 1, 0, 0 } } }, {}, K::Vector3, "상수 (Inline Vector3)" },
				{ "Color", "Color", "Inputs", { { "Value", K::Color, { 1, 1, 1, 1 } } }, {}, K::Color, "상수 색 (HDR)" },
				{ "Property", "Property (Blackboard)", "Inputs", {}, { { "Name", K::Text, {}, 0, 0, nullptr, "Blackboard 속성 이름" } }, K::Float, "Exposed Property 값 (Visual Effect 의 덮어쓰기 포함)" },
				{ "Time", "Total Time", "Inputs", {}, {}, K::Float, "Visual Effect 가 시작한 뒤 지난 시간 (초)" },
				{ "DeltaTime", "Delta Time", "Inputs", {}, {}, K::Float, "이번 프레임 시간 (초)" },
				// ---------------------------------------------------------------- Attributes (지금 파티클)
				{ "Age", "Get Age", "Attributes", {}, {}, K::Float, "태어난 뒤 지난 시간 (초)" },
				{ "Lifetime", "Get Lifetime", "Attributes", {}, {}, K::Float, "수명 (초)" },
				{ "AgeOverLifetime", "Age over Lifetime", "Attributes", {}, {}, K::Float, "나이 / 수명 (0 → 1)" },
				{ "Position", "Get Position", "Attributes", {}, {}, K::Vector3, "자리 (시뮬레이션 공간)" },
				{ "Velocity", "Get Velocity", "Attributes", {}, {}, K::Vector3, "속도" },
				{ "ParticleColor", "Get Color", "Attributes", {}, {}, K::Color, "지금 색" },
				{ "Size", "Get Size", "Attributes", {}, {}, K::Float, "지금 크기" },
				{ "Speed", "Get Speed", "Attributes", {}, {}, K::Float, "속력 (속도의 길이)" },
				// ---------------------------------------------------------------- Random
				{ "RandomPerParticle", "Random Number (per Particle)", "Random", { { "Min", K::Float, { 0 } }, { "Max", K::Float, { 1 } } }, {}, K::Float, "파티클마다 고정된 무작위 값 (Min..Max)" },
				{ "RandomPerFrame", "Random Number (per Frame)", "Random", { { "Min", K::Float, { 0 } }, { "Max", K::Float, { 1 } } }, {}, K::Float, "프레임마다 바뀌는 무작위 값 (Min..Max)" },
				// ---------------------------------------------------------------- Math
				{ "Add", "Add", "Math", { { "A", K::Float, zero }, { "B", K::Float, zero } }, {}, K::Float, "A + B" },
				{ "Subtract", "Subtract", "Math", { { "A", K::Float, zero }, { "B", K::Float, zero } }, {}, K::Float, "A - B" },
				{ "Multiply", "Multiply", "Math", { { "A", K::Float, one }, { "B", K::Float, one } }, {}, K::Float, "A × B" },
				{ "Divide", "Divide", "Math", { { "A", K::Float, one }, { "B", K::Float, one } }, {}, K::Float, "A ÷ B" },
				{ "Minimum", "Minimum", "Math", { { "A", K::Float, zero }, { "B", K::Float, zero } }, {}, K::Float, "min(A, B)" },
				{ "Maximum", "Maximum", "Math", { { "A", K::Float, zero }, { "B", K::Float, zero } }, {}, K::Float, "max(A, B)" },
				{ "Power", "Power", "Math", { { "A", K::Float, one }, { "B", K::Float, { 2, 2, 2, 2 } } }, {}, K::Float, "A ^ B" },
				{ "Modulo", "Modulo", "Math", { { "A", K::Float, zero }, { "B", K::Float, one } }, {}, K::Float, "A mod B" },
				{ "Step", "Step", "Math", { { "Edge", K::Float, { 0.5f } }, { "X", K::Float, zero } }, {}, K::Float, "X >= Edge 이면 1" },
				{ "Lerp", "Lerp", "Math", { { "A", K::Float, zero }, { "B", K::Float, one }, { "T", K::Float, { 0.5f } } }, {}, K::Float, "A 에서 B 로 T 만큼" },
				{ "Clamp", "Clamp", "Math", { { "X", K::Float, zero }, { "Min", K::Float, zero }, { "Max", K::Float, one } }, {}, K::Float, "Min..Max 안으로" },
				{ "Smoothstep", "Smoothstep", "Math", { { "Edge0", K::Float, zero }, { "Edge1", K::Float, one }, { "X", K::Float, { 0.5f } } }, {}, K::Float, "부드러운 0 → 1" },
				{ "Remap", "Remap", "Math", { { "X", K::Float, zero }, { "InMin", K::Float, zero }, { "InMax", K::Float, one }, { "OutMin", K::Float, zero }, { "OutMax", K::Float, one } }, {}, K::Float, "In 범위를 Out 범위로" },
				{ "Absolute", "Absolute", "Math", { { "X", K::Float, zero } }, {}, K::Float, "|X|" },
				{ "Sine", "Sine", "Math", { { "X", K::Float, zero } }, {}, K::Float, "sin(X) (라디안)" },
				{ "Cosine", "Cosine", "Math", { { "X", K::Float, zero } }, {}, K::Float, "cos(X) (라디안)" },
				{ "Fractional", "Fractional", "Math", { { "X", K::Float, zero } }, {}, K::Float, "소수 부분" },
				{ "Saturate", "Saturate", "Math", { { "X", K::Float, zero } }, {}, K::Float, "0..1 안으로" },
				{ "OneMinus", "One Minus", "Math", { { "X", K::Float, zero } }, {}, K::Float, "1 - X" },
				{ "Negate", "Negate", "Math", { { "X", K::Float, zero } }, {}, K::Float, "-X" },
				{ "Floor", "Floor", "Math", { { "X", K::Float, zero } }, {}, K::Float, "내림" },
				{ "Round", "Round", "Math", { { "X", K::Float, zero } }, {}, K::Float, "반올림" },
				{ "SquareRoot", "Square Root", "Math", { { "X", K::Float, zero } }, {}, K::Float, "√X" },
				// ---------------------------------------------------------------- Vector
				{ "Length", "Length", "Vector", { { "X", K::Vector3, zero } }, {}, K::Float, "벡터 길이" },
				{ "Normalize", "Normalize", "Vector", { { "X", K::Vector3, { 0, 1, 0, 0 } } }, {}, K::Vector3, "길이 1 로" },
				{ "Dot", "Dot Product", "Vector", { { "A", K::Vector3, zero }, { "B", K::Vector3, zero } }, {}, K::Float, "A · B" },
				{ "Cross", "Cross Product", "Vector", { { "A", K::Vector3, { 1, 0, 0, 0 } }, { "B", K::Vector3, { 0, 1, 0, 0 } } }, {}, K::Vector3, "A × B" },
				{ "Distance", "Distance", "Vector", { { "A", K::Vector3, zero }, { "B", K::Vector3, zero } }, {}, K::Float, "두 점 사이 거리" },
				{ "Split", "Split (Component)", "Vector", { { "X", K::Vector3, zero } }, { { "Component", K::Enum, { 0 }, 0, 0, comps } }, K::Float, "성분 하나 (x · y · z · w)" },
				{ "Combine", "Combine (Vector)", "Vector", { { "X", K::Float, zero }, { "Y", K::Float, zero }, { "Z", K::Float, zero }, { "W", K::Float, zero } }, {}, K::Vector3, "스칼라 넷 → 벡터" },
				// ---------------------------------------------------------------- Sampling
				{ "SampleCurve", "Sample Curve", "Sampling", { { "T", K::Float, zero } }, { { "Curve", K::Curve, {} } }, K::Float, "곡선 (T = 0..1)" },
				{ "SampleGradient", "Sample Gradient", "Sampling", { { "T", K::Float, zero } }, { { "Gradient", K::Gradient, {} } }, K::Color, "그라디언트 (T = 0..1, HDR)" },
				{ "Noise", "Noise (Value)", "Sampling", { { "Position", K::Vector3, zero }, { "Frequency", K::Float, one } }, {}, K::Float, "3D 잡음 (-1..1)" },
				{ "NoiseVector", "Noise (Vector)", "Sampling", { { "Position", K::Vector3, zero }, { "Frequency", K::Float, one } }, {}, K::Vector3, "3D 잡음 벡터 (-1..1) — 흔들림 · 흐름" },
				// ---------------------------------------------------------------- Color
				{ "HSVToRGB", "HSV to RGB", "Color", { { "H", K::Float, zero }, { "S", K::Float, one }, { "V", K::Float, one } }, {}, K::Color, "색상 (0..1 한 바퀴) · 채도 · 밝기 → 색" },
			};
		}

		F4 Make(float x, float y = 0, float z = 0, float w = 0) { return { x, y, z, w }; }

		// 연결하지 않은 입력의 값 (스칼라는 네 칸에 같은 값)
		F4 InputConst(const OperatorNode& n, const OperatorInput& in)
		{
			F4 v = in.Default;
			if (in.Kind == K::Float) v = { v[0], v[0], v[0], v[0] };
			for (auto it = n.Params.begin(); it != n.Params.end(); ++it)
				if (Simplify(it.key()) == Simplify(in.Name))
					v = ValueFromJson(*it, v);
			if (in.Kind == K::Float && n.Params.contains(in.Name) && n.Params[in.Name].is_number())
				v = { v[0], v[0], v[0], v[0] };
			return v;
		}

		const json* Setting(const OperatorNode& n, const char* name)
		{
			for (auto it = n.Params.begin(); it != n.Params.end(); ++it)
				if (Simplify(it.key()) == Simplify(name))
					return &*it;
			return nullptr;
		}

		struct Compiler
		{
			const Asset& A;
			const PropertySource& Props;
			std::vector<F4>& Out;
			std::string& Error;
			std::set<int> Visiting;
			int Depth = 0, MaxDepth = 0;

			void Push(int n = 1) { Depth += n; MaxDepth = (std::max)(MaxDepth, Depth); }
			void Pop(int n = 1) { Depth -= n; }
			void Const(const F4& v) { Out.push_back(Make((float)OpConst)); Out.push_back(v); Push(); }

			bool Input(const OperatorNode& n, const OperatorInput& in)
			{
				for (auto it = n.Inputs.begin(); it != n.Inputs.end(); ++it)
					if (Simplify(it.key()) == Simplify(in.Name) && it->is_number_integer())
						return Node(it->get<int>());
				Const(InputConst(n, in));
				return true;
			}

			bool Node(int id)
			{
				const OperatorNode* n = A.FindOperatorNode(id);
				if (!n) { Error = "operator " + std::to_string(id) + " does not exist"; return false; }
				const OperatorDesc* d = FindOperator(n->Type);
				if (!d) { Error = "unknown operator '" + n->Type + "'"; return false; }
				if (!Visiting.insert(id).second) { Error = "operators form a loop"; return false; }
				for (const OperatorInput& in : d->Inputs)
					if (!Input(*n, in))
						return false;
				Visiting.erase(id);
				const std::string t = d->Type;
				const int nin = (int)d->Inputs.size();
				auto op = [&](int code, float a = 0, float b = 0) { Out.push_back(Make((float)code, a, b)); Pop(nin); Push(); };
				if (t == "Float" || t == "Vector3" || t == "Color") { /* 입력 값이 곧 결과 */ }
				else if (t == "Property")
				{
					const json* s = Setting(*n, "Name");
					const std::string name = s && s->is_string() ? s->get<std::string>() : std::string();
					F4 v = { 0, 0, 0, 0 };
					const Property* p = A.FindProperty(name);
					if (!p) { Error = "operator Property: no property '" + name + "'"; return false; }
					Props.Get(name, v);
					if (p->Type == PropertyType::Float || p->Type == PropertyType::Int || p->Type == PropertyType::Bool) v = { v[0], v[0], v[0], v[0] };
					else if (p->Type == PropertyType::Vector3) v[3] = 0.0f;
					Const(v);
				}
				else if (t == "Time") op(OpTime);
				else if (t == "DeltaTime") op(OpDeltaTime);
				else if (t == "Age") op(OpAge);
				else if (t == "Lifetime") op(OpLifetime);
				else if (t == "AgeOverLifetime") op(OpAge01);
				else if (t == "Position") op(OpPosition);
				else if (t == "Velocity") op(OpVelocity);
				else if (t == "ParticleColor") op(OpColor);
				else if (t == "Size") op(OpSize);
				else if (t == "Speed") op(OpSpeed);
				else if (t == "RandomPerParticle" || t == "RandomPerFrame")
				{
					// Min, Max 가 쌓여 있다 → 무작위 t (노드마다 다른 소금) → Lerp
					Out.push_back(Make((float)(t == "RandomPerParticle" ? OpRandParticle : OpRandFrame), (float)(id * 7919 % 65536)));
					Push();
					Out.push_back(Make((float)OpLerp));
					Pop(3);
					Push();
				}
				else if (t == "Add") op(OpAdd);
				else if (t == "Subtract") op(OpSub);
				else if (t == "Multiply") op(OpMul);
				else if (t == "Divide") op(OpDiv);
				else if (t == "Minimum") op(OpMin);
				else if (t == "Maximum") op(OpMax);
				else if (t == "Power") op(OpPow);
				else if (t == "Modulo") op(OpMod);
				else if (t == "Step") op(OpStep);
				else if (t == "Lerp") op(OpLerp);
				else if (t == "Clamp") op(OpClamp);
				else if (t == "Smoothstep") op(OpSmoothstep);
				else if (t == "Remap") op(OpRemap);
				else if (t == "Absolute") op(OpAbs);
				else if (t == "Sine") op(OpSin);
				else if (t == "Cosine") op(OpCos);
				else if (t == "Fractional") op(OpFrac);
				else if (t == "Saturate") op(OpSaturate);
				else if (t == "OneMinus") op(OpOneMinus);
				else if (t == "Negate") op(OpNegate);
				else if (t == "Floor") op(OpFloor);
				else if (t == "Round") op(OpRound);
				else if (t == "SquareRoot") op(OpSqrt);
				else if (t == "Length") op(OpLength);
				else if (t == "Normalize") op(OpNormalize);
				else if (t == "Dot") op(OpDot);
				else if (t == "Cross") op(OpCross);
				else if (t == "Distance") op(OpDistance);
				else if (t == "Split")
				{
					const json* s = Setting(*n, "Component");
					int c = 0;
					if (s && s->is_number()) c = std::clamp(s->get<int>(), 0, 3);
					else if (s && s->is_string()) { const std::string v = Simplify(s->get<std::string>()); c = v == "y" ? 1 : v == "z" ? 2 : v == "w" ? 3 : 0; }
					op(OpSplit, (float)c);
				}
				else if (t == "Combine") op(OpCombine);
				else if (t == "SampleCurve" || t == "SampleGradient")
				{
					// 곡선 16 칸 (명령 뒤 4 칸) · 그라디언트 8 칸 (8 칸) — 블록의 곡선 · 그라디언트와 같은 표본
					Block tmp;
					const json* s = Setting(*n, t == "SampleCurve" ? "Curve" : "Gradient");
					if (s) tmp.Params[t == "SampleCurve" ? "Curve" : "Gradient"] = *s;
					static const BlockDesc dummy{};
					op(t == "SampleCurve" ? OpCurve : OpGradient);
					if (t == "SampleCurve")
					{
						const auto keys = tmp.GetCurve(dummy, "Curve");
						auto sample = [&](float x) {
							if (keys.empty()) return 1.0f;
							if (x <= keys.front().T) return keys.front().V;
							for (size_t k = 1; k < keys.size(); ++k)
								if (x <= keys[k].T)
									return keys[k - 1].V + (keys[k].V - keys[k - 1].V) * (x - keys[k - 1].T) / (std::max)(keys[k].T - keys[k - 1].T, 1e-6f);
							return keys.back().V;
						};
						for (int i = 0; i < 4; ++i)
							Out.push_back(Make(sample((i * 4 + 0) / 15.0f), sample((i * 4 + 1) / 15.0f), sample((i * 4 + 2) / 15.0f), sample((i * 4 + 3) / 15.0f)));
					}
					else
					{
						const auto keys = tmp.GetGradient(dummy, "Gradient");
						for (int i = 0; i < 8; ++i)
						{
							const float x = i / 7.0f;
							F4 c = { 1, 1, 1, 1 };
							if (!keys.empty())
							{
								const GradientKey* a = &keys.front();
								const GradientKey* b = &keys.back();
								if (x <= keys.front().T) b = a;
								else if (x >= keys.back().T) a = b;
								else
									for (size_t k = 1; k < keys.size(); ++k)
										if (x <= keys[k].T) { a = &keys[k - 1]; b = &keys[k]; break; }
								const float f = a == b ? 0.0f : (x - a->T) / (std::max)(b->T - a->T, 1e-6f);
								for (int m = 0; m < 4; ++m) c[m] = a->C[m] + (b->C[m] - a->C[m]) * f;
							}
							Out.push_back(c);
						}
					}
				}
				else if (t == "Noise") op(OpNoise);
				else if (t == "NoiseVector") op(OpNoiseVec);
				else if (t == "HSVToRGB")
				{
					// H, S, V → (H, S, V, 1) → 색
					Const({ 1, 1, 1, 1 });
					Out.push_back(Make((float)OpCombine));
					Pop(4);
					Push();
					Out.push_back(Make((float)OpHsv));
				}
				else { Error = "operator '" + t + "' has no code"; return false; }
				return true;
			}
		};
	}

	const std::vector<OperatorDesc>& Operators()
	{
		static const std::vector<OperatorDesc> s = MakeOperators();
		return s;
	}

	const OperatorDesc* FindOperator(const std::string& type)
	{
		const std::string t = Simplify(type);
		for (const OperatorDesc& d : Operators())
			if (Simplify(d.Type) == t || Simplify(d.Label) == t)
				return &d;
		return nullptr;
	}

	bool Linkable(const ParamDesc& p)
	{
		return p.Kind == ParamKind::Float || p.Kind == ParamKind::Int || p.Kind == ParamKind::Bool || p.Kind == ParamKind::Enum ||
			p.Kind == ParamKind::Vector3 || p.Kind == ParamKind::Color;
	}

	json OperatorToJson(const OperatorNode& n)
	{
		json j = { { "id", n.Id }, { "type", n.Type }, { "x", n.X }, { "y", n.Y } };
		if (!n.Params.empty()) j["params"] = n.Params;
		if (!n.Inputs.empty()) j["inputs"] = n.Inputs;
		return j;
	}

	OperatorNode OperatorFromJson(const json& j)
	{
		OperatorNode n;
		n.Id = j.value("id", 0);
		n.Type = j.value("type", std::string());
		if (const OperatorDesc* d = FindOperator(n.Type))
			n.Type = d->Type;
		n.X = j.value("x", 0.0f);
		n.Y = j.value("y", 0.0f);
		if (j.contains("params") && j["params"].is_object()) n.Params = j["params"];
		if (j.contains("inputs") && j["inputs"].is_object()) n.Inputs = j["inputs"];
		return n;
	}

	const OperatorNode* Asset::FindOperatorNode(int id) const
	{
		for (const OperatorNode& n : Operators)
			if (n.Id == id)
				return &n;
		return nullptr;
	}

	int Asset::NewOperatorId() const
	{
		int id = 1;
		for (const OperatorNode& n : Operators)
			id = (std::max)(id, n.Id + 1);
		return id;
	}

	bool CompileOperator(const Asset& asset, int id, const PropertySource& props, std::vector<std::array<float, 4>>& out, std::string& error)
	{
		out.clear();
		Compiler c{ asset, props, out, error };
		if (!c.Node(id))
			return false;
		if (c.MaxDepth > kMaxStack)
		{
			error = "operator graph is too deep (stack " + std::to_string(c.MaxDepth) + " > " + std::to_string(kMaxStack) + ")";
			return false;
		}
		return true;
	}
}
