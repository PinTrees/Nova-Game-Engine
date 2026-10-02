#pragma once
#include "Component.h"
#include "AnimatorController.h"
#include <deque>

class SkinnedMeshRenderer;
class SkeletonAvataData;
namespace Humanoid { struct Avatar; }

// Unity 의 Animator 컴포넌트.
//  - Animator Controller(.controller) 의 상태 머신을 실행한다: 조건/Exit Time 으로 전이, 전이 동안 두 상태를 섞는다(크로스페이드).
//  - 파라미터 API: SetFloat / SetInteger / SetBool / SetTrigger (+ Get), Play / CrossFade.
//  - 포즈는 자기와 자식들의 Skinned Mesh Renderer 에 넣는다 (Animation 컴포넌트와 같은 방식).
//  - 상태의 Motion 은 클립 또는 Blend Tree (파라미터로 여러 클립을 섞는다 — Blend Tree 상태의 Time 은 정규화 시간).
//  - 루트 모션: 클립의 가장 위쪽 움직이는 본(보통 Hips)의 수평 이동은 늘 포즈에서 뺀다 (제자리 걷기, Unity Humanoid 기본값).
//    Apply Root Motion 이면 기본 레이어의 그 이동을 오브젝트로 옮긴다 (Character Controller 가 있으면 Move).
//  - Avatar Auto: 클립의 본 이름이 모델과 반도 맞지 않고 둘 다 사람 모양이면 Humanoid 리타게팅 (Mixamo 애니메이션을 다른 캐릭터에)
class Animator : public Component
{
public:
	// 레이어 하나의 실행 상태 (에디터 Live Link 표시에도 쓴다)
	struct LayerRuntime
	{
		int Current = -1;            // 상태 인덱스
		float Time = 0.0f;           // 현재 상태의 경과 시간 (초, 속도 반영)
		int Next = -1;               // 전이 중인 목적 상태 (-1 = 전이 없음)
		float NextTime = 0.0f;
		float TransitionElapsed = 0.0f;
		float TransitionDuration = 0.0f;
		int ActiveTransition = -1;   // 진행 중인 전이의 인덱스 (레이어 Transitions 안)
	};

private:
	std::string m_ControllerPath;
	std::shared_ptr<AnimatorController> m_Controller;
	bool m_ApplyRootMotion = false;
	bool m_AnimatePhysics = false;
	int m_UpdateMode = 0;          // Normal / Animate Physics / Unscaled Time
	int m_CullingMode = 0;         // Always Animate / Cull Update Transforms / Cull Completely
	int m_AvatarMode = 0;          // 0 Auto (이름이 다르면 Humanoid 리타게팅), 1 Generic (이름이 같은 본만)

	// 런타임
	std::vector<float> m_Floats;   // 파라미터 값 (Float/Int/Bool/Trigger 모두 float 로 보관)
	std::vector<LayerRuntime> m_Layers;
	unsigned m_ControllerRevision = 0;
	bool m_Started = false;

	// 포즈 계산 캐시 (RootChannel = 위치가 움직이는 채널 중 스켈레톤에서 가장 위쪽 — 루트 모션 본)
	struct ClipMap
	{
		const AnimationClip* Clip = nullptr;
		const SkeletonAvataData* Skeleton = nullptr;
		std::vector<int> Map;
		int RootChannel = -1;
		// Humanoid 리타게팅 (원래 스켈레톤 기준으로 샘플해 옮긴다)
		bool Retarget = false;
		std::shared_ptr<SkeletonAvataData> SourceSkeleton;
		const Humanoid::Avatar* Source = nullptr;
		const Humanoid::Avatar* Target = nullptr;
		std::vector<int> SourceMap;
		int SourceRootChannel = -1;
	};
	std::deque<ClipMap> m_ClipMaps;   // deque: 새로 넣어도 앞의 포인터가 그대로
	std::vector<XMFLOAT4X4> m_LocalA, m_LocalB, m_LocalLayer, m_LocalFinal, m_Global, m_TreeTmp, m_TreeMix;

	// 루트 모션
	struct RootSkeleton { const SkeletonAvataData* Skeleton = nullptr; std::vector<XMFLOAT4X4> BindGlobal; int Node = -1; };
	std::vector<RootSkeleton> m_RootSkeletons;
	float m_Speed = 1.0f;
	Vec3 m_RootDeltaModel = Vec3::Zero;   // 이번 Step 동안 모은 모델 공간 이동
	Vec3 m_DeltaPosition = Vec3::Zero;    // 마지막으로 오브젝트에 옮긴 월드 이동
	float m_LastDt = 0.0f;

public:
	Animator();
	virtual ~Animator();

	// ---- 컨트롤러 ----
	void SetController(const std::string& path);
	std::shared_ptr<AnimatorController> GetController() const { return m_Controller; }
	const std::string& GetControllerPath() const { return m_ControllerPath; }

