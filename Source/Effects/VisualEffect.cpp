#include "pch.h"
#include "VisualEffect.h"
#include "SceneViewOverlay.h"
#include "UnityGUI.h"
#include "ObjectPicker.h"
#include "VfxRuntime.h"

namespace
{
	std::vector<VisualEffect*> s_All;
	uint32 s_SeedCounter = 0x2545F491u;
	constexpr int kMaxSteps = 64;   // 한 프레임에 도는 Spawn 바퀴 (아주 짧은 Duration 의 Loop)
}

VisualEffect::VisualEffect()
{
	m_InspectorTitleName = "Visual Effect";
	m_Props.Owner = this;
	s_All.push_back(this);
}

VisualEffect::~VisualEffect()
{
	s_All.erase(std::remove(s_All.begin(), s_All.end(), this), s_All.end());
}

const std::vector<VisualEffect*>& VisualEffect::All()
{
	return s_All;
}

void VisualEffect::Start()
{
	// Play 시작: 에디터에서 돌던 것을 지우고 Initial Event 부터 (Unity 와 같음)
	if (Application::IsPlaying())
		Reinit();
}

void VisualEffect::OnDestroy()
{
	m_Gpu.reset();
	m_Systems.clear();
}

bool VisualEffect::ActiveInHierarchy()
{
	for (GameObject* g = m_pGameObject; g != nullptr; g = g->GetParent())
		if (!g->IsActive())
			return false;
	return m_pGameObject != nullptr;
}

// ------------------------------------------------------------------ 속성
bool VisualEffect::HasProperty(const std::string& name) const
{
	return m_Asset && m_Asset->FindProperty(name) != nullptr;
}

bool VisualEffect::GetProperty(const std::string& name, std::array<float, 4>& out) const
{
	if (auto it = Overrides.find(name); it != Overrides.end() && it->second.Enabled)
	{
		out = it->second.Value;
		return true;
	}
	if (m_Asset)
		if (const Vfx::Property* p = m_Asset->FindProperty(name))
		{
			out = p->Value;
			return true;
		}
	return false;
}

bool VisualEffect::SetProperty(const std::string& name, const std::array<float, 4>& value)
{
	// 에셋을 아직 읽지 않았으면 (Awake 에서 부름) 이름을 믿고 넣는다 — 쓸 때 속성이 없으면 무시된다
	if (m_Asset && !m_Asset->FindProperty(name))
		return false;
	OverrideValue& o = Overrides[name];
	o.Value = value;
	o.Enabled = true;
	return true;
}

void VisualEffect::ResetOverride(const std::string& name)
{
	Overrides.erase(name);
}

int VisualEffect::SystemAliveCount(int system) const
{
	return system >= 0 && system < (int)m_Systems.size() ? m_Systems[system].Alive : 0;
}

bool VisualEffect::HasAnySystemAwake() const
{
	for (const SystemState& s : m_Systems)
		if (s.Active)
			return true;
	return false;
}

void VisualEffect::SetAliveCounts(const std::vector<int>& perSystem)
{
	int total = 0;
	for (size_t i = 0; i < perSystem.size() && i < m_Systems.size(); ++i)
	{
		m_Systems[i].Alive = perSystem[i];
		total += perSystem[i];
	}
	m_Alive = total;
}

// ------------------------------------------------------------------ 재생
void VisualEffect::Reinit()
{
	m_ResetGpu = true;
	m_Events.clear();
	m_PendingDt = 0.0f;
	m_TotalTime = 0.0f;
	m_Alive = 0;
	for (SystemState& s : m_Systems)
		s = SystemState();
	if (m_Asset)
		m_Systems.resize(m_Asset->Systems.size());
	m_Seed = ResetSeedOnPlay ? (s_SeedCounter = s_SeedCounter * 747796405u + 2891336453u) : StartSeed * 2654435761u + 1u;
	if (!InitialEvent.empty())
		m_Events.push_back(InitialEvent);
	m_Initialized = true;
}

void VisualEffect::SendEvent(const std::string& name)
{
	if (!name.empty())
		m_Events.push_back(name);
}

void VisualEffect::StartSystem(int i)
{
	const Vfx::System& sys = m_Asset->Systems[i];
	SystemState& st = m_Systems[i];
	st.Active = true;
	st.Time = 0.0f;
	st.DelayLeft = sys.SpawnCtx.Delay;
	st.RateAccumulator = 0.0f;
	st.BurstFired.assign(sys.SpawnCtx.Bursts.size(), 0);
}

