#include "pch.h"
#include "Animator.h"
#include "UnityGUI.h"
#include "AnimationPose.h"
#include "SkinnedMeshRenderer.h"
#include "SkinnedMesh.h"
#include "PhysicsManager.h"
#include "CharacterController.h"

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

Animator::~Animator()
{
}

// ------------------------------------------------------------------ 컨트롤러 / 파라미터
void Animator::SetController(const std::string& path)
{
	m_Controller = AnimatorController::Load(path);
	m_ControllerPath = m_Controller ? m_Controller->Path : path;
	m_ControllerRevision = ~0u;
	ResetRuntime();
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
	const std::string& currentName = layer.States[r.Current].Name;

	// Solo 가 하나라도 있으면 Solo 전이만 본다 (출발 상태별)
	auto hasSolo = [&](const std::string& from) {
		for (const auto& t : layer.Transitions)
			if (t.From == from && t.Solo) return true;
		return false;
	};
	const bool soloAny = hasSolo(kAnyState), soloCur = hasSolo(currentName);

	// Any State 전이가 먼저, 그다음 현재 상태의 전이 (목록 순서 = 우선순위)
	for (int pass = 0; pass < 2; ++pass)
	{
		for (int i = 0; i < (int)layer.Transitions.size(); ++i)
		{
			const AnimatorTransition& t = layer.Transitions[i];
			const bool fromAny = t.From == kAnyState;
			if ((pass == 0) != fromAny)
				continue;
			if (!fromAny && t.From != currentName)
				continue;
			if (t.Mute || ((fromAny ? soloAny : soloCur) && !t.Solo))
				continue;

			int target = -1;
			if (t.To == kExit)
			{
				// Exit → Entry 로 돌아가 Entry 전이(조건) 또는 기본 상태
				target = layer.FindState(layer.DefaultState);
				for (const auto& et : layer.Transitions)
					if (et.From == kEntry && CheckConditions(et)) { target = layer.FindState(et.To); break; }
			}
			else
				target = layer.FindState(t.To);
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

			if (exitOk && CheckConditions(t))
			{
				ConsumeTriggers(t);
				StartTransition(layerIndex, i, target);
				return;
			}
		}
	}
}

void Animator::Step(float dt)
{
	if (m_Controller == nullptr)
		return;
	SyncWithController();
	const float scaled = dt * m_Speed;   // Animator.speed
	m_RootDeltaModel = Vec3::Zero;
	for (int i = 0; i < (int)m_Layers.size(); ++i)
		StepLayer(i, scaled);
	ApplyRootMotion(dt);
	EvaluatePose();
}

// ------------------------------------------------------------------ 포즈
Animator::ClipMap* Animator::GetClipMap(const AnimationClip* clip, const SkeletonAvataData& skeleton)
{
	for (auto& m : m_ClipMaps)
		if (m.Clip == clip && m.Skeleton == &skeleton)
			return &m;
	ClipMap m{ clip, &skeleton, AnimationPose::MapChannels(*clip, skeleton) };
	// 루트 모션 채널: 위치가 실제로 움직이는 채널 중 스켈레톤에서 가장 위쪽 (보통 Hips)
	int bestDepth = INT_MAX;
	for (size_t c = 0; c < clip->Channels.size() && c < m.Map.size(); ++c)
	{
		const int node = m.Map[c];
		const auto& keys = clip->Channels[c].Positions;
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
			m.RootChannel = (int)c;
		}
	}
	if (m.RootChannel >= 0)
	{
		RootSkeleton* rs = GetRootSkeleton(skeleton);
		if (rs->Node < 0)
			rs->Node = m.Map[m.RootChannel];   // 포즈에서 수평 이동을 뺄 본
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

void Animator::CollectRenderers(GameObject* go, std::vector<SkinnedMeshRenderer*>& out)
{
	if (go == nullptr)
		return;
	if (SkinnedMeshRenderer* r = go->GetComponent<SkinnedMeshRenderer>())
		out.push_back(r);
	for (GameObject* child : go->GetChildren())
		CollectRenderers(child, out);
}

void Animator::EvaluatePose()
{
	if (m_Controller == nullptr)
		return;
	std::vector<SkinnedMeshRenderer*> renderers;
	CollectRenderers(m_pGameObject, renderers);
	const SkeletonAvataData* computedFor = nullptr;
	for (SkinnedMeshRenderer* renderer : renderers)
	{
		shared_ptr<SkeletonAvataData> skeleton = renderer->GetSkeleton();
		if (skeleton == nullptr)
			continue;
		if (computedFor != skeleton.get())
		{
			m_LocalFinal = skeleton->BindLocal;
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
					m_LocalLayer = m_LocalA;
				// 첫 레이어는 그대로, 다음 레이어는 Weight 만큼 덮어쓴다 (Override)
				const float weight = li == 0 ? 1.0f : layer.Weight;
				if (weight >= 0.999f)
					m_LocalFinal = m_LocalLayer;
				else if (weight > 0.001f)
				{
					std::vector<XMFLOAT4X4> mixed;
					AnimationPose::Blend(m_LocalFinal, m_LocalLayer, weight, mixed);
					m_LocalFinal.swap(mixed);
				}
			}
			PinRoot(*skeleton, m_LocalFinal);
			AnimationPose::ComputeGlobals(*skeleton, m_LocalFinal, m_Global);
			computedFor = skeleton.get();
		}
		renderer->ApplyPose(m_Global);
	}
}

// ------------------------------------------------------------------ 컴포넌트
void Animator::Awake()
{
}

void Animator::Start()
{
	ResetRuntime();
	m_Started = true;
	EvaluatePose();
}

void Animator::Update()
{
	if (m_UpdateMode == 1 || m_AnimatePhysics)
		return;
	Step(m_UpdateMode == 2 ? TimeManager::GetI()->GetfDT() : DT);
}

void Animator::FixedUpdate()
{
	if (m_UpdateMode == 1 || m_AnimatePhysics)
		Step(PhysicsManager::GetI()->GetFixedTimestep());
}

// ------------------------------------------------------------------ Inspector (Unity 6 Animator)
void Animator::OnInspectorGUI()
{
	using namespace UnityGUI;
	std::string controllerName = m_Controller ? m_Controller->Name() : "None (Runtime Animator Controller)";
	if (ObjectField("Controller", controllerName.c_str(), 0, m_Controller ? "animator_controller" : nullptr))
		ImGui::OpenPopup("##ControllerPicker");
	ObjectField("Avatar", "None (Avatar)", 0);
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
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Animator)
{
	m_Enabled = j.value("enabled", true);
	m_ApplyRootMotion = j.value("applyRootMotion", false);
	m_AnimatePhysics = j.value("animatePhysics", false);
	m_UpdateMode = j.value("updateMode", 0);
	m_CullingMode = j.value("cullingMode", 0);
	SetController(j.value("controller", std::string()));
}
