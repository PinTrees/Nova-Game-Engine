#include "pch.h"
#include "Animator.h"
#include "UnityGUI.h"
#include "AnimationPose.h"
#include "SkinnedMeshRenderer.h"
#include "SkinnedMesh.h"
#include "PhysicsManager.h"
#include "CharacterController.h"
#include "HumanoidAvatar.h"
#include "SceneStreaming.h"
#include "AnimatorIK.h"
#include "JobSystem.h"
#include "Profiler.h"
#include "SkinnedLod.h"
#include "SceneCulling.h"

using namespace AnimatorTypes;

namespace
{
	// 컨트롤러 파일 목록 (Inspector 선택 팝업)
	std::vector<std::string> ScanControllers()
	{
		std::vector<std::string> out;
		auto scan = [&](const std::wstring& root, const std::wstring& prefix) {
			std::error_code ec;
			if (!std::filesystem::exists(root, ec))
				return;
			for (const auto& e : std::filesystem::recursive_directory_iterator(root, ec))
				if (e.is_regular_file() && e.path().extension() == L".controller")
					out.push_back(wstring_to_string(prefix + std::filesystem::relative(e.path(), root, ec).wstring()));
		};
		scan(PathManager::GetI()->GetMovePathW(L"Assets\\"), L"Assets\\");
		scan(PathManager::GetI()->GetMovePathW(L"Resources\\Packages\\"), L"Resources\\Packages\\");
		return out;
	}
}

Animator::Animator()
{
	m_InspectorTitleName = "Animator";
}

namespace
{
	std::vector<Animator*> s_PendingPoses;
	int s_LastFlush = 0, s_LastFlushParallel = 0;
}

Animator::~Animator()
{
	if (m_PosePending)
		s_PendingPoses.erase(std::remove(s_PendingPoses.begin(), s_PendingPoses.end(), this), s_PendingPoses.end());
}

// ------------------------------------------------------------------ 컨트롤러 / 파라미터
void Animator::SetController(const std::string& path)
{
	m_Controller = AnimatorController::Load(path);
	m_ControllerPath = m_Controller ? m_Controller->Path : path;
	m_ControllerRevision = ~0u;
	ResetRuntime();
	m_NeedPreview = true;
}

void Animator::ResetRuntime()
{
	m_Floats.clear();
	m_Layers.clear();
	if (m_Controller == nullptr)
		return;
	for (const auto& p : m_Controller->Parameters)
		m_Floats.push_back(p.Type == ParamType::Int ? (float)p.DefaultInt : (p.Type == ParamType::Bool ? (p.DefaultBool ? 1.0f : 0.0f) : (p.Type == ParamType::Float ? p.DefaultFloat : 0.0f)));
	m_Layers.resize(m_Controller->Layers.size());
	for (size_t i = 0; i < m_Controller->Layers.size(); ++i)
	{
		const AnimatorLayer& layer = m_Controller->Layers[i];
		LayerRuntime& r = m_Layers[i];
		r = LayerRuntime();
		r.Current = layer.FindState(layer.DefaultState);
		if (r.Current < 0 && !layer.States.empty())
			r.Current = 0;
		if (r.Current >= 0)
			r.Time = layer.States[r.Current].CycleOffset * StateDuration(layer, r.Current);
	}
	m_ControllerRevision = m_Controller->Revision;
}

// 에디터에서 컨트롤러를 고치면 (상태/파라미터 추가·삭제) 런타임을 맞춘다
void Animator::SyncWithController()
{
	if (m_Controller == nullptr)
		return;
	if (m_ControllerRevision == m_Controller->Revision && m_Layers.size() == m_Controller->Layers.size() && m_Floats.size() == m_Controller->Parameters.size())
		return;
	std::vector<float> old = m_Floats;
	m_Floats.resize(m_Controller->Parameters.size(), 0.0f);
	m_Layers.resize(m_Controller->Layers.size());
	for (size_t i = 0; i < m_Layers.size(); ++i)
	{
		const AnimatorLayer& layer = m_Controller->Layers[i];
		LayerRuntime& r = m_Layers[i];
		if (r.Current >= (int)layer.States.size()) r.Current = layer.FindState(layer.DefaultState);
		if (r.Current < 0 && !layer.States.empty()) r.Current = 0;
		if (r.Next >= (int)layer.States.size() || r.ActiveTransition >= (int)layer.Transitions.size()) { r.Next = -1; r.ActiveTransition = -1; }
	}
	m_ControllerRevision = m_Controller->Revision;
}

static int ParamIndex(const std::shared_ptr<AnimatorController>& c, const std::string& name)
{
	return c ? c->FindParameter(name) : -1;
}

void Animator::SetFloat(const std::string& name, float v) { SyncWithController(); int i = ParamIndex(m_Controller, name); if (i >= 0) m_Floats[i] = v; }
void Animator::SetInteger(const std::string& name, int v) { SyncWithController(); int i = ParamIndex(m_Controller, name); if (i >= 0) m_Floats[i] = (float)v; }
void Animator::SetBool(const std::string& name, bool v) { SyncWithController(); int i = ParamIndex(m_Controller, name); if (i >= 0) m_Floats[i] = v ? 1.0f : 0.0f; }
void Animator::SetTrigger(const std::string& name) { SetBool(name, true); }
void Animator::ResetTrigger(const std::string& name) { SetBool(name, false); }
float Animator::GetFloat(const std::string& name) const { int i = ParamIndex(m_Controller, name); return i >= 0 && i < (int)m_Floats.size() ? m_Floats[i] : 0.0f; }
int Animator::GetInteger(const std::string& name) const { return (int)GetFloat(name); }
bool Animator::GetBool(const std::string& name) const { return GetFloat(name) != 0.0f; }

// ------------------------------------------------------------------ 재생 제어
float Animator::StateDuration(const AnimatorLayer& layer, int state) const
{
	if (state < 0 || state >= (int)layer.States.size())
		return 1.0f;
	const AnimatorState& s = layer.States[state];
	if (s.IsBlendTree)
		return 1.0f;   // Blend Tree 의 Time 은 정규화 시간
	const float d = s.Clip ? s.Clip->GetClipEndTime() : 0.0f;
	return d > 1e-4f ? d : 1.0f;
}

float Animator::RealDuration(const AnimatorLayer& layer, int state) const
{
	if (state >= 0 && state < (int)layer.States.size() && layer.States[state].IsBlendTree)
		return BlendTreeDuration(layer.States[state]);
	return StateDuration(layer, state);
}

float Animator::TimeRate(const AnimatorLayer& layer, int state) const
{
	if (state >= 0 && state < (int)layer.States.size() && layer.States[state].IsBlendTree)
		return 1.0f / BlendTreeDuration(layer.States[state]);
	return 1.0f;
}

void Animator::GetBlendWeights(const AnimatorState& s, std::vector<float>& w) const
{
	s.Tree.ComputeWeights(GetFloat(s.Tree.ParameterX), GetFloat(s.Tree.ParameterY), w);
}

// Unity 와 같이 자식 길이를 가중치로 평균 (Time Scale 이 크면 짧아진다)
float Animator::BlendTreeDuration(const AnimatorState& s) const
{
	std::vector<float> w;
	GetBlendWeights(s, w);
	float d = 0.0f;
	for (size_t i = 0; i < w.size(); ++i)
	{
		const BlendTreeChild& c = s.Tree.Children[i];
		if (w[i] <= 0.0f || c.Clip == nullptr)
			continue;
		d += w[i] * c.Clip->GetClipEndTime() / (std::max)(0.01f, c.TimeScale);
	}
	return d > 1e-3f ? d : 1.0f;
}

float Animator::GetCurrentStateLength(int layerIndex) const
{
	const LayerRuntime* r = GetLayerRuntime(layerIndex);
	if (r == nullptr || m_Controller == nullptr || layerIndex >= (int)m_Controller->Layers.size())
		return 0.0f;
	return RealDuration(m_Controller->Layers[layerIndex], r->Current);
}