void VisualEffect::RefreshAsset()
{
	const Vfx::Loaded l = Vfx::Load(AssetPath);
	if (l.Revision == m_Revision && AssetPath == m_LoadedPath)
		return;
	// 에셋이 바뀌었다 (다른 파일 · 저장 · 그래프 창의 편집) → 처음부터 다시 (Unity 도 다시 컴파일하면 다시 시작)
	m_Revision = l.Revision;
	m_LoadedPath = AssetPath;
	m_Asset = l.Data;
	m_AssetError = l.Error;
	m_Systems.clear();
	if (m_Asset)
		m_Systems.resize(m_Asset->Systems.size());
	Reinit();
}

void VisualEffect::Advance(float dt)
{
	RefreshAsset();
	if (!m_Asset)
		return;
	if (!m_Initialized)
		Reinit();
	dt = Paused ? 0.0f : dt * (std::max)(0.0f, PlayRate);

	// 이벤트: 시스템의 Start · Stop 이벤트 (GPU Event 로 태어나는 시스템은 부모를 따른다)
	const auto& systems = m_Asset->Systems;
	for (const std::string& ev : m_Events)
		for (int i = 0; i < (int)systems.size(); ++i)
		{
			const Vfx::Spawn& sp = systems[i].SpawnCtx;
			if (!sp.Parent.empty())
				continue;
			if (sp.StartEvent == ev)
				StartSystem(i);
			else if (sp.StopEvent == ev)
				m_Systems[i].Active = false;
		}
	m_Events.clear();
	if (m_Culled)
		return;   // 화면 밖: 시뮬레이션과 같이 쉰다

	for (int i = 0; i < (int)systems.size(); ++i)
	{
		const Vfx::System& sys = systems[i];
		SystemState& st = m_Systems[i];
		if (!sys.Enabled || !sys.SpawnCtx.Parent.empty() || !st.Active || dt <= 0.0f)
			continue;
		const Vfx::Spawn& sp = sys.SpawnCtx;
		float rate = sp.Rate;
		std::array<float, 4> bound;
		if (!sp.RateBind.empty() && GetProperty(sp.RateBind, bound))
			rate = bound[0];
		float remaining = dt;
		if (st.DelayLeft > 0.0f)
		{
			const float use = (std::min)(remaining, st.DelayLeft);
			st.DelayLeft -= use;
			remaining -= use;
		}
		uint64 count = 0;
		if (st.BurstFired.size() != sp.Bursts.size())
			st.BurstFired.assign(sp.Bursts.size(), 0);
		for (int step = 0; remaining > 0.0f && st.Active && step < kMaxSteps; ++step)
		{
			const bool timed = sp.Duration > 0.0f;
			const float slice = timed ? (std::min)(remaining, (std::max)(0.0f, sp.Duration - st.Time)) : remaining;
			st.RateAccumulator += (std::max)(0.0f, rate) * slice;
			const float newTime = st.Time + slice;
			for (size_t b = 0; b < sp.Bursts.size(); ++b)
			{
				const Vfx::Burst& burst = sp.Bursts[b];
				int& fired = st.BurstFired[b];
				while ((burst.Cycles == 0 || fired < burst.Cycles) && burst.Time + fired * burst.Interval <= newTime && fired < 100000)
				{
					count += (uint64)burst.Count;
					++fired;
				}
			}
			st.Time = newTime;
			remaining -= slice;
			if (timed && st.Time >= sp.Duration - 1e-6f)
			{
				if (sp.Loop)
				{
					st.Time = 0.0f;
					std::fill(st.BurstFired.begin(), st.BurstFired.end(), 0);
				}
				else
					st.Active = false;
			}
			if (slice <= 0.0f && !timed)
				break;
		}
		const float whole = std::floor(st.RateAccumulator);
		st.RateAccumulator -= whole;
		count += (uint64)whole;
		st.PendingSpawn = (uint32)(std::min)((uint64)st.PendingSpawn + count, (uint64)(std::max)(1, sys.Capacity));
	}
	m_PendingDt = (std::min)(m_PendingDt + dt, 0.1f);   // 그리기가 멈춘 동안 쌓여도 한 번에 크게 튀지 않게
	m_TotalTime += dt;
}

void VisualEffect::UpdateAll()
{
	const float dt = (std::min)((float)DT, 0.25f);
	const bool playing = Application::IsPlaying();
	if (playing && !Application::ShouldUpdateGame())
		return;
	VfxRuntime::MarkFrame();   // 이번 프레임의 첫 그리기에서 GPU 시뮬레이션
	for (VisualEffect* vfx : s_All)
	{
		// Unity 처럼 에디터에서도 늘 재생한다 (꺼진 오브젝트 · 컴포넌트는 멈춘다)
		if (!vfx->IsEnabled() || !vfx->ActiveInHierarchy())
			continue;
		vfx->Advance(dt);
	}
}

