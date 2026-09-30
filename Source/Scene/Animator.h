#pragma once
#include "Component.h"
#include "AnimatorController.h"

class SkinnedMeshRenderer;
class SkeletonAvataData;

// Unity 의 Animator 컴포넌트.
//  - Animator Controller(.controller) 의 상태 머신을 실행한다: 조건/Exit Time 으로 전이, 전이 동안 두 상태를 섞는다(크로스페이드).
//  - 파라미터 API: SetFloat / SetInteger / SetBool / SetTrigger (+ Get), Play / CrossFade.
//  - 포즈는 자기와 자식들의 Skinned Mesh Renderer 에 넣는다 (Animation 컴포넌트와 같은 방식).
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

	// 런타임
	std::vector<float> m_Floats;   // 파라미터 값 (Float/Int/Bool/Trigger 모두 float 로 보관)
	std::vector<LayerRuntime> m_Layers;
	unsigned m_ControllerRevision = 0;
	bool m_Started = false;

	// 포즈 계산 캐시
	struct ClipMap { const AnimationClip* Clip = nullptr; const SkeletonAvataData* Skeleton = nullptr; std::vector<int> Map; };
	std::vector<ClipMap> m_ClipMaps;
	std::vector<XMFLOAT4X4> m_LocalA, m_LocalB, m_LocalLayer, m_LocalFinal, m_Global;

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
	float StateDuration(const AnimatorLayer& layer, int state) const;
	void SampleState(const AnimatorLayer& layer, int state, float time, const SkeletonAvataData& skeleton, std::vector<XMFLOAT4X4>& out);
	void CollectRenderers(GameObject* go, std::vector<SkinnedMeshRenderer*>& out);

	GENERATE_COMPONENT_BODY(Animator)
};

REGISTER_COMPONENT(Animator)