float Animator::GetNormalizedTime(int layerIndex, int state, float time) const
{
	if (m_Controller == nullptr || layerIndex < 0 || layerIndex >= (int)m_Controller->Layers.size())
		return 0.0f;
	return time / StateDuration(m_Controller->Layers[layerIndex], state);
}

std::string Animator::GetCurrentStateName(int layerIndex) const
{
	const LayerRuntime* r = GetLayerRuntime(layerIndex);
	if (r == nullptr || m_Controller == nullptr || r->Current < 0 || r->Current >= (int)m_Controller->Layers[layerIndex].States.size())
		return "";
	return m_Controller->Layers[layerIndex].States[r->Current].Name;
}

void Animator::Play(const std::string& stateName, int layerIndex, float normalizedTime)
{
	SyncWithController();
	if (m_Controller == nullptr || layerIndex < 0 || layerIndex >= (int)m_Layers.size())
		return;
	const AnimatorLayer& layer = m_Controller->Layers[layerIndex];
	int s = layer.FindState(stateName);
	if (s < 0)
		return;
	LayerRuntime& r = m_Layers[layerIndex];
	r.Current = s;
	r.Time = normalizedTime * StateDuration(layer, s);
	r.Next = -1;
	r.ActiveTransition = -1;
}

void Animator::CrossFade(const std::string& stateName, float duration, int layerIndex)
{
	SyncWithController();
	if (m_Controller == nullptr || layerIndex < 0 || layerIndex >= (int)m_Layers.size())
		return;
	int s = m_Controller->Layers[layerIndex].FindState(stateName);
	if (s < 0)
		return;
	LayerRuntime& r = m_Layers[layerIndex];
	if (duration <= 0.0f || r.Current < 0)
	{
		Play(stateName, layerIndex);
		return;
	}
	r.Next = s;
	r.NextTime = 0.0f;
	r.TransitionElapsed = 0.0f;
	r.TransitionDuration = duration;
	r.ActiveTransition = -1;
}

// ------------------------------------------------------------------ 상태 머신
// 레이어의 전이를 번호로 (프레임마다 상태 · 파라미터 이름을 찾지 않게 — 군중 1000 명)
static void EnsureLayerCache(const AnimatorLayer& layer, const AnimatorController& controller)
{
	if (layer.CacheRevision == controller.Revision && layer.TFrom.size() == layer.Transitions.size())
		return;
	layer.CacheRevision = controller.Revision;
	const size_t n = layer.Transitions.size();
	layer.TFrom.assign(n, -3);
	layer.TTo.assign(n, -3);
	layer.TParams.assign(n, {});
	layer.SoloFrom.assign(layer.States.size(), 0);
	layer.SoloAny = false;
	for (size_t i = 0; i < n; ++i)
	{
		const AnimatorTransition& t = layer.Transitions[i];
		layer.TFrom[i] = t.From == kAnyState ? -1 : t.From == kEntry ? -2 : layer.FindState(t.From);
		if (layer.TFrom[i] == -1 && t.From != kAnyState) layer.TFrom[i] = -3;
		layer.TTo[i] = t.To == kExit ? -1 : layer.FindState(t.To);
		if (layer.TTo[i] < 0 && t.To != kExit) layer.TTo[i] = -3;
		for (const auto& c : t.Conditions)
			layer.TParams[i].push_back(controller.FindParameter(c.Parameter));
		if (t.Solo)
		{
			if (layer.TFrom[i] == -1) layer.SoloAny = true;
			else if (layer.TFrom[i] >= 0) layer.SoloFrom[(size_t)layer.TFrom[i]] = 1;
		}
	}
	layer.DefaultIndex = layer.FindState(layer.DefaultState);
}

bool Animator::CheckConditionsCached(const AnimatorLayer& layer, int ti)
{
	const AnimatorTransition& t = layer.Transitions[(size_t)ti];
	const std::vector<int>& params = layer.TParams[(size_t)ti];
	for (size_t k = 0; k < t.Conditions.size(); ++k)
	{
		const AnimatorCondition& c = t.Conditions[k];
		const int i = k < params.size() ? params[k] : -1;
		if (i < 0 || i >= (int)m_Floats.size())
			return false;
		const float v = m_Floats[(size_t)i];
		bool ok = false;
		switch (c.Mode)
		{
		case ConditionMode::If: ok = v != 0.0f; break;
		case ConditionMode::IfNot: ok = v == 0.0f; break;
		case ConditionMode::Greater: ok = v > c.Threshold; break;
		case ConditionMode::Less: ok = v < c.Threshold; break;
		case ConditionMode::Equals: ok = (int)v == (int)c.Threshold; break;
		case ConditionMode::NotEqual: ok = (int)v != (int)c.Threshold; break;
		}
		if (!ok)
			return false;
	}
	return true;
}

bool Animator::CheckConditions(const AnimatorTransition& t)
{
	for (const auto& c : t.Conditions)
	{
		const int i = m_Controller->FindParameter(c.Parameter);
		if (i < 0)
			return false;
		const float v = m_Floats[i];
		bool ok = false;
		switch (c.Mode)
		{
		case ConditionMode::If: ok = v != 0.0f; break;
		case ConditionMode::IfNot: ok = v == 0.0f; break;
		case ConditionMode::Greater: ok = v > c.Threshold; break;
		case ConditionMode::Less: ok = v < c.Threshold; break;
		case ConditionMode::Equals: ok = (int)v == (int)c.Threshold; break;
		case ConditionMode::NotEqual: ok = (int)v != (int)c.Threshold; break;
		}
		if (!ok)
			return false;
	}
	return true;
}

void Animator::ConsumeTriggers(const AnimatorTransition& t)
{
	// Unity: 전이에 쓰인 Trigger 는 소비된다
	for (const auto& c : t.Conditions)
	{
		const int i = m_Controller->FindParameter(c.Parameter);
		if (i >= 0 && m_Controller->Parameters[i].Type == ParamType::Trigger)
			m_Floats[i] = 0.0f;
	}
}

void Animator::StartTransition(int layerIndex, int transitionIndex, int target)
{
	const AnimatorLayer& layer = m_Controller->Layers[layerIndex];
	const AnimatorTransition& t = layer.Transitions[transitionIndex];
	LayerRuntime& r = m_Layers[layerIndex];
	const float duration = t.FixedDuration ? t.Duration : t.Duration * RealDuration(layer, r.Current);
	const float startTime = t.Offset * StateDuration(layer, target);
	if (duration <= 1e-4f || r.Current < 0)
	{
		r.Current = target;
		r.Time = startTime;
		r.Next = -1;
		r.ActiveTransition = -1;
		return;
	}
	r.Next = target;
	r.NextTime = startTime;
	r.TransitionElapsed = 0.0f;
	r.TransitionDuration = duration;
	r.ActiveTransition = transitionIndex;
}