// ------------------------------------------------------------------ 기즈모 · Inspector
bool VfxGpuBounds(VisualEffect* vfx, Vec3& mn, Vec3& mx);   // VfxRuntime.cpp

bool VisualEffect::GetWorldBounds(Vec3& mn, Vec3& mx)
{
	return VfxGpuBounds(this, mn, mx);
}

void VisualEffect::OnDrawGizmos()
{
	if (m_pGameObject == nullptr || !SceneViewOverlay::IsActive())
		return;
	Transform* tr = m_pGameObject->GetTransform();
	ImVec2 sp;
	if (SceneViewOverlay::Project(XMFLOAT3(tr->GetPosition().x, tr->GetPosition().y, tr->GetPosition().z), sp))
		UnityGUI::DrawIcon(ImGui::GetWindowDrawList(), "particle_system", ImVec2(sp.x - 12.0f, sp.y - 12.0f), 24.0f, IM_COL32(255, 200, 255, 220));
	// 골랐을 때: 경계 상자 (컬링에 쓰는 상자 — Unity 의 Bounds 기즈모)
	Vec3 mn, mx;
	if (SelectionManager::GetSelectedGameObject() != m_pGameObject || !GetWorldBounds(mn, mx))
		return;
	const ImU32 col = IM_COL32(255, 160, 220, 180);
	auto corner = [&](int k) { return XMFLOAT3((k & 1) ? mx.x : mn.x, (k & 2) ? mx.y : mn.y, (k & 4) ? mx.z : mn.z); };
	const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
	for (const auto& e : edges)
		SceneViewOverlay::DrawLine(corner(e[0]), corner(e[1]), col);
}

