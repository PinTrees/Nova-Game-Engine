#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <memory>
#include <string>
#include <vector>

// Unity VFX Graph 의 .vfx 에셋 (JSON).
//  - Blackboard 속성 (Exposed Property) + 시스템 (Spawn → Initialize → Update → Output) 여러 개
//  - 블록 (Initialize · Update) 은 종류 이름 + 값 (params) + 속성 연결 (bind). 블록 정의표 (Blocks) 하나를
//    그래프 창 · nova vfx CLI · 런타임 (Encode — 58. VFX.fx 가 읽는 블록 목록) 이 함께 쓴다
//  - GPU Event: Spawn 의 parent 가 다른 시스템 이름이면 그 시스템의 파티클이 죽을 때마다 그 자리에서 태어난다 (Trigger Event On Die)
namespace Vfx
{
	using json = nlohmann::json;

	enum class ParamKind { Float, Int, Bool, Enum, Vector3, Color, Curve, Gradient, Text };
	enum class Context { Initialize, Update };
	// 위치 · 방향 값: World 시스템이면 Visual Effect 의 변환을 곱한다 (블록 값은 늘 Visual Effect 기준)
	enum class ParamSpace { None, Position, Direction };

	struct ParamDesc
	{
		const char* Name;
		ParamKind Kind;
		std::array<float, 4> Default;
		float Min = 0.0f, Max = 0.0f;      // UI 끌기 범위 (둘 다 0 = 제한 없음)
		const char* Options = nullptr;     // Enum: "A|B|C"
		const char* Tip = nullptr;
		ParamSpace Space = ParamSpace::None;
	};

	struct BlockDesc
	{
		const char* Type;        // 저장 이름 (SetPosition …)
		const char* Label;       // 화면 이름 (Set Position (Shape) …)
		const char* Category;    // 검색 창 분류
		Context Ctx;
		int Id;                  // 58. VFX.fx 의 블록 번호
		std::vector<ParamDesc> Params;
		const char* Help;
	};

	const std::vector<BlockDesc>& Blocks();
	const BlockDesc* FindBlock(const std::string& type);   // 대소문자 · 공백 무시 (Label 도 받는다)
	const ParamDesc* FindParam(const BlockDesc& d, const std::string& name);
	std::vector<std::string> EnumOptions(const ParamDesc& p);

	// 곡선 (t, 값) · 그라디언트 (t, r, g, b, a) 의 점 — 선형 보간
	struct CurveKey { float T, V; };
	struct GradientKey { float T; float C[4]; };

	struct Block
	{
		std::string Type;
		bool Enabled = true;
		json Params = json::object();   // 이름 → 값 (없으면 기본값)
		json Bind = json::object();     // 값 이름 → 속성 이름
		json Links = json::object();    // 값 이름 → 연산 노드 Id (Operator — 파티클마다 계산한 값)

		float GetFloat(const BlockDesc& d, const char* name) const;
		std::array<float, 4> GetVector(const BlockDesc& d, const char* name) const;
		std::vector<CurveKey> GetCurve(const BlockDesc& d, const char* name) const;
		std::vector<GradientKey> GetGradient(const BlockDesc& d, const char* name) const;
	};

	struct Burst
	{
		float Time = 0.0f;
		int Count = 100;
		int Cycles = 1;          // 0 = 끝없이
		float Interval = 1.0f;
	};

	// GPU Event 를 보내는 때 (Unity 의 Trigger Event On Die · Trigger Event Rate)
	enum class EventTrigger { OnDie = 0, Rate = 1 };

	struct Spawn
	{
		float Rate = 50.0f;              // 초당 (Constant Spawn Rate)
		std::string RateBind;            // 비지 않으면 이 속성 (Float) 이 Rate
		std::vector<Burst> Bursts;       // Single / Periodic Burst
		bool Loop = true;
		float Duration = 3.0f;           // 한 바퀴 (Loop 이면 반복, 아니면 끝나고 멈춤)
		float Delay = 0.0f;
		std::string StartEvent = "OnPlay";
		std::string StopEvent = "OnStop";
		std::string Parent;              // GPU Event: 이 시스템 이름의 파티클에서 태어난다 (비면 스스로)
		EventTrigger Trigger = EventTrigger::OnDie;   // 부모가 죽을 때 · 살아 있는 동안 초당 EventRate 번
		float EventRate = 20.0f;         // Trigger = Rate: 부모 파티클 하나가 초당 보내는 이벤트
		int CountPerEvent = 20;          // GPU Event 하나마다 태어나는 수
	};