void Animator::StepLayer(int layerIndex, float dt)
{
	const AnimatorLayer& layer = m_Controller->Layers[layerIndex];
	LayerRuntime& r = m_Layers[layerIndex];
	if (layer.States.empty())
		return;
	if (r.Current < 0)
		r.Current = (std::max)(0, layer.FindState(layer.DefaultState));

	// 초당 Time 증가: 상태 Speed × (Blend Tree 면 1/길이)
	auto speedOf = [&](int s) { return s >= 0 && s < (int)layer.States.size() ? layer.States[s].Speed * TimeRate(layer, s) : 1.0f; };
	// 루트 모션은 기본 레이어에서만 모은다
	const SkeletonAvataData* rootSkeleton = nullptr;
	if (layerIndex == 0 && m_ApplyRootMotion && Application::IsPlaying())
		if (SkinnedMeshRenderer* pr = PrimaryRenderer())
			rootSkeleton = pr->GetSkeleton().get();

	// 전이 진행 중 (중단 없음)
	if (r.Next >= 0)
	{
		const float a0 = r.Time, b0 = r.NextTime;
		r.Time += dt * speedOf(r.Current);
		r.NextTime += dt * speedOf(r.Next);
		r.TransitionElapsed += dt;
		if (rootSkeleton)
		{
			const float w = r.TransitionDuration > 0.0f ? std::clamp(r.TransitionElapsed / r.TransitionDuration, 0.0f, 1.0f) : 1.0f;
			m_RootDeltaModel += StateRootDelta(layer, r.Current, *rootSkeleton, a0, r.Time) * (1.0f - w)
				+ StateRootDelta(layer, r.Next, *rootSkeleton, b0, r.NextTime) * w;
		}
		if (r.TransitionElapsed >= r.TransitionDuration)
		{
			r.Current = r.Next;
			r.Time = r.NextTime;
			r.Next = -1;
			r.ActiveTransition = -1;
		}
		return;
	}

	const float duration = StateDuration(layer, r.Current);
	const float prevNorm = r.Time / duration;
	const float prevTime = r.Time;
	r.Time += dt * speedOf(r.Current);
	if (rootSkeleton)
		m_RootDeltaModel += StateRootDelta(layer, r.Current, *rootSkeleton, prevTime, r.Time);
	const float curNorm = r.Time / duration;

	// 이름 대신 번호 (EnsureLayerCache). Solo 가 하나라도 있으면 Solo 전이만 본다 (출발 상태별)
	EnsureLayerCache(layer, *m_Controller);
	const bool soloAny = layer.SoloAny;
	const bool soloCur = r.Current >= 0 && r.Current < (int)layer.SoloFrom.size() && layer.SoloFrom[(size_t)r.Current];

	// Any State 전이가 먼저, 그다음 현재 상태의 전이 (목록 순서 = 우선순위)
	for (int pass = 0; pass < 2; ++pass)
	{
		for (int i = 0; i < (int)layer.Transitions.size(); ++i)
		{
			const AnimatorTransition& t = layer.Transitions[i];
			const int from = layer.TFrom[(size_t)i];
			const bool fromAny = from == -1;
			if ((pass == 0) != fromAny)
				continue;
			if (!fromAny && from != r.Current)
				continue;
			if (t.Mute || ((fromAny ? soloAny : soloCur) && !t.Solo))
				continue;

			int target = -1;
			if (layer.TTo[(size_t)i] == -1)
			{
				// Exit → Entry 로 돌아가 Entry 전이(조건) 또는 기본 상태
				target = layer.DefaultIndex;
				for (int e = 0; e < (int)layer.Transitions.size(); ++e)
					if (layer.TFrom[(size_t)e] == -2 && CheckConditionsCached(layer, e)) { target = layer.TTo[(size_t)e]; break; }
			}
			else
				target = layer.TTo[(size_t)i];
			if (target < 0)
				continue;
			if (fromAny && !t.CanTransitionToSelf && target == r.Current)
				continue;

			bool exitOk = true;
			if (t.HasExitTime && !fromAny)
			{
				const float e = t.ExitTime;
				if (e < 1.0f)
					exitOk = floorf(curNorm - e) > floorf(prevNorm - e);   // 매 루프마다 e 지점을 지날 때
				else
					exitOk = curNorm >= e && prevNorm < e;
			}
			else if (t.Conditions.empty())
				exitOk = false;   // 조건도 Exit Time 도 없는 전이는 무시 (Unity 경고와 같은 경우)

			if (exitOk && CheckConditionsCached(layer, i))
			{
				ConsumeTriggers(t);
				StartTransition(layerIndex, i, target);
				return;
			}
		}
	}
}

void Animator::StepState(float dt)
{
	SyncWithController();
	const float scaled = dt * m_Speed;   // Animator.speed
	m_RootDeltaModel = Vec3::Zero;
	for (int i = 0; i < (int)m_Layers.size(); ++i)
		StepLayer(i, scaled);
	ApplyRootMotion(dt);
}

void Animator::Step(float dt)
{
	if (m_Controller == nullptr)
		return;
	StepState(dt);
	EvaluatePose();
}

// ------------------------------------------------------------------ 자세 평가 모으기 (군중)
bool Animator::AnyRendererVisible(const std::vector<SkinnedMeshRenderer*>& renderers)
{
	if (renderers.empty())
		return true;
	for (SkinnedMeshRenderer* r : renderers)
		if (r->IsEnabled() && (SceneCulling::IsVisible(r) || r->GetUpdateWhenOffscreen()))
			return true;
	return false;
}

bool Animator::PoseDueThisFrame(const std::vector<SkinnedMeshRenderer*>& renderers)
{
	// 자동 LOD (NOVA): 화면에 작게 보이는 캐릭터는 자세를 덜 자주 — LOD 2 는 2 프레임, LOD 3 은 4 프레임에 한 번.
	//  상태 · 시간은 매 프레임 나아간다 (다음 계산 때 그 시간의 자세). 캐릭터마다 엇갈려 한 프레임에 몰리지 않게
	SkinnedMeshRenderer* r = nullptr;
	for (SkinnedMeshRenderer* c : renderers)
		if (c->GetSkeleton()) { r = c; break; }
	if (r == nullptr || !r->GetAutoLod())
		return true;
	const int lod = r->CurrentLod();
	// 임포스터 (4) 는 그림 대신 클립 · 시간만 쓴다 — 그림자에도 보이지 않으면 자세가 필요 없다 (메시 단계로 돌아오면 그 프레임에 바로 다시)
	if (lod >= SkinnedLod::kImpostorLevel && SceneCulling::Enabled && r->ShadowCullStamp != SceneCulling::ShadowStamp)
	{
		m_PoseSkipped = true;
		return false;
	}
	if (m_PoseSkipped)
	{
		m_PoseSkipped = false;
		return true;
	}
	// 그림자 (가장 낮은 메시) 용 자세만 가끔
	const uint32_t interval = lod >= 4 ? 8u : lod == 3 ? 4u : lod == 2 ? 2u : 1u;
	if (interval == 1)
		return true;
	const uint32_t phase = (uint32_t)(((uintptr_t)this >> 6) * 2654435761u >> 16);
	return (SceneCulling::FrameIndex() + phase) % interval == 0;
}

bool Animator::CanEvaluateOffMain()
{
	if (m_Controller == nullptr || m_pGameObject == nullptr)
		return true;
	// 자세 후처리 (Legs · Look · Hands Animator · Dynamic Bone): 물리 · Transform 을 읽고 쓴다 → 메인
	for (const auto& c : m_pGameObject->GetComponents())
		if (c && c->IsEnabled() && dynamic_cast<IAnimatorPoseModifier*>(c.get()))
			return false;
	// 클립 읽기 (에셋) 는 메인 — 처음 쓰는 상태가 있으면 메인에서 읽는다
	auto loaded = [](const AnimatorState& s) {
		if (!s.IsBlendTree)
			return s.Clip != nullptr || s.ClipPath.empty();
		for (const BlendTreeChild& c : s.Tree.Children)
			if (c.Clip == nullptr && !c.ClipPath.empty())
				return false;
		return true;
	};
	for (int li = 0; li < (int)m_Controller->Layers.size() && li < (int)m_Layers.size(); ++li)
	{
		const AnimatorLayer& layer = m_Controller->Layers[li];
		for (int state : { m_Layers[li].Current, m_Layers[li].Next })
			if (state >= 0 && state < (int)layer.States.size() && !loaded(layer.States[state]))
				return false;
	}
	return true;
}

void Animator::FlushPendingPoses()
{
	s_LastFlush = (int)s_PendingPoses.size();
	s_LastFlushParallel = 0;
	if (s_PendingPoses.empty())
		return;
	PROFILE_SCOPE("Animator.EvaluatePoses");
	std::vector<Animator*> pending;
	pending.swap(s_PendingPoses);
	std::vector<Animator*> parallel, main;
	parallel.reserve(pending.size());
	for (Animator* a : pending)
	{
		a->m_PosePending = false;
		(a->CanEvaluateOffMain() ? parallel : main).push_back(a);
	}
	// 캐릭터마다 따로 (자기 캐시 · 자기 렌더러 팔레트만 쓴다). 공유하는 Humanoid 표는 잠금 · 스레드마다 임시 버퍼
	Jobs::ParallelFor((int)parallel.size(), 8, [&](int b, int e) {
		for (int i = b; i < e; ++i)
			parallel[(size_t)i]->EvaluatePose();
	}, "Animator Pose");
	for (Animator* a : main)
		a->EvaluatePose();
	s_LastFlushParallel = (int)parallel.size();
}

