#include "pch.h"
#include "PlayableDirector.h"
#include "AnimationOverrides.h"
#include "AudioSource.h"
#include "ScriptEngine.h"
#include "UnityGUI.h"
#include "TimelineWindow.h"

namespace
{
	GameObject* FindObject(uint64 id)
	{
		if (id == 0)
			return nullptr;
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		return scene ? scene->FindByFileID(id) : nullptr;
	}

	// 겹침으로 생긴 섞기 + Ease (선형) — Unity 는 겹친 두 클립 사이에 자동 Blend 를 만든다
	float ClipWeight(const TimelineTrack& track, size_t i, double t)
	{
		const TimelineClip& c = track.Clips[i];
		if (t < c.Start || t > c.End())
			return 0.0f;
		double ein = c.EaseIn, eout = c.EaseOut;
		if (i > 0 && track.Clips[i - 1].End() > c.Start)
			ein = (std::max)(ein, track.Clips[i - 1].End() - c.Start);
		if (i + 1 < track.Clips.size() && track.Clips[i + 1].Start < c.End())
			eout = (std::max)(eout, c.End() - track.Clips[i + 1].Start);
		double w = 1.0;
		if (ein > 1e-6 && t < c.Start + ein)
			w *= (t - c.Start) / ein;
		if (eout > 1e-6 && t > c.End() - eout)
			w *= (c.End() - t) / eout;
		return (float)std::clamp(w, 0.0, 1.0);
	}

	std::vector<std::string> ListAssets(const wchar_t* ext)
	{
		std::vector<std::string> out;
		std::error_code ec;
		const std::filesystem::path root = PathManager::GetI()->GetMovePathW(L"Assets\\");
		const std::filesystem::path project = root.parent_path().parent_path();
		for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
			!ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
			if (it->is_regular_file(ec) && _wcsicmp(it->path().extension().c_str(), ext) == 0)
				out.push_back(wstring_to_string(std::filesystem::relative(it->path(), project, ec).wstring()));
		std::sort(out.begin(), out.end());
		return out;
	}
}

PlayableDirector::PlayableDirector()
{
	m_InspectorTitleName = "Playable Director";
}

PlayableDirector::~PlayableDirector()
{
	for (const GameObject* go : m_Overridden)
		AnimationOverrides::Clear(go);
}

std::shared_ptr<TimelineAsset> PlayableDirector::GetAsset()
{
	const unsigned long long now = ::GetTickCount64();
	if (m_AssetFor != AssetPath || (m_Asset && now - m_AssetCheck > 500))
	{
		m_AssetFor = AssetPath;
		m_AssetCheck = now;
		m_Asset = AssetPath.empty() ? nullptr : TimelineAsset::Load(AssetPath);   // 파일이 바뀌었으면 다시 (같은 객체)
	}
	return m_Asset;
}

double PlayableDirector::Duration()
{
	auto a = GetAsset();
	return a ? a->Duration() : 0.0;
}

GameObject* PlayableDirector::BoundObject(const TimelineTrack& track) const
{
	auto it = Bindings.find(track.Id);
	return it == Bindings.end() ? nullptr : FindObject(it->second);
}

void PlayableDirector::SetBinding(const std::string& trackIdOrName, uint64 gameObject)
{
	auto a = GetAsset();
	std::string id = trackIdOrName;
	if (a)
		if (TimelineTrack* t = a->FindTrack(trackIdOrName))
			id = t->Id;
	if (gameObject == 0)
		Bindings.erase(id);
	else
		Bindings[id] = gameObject;
}

std::shared_ptr<AnimationClip> PlayableDirector::ClipOf(const TimelineClip& c)
{
	const std::string key = c.Asset + "#" + std::to_string(c.ClipIndex);
	auto it = m_Clips.find(key);
	if (it != m_Clips.end())
		return it->second;
	auto clip = c.Asset.empty() ? nullptr : ResourceManager::GetI()->LoadAnimationClip(c.Asset, c.ClipIndex);
	m_Clips[key] = clip;
	return clip;
}

void PlayableDirector::SetActiveTracked(GameObject* go, bool active)
{
	if (go == nullptr)
		return;
	const uint64 id = go->GetFileID();
	if (m_InitialActive.find(id) == m_InitialActive.end())
		m_InitialActive[id] = go->IsActive();
	if (go->IsActive() != active)
		go->SetActive(active);
}

void PlayableDirector::RevertAll()
{
	for (const auto& [id, active] : m_InitialActive)
		if (GameObject* go = FindObject(id))
			go->SetActive(active);
	m_InitialActive.clear();
	for (const GameObject* go : m_Overridden)
		AnimationOverrides::Clear(go);
	m_Overridden.clear();
	auto a = m_Asset;
	if (a)
		for (const TimelineTrack& t : a->Tracks)
			if (t.Type == "Audio")
				if (GameObject* go = BoundObject(t))
					if (auto* src = go->GetComponent<AudioSource>())
						src->Stop();
	m_AudioStarted.clear();
}