	enum class Blend { Additive = 0, Alpha = 1 };
	// 58. VFX.fx 의 ShapeAlpha 와 같은 번호 (8 = 텍스처)
	enum class Shape { SoftDot = 0, Glow, Star, Sparkle, Ring, Spark, Smoke, Square, Texture, Heart };
	enum class Orient { FaceCamera = 0, AlongVelocity, Horizontal };
	enum class SortMode { Auto = 0, On, Off };   // Auto = Alpha 섞기면 정렬

	struct Output
	{
		Blend BlendMode = Blend::Additive;
		Shape Look = Shape::Glow;
		Orient Orientation = Orient::FaceCamera;
		float Stretch = 0.05f;           // Along Velocity: 속도 1 당 늘어나는 비율
		float SoftDistance = 0.3f;       // Soft Particles (0 = 끔)
		float Intensity = 1.0f;          // HDR 배율 (1 보다 크면 Bloom 으로 빛난다)
		std::string Texture;             // Shape = Texture 일 때 (builtin:… 또는 Assets 이미지)
		int FlipbookColumns = 1, FlipbookRows = 1;
		float FlipbookFps = 0.0f;        // 0 = 수명 동안 한 번
		SortMode Sort = SortMode::Auto;  // Alpha 끼리 먼 것부터 (GPU 정렬)
		// 꼬리 (Unity 의 Output Particle Strip): 파티클이 지난 자리를 띠로
		bool Trail = false;
		int TrailPoints = 12;            // 기록하는 자리 수 (2..32)
		float TrailLength = 0.4f;        // 초 (자리 사이 = 길이 / 수)
		float TrailWidth = 1.0f;         // 파티클 크기의 배율
		bool TrailOnly = false;          // 꼬리만 그린다 (파티클 사각형은 그리지 않음)
		bool Sorted() const { return Sort == SortMode::On || (Sort == SortMode::Auto && BlendMode == Blend::Alpha); }
	};

	struct System
	{
		std::string Name = "System";
		bool Enabled = true;
		int Capacity = 4096;
		bool Local = false;              // 시뮬레이션 공간 (false = World)
		Spawn SpawnCtx;
		std::vector<Block> Initialize;
		std::vector<Block> Update;
		Output OutputCtx;
		json Editor = json::object();    // 그래프 창의 노드 자리 등
	};

	enum class PropertyType { Float, Int, Bool, Vector3, Color };
	struct Property
	{
		std::string Name;
		PropertyType Type = PropertyType::Float;
		std::array<float, 4> Value = { 0, 0, 0, 1 };
		float Min = 0.0f, Max = 0.0f;
		std::string Tooltip;
	};

	// 화면 밖일 때 (Unity 의 Culling Flags): 보일 때만 시뮬레이션 (기본) · 늘 시뮬레이션
	enum class Culling { SimulateWhenVisible = 0, AlwaysSimulate = 1 };

	// ---------------------------------------------------------------- 연산 노드 (Unity 의 Operator)
	//  블록 값에 이어 파티클마다 계산한다 (나이 · 자리 · 시간 · 무작위 · 곡선 …). 그래프는 짧은 스택 명령 목록이 되어
	//  58. VFX.fx 가 해석한다 — 블록처럼 다시 컴파일하지 않는다. 값은 늘 float4 (스칼라는 네 칸에 같은 값)
	struct OperatorInput
	{
		const char* Name;
		ParamKind Kind;                  // Float · Vector3 · Color (연결하지 않았을 때 쓰는 값의 모양)
		std::array<float, 4> Default;
	};

	struct OperatorDesc
	{
		const char* Type;                // 저장 이름 (Multiply …)
		const char* Label;
		const char* Category;            // 검색 창 분류 (Inputs · Attributes · Math · Vector · Sampling · Random · Color)
		std::vector<OperatorInput> Inputs;
		std::vector<ParamDesc> Settings; // 노드 설정 (성분 · 곡선 · 그라디언트 · 속성 이름)
		ParamKind Output;                // 표시용
		const char* Help;
	};
	const std::vector<OperatorDesc>& Operators();
	const OperatorDesc* FindOperator(const std::string& type);   // 대소문자 · 공백 무시