int Animator::LastFlushCount() { return s_LastFlush; }
int Animator::LastFlushParallel() { return s_LastFlushParallel; }

// ------------------------------------------------------------------ 포즈
namespace
{
	// 루트 모션 채널: 위치가 실제로 움직이는 채널 중 스켈레톤에서 가장 위쪽 (보통 Hips)
	int FindRootChannel(const AnimationClip& clip, const std::vector<int>& map, const SkeletonAvataData& skeleton)
	{
		int best = -1, bestDepth = INT_MAX;
		for (size_t c = 0; c < clip.Channels.size() && c < map.size(); ++c)
		{
			const int node = map[c];
			const auto& keys = clip.Channels[c].Positions;
			if (node < 0 || keys.size() < 2)
				continue;
			float move = 0.0f;
			for (const auto& k : keys)
				move = (std::max)(move, fabsf(k.Value.x - keys[0].Value.x) + fabsf(k.Value.y - keys[0].Value.y) + fabsf(k.Value.z - keys[0].Value.z));
			if (move < 1e-4f)
				continue;
			int depth = 0;
			for (int p = node; p >= 0 && p < (int)skeleton.BoneHierarchy.size() && depth < 512; p = skeleton.BoneHierarchy[p])
				++depth;
			if (depth < bestDepth)
			{
				bestDepth = depth;
				best = (int)c;
			}
		}
		return best;
	}

	// 이름이 맞는 두 스켈레톤의 바인드 자세가 같은가 (사람 본의 바인드 로컬 회전 + Hips 의 바인드 전역 회전이 15 도 안).
	//  같은 모델 계열이면 이름 그대로 옮겨도 되지만, 이름만 같고 축 · 쉬는 자세가 다른 리그 (Unreal 마네킹 클립 → 다른 회사 캐릭터) 는
	//  로컬 회전을 그대로 옮기면 누워 버린다 → Humanoid 로 옮긴다 (Unity 의 Humanoid 클립과 같게)
	bool SameBindPose(const SkeletonAvataData& a, const Humanoid::Avatar& av, const SkeletonAvataData& b, const Humanoid::Avatar& bv)
	{
		auto rotationOf = [](const XMFLOAT4X4& m) {
			XMVECTOR scale, rot, pos;
			if (!XMMatrixDecompose(&scale, &rot, &pos, XMLoadFloat4x4(&m)))
				return XMQuaternionIdentity();
			return XMQuaternionNormalize(rot);
		};
		auto close = [](XMVECTOR p, XMVECTOR q) { return fabsf(XMVectorGetX(XMQuaternionDot(p, q))) > cosf(XMConvertToRadians(15.0f) * 0.5f); };
		static const Humanoid::Bone kBones[] = { Humanoid::Hips, Humanoid::Spine, Humanoid::Chest, Humanoid::Head, Humanoid::LeftUpperArm, Humanoid::RightUpperArm,
			Humanoid::LeftLowerArm, Humanoid::RightLowerArm, Humanoid::LeftUpperLeg, Humanoid::RightUpperLeg, Humanoid::LeftLowerLeg, Humanoid::RightLowerLeg };
		for (Humanoid::Bone bone : kBones)
		{
			const int na = av.Node[bone], nb = bv.Node[bone];
			if (na < 0 || nb < 0 || (size_t)na >= a.BindLocal.size() || (size_t)nb >= b.BindLocal.size())
				continue;
			if (!close(rotationOf(a.BindLocal[(size_t)na]), rotationOf(b.BindLocal[(size_t)nb])))
				return false;
		}
		std::vector<XMFLOAT4X4> ga, gb;
		AnimationPose::ComputeGlobals(a, a.BindLocal, ga);
		AnimationPose::ComputeGlobals(b, b.BindLocal, gb);
		const int ha = av.Node[Humanoid::Hips], hb = bv.Node[Humanoid::Hips];
		if (ha >= 0 && hb >= 0 && (size_t)ha < ga.size() && (size_t)hb < gb.size() && !close(rotationOf(ga[(size_t)ha]), rotationOf(gb[(size_t)hb])))
			return false;
		return true;
	}
}

Animator::ClipMap* Animator::GetClipMap(const AnimationClip* clip, const SkeletonAvataData& skeleton)
{
	for (auto& m : m_ClipMaps)
		if (m.Clip == clip && m.Skeleton == &skeleton)
			return &m;
	ClipMap m{ clip, &skeleton, AnimationPose::MapChannels(*clip, skeleton) };
	// Avatar Auto: 이름으로 맞는 채널이 반도 안 되면 원래 스켈레톤과 사람 본으로 옮긴다
	int mapped = 0;
	for (int node : m.Map)
		mapped += node >= 0;
	auto source = clip->SourceSkeleton.lock();
	if (m_AvatarMode == 0 && source && source.get() != &skeleton)
	{
		const bool namesMatch = mapped * 2 >= (int)clip->Channels.size();
		const Humanoid::Avatar& src = Humanoid::Get(*source);
		const Humanoid::Avatar& dst = Humanoid::Get(skeleton);
		// 이름이 반 넘게 맞아도 바인드 자세가 다르면 Humanoid 로 (이름만 같은 다른 리그)
		if (src.Valid && dst.Valid && (!namesMatch || !SameBindPose(*source, src, skeleton, dst)))
		{
			m.Retarget = true;
			m.SourceSkeleton = source;
			m.Source = &src;
			m.Target = &dst;
			m.SourceMap = AnimationPose::MapChannels(*clip, *source);
			m.SourceRootChannel = FindRootChannel(*clip, m.SourceMap, *source);
			RootSkeleton* rs = GetRootSkeleton(skeleton);
			if (rs->Node < 0)
				rs->Node = dst.Node[Humanoid::Hips];
			EditorLog::Write("Animation", "retarget %s: %s -> %s (humanoid)", clip->Name.c_str(), source->Name.c_str(), skeleton.Name.c_str());
		}
	}
	if (!m.Retarget)
	{
		m.RootChannel = FindRootChannel(*clip, m.Map, skeleton);
		if (m.RootChannel >= 0)
		{
			RootSkeleton* rs = GetRootSkeleton(skeleton);
			if (rs->Node < 0)
				rs->Node = m.Map[m.RootChannel];   // 포즈에서 수평 이동을 뺄 본
		}
	}
	m_ClipMaps.push_back(std::move(m));
	return &m_ClipMaps.back();
}

void Animator::SampleClip(const AnimationClip* clip, bool loop, float time, const SkeletonAvataData& skeleton, std::vector<XMFLOAT4X4>& out)
{
	if (clip == nullptr)
	{
		out = skeleton.BindLocal;
		return;
	}
	ClipMap* map = GetClipMap(clip, skeleton);
	const float d = clip->GetClipEndTime();
	float t = time;
	if (d > 1e-4f)
		t = loop ? fmodf((std::max)(0.0f, time), d) : std::clamp(time, 0.0f, d);
	if (map->Retarget)
		Humanoid::Retarget(*map->Source, *map->Target, *clip, map->SourceMap, t, out);
	else
		AnimationPose::SampleLocal(skeleton, clip, map->Map, t, out);
}