void PlayableDirector::EvaluateAnimation(const TimelineTrack& track, GameObject* bound, double t)
{
	std::vector<AnimationOverrides::Clip> list;
	for (size_t i = 0; i < track.Clips.size(); ++i)
	{
		const float w = ClipWeight(track, i, t);
		if (w <= 0.0f)
			continue;
		const TimelineClip& c = track.Clips[i];
		auto clip = ClipOf(c);
		if (!clip)
			continue;
		AnimationOverrides::Clip o;
		o.Source = clip;
		o.Weight = w;
		o.Loop = c.Loop;
		const double len = clip->GetClipEndTime();
		double local = c.ClipIn + (t - c.Start) * c.Speed;
		if (!c.Loop && len > 0.0)
			local = std::clamp(local, 0.0, len);   // 클립보다 길면 마지막 프레임 (Unity Hold)
		o.Time = (float)local;
		list.push_back(o);
	}
	if (list.empty())
	{
		if (m_Overridden.erase(bound))
			AnimationOverrides::Clear(bound);   // 클립 사이: Animator 그대로
		return;
	}
	AnimationOverrides::Set(bound, list);
	m_Overridden.insert(bound);
}

void PlayableDirector::EvaluateRange(double prev, double now, bool events)
{
	auto a = GetAsset();
	if (!a)
		return;
	for (const TimelineTrack& track : a->Tracks)
	{
		if (track.Muted)
			continue;
		GameObject* bound = BoundObject(track);
		if (track.Type == "Animation")
		{
			if (bound)
				EvaluateAnimation(track, bound, now);
		}
		else if (track.Type == "Activation")
		{
			if (!bound)
				continue;
			bool active = false;
			for (const TimelineClip& c : track.Clips)
				active |= now >= c.Start && now < c.End() + (WrapMode == (int)Wrap::Hold && now >= Duration() ? 1e-6 : 0.0);
			SetActiveTracked(bound, active);
		}
		else if (track.Type == "Cinemachine")
		{
			// 클립의 카메라: 그 클립 시간에만 켠다 (밖에서는 끈다 — 처음 상태는 Stop 이 되돌린다)
			for (const TimelineClip& c : track.Clips)
				if (GameObject* cam = FindObject(c.Object))
					SetActiveTracked(cam, now >= c.Start && now <= c.End());
		}
		else if (track.Type == "Audio" && events)
		{
			AudioSource* src = bound ? bound->GetComponent<AudioSource>() : nullptr;
			if (!src)
				continue;
			for (size_t i = 0; i < track.Clips.size(); ++i)
			{
				const TimelineClip& c = track.Clips[i];
				const std::string key = track.Id + "/" + std::to_string(i);
				const bool inside = now >= c.Start && now < c.End();
				if (inside && !m_AudioStarted.count(key))
				{
					src->SetClip(c.Asset);
					src->Play();
					m_AudioStarted.insert(key);
				}
				else if (!inside && m_AudioStarted.count(key))
				{
					src->Stop();
					m_AudioStarted.erase(key);
				}
			}
		}
		if (events && !track.Markers.empty() && bound)
			for (const TimelineMarker& m : track.Markers)
				if (m.Time > prev && m.Time <= now && !m.Method.empty())
					ScriptEngine::InvokeAnimationEvent(bound->GetFileID(), m.Method, m.String, m.Float, m.Int, (float)m.Time, track.Name, std::string(), 0, 1.0f);
	}
}

void PlayableDirector::Play()
{
	auto a = GetAsset();
	if (!a)
		return;
	if (!m_Playing && !m_Started)
	{
		m_Time = InitialTime;
		// Retroactive 마커: 이미 지난 시간에서 시작해도 부른다
		for (const TimelineTrack& t : a->Tracks)
			if (GameObject* bound = BoundObject(t))
				for (const TimelineMarker& m : t.Markers)
					if (m.Retroactive && m.Time < m_Time && !m.Method.empty())
						ScriptEngine::InvokeAnimationEvent(bound->GetFileID(), m.Method, m.String, m.Float, m.Int, (float)m.Time, t.Name, std::string(), 0, 1.0f);
	}
	m_Playing = true;
	m_Started = true;
	EvaluateRange(m_Time - 1e-9, m_Time, Application::IsPlaying());
}

void PlayableDirector::Pause() { m_Playing = false; }
void PlayableDirector::Resume() { if (m_Started) m_Playing = true; }

void PlayableDirector::Stop()
{
	m_Playing = false;
	m_Started = false;
	m_Time = InitialTime;
	RevertAll();
}

void PlayableDirector::Evaluate()
{
	EvaluateRange(m_Time, m_Time, false);
}

void PlayableDirector::SetTime(double t)
{
	m_Time = (std::max)(0.0, t);
}

void PlayableDirector::Start()
{
	if (PlayOnAwake && Application::IsPlaying())
		Play();
}

