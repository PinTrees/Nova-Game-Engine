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

struct AnimatorState
{
	std::string Name;
	std::string ClipPath;             // FBX 경로
	int ClipIndex = 0;
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
	void MarkChanged() { ++Revision; }
	// 편집 후 호출: Revision 증가 + 파일 저장
	void Commit() { MarkChanged(); Save(); }

	// ---- 편집 (Animator 창 / Inspector) ----
	std::string MakeUniqueStateName(int layer, const std::string& base) const;
	int AddState(int layer, const std::string& name, float x, float y);
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