void Animator::SampleState(const AnimatorLayer& layer, int state, float time, const SkeletonAvataData& skeleton, std::vector<XMFLOAT4X4>& out)
{
	if (state < 0 || state >= (int)layer.States.size())
	{
		out = skeleton.BindLocal;
		return;
	}
	AnimatorState& s = const_cast<AnimatorState&>(layer.States[state]);
	if (!s.IsBlendTree)
	{
		if (s.Clip == nullptr && !s.ClipPath.empty())
			s.LoadClip();
		SampleClip(s.Clip.get(), s.Loop, time, skeleton, out);
		return;
	}
	// Blend Tree: 가중치가 있는 자식을 같은 정규화 시간으로 샘플해 차례로 섞는다
	std::vector<float> w;
	GetBlendWeights(s, w);
	float acc = 0.0f;
	bool first = true;
	for (size_t i = 0; i < w.size(); ++i)
	{
		if (w[i] < 0.001f)
			continue;
		BlendTreeChild& c = s.Tree.Children[i];
		if (c.Clip == nullptr && !c.ClipPath.empty())
			c.LoadClip();
		const float d = c.Clip ? c.Clip->GetClipEndTime() : 0.0f;
		SampleClip(c.Clip.get(), s.Loop, time * d, skeleton, m_TreeTmp);
		if (first)
		{
			out = m_TreeTmp;
			acc = w[i];
			first = false;
			continue;
		}
		acc += w[i];
		AnimationPose::Blend(out, m_TreeTmp, w[i] / acc, m_TreeMix);
		out.swap(m_TreeMix);
	}
	if (first)
		out = skeleton.BindLocal;
}

// ------------------------------------------------------------------ 루트 모션
SkinnedMeshRenderer* Animator::PrimaryRenderer()
{
	std::vector<SkinnedMeshRenderer*> renderers;
	CollectRenderers(m_pGameObject, renderers);
	for (SkinnedMeshRenderer* r : renderers)
		if (r->GetSkeleton())
			return r;
	return nullptr;
}

Animator::RootSkeleton* Animator::GetRootSkeleton(const SkeletonAvataData& skeleton)
{
	for (auto& r : m_RootSkeletons)
		if (r.Skeleton == &skeleton)
			return &r;
	RootSkeleton r;
	r.Skeleton = &skeleton;
	AnimationPose::ComputeGlobals(skeleton, skeleton.BindLocal, r.BindGlobal);
	m_RootSkeletons.push_back(std::move(r));
	return &m_RootSkeletons.back();
}

namespace
{
	// 노드의 부모까지 바인드 포즈 전역 행렬 (로컬 위치 → 모델 공간)
	XMMATRIX ParentGlobal(const SkeletonAvataData& skeleton, const std::vector<XMFLOAT4X4>& bindGlobal, int node)
	{
		const int parent = node >= 0 && node < (int)skeleton.BoneHierarchy.size() ? skeleton.BoneHierarchy[node] : -1;
		if (parent >= 0 && parent < (int)bindGlobal.size())
			return XMLoadFloat4x4(&bindGlobal[parent]);
		return XMMatrixScaling(skeleton.UnitScale, skeleton.UnitScale, skeleton.UnitScale);
	}
}

XMFLOAT3 Animator::ClipRootPosition(const AnimationClip* clip, const SkeletonAvataData& skeleton, float t)
{
	XMFLOAT3 out(0.0f, 0.0f, 0.0f);
	ClipMap* map = GetClipMap(clip, skeleton);
	if (map->Retarget)
	{
		// 원래 스켈레톤에서 Hips 위치 → 이 모델 크기로
		if (map->SourceRootChannel < 0)
			return out;
		const SkeletonAvataData& src = *map->SourceSkeleton;
		const int node = map->SourceMap[map->SourceRootChannel];
		XMFLOAT4X4 local = src.BindLocal[node];
		clip->Channels[map->SourceRootChannel].Sample(t, local);
		XMFLOAT3 p;
		XMStoreFloat3(&p, XMVector3TransformCoord(XMVectorSet(local._41, local._42, local._43, 1.0f), ParentGlobal(src, map->Source->BindGlobal, node)));
		return Humanoid::ScaleHips(*map->Source, *map->Target, p);
	}
	if (map->RootChannel < 0)
		return out;
	const int node = map->Map[map->RootChannel];
	RootSkeleton* rs = GetRootSkeleton(skeleton);
	XMFLOAT4X4 local = skeleton.BindLocal[node];
	clip->Channels[map->RootChannel].Sample(t, local);
	XMStoreFloat3(&out, XMVector3TransformCoord(XMVectorSet(local._41, local._42, local._43, 1.0f), ParentGlobal(skeleton, rs->BindGlobal, node)));
	return out;
}

Vec3 Animator::ClipRootDelta(const AnimationClip* clip, bool loop, const SkeletonAvataData& skeleton, float t0, float t1)
{
	if (clip == nullptr)
		return Vec3::Zero;
	const float d = clip->GetClipEndTime();
	if (d <= 1e-4f || t1 == t0)
		return Vec3::Zero;
	auto at = [&](float t) { const XMFLOAT3 p = ClipRootPosition(clip, skeleton, t); return Vec3(p.x, p.y, p.z); };
	if (!loop)
		return at(std::clamp(t1, 0.0f, d)) - at(std::clamp(t0, 0.0f, d));
	// 반복: 한 바퀴를 넘어가면 (끝 - 처음) 을 바퀴 수만큼 더한다
	const float n0 = floorf(t0 / d), n1 = floorf(t1 / d);
	Vec3 delta = at(t1 - n1 * d) - at(t0 - n0 * d);
	if (n1 != n0)
		delta += (at(d) - at(0.0f)) * (n1 - n0);
	return delta;
}

Vec3 Animator::StateRootDelta(const AnimatorLayer& layer, int state, const SkeletonAvataData& skeleton, float t0, float t1)
{
	if (state < 0 || state >= (int)layer.States.size())
		return Vec3::Zero;
	const AnimatorState& s = layer.States[state];
	if (!s.IsBlendTree)
		return ClipRootDelta(s.Clip.get(), s.Loop, skeleton, t0, t1);
	std::vector<float> w;
	GetBlendWeights(s, w);
	Vec3 delta = Vec3::Zero;
	for (size_t i = 0; i < w.size(); ++i)
	{
		const BlendTreeChild& c = s.Tree.Children[i];
		if (w[i] < 0.001f || c.Clip == nullptr)
			continue;
		const float d = c.Clip->GetClipEndTime();
		delta += ClipRootDelta(c.Clip.get(), s.Loop, skeleton, t0 * d, t1 * d) * w[i];
	}
	return delta;
}

void Animator::ApplyRootMotion(float dt)
{
	m_DeltaPosition = Vec3::Zero;
	m_LastDt = dt;
	if (!m_ApplyRootMotion || !Application::IsPlaying() || m_pGameObject == nullptr || m_RootDeltaModel.LengthSquared() < 1e-14f)
		return;
	SkinnedMeshRenderer* r = PrimaryRenderer();
	if (r == nullptr || r->GetGameObject() == nullptr)
		return;
	// 모델 공간 → 월드 (렌더러 오브젝트의 회전·크기), 수평만 (높이는 중력·바닥이 정한다)
	Vec3 d = Vec3::TransformNormal(m_RootDeltaModel, r->GetGameObject()->GetTransform()->GetWorldMatrix());
	d.y = 0.0f;
	m_DeltaPosition = d;
	if (CharacterController* cc = m_pGameObject->GetComponent<CharacterController>())
		cc->Move(d, dt);   // 벽·계단은 Character Controller 가 처리
	else
	{
		Transform* t = m_pGameObject->GetTransform();
		t->SetPosition(t->GetPosition() + d);
	}
}

// 루트 모션 본의 수평 위치를 바인드 포즈 자리로 — Unity Humanoid 기본값(Root Transform Position XZ 를 포즈에 굽지 않음)과 같이 늘.
// Apply Root Motion 이면 그 이동을 오브젝트가 대신 하고, 아니면 버린다 (에이전트·스크립트가 움직이는 캐릭터는 제자리에서 걷는다)
void Animator::PinRoot(const SkeletonAvataData& skeleton, std::vector<XMFLOAT4X4>& local)
{
	RootSkeleton* rs = GetRootSkeleton(skeleton);
	const int node = rs->Node;
	if (node < 0 || node >= (int)local.size() || node >= (int)skeleton.BindLocal.size())
		return;
	const XMMATRIX pg = ParentGlobal(skeleton, rs->BindGlobal, node);
	XMVECTOR det;
	const XMMATRIX inv = XMMatrixInverse(&det, pg);
	XMFLOAT3 cur, bind;
	XMStoreFloat3(&cur, XMVector3TransformCoord(XMVectorSet(local[node]._41, local[node]._42, local[node]._43, 1.0f), pg));
	XMStoreFloat3(&bind, XMVector3TransformCoord(XMVectorSet(skeleton.BindLocal[node]._41, skeleton.BindLocal[node]._42, skeleton.BindLocal[node]._43, 1.0f), pg));
	XMFLOAT3 back;
	XMStoreFloat3(&back, XMVector3TransformCoord(XMVectorSet(bind.x, cur.y, bind.z, 1.0f), inv));
	local[node]._41 = back.x;
	local[node]._42 = back.y;
	local[node]._43 = back.z;
}

