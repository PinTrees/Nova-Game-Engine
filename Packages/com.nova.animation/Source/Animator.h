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
	bool m_NeedPreview = true;     // 편집 중: 컨트롤러가 바뀌면 다음 프레임에 첫 포즈 (씬을 열거나 캐릭터를 만든 직후에도 보이게)

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
	const SkeletonAvataData* m_GlobalFor = nullptr;   // m_Global 을 마지막으로 계산한 스켈레톤

	// 자세 평가 모으기 (Play 의 Update): 상태 머신은 Update 에서, 자세는 모든 Update 뒤 한 번에 작업 스레드로 (FlushPendingPoses)
	bool m_PosePending = false;
	bool m_PoseSkipped = false;   // 임포스터로만 그려 자세를 건너뛰었다 (메시 단계로 돌아오면 바로 계산)
	std::vector<SkinnedMeshRenderer*> m_UpdateRenderers, m_EvalRenderers;
	// 렌더러 목록 캐시: 자식 수가 바뀌거나 렌더러 컴포넌트가 사라지면 (weak_ptr) 다시 모은다 — 모듈형 캐릭터 1000 명 × 부위 17 개를 프레임마다 찾지 않게
	std::vector<std::weak_ptr<Component>> m_RendererRefs;
	size_t m_RendererKids = (size_t)-1;
	uint32_t m_RendererSerial = ~0u;   // weak_ptr 를 마지막으로 본 때의 물리 번호 (컴포넌트를 지우면 오른다 — 그대로면 보지 않는다)
	// 그리는 렌더러 (합쳐진 부위를 뺀 것 — 대표가 그린다). 힌트 · 보이는지 · 자세는 이것만 (부위 17 개 → 대표 1 ~ 2 개)
	std::vector<SkinnedMeshRenderer*> m_DrawRenderers;
	uint32_t m_MergeSerial = ~0u;
	uint32_t m_RenderersFrame = ~0u;   // m_UpdateRenderers 가 이 프레임 것 (EvaluatePose 가 다시 모으지 않는다)
	void RefreshRenderers();
	void CollectRendererRefs(GameObject* go);
	// 전역 행렬을 계산할 노드 (팔레트 본 · 사람 본과 그 조상 — 오름차순). 모델의 메시 노드 수백 개는 계산하지 않는다
	std::vector<int> m_NeededNodes;
	const SkeletonAvataData* m_NeededFor = nullptr;
	size_t m_NeededKey = 0;
	void UpdateNeededNodes(const SkeletonAvataData& skeleton, const std::vector<SkinnedMeshRenderer*>& renderers);
	bool CanEvaluateOffMain();      // 클립이 다 읽혀 있고 자세 후처리 (IK 등) 가 없으면
	bool AnyRendererVisible(const std::vector<SkinnedMeshRenderer*>& renderers);   // Culling Mode: 지난 카메라 · 그림자에 보인 렌더러가 있나
	bool PoseDueThisFrame(const std::vector<SkinnedMeshRenderer*>& renderers);     // 자동 LOD 렌더러가 멀면 2 · 4 프레임에 한 번 (캐릭터마다 엇갈려)
	void StepState(float dt);       // 상태 머신 + 루트 모션 (자세 계산 없이)

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
	// 이번 프레임 Update 가 모은 자세를 계산한다 (Scene 의 Update 뒤 훅 — Unity 의 애니메이션 평가 자리). 작업 스레드로 나눈다
	static void FlushPendingPoses();
	static int LastFlushCount();       // 지난 Flush 의 자세 수 (검사)
	static int LastFlushParallel();    // 그중 작업 스레드에서 계산한 수
	int GetCullingMode() const { return m_CullingMode; }
	void SetCullingMode(int mode) { m_CullingMode = std::clamp(mode, 0, 2); }

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
	// Unity: 오브젝트를 다시 켜면 기본 상태부터 (Keep Animator State On Disable 이 꺼진 기본값과 같다)
	void OnHierarchyActiveChanged(bool active) override { if (active) Rebind(); }
	// Unity 의 GetBoneTransform 대신: 사람 본(Humanoid::Bone)의 월드 위치·회전 (마지막 포즈, IK 포함). 없으면 false
	bool GetHumanBoneWorld(int bone, XMFLOAT3& position, XMFLOAT4& rotation);

public:
	virtual void Awake() override;
	virtual void Start() override;
	virtual void PrewarmStaged() override;   // 씬 스트리밍: 스켈레톤의 Humanoid 표 · 기본 상태 샘플링 캐시를 바꿔 끼우기 전에
	virtual void Update() override;
	virtual void FixedUpdate() override;
	virtual void LastUpdate() override;

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "animator"; }

private:
	void SyncWithController();
	void ResetRuntime();
	void Step(float dt);
	void StepLayer(int layerIndex, float dt);
	bool CheckConditions(const AnimatorTransition& t);
	bool CheckConditionsCached(const AnimatorLayer& layer, int transition);   // 파라미터 번호 캐시 (EnsureLayerCache)
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
	// LegsAnimator · LookAnimator 등 같은 GameObject 의 포즈 후처리 (Play 중)
	void ApplyPoseModifiers(const SkeletonAvataData& skeleton, SkinnedMeshRenderer* renderer);
	SkinnedMeshRenderer* PrimaryRenderer();
	void CollectRenderers(GameObject* go, std::vector<SkinnedMeshRenderer*>& out);

	GENERATE_COMPONENT_BODY(Animator)
};

REGISTER_PACKAGE_COMPONENT(Animator)