void VisualEffect::OnInspectorGUI()
{
	// Asset Template [이름 ⊙][Edit]
	const std::string key = "vfx:" + std::to_string((uintptr_t)this);
	const std::string text = AssetPath.empty() ? "None (Visual Effect Asset)" : std::filesystem::path(AssetPath).stem().string();
	const char* buttons[] = { "Edit" };
	ImVec2 fmin, fmax;
	const int pressed = UnityGUI::ObjectFieldButtons("Asset Template", text.c_str(), "particle_system", buttons, OpenGraph ? 1 : 0, &fmin, &fmax);
	const ImVec2 after = ImGui::GetCursorScreenPos();
	if (pressed == -1)
	{
		ObjectPicker::Options opt;
		opt.TypeName = "Visual Effect Asset";
		opt.Icon = "particle_system";
		opt.Items = Vfx::FindAssets();
		opt.Current = AssetPath;
		ObjectPicker::Open(key, std::move(opt));
	}
	else if (pressed == 0 && OpenGraph && !AssetPath.empty())
		OpenGraph(AssetPath);
	std::string picked;
	if (ObjectPicker::Poll(key, picked))
		AssetPath = picked;
	// Project 창에서 .vfx 끌어 놓기
	ImGui::SetCursorScreenPos(fmin);
	ImGui::InvisibleButton("##vfxDrop", ImVec2((std::max)(1.0f, fmax.x - fmin.x - 22.0f), (std::max)(1.0f, fmax.y - fmin.y)));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
		{
			std::string dropped(static_cast<const char*>(payload->Data));
			const std::string root = wstring_to_string(PathManager::GetI()->GetContentPathW());
			if (_strnicmp(dropped.c_str(), root.c_str(), root.size()) == 0)
				dropped = dropped.substr(root.size());
			while (!dropped.empty() && (dropped[0] == '\\' || dropped[0] == '/'))
				dropped.erase(0, 1);
			std::string ext = std::filesystem::path(dropped).extension().string();
			for (char& c : ext) c = (char)tolower((unsigned char)c);
			if (ext == ".vfx")
				AssetPath = dropped;
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::SetCursorScreenPos(after);

	if (!m_AssetError.empty())
		UnityGUI::HelpBox(m_AssetError.c_str());
	else if (m_Asset)
		for (const std::string& issue : m_Asset->Validate())
			UnityGUI::HelpBox(issue.c_str());

	UnityGUI::TextField("Initial Event Name", &InitialEvent);
	UnityGUI::Slider("Play Rate", &PlayRate, 0.0f, 4.0f);
	UnityGUI::Toggle("Reseed On Play", &ResetSeedOnPlay);
	if (!ResetSeedOnPlay)
	{
		int seed = (int)StartSeed;
		if (UnityGUI::Int("Random Seed", &seed))
			StartSeed = (uint32)(std::max)(0, seed);
	}

	// Play Controls (Unity 의 Scene 뷰 패널을 Inspector 안에)
	UnityGUI::Spacing(4.0f);
	ImGui::Indent(8.0f);
	if (ImGui::Button("Play")) Play();
	ImGui::SameLine();
	if (ImGui::Button("Stop")) Stop();
	ImGui::SameLine();
	if (ImGui::Button("Restart")) Reinit();
	ImGui::SameLine();
	ImGui::Checkbox("Pause", &Paused);
	ImGui::SameLine(0.0f, 14.0f);
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("Alive %d", m_Alive);
	ImGui::Unindent(8.0f);
	UnityGUI::Spacing(4.0f);

	// Properties (Exposed): 왼쪽 체크 = 덮어쓰기
	if (m_Asset && !m_Asset->Properties.empty() && UnityGUI::Foldout("Properties", 0, true, false))
	{
		for (const Vfx::Property& p : m_Asset->Properties)
		{
			ImGui::PushID(p.Name.c_str());
			auto it = Overrides.find(p.Name);
			bool overridden = it != Overrides.end() && it->second.Enabled;
			if (UnityGUI::LeadingCheckbox("##ov", &overridden, 0))
			{
				if (overridden)
					Overrides[p.Name] = { p.Value, true };
				else
					Overrides.erase(p.Name);
			}
			std::array<float, 4> v = p.Value;
			if (overridden)
				v = Overrides[p.Name].Value;
			bool changed = false;
			ImGui::BeginDisabled(!overridden);
			switch (p.Type)
			{
			case Vfx::PropertyType::Float:
				changed = (p.Min != 0.0f || p.Max != 0.0f) ? UnityGUI::Slider(p.Name.c_str(), &v[0], p.Min, p.Max, 1) : UnityGUI::Float(p.Name.c_str(), &v[0], 1);
				break;
			case Vfx::PropertyType::Int:
			{
				int i = (int)v[0];
				changed = UnityGUI::Int(p.Name.c_str(), &i, 1);
				v[0] = (float)i;
				break;
			}
			case Vfx::PropertyType::Bool:
			{
				bool b = v[0] > 0.5f;
				changed = UnityGUI::Toggle(p.Name.c_str(), &b, 1);
				v[0] = b ? 1.0f : 0.0f;
				break;
			}
			case Vfx::PropertyType::Vector3: changed = UnityGUI::Vector3(p.Name.c_str(), v.data(), false, 1); break;
			case Vfx::PropertyType::Color: changed = UnityGUI::Color(p.Name.c_str(), v.data(), 1); break;
			}
			ImGui::EndDisabled();
			if (changed && overridden)
				Overrides[p.Name].Value = v;
			ImGui::PopID();
		}
	}
}

// ------------------------------------------------------------------ 저장
GENERATE_COMPONENT_FUNC_TOJSON(VisualEffect)
{
	json j;
	SERIALIZE_TYPE(j, VisualEffect);
	j["enabled"] = m_Enabled;
	j["asset"] = AssetPath;
	j["initialEvent"] = InitialEvent;
	j["playRate"] = PlayRate;
	j["seed"] = StartSeed;
	j["resetSeedOnPlay"] = ResetSeedOnPlay;
	json ov = json::array();
	for (const auto& [name, o] : Overrides)
		ov.push_back({ { "name", name }, { "value", { o.Value[0], o.Value[1], o.Value[2], o.Value[3] } }, { "enabled", o.Enabled } });
	j["overrides"] = ov;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(VisualEffect)
{
	m_Enabled = j.value("enabled", true);
	AssetPath = j.value("asset", std::string());
	InitialEvent = j.value("initialEvent", std::string("OnPlay"));
	PlayRate = j.value("playRate", 1.0f);
	StartSeed = j.value("seed", 0u);
	ResetSeedOnPlay = j.value("resetSeedOnPlay", true);
	Overrides.clear();
	if (j.contains("overrides") && j["overrides"].is_array())
		for (const json& o : j["overrides"])
		{
			OverrideValue v;
			if (o.contains("value"))
				v.Value = Vfx::ValueFromJson(o["value"], v.Value);
			v.Enabled = o.value("enabled", true);
			Overrides[o.value("name", std::string())] = v;
		}
	m_Revision = 0;   // 다음 Advance 에서 에셋을 다시 맞춘다
	m_LoadedPath.clear();
}