	// ---- 파라미터 (Unity API) ----
	void SetFloat(const std::string& name, float v);
	void SetInteger(const std::string& name, int v);
	void SetBool(const std::string& name, bool v);
	void SetTrigger(const std::string& name);
	void ResetTrigger(const std::string& name);
	float GetFloat(const std::string& name) const;
	int GetInteger(const std::string& name) const;
	bool GetBool(const std::string& name) const;
	// 에디터 Parameters 탭에서 값을 직접 바꿀 때
	float GetParameterValue(int index) const { return index >= 0 && index < (int)m_Floats.size() ? m_Floats[index] : 0.0f; }
	void SetParameterValue(int index, float v) { if (index >= 0 && index < (int)m_Floats.size()) m_Floats[index] = v; }

	// ---- 재생 제어 ----
	void Play(const std::string& stateName, int layer = 0, float normalizedTime = 0.0f);
	void CrossFade(const std::string& stateName, float duration, int layer = 0);
	const LayerRuntime* GetLayerRuntime(int layer) const { return layer >= 0 && layer < (int)m_Layers.size() ? &m_Layers[layer] : nullptr; }
	float GetNormalizedTime(int layer, int state, float time) const;
	std::string GetCurrentStateName(int layer = 0) const;
	bool IsInTransition(int layer = 0) const { auto* r = GetLayerRuntime(layer); return r && r->Next >= 0; }

	// 현재 포즈를 렌더러에 적용
	void EvaluatePose();

	// ---- Unity: speed, applyRootMotion, deltaPosition, velocity
	void SetSpeed(float s) { m_Speed = s; }
	float GetSpeed() const { return m_Speed; }
	void SetApplyRootMotion(bool b) { m_ApplyRootMotion = b; }
	bool GetApplyRootMotion() const { return m_ApplyRootMotion; }
	Vec3 GetDeltaPosition() const { return m_DeltaPosition; }
	Vec3 GetVelocity() const { return m_LastDt > 1e-6f ? m_DeltaPosition / m_LastDt : Vec3::Zero; }
	// 현재 상태 한 바퀴 길이 (초, Blend Tree 는 지금 가중치로)
	float GetCurrentStateLength(int layer = 0) const;
	// Blend Tree 상태의 자식 가중치 (에디터 표시·검사용)
	void GetBlendWeights(const AnimatorState& state, std::vector<float>& weights) const;
	// Unity 의 Animator.Update(deltaTime): 수동으로 시간을 진행한다
	void Update(float deltaTime) { Step(deltaTime); }
	// Unity 의 Animator.Rebind(): 기본 상태로 되돌리고 포즈를 다시 계산한다
	void Rebind() { ResetRuntime(); EvaluatePose(); }

public:
	virtual void Awake() override;
	virtual void Start() override;
	virtual void Update() override;
	virtual void FixedUpdate() override;

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "animator"; }

private:
	void SyncWithController();
	void ResetRuntime();
	void Step(float dt);
	void StepLayer(int layerIndex, float dt);
	bool CheckConditions(const AnimatorTransition& t);
	void ConsumeTriggers(const AnimatorTransition& t);
	void StartTransition(int layerIndex, int transitionIndex, int target);
	// Time 단위의 한 바퀴 (클립 = 초, Blend Tree = 1 — 정규화)
	float StateDuration(const AnimatorLayer& layer, int state) const;
	// 한 바퀴의 실제 초 (전이 길이 등)
	float RealDuration(const AnimatorLayer& layer, int state) const;
	// 초당 Time 증가 (클립 1, Blend Tree 1/길이)
	float TimeRate(const AnimatorLayer& layer, int state) const;
	float BlendTreeDuration(const AnimatorState& state) const;
	void SampleState(const AnimatorLayer& layer, int state, float time, const SkeletonAvataData& skeleton, std::vector<XMFLOAT4X4>& out);
	void SampleClip(const AnimationClip* clip, bool loop, float t, const SkeletonAvataData& skeleton, std::vector<XMFLOAT4X4>& out);
	ClipMap* GetClipMap(const AnimationClip* clip, const SkeletonAvataData& skeleton);
	RootSkeleton* GetRootSkeleton(const SkeletonAvataData& skeleton);
	XMFLOAT3 ClipRootPosition(const AnimationClip* clip, const SkeletonAvataData& skeleton, float t);
	Vec3 ClipRootDelta(const AnimationClip* clip, bool loop, const SkeletonAvataData& skeleton, float t0, float t1);
	Vec3 StateRootDelta(const AnimatorLayer& layer, int state, const SkeletonAvataData& skeleton, float t0, float t1);
	void ApplyRootMotion(float dt);
	void PinRoot(const SkeletonAvataData& skeleton, std::vector<XMFLOAT4X4>& local);
	SkinnedMeshRenderer* PrimaryRenderer();
	void CollectRenderers(GameObject* go, std::vector<SkinnedMeshRenderer*>& out);

	GENERATE_COMPONENT_BODY(Animator)
};

REGISTER_COMPONENT(Animator)