void PlayableDirector::Update()
{
	if (!m_Playing || UpdateMethod == 2)
		return;
	const double dt = (std::min)((double)DT, 1.0 / 3.0);   // Unity maximumDeltaTime 과 같게
	const double duration = Duration();
	const double prev = m_Time;
	m_Time += dt;
	if (duration > 0.0 && m_Time >= duration)
	{
		if (WrapMode == (int)Wrap::Loop)
		{
			EvaluateRange(prev, duration, true);   // 끝까지의 신호
			m_Time = fmod(m_Time - duration, duration);
			m_AudioStarted.clear();
			EvaluateRange(-1e-9, m_Time, true);
			return;
		}
		if (WrapMode == (int)Wrap::None)
		{
			EvaluateRange(prev, duration, true);
			Stop();
			return;
		}
		m_Time = duration;   // Hold
	}
	EvaluateRange(prev, m_Time, true);
}

void PlayableDirector::OnDestroy()
{
	RevertAll();
}

void PlayableDirector::OnInspectorGUI()
{
	using namespace UnityGUI;
	const std::string shown = AssetPath.empty() ? "None (Timeline Asset)" : std::filesystem::path(AssetPath).stem().string();
	if (ObjectField("Playable", shown.c_str(), 0, "timeline"))
		ImGui::OpenPopup("##timelinepick");
	if (ImGui::BeginPopup("##timelinepick"))
	{
		if (ImGui::Selectable("None", AssetPath.empty()))
			AssetPath.clear();
		for (const std::string& p : ListAssets(L".playable"))
			if (ImGui::Selectable(p.c_str(), p == AssetPath))
				AssetPath = p;
		ImGui::EndPopup();
	}
	if (AssetPath.empty())
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14);
		if (ImGui::SmallButton("Create Timeline Asset"))
		{
			const std::string name = m_pGameObject ? m_pGameObject->GetName() : std::string("Timeline");
			AssetPath = "Assets\\Timelines\\" + name + ".playable";
			TimelineAsset::Create(AssetPath);
		}
	}
	const char* updates[] = { "Game Time", "Unscaled Game Time", "Manual" };
	Dropdown("Update Method", &UpdateMethod, updates, 3);
	Toggle("Play On Awake", &PlayOnAwake);
	const char* wraps[] = { "Hold", "Loop", "None" };
	Dropdown("Wrap Mode", &WrapMode, wraps, 3);
	float initial = (float)InitialTime;
	if (Float("Initial Time", &initial))
		InitialTime = (std::max)(0.0f, initial);
	if (auto a = GetAsset())
	{
		if (Foldout("Bindings", 0, true, false))
			for (const TimelineTrack& t : a->Tracks)
			{
				if (t.Type == "Cinemachine")
					continue;   // 클립마다 카메라
				uint64 id = Bindings.count(t.Id) ? Bindings[t.Id] : 0;
				const std::string label = t.Name + " (" + t.Type + ")";
				if (GameObjectField(label.c_str(), &id, 1))
					SetBinding(t.Id, id);
			}
		char info[96];
		sprintf_s(info, "%.2f s / %.2f s%s", m_Time, a->Duration(), m_Playing ? "  playing" : "");
		ValueLabel("Time", info);
	}
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14);
	if (ImGui::SmallButton("Open Timeline Window"))
		TimelineWindow::Focus(this);
}

void PlayableDirector::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	for (auto& [track, id] : Bindings)
		if (auto it = map.find(id); it != map.end())
			id = it->second;
}

GENERATE_COMPONENT_FUNC_TOJSON(PlayableDirector)
{
	json j;
	j["type"] = "PlayableDirector";
	j["enabled"] = m_Enabled;
	j["playableAsset"] = AssetPath;
	j["playOnAwake"] = PlayOnAwake;
	j["wrapMode"] = WrapMode == 1 ? "Loop" : WrapMode == 2 ? "None" : "Hold";
	j["initialTime"] = InitialTime;
	j["updateMethod"] = UpdateMethod;
	json b = json::object();
	for (const auto& [track, id] : Bindings)
		b[track] = id;
	j["bindings"] = b;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(PlayableDirector)
{
	m_Enabled = j.value("enabled", true);
	AssetPath = j.value("playableAsset", std::string());
	PlayOnAwake = j.value("playOnAwake", true);
	const std::string wrap = j.value("wrapMode", std::string("Hold"));
	WrapMode = wrap == "Loop" ? 1 : wrap == "None" ? 2 : 0;
	InitialTime = j.value("initialTime", 0.0);
	UpdateMethod = j.value("updateMethod", 0);
	Bindings.clear();
	if (j.contains("bindings") && j["bindings"].is_object())
		for (auto it = j["bindings"].begin(); it != j["bindings"].end(); ++it)
			if (it.value().is_number_unsigned() || it.value().is_number_integer())
				Bindings[it.key()] = it.value().get<uint64>();
}
