#include "pch.h"
#include "VfxAsset.h"
#include <functional>
#include <set>
#include <utility>

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
			OpSize = 10, OpSpeed = 11, OpRandParticle = 12, OpRandFrame = 13, OpGetAttribute = 14,
			OpAdd = 20, OpSub = 21, OpMul = 22, OpDiv = 23, OpMin = 24, OpMax = 25, OpPow = 26, OpStep = 27, OpDot = 28, OpCross = 29, OpDistance = 30, OpMod = 31, OpCompare = 32, OpAnd = 33, OpOr = 34,
			OpLerp = 40, OpClamp = 41, OpSmoothstep = 42, OpRemap = 43, OpBranch = 44,
			OpAbs = 50, OpSin = 51, OpCos = 52, OpFrac = 53, OpSaturate = 54, OpOneMinus = 55, OpNegate = 56, OpLength = 57, OpNormalize = 58,
			OpFloor = 59, OpSqrt = 60, OpRound = 61, OpHsv = 62, OpNot = 63,
			OpSplit = 70, OpCombine = 71,
			OpCurve = 80, OpGradient = 81, OpNoise = 82, OpNoiseVec = 83,
		};
		constexpr int kMaxStack = 10;   // 58. VFX.fx 의 Eval 스택 크기
		constexpr int kMaxSubgraphLevel = 8;   // Sub Graph 안의 Sub Graph (자기를 부르는 고리도 여기서 멈춘다)

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
				{ "GetAttribute", "Get Attribute (Custom)", "Attributes", {}, { { "Attribute", K::Text, {}, 0, 0, nullptr, "사용자 속성 이름" } }, K::Float,
					"사용자 속성 (Blackboard 의 Custom Attributes) 값 — Set Attribute 블록이 쓴다" },
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
				// ---------------------------------------------------------------- Logic (Unity 의 Compare · Branch · Logical And / Or / Not — 참 = 1, 거짓 = 0)
				{ "Compare", "Compare", "Logic", { { "A", K::Float, zero }, { "B", K::Float, zero } },
					{ { "Condition", K::Enum, { 2 }, 0, 0, "Equal|Not Equal|Less|Less Or Equal|Greater|Greater Or Equal", "A 와 B 를 견준다 (x 성분)" } }, K::Bool, "A ? B → 1 (참) 또는 0 (거짓)" },
				{ "Branch", "Branch", "Logic", { { "Predicate", K::Float, one }, { "True", K::Float, one }, { "False", K::Float, zero } }, {}, K::Float,
					"Predicate 가 참 (0.5 보다 크면) 이면 True, 아니면 False — 벡터 · 색도" },
				{ "And", "Logical And", "Logic", { { "A", K::Float, one }, { "B", K::Float, one } }, {}, K::Bool, "둘 다 참이면 1" },
				{ "Or", "Logical Or", "Logic", { { "A", K::Float, zero }, { "B", K::Float, zero } }, {}, K::Bool, "하나라도 참이면 1" },
				{ "Not", "Logical Not", "Logic", { { "X", K::Float, zero } }, {}, K::Bool, "참이면 0, 거짓이면 1" },
				// ---------------------------------------------------------------- Sub Graph (Unity 의 Visual Effect Subgraph Operator)
				{ "SubGraph", "Sub Graph", "Sub Graph", {}, { { "Path", K::Text, {}, 0, 0, nullptr, "Sub Graph 파일 (.vfxoperator)" } }, K::Float,
					"다른 파일 (.vfxoperator) 의 연산 노드 묶음을 노드 하나로 — 그 파일의 Blackboard 속성이 입력, Output (Sub Graph) 노드가 결과" },
				{ "SubgraphOutput", "Output (Sub Graph)", "Sub Graph", { { "Value", K::Float, zero } }, {}, K::Float,
					"Sub Graph 파일의 결과 — 이 노드에 이은 값이 그 파일을 쓰는 Sub Graph 노드의 출력" },
			};
		}

		F4 Make(float x, float y = 0, float z = 0, float w = 0) { return { x, y, z, w }; }

		// 연결하지 않은 입력의 값 (스칼라는 네 칸에 같은 값)
		F4 InputConst(const OperatorNode& n, const NodeInput& in)
		{
			F4 v = in.Default;
			if (in.Kind == K::Float) v = { v[0], v[0], v[0], v[0] };
			for (auto it = n.Params.begin(); it != n.Params.end(); ++it)
				if (Simplify(it.key()) == Simplify(in.Name))
				{
					v = ValueFromJson(*it, v);
					if (in.Kind == K::Float && it->is_number())
						v = { v[0], v[0], v[0], v[0] };
				}
			return v;
		}

		const json* Setting(const OperatorNode& n, const char* name)
		{
			for (auto it = n.Params.begin(); it != n.Params.end(); ++it)
				if (Simplify(it.key()) == Simplify(name))
					return &*it;
			return nullptr;
		}

		std::string SettingText(const OperatorNode& n, const char* name)
		{
			const json* s = Setting(n, name);
			return s && s->is_string() ? s->get<std::string>() : std::string();
		}

		// 속성 값의 모양: Float · Int · Bool 은 네 칸에 같은 값, Vector3 는 w = 0
		F4 ShapeProperty(const Property& p, F4 v)
		{
			if (p.Type == PropertyType::Float || p.Type == PropertyType::Int || p.Type == PropertyType::Bool) v = { v[0], v[0], v[0], v[0] };
			else if (p.Type == PropertyType::Vector3) v[3] = 0.0f;
			return v;
		}

		const OperatorNode* SubgraphOutputNode(const Asset& a)
		{
			for (const OperatorNode& n : a.Operators)
				if (n.Type == "SubgraphOutput")
					return &n;
			return nullptr;
		}

		// 그래프 한 겹: 지금 에셋 · 이 겹을 부른 Sub Graph 노드 (맨 위는 없음) · 부른 겹
		struct Frame
		{
			const Asset* A;
			const OperatorNode* Call;
			const Frame* Parent;
			int Level;
		};

		struct Compiler
		{
			const PropertySource& Props;
			std::vector<F4>& Out;
			std::string& Error;
			std::set<std::pair<const Asset*, int>> Visiting;
			std::vector<std::shared_ptr<const Asset>> Keep;   // 컴파일 동안 Sub Graph 파일을 붙들어 둔다
			int Depth = 0, MaxDepth = 0;

			void Push(int n = 1) { Depth += n; MaxDepth = (std::max)(MaxDepth, Depth); }
			void Pop(int n = 1) { Depth -= n; }
			void Const(const F4& v) { Out.push_back(Make((float)OpConst)); Out.push_back(v); Push(); }

			bool Input(const Frame& f, const OperatorNode& n, const NodeInput& in)
			{
				for (auto it = n.Inputs.begin(); it != n.Inputs.end(); ++it)
					if (Simplify(it.key()) == Simplify(in.Name) && it->is_number_integer())
						return Node(f, it->get<int>());
				Const(InputConst(n, in));
				return true;
			}

			// Blackboard 속성: 맨 위면 Visual Effect 의 값 (덮어쓰기 포함), Sub Graph 안이면 부른 노드의 입력 (연결 · 값 · 파일의 기본값)
			bool PropertyValue(const Frame& f, const std::string& name)
			{
				const Property* p = f.A->FindProperty(name);
				if (!p) { Error = "operator Property: no property '" + name + "'"; return false; }
				F4 v = p->Value;
				if (f.Call)
				{
					for (auto it = f.Call->Inputs.begin(); it != f.Call->Inputs.end(); ++it)
						if (Simplify(it.key()) == Simplify(name) && it->is_number_integer())
							return Node(*f.Parent, it->get<int>());
					for (auto it = f.Call->Params.begin(); it != f.Call->Params.end(); ++it)
						if (Simplify(it.key()) == Simplify(name))
							v = ValueFromJson(*it, v);
				}
				else
					Props.Get(name, v);
				Const(ShapeProperty(*p, v));
				return true;
			}

			bool SubGraph(const Frame& f, const OperatorNode& n)
			{
				if (f.Level >= kMaxSubgraphLevel) { Error = "Sub Graphs nest too deep (or a Sub Graph uses itself)"; return false; }
				const std::string path = SettingText(n, "Path");
				if (path.empty()) { Error = "Sub Graph node " + std::to_string(n.Id) + " has no file"; return false; }
				const Loaded l = Load(path);
				if (!l.Data) { Error = "Sub Graph '" + path + "': " + (l.Error.empty() ? std::string("not found") : l.Error); return false; }
				Keep.push_back(l.Data);
				const OperatorNode* out = SubgraphOutputNode(*l.Data);
				if (!out) { Error = "Sub Graph '" + path + "' has no Output (Sub Graph) node"; return false; }
				const Frame inner{ l.Data.get(), &n, &f, f.Level + 1 };
				return Node(inner, out->Id);
			}

			bool Node(const Frame& f, int id)
			{
				const OperatorNode* n = f.A->FindOperatorNode(id);
				if (!n) { Error = "operator " + std::to_string(id) + " does not exist"; return false; }
				const OperatorDesc* d = FindOperator(n->Type);
				if (!d) { Error = "unknown operator '" + n->Type + "'"; return false; }
				if (!Visiting.insert({ f.A, id }).second) { Error = "operators form a loop"; return false; }
				const std::string t = d->Type;
				if (t == "SubGraph")
				{
					// 입력은 안의 Property 노드가 쓸 때 계산한다
					const bool ok = SubGraph(f, *n);
					Visiting.erase({ f.A, id });
					return ok;
				}
				for (const OperatorInput& in : d->Inputs)
					if (!Input(f, *n, { in.Name, in.Kind, in.Default }))
						return false;
				Visiting.erase({ f.A, id });
				const int nin = (int)d->Inputs.size();
				auto op = [&](int code, float a = 0, float b = 0) { Out.push_back(Make((float)code, a, b)); Pop(nin); Push(); };
				if (t == "Float" || t == "Vector3" || t == "Color" || t == "SubgraphOutput") { /* 입력 값이 곧 결과 */ }
				else if (t == "Property")
				{
					if (!PropertyValue(f, SettingText(*n, "Name")))
						return false;
				}
				else if (t == "GetAttribute")
				{
					// 사용자 속성은 맨 위 에셋 (Visual Effect 의 .vfx) 것 — Sub Graph 안에서도
					const Frame* top = &f;
					while (top->Parent) top = top->Parent;
					const std::string name = SettingText(*n, "Attribute");
					int lane = 0, width = 0;
					if (!top->A->AttributeLanes(name, lane, width)) { Error = "operator Get Attribute: no custom attribute '" + name + "' (or past 4 floats)"; return false; }
					Out.push_back(Make((float)OpGetAttribute, (float)lane, (float)width));
					Push();
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
					// Min, Max 가 쌓여 있다 → 무작위 t (노드마다 다른 소금 — Sub Graph 겹마다도) → Lerp
					const int salt = (id * 7919 + f.Level * 104729 + (f.Call ? f.Call->Id * 15485863 : 0)) % 65536;
					Out.push_back(Make((float)(t == "RandomPerParticle" ? OpRandParticle : OpRandFrame), (float)(salt < 0 ? -salt : salt)));
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
				else if (t == "Compare")
				{
					const json* s = Setting(*n, "Condition");
					int c = 2;
					if (s && s->is_number()) c = std::clamp(s->get<int>(), 0, 5);
					else if (s && s->is_string())
					{
						static const char* names[] = { "equal", "notequal", "less", "lessorequal", "greater", "greaterorequal" };
						for (int k = 0; k < 6; ++k) if (Simplify(s->get<std::string>()) == names[k]) c = k;
					}
					op(OpCompare, (float)c);
				}
				else if (t == "Branch") op(OpBranch);
				else if (t == "And") op(OpAnd);
				else if (t == "Or") op(OpOr);
				else if (t == "Not") op(OpNot);
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
								const float fr = a == b ? 0.0f : (x - a->T) / (std::max)(b->T - a->T, 1e-6f);
								for (int m = 0; m < 4; ++m) c[m] = a->C[m] + (b->C[m] - a->C[m]) * fr;
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
		return !p.NoLink && (p.Kind == ParamKind::Float || p.Kind == ParamKind::Int || p.Kind == ParamKind::Bool || p.Kind == ParamKind::Enum ||
			p.Kind == ParamKind::Vector3 || p.Kind == ParamKind::Color);
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
		Compiler c{ props, out, error };
		if (!c.Node(Frame{ &asset, nullptr, nullptr, 0 }, id))
			return false;
		if (c.MaxDepth > kMaxStack)
		{
			error = "operator graph is too deep (stack " + std::to_string(c.MaxDepth) + " > " + std::to_string(kMaxStack) + ")";
			return false;
		}
		return true;
	}

	std::vector<NodeInput> OperatorInputs(const OperatorNode& n)
	{
		std::vector<NodeInput> r;
		const OperatorDesc* d = FindOperator(n.Type);
		if (!d)
			return r;
		if (std::string(d->Type) == "SubGraph")
		{
			// Sub Graph 파일의 Blackboard 속성 = 입력 (기본값 = 그 파일의 값)
			const std::string path = SettingText(n, "Path");
			const Loaded l = path.empty() ? Loaded{} : Load(path);
			if (l.Data)
				for (const Property& p : l.Data->Properties)
					r.push_back({ p.Name, p.Type == PropertyType::Vector3 ? K::Vector3 : p.Type == PropertyType::Color ? K::Color : K::Float, ShapeProperty(p, p.Value) });
			return r;
		}
		for (const OperatorInput& in : d->Inputs)
			r.push_back({ in.Name, in.Kind, in.Default });
		return r;
	}

	uint64_t DependencyRevision(const Asset& asset)
	{
		uint64_t h = 0;
		std::function<void(const Asset&, int)> visit = [&](const Asset& a, int level) {
			auto file = [&](const std::string& path) {
				if (path.empty())
					return;
				const Loaded l = Load(path);
				h = (h ^ (l.Revision + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2))) * 1099511628211ull;
				if (l.Data && level < kMaxSubgraphLevel)
					visit(*l.Data, level + 1);
			};
			for (const OperatorNode& n : a.Operators)
				if (n.Type == "SubGraph")
					file(SettingText(n, "Path"));
			// Block Sub Graph (.vfxblock)
			for (const System& s : a.Systems)
				for (const auto* list : { &s.Initialize, &s.Update })
					for (const Block& b : *list)
						if (b.Type == "SubgraphBlock")
							for (auto it = b.Params.begin(); it != b.Params.end(); ++it)
								if (Simplify(it.key()) == "path" && it->is_string())
									file(it->get<std::string>());
		};
		visit(asset, 0);
		return h;
	}

	Asset DefaultBlockSubgraph()
	{
		// 입력 Strength → Update 의 Turbulence 세기 (Initialize 에 놓으면 그쪽 목록 — 비어 있다)
		Asset a;
		Property in;
		in.Name = "Strength";
		in.Value = { 3, 3, 3, 3 };
		in.Min = 0.0f;
		in.Max = 20.0f;
		a.Properties.push_back(in);
		System s;
		s.Name = "Block";
		s.SpawnCtx.Rate = 0.0f;
		Block t;
		t.Type = "Turbulence";
		t.Bind = { { "Intensity", "Strength" } };
		s.Update.push_back(t);
		a.Systems.push_back(s);
		return a;
	}

	Asset DefaultSubgraph()
	{
		// In × 2 → Output (Sub Graph)
		Asset a;
		Property in;
		in.Name = "In";
		in.Value = { 1, 1, 1, 1 };
		a.Properties.push_back(in);
		OperatorNode p, m, o;
		p.Id = 1; p.Type = "Property"; p.Params = { { "Name", "In" } }; p.X = -420; p.Y = 0;
		m.Id = 2; m.Type = "Multiply"; m.Inputs = { { "A", 1 } }; m.Params = { { "B", 2.0f } }; m.X = -200; m.Y = 0;
		o.Id = 3; o.Type = "SubgraphOutput"; o.Inputs = { { "Value", 2 } }; o.X = 20; o.Y = 0;
		a.Operators = { p, m, o };
		return a;
	}
}
