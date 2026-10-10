#pragma once
#include <string>
#include <vector>
#include <memory>

struct AnimationClip;

// Unity 의 Animator Controller 에셋 (.controller, JSON).
//  - Parameters: Float / Int / Bool / Trigger
//  - Layers: 각 레이어는 상태 머신 (상태, 전이, 기본 상태, Entry/Any State/Exit 위치)
//  - 상태의 Motion 은 "FBX 경로 + 클립 번호" 로 참조한다.
namespace AnimatorTypes
{
	enum class ParamType { Float, Int, Bool, Trigger };
	enum class ConditionMode { If, IfNot, Greater, Less, Equals, NotEqual };

	constexpr const char* kAnyState = "<Any State>";
	constexpr const char* kEntry = "<Entry>";
	constexpr const char* kExit = "<Exit>";
}

struct AnimatorParameter
{
	std::string Name;
	AnimatorTypes::ParamType Type = AnimatorTypes::ParamType::Float;
	float DefaultFloat = 0.0f;
	int DefaultInt = 0;
	bool DefaultBool = false;
};

struct AnimatorCondition
{
	std::string Parameter;
	AnimatorTypes::ConditionMode Mode = AnimatorTypes::ConditionMode::If;
	float Threshold = 0.0f;
};

struct AnimatorTransition
{
	std::string From;                 // 상태 이름, 또는 kAnyState / kEntry
	std::string To;                   // 상태 이름, 또는 kExit
	bool HasExitTime = true;
	float ExitTime = 0.75f;           // 정규화 시간 (0~1, 반복 상태면 매 루프)
	bool FixedDuration = true;
	float Duration = 0.25f;           // 초 (FixedDuration) 또는 정규화
	float Offset = 0.0f;              // 목적 상태 시작 위치 (정규화)
	bool CanTransitionToSelf = true;  // Any State 전이 전용
	bool Mute = false;
	bool Solo = false;
	std::vector<AnimatorCondition> Conditions;
};

// Unity 의 Blend Tree: 파라미터 값에 따라 여러 클립을 섞는다 (예: Speed 0 = Idle, 2 = Walk, 6 = Run).
// 자식 클립은 정규화 시간을 맞춰 재생한다 (걷기·뛰기 발이 맞게).
struct BlendTreeChild
{
	std::string ClipPath;
	int ClipIndex = 0;
	float Threshold = 0.0f;           // 1D
	float PosX = 0.0f, PosY = 0.0f;   // 2D
	float TimeScale = 1.0f;
	std::shared_ptr<AnimationClip> Clip;
	void LoadClip();
	// 표시 이름: 클립 이름이 "Take 001"·"mixamo.com" 같은 기본값이면 파일 이름 (Cameron@Walk)
	std::string DisplayName() const;
};

struct BlendTree
{
	enum Type { Simple1D = 0, SimpleDirectional2D = 1, FreeformDirectional2D = 2, FreeformCartesian2D = 3 };
	int BlendType = Simple1D;
	std::string ParameterX = "Blend";
	std::string ParameterY = "Blend";
	std::vector<BlendTreeChild> Children;

	bool Is2D() const { return BlendType != Simple1D; }
	// 파라미터 값 → 자식마다 가중치 (합 1). 1D = 이웃 두 개 선형, 2D = Gradient Band (Unity Freeform 과 같은 방식)
	void ComputeWeights(float x, float y, std::vector<float>& weights) const;
};

struct AnimatorState
{
	std::string Name;
	std::string ClipPath;             // FBX 경로
	int ClipIndex = 0;
	bool IsBlendTree = false;         // true 면 Clip 대신 Tree
	BlendTree Tree;
	float Speed = 1.0f;
	float CycleOffset = 0.0f;
	bool Loop = true;                 // Unity 의 클립 Loop Time
	bool WriteDefaults = true;
	bool FootIK = false;
	float PosX = 0.0f, PosY = 0.0f;   // 그래프 위치

	std::shared_ptr<AnimationClip> Clip;   // 런타임 캐시
	void LoadClip();
};

struct AnimatorLayer
{
	std::string Name = "Base Layer";
	float Weight = 1.0f;
	std::string DefaultState;
	std::vector<AnimatorState> States;
	std::vector<AnimatorTransition> Transitions;
	float EntryX = -300.0f, EntryY = 0.0f;
	float AnyX = -300.0f, AnyY = -120.0f;
	float ExitX = 500.0f, ExitY = 0.0f;

	int FindState(const std::string& name) const;

	// 실행 캐시 (Animator 의 프레임마다 전이 검사 — 이름 대신 번호): 컨트롤러 Revision 이 바뀌면 다시 만든다
	mutable unsigned CacheRevision = ~0u;
	mutable std::vector<int> TFrom;                    // 전이마다 출발 상태 번호 (-1 Any State, -2 Entry, -3 없는 상태)
	mutable std::vector<int> TTo;                      // 도착 상태 번호 (-1 Exit, -3 없는 상태)
	mutable std::vector<std::vector<int>> TParams;     // 전이 · 조건마다 파라미터 번호
	mutable std::vector<char> SoloFrom;                // 상태마다 Solo 전이가 있나
	mutable bool SoloAny = false;
	mutable int DefaultIndex = -1;
};

class AnimatorController
{
public:
	std::string Path;                 // 에셋 경로 (프로젝트 기준 "Assets\\..." 또는 엔진 "Resources\\...")
	std::vector<AnimatorParameter> Parameters;
	std::vector<AnimatorLayer> Layers;
	unsigned Revision = 0;            // 편집할 때마다 증가 (런타임이 구조 변경을 감지)

	int FindParameter(const std::string& name) const;
	std::string Name() const;

	bool Save() const;
	// Undo 용: 전체 상태를 JSON 문자열로 / 문자열에서 그대로 되돌리기 (경로와 캐시는 유지)
	std::string ToJsonString() const;
	void ApplyJson(const std::string& text);
	void MarkChanged() { ++Revision; }
	// 편집 후 호출: Revision 증가 + 파일 저장
	void Commit() { MarkChanged(); Save(); }

	// ---- 편집 (Animator 창 / Inspector) ----
	std::string MakeUniqueStateName(int layer, const std::string& base) const;
	int AddState(int layer, const std::string& name, float x, float y);
	// 상태의 Motion 을 Blend Tree 로 (Float 파라미터가 없으면 "Blend" 를 만든다)
	void MakeBlendTree(int layer, int state);
	void RemoveState(int layer, int state);
	bool RenameState(int layer, int state, const std::string& newName);
	int AddTransition(int layer, const std::string& from, const std::string& to);
	void RemoveTransition(int layer, int index);
	std::string MakeUniqueParameterName(const std::string& base) const;
	int AddParameter(AnimatorTypes::ParamType type);
	void RemoveParameter(int index);
	bool RenameParameter(int index, const std::string& newName);
	int AddLayer();
	void RemoveLayer(int index);

	// 캐시된 컨트롤러 (같은 경로면 같은 객체)
	static std::shared_ptr<AnimatorController> Load(const std::string& path);
	// 새 컨트롤러 파일 만들기 (Base Layer 하나)
	static std::shared_ptr<AnimatorController> Create(const std::string& path);
};