void Animator::CollectRendererRefs(GameObject* go)
{
	if (go == nullptr)
		return;
	for (const auto& c : go->GetComponents())
		if (SkinnedMeshRenderer* s = dynamic_cast<SkinnedMeshRenderer*>(c.get()))
		{
			m_UpdateRenderers.push_back(s);
			m_RendererRefs.push_back(c);
		}
	for (GameObject* child : go->Children())
		CollectRendererRefs(child);
}

void Animator::RefreshRenderers()
{
	bool stale = m_pGameObject == nullptr || m_RendererKids != m_pGameObject->Children().size();
	// 지운 렌더러: 컴포넌트가 지워지면 물리 번호가 오른다 — 번호가 그대로면 weak_ptr 를 보지 않는다 (1 만 명 × 부위 17 개를 프레임마다)
	if (!stale && m_RendererSerial != Component::s_PhysicsSerial)
		for (size_t i = 0; i < m_RendererRefs.size() && !stale; ++i)
			stale = m_RendererRefs[i].expired();
	m_RendererSerial = Component::s_PhysicsSerial;
	if (stale)
	{
		m_UpdateRenderers.clear();
		m_RendererRefs.clear();
		m_RendererKids = m_pGameObject ? m_pGameObject->Children().size() : 0;
		CollectRendererRefs(m_pGameObject);
		m_MergeSerial = ~0u;
	}
	if (m_MergeSerial != SkinnedMeshRenderer::s_MergeSerial)
	{
		m_MergeSerial = SkinnedMeshRenderer::s_MergeSerial;
		m_DrawRenderers.clear();
		for (SkinnedMeshRenderer* r : m_UpdateRenderers)
			if (!r->IsMerged())
				m_DrawRenderers.push_back(r);
	}
	m_RenderersFrame = SceneCulling::FrameIndex();
}

void Animator::CollectRenderers(GameObject* go, std::vector<SkinnedMeshRenderer*>& out)
{
	if (go == nullptr)
		return;
	if (SkinnedMeshRenderer* r = go->GetComponent<SkinnedMeshRenderer>())
		out.push_back(r);
	for (GameObject* child : go->Children())   // 복사 없이 (프레임마다 — 군중)
		CollectRenderers(child, out);
}

void Animator::UpdateNeededNodes(const SkeletonAvataData& skeleton, const std::vector<SkinnedMeshRenderer*>& renderers)
{
	// 본 줄이기 (Skeletal LOD): 렌더러가 모두 LOD 2 이상이면 그 단계 (와 그림자의 한 단계 아래) 정점이 가리키는 본만
	int level = SkinnedLod::kImpostorLevel;
	for (SkinnedMeshRenderer* r : renderers)
		if (r->GetSkeleton().get() == &skeleton && !r->IsMerged())   // 합쳐진 부위는 그리지 않는다 (대표만)
			level = (std::min)(level, r->GetAutoLod() ? r->CurrentLod() : 0);
	const bool reduced = level >= 2 && Application::IsPlaying() && CanEvaluateOffMain();   // 자세 후처리 (IK) 가 있으면 전부
	size_t key = (size_t)&skeleton + (reduced ? (size_t)level * 7919u : 0u);
	for (SkinnedMeshRenderer* r : renderers)
		key = key * 1315423911u + (size_t)r->GetMesh().get() * 31u + r->PaletteNodes().size();
	if (m_NeededFor == &skeleton && m_NeededKey == key)
		return;
	if (reduced)
	{
		const size_t n = skeleton.BoneHierarchy.size();
		std::vector<uint8_t> need(n, 0);
		bool ok = true;
		for (SkinnedMeshRenderer* r : renderers)
		{
			if (r->GetSkeleton().get() != &skeleton || r->IsMerged())
				continue;
			const std::vector<int>& pal = r->PaletteNodes();
			const int lv = (std::min)(level, SkinnedLod::kLevels - 1);
			for (int l : { lv, (std::min)(lv + 1, SkinnedLod::kLevels - 1) })
			{
				const std::vector<int>* used = SkinnedLod::UsedBones(r->GetMesh().get(), l);
				if (used == nullptr) { ok = false; break; }
				for (int k : *used)
					for (int p = k < (int)pal.size() ? pal[(size_t)k] : -1; p >= 0 && (size_t)p < n && !need[(size_t)p]; p = skeleton.BoneHierarchy[(size_t)p])
						need[(size_t)p] = 1;
			}
		}
		if (ok)
		{
			m_NeededFor = &skeleton;
			m_NeededKey = key;
			m_NeededNodes.clear();
			for (size_t i = 0; i < n; ++i)
				if (need[i])
					m_NeededNodes.push_back((int)i);
			return;
		}
		key -= (size_t)level * 7919u;   // LOD 가 아직 없다 — 전부 (아래)
	}
	m_NeededFor = &skeleton;
	m_NeededKey = key;
	const size_t n = skeleton.BoneHierarchy.size();
	std::vector<uint8_t> need(n, 0);
	auto mark = [&](int node) {
		for (int p = node; p >= 0 && (size_t)p < n && !need[(size_t)p]; p = skeleton.BoneHierarchy[(size_t)p])
			need[(size_t)p] = 1;
	};
	for (SkinnedMeshRenderer* r : renderers)
		if (r->GetSkeleton().get() == &skeleton && !r->IsMerged())
			for (int node : r->PaletteNodes())
				mark(node);
	const Humanoid::Avatar& av = Humanoid::Get(skeleton);
	if (av.Valid)
		for (int b = 0; b < Humanoid::BoneCount; ++b)
			mark(av.Node[b]);
	m_NeededNodes.clear();
	for (size_t i = 0; i < n; ++i)
		if (need[i])
			m_NeededNodes.push_back((int)i);
}

void Animator::EvaluatePose()
{
	if (m_Controller == nullptr)
		return;
	// 이번 프레임 Update 가 모은 목록 (Play 의 자세 모으기) — 아니면 (편집 · Rebind · 물리 갱신) 새로 모은다
	std::vector<SkinnedMeshRenderer*>& renderers = m_EvalRenderers;   // 작업 스레드에서도 이 Animator 것만
	if (m_RenderersFrame == SceneCulling::FrameIndex())
		renderers = m_DrawRenderers;   // 합쳐진 부위는 자세를 받지 않는다 (대표의 팔레트)
	else
	{
		renderers.clear();
		CollectRenderers(m_pGameObject, renderers);
	}
	const SkeletonAvataData* computedFor = nullptr;
	for (SkinnedMeshRenderer* renderer : renderers)
	{
		shared_ptr<SkeletonAvataData> skeleton = renderer->GetSkeleton();
		if (skeleton == nullptr)
			continue;
		if (computedFor != skeleton.get())
		{
			// 레이어 자세는 바꿔 끼운다 (복사하지 않는다 — 노드 수백 개 × 64 바이트)
			bool have = false;
			for (int li = 0; li < (int)m_Controller->Layers.size() && li < (int)m_Layers.size(); ++li)
			{
				const AnimatorLayer& layer = m_Controller->Layers[li];
				const LayerRuntime& r = m_Layers[li];
				if (r.Current < 0)
					continue;
				SampleState(layer, r.Current, r.Time, *skeleton, m_LocalA);
				if (r.Next >= 0)
				{
					SampleState(layer, r.Next, r.NextTime, *skeleton, m_LocalB);
					AnimationPose::Blend(m_LocalA, m_LocalB, r.TransitionDuration > 0.0f ? r.TransitionElapsed / r.TransitionDuration : 1.0f, m_LocalLayer);
				}
				else
					m_LocalLayer.swap(m_LocalA);
				// 첫 레이어는 그대로, 다음 레이어는 Weight 만큼 덮어쓴다 (Override)
				const float weight = li == 0 ? 1.0f : layer.Weight;
				if (weight >= 0.999f)
				{
					m_LocalFinal.swap(m_LocalLayer);
					have = true;
				}
				else if (weight > 0.001f)
				{
					if (!have)
						m_LocalFinal = skeleton->BindLocal;
					have = true;
					std::vector<XMFLOAT4X4> mixed;
					AnimationPose::Blend(m_LocalFinal, m_LocalLayer, weight, mixed);
					m_LocalFinal.swap(mixed);
				}
			}
			if (!have)
				m_LocalFinal = skeleton->BindLocal;
			PinRoot(*skeleton, m_LocalFinal);
			UpdateNeededNodes(*skeleton, renderers);
			AnimationPose::ComputeGlobals(*skeleton, m_LocalFinal, m_Global, m_NeededNodes);
			if (Application::IsPlaying())
				ApplyPoseModifiers(*skeleton, renderer);
			computedFor = skeleton.get();
			if (m_GlobalFor == nullptr || renderer == PrimaryRenderer())
				m_GlobalFor = computedFor;
		}
		renderer->ApplyPose(m_Global);
	}
}