	struct OperatorNode
	{
		int Id = 0;
		std::string Type;
		json Params = json::object();    // 연결하지 않은 입력의 값 · 설정 (이름 → 값)
		json Inputs = json::object();    // 입력 이름 → 다른 연산 노드 Id
		float X = 0.0f, Y = 0.0f;        // 그래프 창 자리
	};
	json OperatorToJson(const OperatorNode& n);
	OperatorNode OperatorFromJson(const json& j);
	// 블록 값에 이을 수 있는 종류 (곡선 · 그라디언트 · 글은 안 됨)
	bool Linkable(const ParamDesc& p);

	struct Asset
	{
		Culling CullingMode = Culling::SimulateWhenVisible;
		std::vector<Property> Properties;
		std::vector<System> Systems;
		std::vector<OperatorNode> Operators;
		const OperatorNode* FindOperatorNode(int id) const;
		int NewOperatorId() const;
		json Editor = json::object();    // 그래프 창 (보기 자리 등)

		json ToJson() const;
		bool FromJson(const json& j, std::string& error);
		const Property* FindProperty(const std::string& name) const;
		int FindSystem(const std::string& name) const;
		// 부모 (GPU Event 를 보내는 시스템) 가 자식보다 먼저 오는 순서 (고리면 그 시스템은 빠진다)
		std::vector<int> SimulationOrder() const;
		// 고칠 수 없는 구성 (없는 부모, 고리, 모르는 블록 …) — 그래프 창 · CLI 가 보여 준다
		std::vector<std::string> Validate() const;
	};

	json SystemToJson(const System& s);
	System SystemFromJson(const json& j);
	json BlockToJson(const Block& b);
	Block BlockFromJson(const json& j);
	json PropertyToJson(const Property& p);
	Property PropertyFromJson(const json& j);
	const char* PropertyTypeName(PropertyType t);
	bool PropertyTypeFromName(const std::string& s, PropertyType& t);
	std::array<float, 4> ValueFromJson(const json& v, std::array<float, 4> fallback);

	// ---------------------------------------------------------------- 58. VFX.fx 의 블록 목록
	// 속성 값 (Visual Effect 의 덮어쓰기 포함) 을 묻는다: 없으면 false
	struct PropertySource
	{
		virtual bool Get(const std::string& name, std::array<float, 4>& out) const = 0;
		virtual ~PropertySource() = default;
	};
	struct Encoded
	{
		std::vector<std::array<float, 4>> Program;   // 머리 (종류, 값 칸 수, 0, 0) + 값 칸들
		uint32_t InitStart = 0, InitCount = 0, UpdateStart = 0, UpdateCount = 0;
	};
	// world = Visual Effect 의 월드 행렬 (행 벡터, 16 개) — World 시스템의 위치 · 방향 값에 곱한다 (Local 이면 무시).
	//  asset = 연산 노드 (블록의 Links) 를 찾는 곳 (없으면 이어진 값은 상수 그대로)
	void Encode(const System& s, const PropertySource& props, const float* world, Encoded& out, const Asset* asset = nullptr);
	// 연산 노드 하나를 스택 명령으로 (Validate · 그래프 창이 확인에 쓴다). 실패하면 false + 이유
	bool CompileOperator(const Asset& asset, int id, const PropertySource& props, std::vector<std::array<float, 4>>& out, std::string& error);

	// ---------------------------------------------------------------- 에셋 읽기 (경로마다 하나, 파일이 바뀌면 다시)
	struct Loaded
	{
		std::shared_ptr<const Asset> Data;
		uint64_t Revision = 0;           // 바뀔 때마다 오른다 (Visual Effect 가 다시 맞춘다)
		std::string Error;
	};
	Loaded Load(const std::string& assetPath);
	// 그래프 창: 저장 전에도 장면의 Visual Effect 가 바로 따라오게 (저장하면 파일과 같아진다)
	void SetLive(const std::string& assetPath, std::shared_ptr<const Asset> asset);
	void ClearLive(const std::string& assetPath);
	bool Save(const std::string& assetPath, const Asset& asset, std::string& error);
	std::wstring FullPath(const std::string& assetPath);
	std::vector<std::string> FindAssets();   // 프로젝트의 .vfx (Assets 기준 경로)

	// 새 에셋의 기본 (Unity 의 Simple Loop: 위로 흩어지는 빛 알갱이)
	Asset DefaultAsset();
	// 이름 있는 견본 (Fireworks · Magic Circle · Tornado · Sparks · Galaxy …) — 그래프 창의 New 메뉴, CLI 의 vfx new --template
	std::vector<std::string> TemplateNames();
	bool MakeTemplate(const std::string& name, Asset& out);
}