bool Animator::GetHumanBoneWorld(int bone, XMFLOAT3& position, XMFLOAT4& rotation)
{
	SkinnedMeshRenderer* r = PrimaryRenderer();
	if (bone < 0 || bone >= Humanoid::BoneCount || r == nullptr || r->GetGameObject() == nullptr || r->GetSkeleton().get() != m_GlobalFor)
		return false;
	const int node = Humanoid::Get(*m_GlobalFor).Node[bone];
	if (node < 0 || node >= (int)m_Global.size())
		return false;
	const XMMATRIX toWorld = r->GetGameObject()->GetTransform()->GetWorldMatrix();
	const XMMATRIX world = XMMatrixMultiply(XMLoadFloat4x4(&m_Global[node]), toWorld);
	XMVECTOR s, q, t;
	if (!XMMatrixDecompose(&s, &q, &t, world))
		return false;
	XMStoreFloat3(&position, t);
	XMStoreFloat4(&rotation, XMQuaternionNormalize(q));
	return true;
}

void Animator::ApplyPoseModifiers(const SkeletonAvataData& skeleton, SkinnedMeshRenderer* renderer)
{
	if (m_pGameObject == nullptr || renderer == nullptr || renderer->GetGameObject() == nullptr)
		return;
	std::vector<IAnimatorPoseModifier*> mods;
	for (const auto& c : m_pGameObject->GetComponents())
		if (c && c->IsEnabled())
			if (auto* m = dynamic_cast<IAnimatorPoseModifier*>(c.get()))
				mods.push_back(m);
	if (mods.empty())
		return;
	std::stable_sort(mods.begin(), mods.end(), [](auto* a, auto* b) { return a->PoseOrder() < b->PoseOrder(); });
	const XMMATRIX toWorld = renderer->GetGameObject()->GetTransform()->GetWorldMatrix();
	XMVECTOR det;
	AnimatorPose pose{ skeleton, Humanoid::Get(skeleton), m_LocalFinal, m_Global, toWorld, XMMatrixInverse(&det, toWorld), m_LastDt };
	for (IAnimatorPoseModifier* m : mods)
		m->ModifyPose(pose);
}

// ------------------------------------------------------------------ 컴포넌트
void Animator::Awake()
{
}

void Animator::PrewarmStaged()
{
	// 처음 Start (EvaluatePose) 가 만드는 캐시 — 도시: 바꿔 끼우는 프레임의 Start 122 ms 의 대부분 (Debug). 자세는 적용하지 않는다
	if (m_Controller == nullptr)
		return;
	// 아바타 표 (Humanoid) 를 만들 스켈레톤: 렌더러의 스켈레톤 + 기본 상태 클립의 원본 스켈레톤 (리타깃 — 도시: 원본 하나에 약 120 ms, Debug)
	std::vector<shared_ptr<SkeletonAvataData>> skeletons;
	auto add = [&](shared_ptr<SkeletonAvataData> s) {
		if (s && std::find(skeletons.begin(), skeletons.end(), s) == skeletons.end())
			skeletons.push_back(std::move(s));
	};
	std::vector<SkinnedMeshRenderer*> renderers;
	CollectRenderers(m_pGameObject, renderers);
	for (SkinnedMeshRenderer* renderer : renderers)
		add(renderer->GetSkeleton());
	for (const AnimatorLayer& layer : m_Controller->Layers)
	{
		const int state = layer.FindState(layer.DefaultState);
		if (state < 0)
			continue;
		AnimatorState& s = const_cast<AnimatorState&>(layer.States[state]);
		if (!s.IsBlendTree && s.Clip == nullptr && !s.ClipPath.empty())
			s.LoadClip();   // 클립 (에셋) 은 메인에서 — ResourceManager 는 메인 스레드 것
		if (s.Clip)
			add(s.Clip->SourceSkeleton.lock());
	}
	if (skeletons.empty())
		return;
	// 표 만들기는 바뀌지 않는 스켈레톤만 읽는다 → 백그라운드 잡 (미리 짓기는 이 잡이 끝날 때까지 0.9 에 이르지 않는다)
	SceneStreaming::QueuePrewarmJob([skeletons]() {
		for (const auto& s : skeletons)
			Humanoid::Get(*s);
	});
}

void Animator::Start()
{
	ResetRuntime();
	m_Started = true;
	EvaluatePose();
}

void Animator::Update()
{
	// Unity: animator.enabled = false 면 멈춘다 (자세 · 루트 모션 그대로 — 래그돌이 끈다)
	if (!m_Enabled || m_UpdateMode == 1 || m_AnimatePhysics || m_Controller == nullptr)
		return;
	const float dt = m_UpdateMode == 2 ? TimeManager::GetI()->GetfDT() : DT;
	RefreshRenderers();
	// Culling Mode (Unity): 렌더러가 하나도 보이지 않으면 Cull Completely 는 멈추고, Cull Update Transforms 는 상태만 나아간다
	const bool visible = m_CullingMode == 0 || AnyRendererVisible(m_DrawRenderers);   // 합쳐진 부위는 컬링에서 빠져 늘 보인다고 나온다 — 그리는 것만
	if (!visible && m_CullingMode == 2)
		return;
	StepState(dt);
	if (!visible)
		return;
	// 렌더러에 재생 중인 클립 · 시간 (기본 레이어) — 멀리서 그 클립의 애니메이션 임포스터로 그린다 (자동)
	if (!m_Layers.empty() && !m_Controller->Layers.empty())
	{
		const AnimatorLayer& layer = m_Controller->Layers[0];
		const LayerRuntime& lr = m_Layers[0];
		if (lr.Current >= 0 && lr.Current < (int)layer.States.size())
		{
			AnimatorState& s = const_cast<AnimatorState&>(layer.States[(size_t)lr.Current]);
			std::shared_ptr<AnimationClip> clip;
			float time = lr.Time;
			if (!s.IsBlendTree)
			{
				if (s.Clip == nullptr && !s.ClipPath.empty())
					s.LoadClip();
				clip = s.Clip;
			}
			else
			{
				// Blend Tree: 가장 무거운 자식 (Time = 정규화 시간)
				std::vector<float> w;
				GetBlendWeights(s, w);
				int best = -1;
				for (int i = 0; i < (int)w.size(); ++i)
					if (best < 0 || w[(size_t)i] > w[(size_t)best]) best = i;
				if (best >= 0 && best < (int)s.Tree.Children.size())
				{
					BlendTreeChild& c = s.Tree.Children[(size_t)best];
					if (c.Clip == nullptr && !c.ClipPath.empty())
						c.LoadClip();
					clip = c.Clip;
					time = lr.Time * (clip ? clip->GetClipEndTime() : 0.0f);
				}
			}
			for (SkinnedMeshRenderer* r : m_DrawRenderers)
				r->SetAnimationHint(clip, time, s.Loop);
		}
	}
	if (!Application::IsPlaying())
	{
		EvaluatePose();
		return;
	}
	// 자세는 모든 Update 뒤에 한 번에 (Scene::SetAfterUpdateHook → FlushPendingPoses)
	if (!m_PosePending && PoseDueThisFrame(m_DrawRenderers))
	{
		m_PosePending = true;
		s_PendingPoses.push_back(this);
	}
}

void Animator::LastUpdate()
{
	// 편집 중 미리 보기: 렌더러(자식)가 다 붙은 뒤 한 번
	if (m_NeedPreview && !Application::IsPlaying())
	{
		m_NeedPreview = false;
		Rebind();
	}
}

void Animator::FixedUpdate()
{
	if (m_Enabled && (m_UpdateMode == 1 || m_AnimatePhysics))
		Step(PhysicsManager::GetI()->GetFixedTimestep());
}

// ------------------------------------------------------------------ Inspector (Unity 6 Animator)
void Animator::OnInspectorGUI()
{
	using namespace UnityGUI;
	std::string controllerName = m_Controller ? m_Controller->Name() : "None (Runtime Animator Controller)";
	if (ObjectField("Controller", controllerName.c_str(), 0, m_Controller ? "animator_controller" : nullptr))
		ImGui::OpenPopup("##ControllerPicker");
	static const char* kAvatar[] = { "Auto (Humanoid retarget)", "Generic (bone names)" };
	if (Dropdown("Avatar", &m_AvatarMode, kAvatar, 2))
		m_ClipMaps.clear();   // 다시 맞춘다
	Toggle("Apply Root Motion", &m_ApplyRootMotion);
	Toggle("Animate Physics", &m_AnimatePhysics);
	static const char* kUpdate[] = { "Normal", "Animate Physics", "Unscaled Time" };
	Dropdown("Update Mode", &m_UpdateMode, kUpdate, 3);
	static const char* kCulling[] = { "Always Animate", "Cull Update Transforms", "Cull Completely" };
	Dropdown("Culling Mode", &m_CullingMode, kCulling, 3);

	// Unity 의 정보 상자: 클립 수와 커브 수
	int clipCount = 0, pos = 0, quat = 0, scale = 0;
	std::vector<const AnimationClip*> seen;
	auto countClip = [&](const AnimationClip* clip) {
		if (clip == nullptr || std::find(seen.begin(), seen.end(), clip) != seen.end())
			return;
		seen.push_back(clip);
		++clipCount;
		for (const auto& ch : clip->Channels)
		{
			pos += !ch.Positions.empty();
			quat += !ch.Rotations.empty();
			scale += !ch.Scales.empty();
		}
	};
	if (m_Controller)
		for (auto& layer : m_Controller->Layers)
			for (auto& s : layer.States)
			{
				if (s.Clip == nullptr && !s.ClipPath.empty()) s.LoadClip();
				countClip(s.Clip.get());
				for (const auto& ch : s.Tree.Children)
					if (s.IsBlendTree)
						countClip(ch.Clip.get());
			}
	const int curves = pos * 3 + quat * 4 + scale * 3;
	char info[512];
	sprintf_s(info, "Clip Count: %d\nCurves Pos: %d Quat: %d Euler: 0 Scale: %d Muscles: 0 Generic: 0 PPtr: 0\nCurves Count: %d Constant: 0 (0.0%%) Dense: 0 (0.0%%) Stream: %d (%s)",
		clipCount, pos, quat, scale, curves, curves, curves > 0 ? "100.0%" : "0.0%");
	HelpBox(info, false);

	// Humanoid: 이 모델의 사람 본 (자동으로 찾은 것)
	if (m_AvatarMode == 0)
		if (SkinnedMeshRenderer* r = PrimaryRenderer())
		{
			const Humanoid::Avatar& av = Humanoid::Get(*r->GetSkeleton());
			char title[96];
			sprintf_s(title, "Humanoid bones (%d/%d%s)", av.Found, (int)Humanoid::BoneCount, av.Valid ? "" : ", not humanoid");
			if (FoldoutPlain(title, 0, false))
			{
				for (int b = 0; b < Humanoid::BoneCount; ++b)
					ValueLabel(Humanoid::BoneName(b), av.Node[b] >= 0 ? av.Skeleton->NodeNames[av.Node[b]].c_str() : "-", 1);
				int retargeted = 0;
				for (const ClipMap& m : m_ClipMaps)
					retargeted += m.Retarget;
				char info[64];
				sprintf_s(info, "%d clip(s) retargeted", retargeted);
				ValueLabel("In use", info, 1);
			}
		}

	if (Application::IsPlaying() && m_Controller && !m_Layers.empty())
	{
		char buf[256];
		const LayerRuntime& r = m_Layers[0];
		sprintf_s(buf, "%s  (%.2f)%s", GetCurrentStateName(0).c_str(), GetNormalizedTime(0, r.Current, r.Time), r.Next >= 0 ? "  → 전이 중" : "");
		ValueLabel("Current State", buf);
		// Blend Tree 면 자식마다 가중치 (Unity 의 Blend Tree 미리 보기 대신)
		const AnimatorLayer& base = m_Controller->Layers[0];
		if (r.Current >= 0 && r.Current < (int)base.States.size() && base.States[r.Current].IsBlendTree)
		{
			const AnimatorState& s = base.States[r.Current];
			std::vector<float> w;
			GetBlendWeights(s, w);
			std::string text;
			for (size_t i = 0; i < w.size(); ++i)
			{
				const BlendTreeChild& c = s.Tree.Children[i];
				char part[96];
				sprintf_s(part, "%s%s %.0f%%", text.empty() ? "" : ", ", c.DisplayName().c_str(), w[i] * 100.0f);
				text += part;
			}
			ValueLabel("Blend", text.c_str());
		}
	}

	// ---- 컨트롤러 선택 팝업 ----
	ImGui::SetNextWindowSizeConstraints(ImVec2(300, 0), ImVec2(520, 400));
	if (ImGui::BeginPopup("##ControllerPicker"))
	{
		if (ImGui::Selectable("None"))
			SetController("");
		for (const std::string& path : ScanControllers())
			if (ImGui::Selectable(path.c_str(), path == m_ControllerPath))
				SetController(path);
		ImGui::Separator();
		if (ImGui::Selectable("Create New Controller"))
		{
			// Assets 에 "<오브젝트 이름>.controller" 를 만들어 연결
			std::string base = "Assets\\" + (m_pGameObject ? m_pGameObject->GetName() : std::string("New Animator Controller"));
			std::string path = base + ".controller";
			for (int n = 1; std::filesystem::exists(PathManager::GetI()->GetMovePathW(string_to_wstring(path))); ++n)
				path = base + " " + std::to_string(n) + ".controller";
			AnimatorController::Create(path);
			SetController(path);
		}
		ImGui::EndPopup();
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(Animator)
{
	json j;
	SERIALIZE_TYPE(j, Animator);
	j["enabled"] = m_Enabled;
	j["controller"] = m_ControllerPath;
	j["applyRootMotion"] = m_ApplyRootMotion;
	j["animatePhysics"] = m_AnimatePhysics;
	j["updateMode"] = m_UpdateMode;
	j["cullingMode"] = m_CullingMode;
	j["avatarMode"] = m_AvatarMode;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Animator)
{
	m_Enabled = j.value("enabled", true);
	m_ApplyRootMotion = j.value("applyRootMotion", false);
	m_AnimatePhysics = j.value("animatePhysics", false);
	m_UpdateMode = j.value("updateMode", 0);
	m_CullingMode = j.value("cullingMode", 0);
	m_AvatarMode = j.value("avatarMode", 0);
	SetController(j.value("controller", std::string()));
}
