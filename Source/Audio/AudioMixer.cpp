#include "pch.h"
#include "AudioMixer.h"
#include "AudioManager.h"
#include <xaudio2.h>
#include <xaudio2fx.h>
#include <xapofx.h>

namespace
{
	constexpr UINT32 kGroupChannels = 2;   // 그룹은 스테레오 (리버브 이펙트가 1·2 채널만 받는다)

	// ---- 이펙트 파라미터 표
	const AudioMixer::ParamInfo kLowpass[] = { { "Cutoff freq", 5000.0f, 10.0f, 22000.0f, "Hz" }, { "Resonance", 1.0f, 0.5f, 10.0f, "" } };
	const AudioMixer::ParamInfo kHighpass[] = { { "Cutoff freq", 500.0f, 10.0f, 22000.0f, "Hz" }, { "Resonance", 1.0f, 0.5f, 10.0f, "" } };
	const AudioMixer::ParamInfo kEcho[] = { { "Delay", 500.0f, 1.0f, 2000.0f, "ms" }, { "Feedback", 0.5f, 0.0f, 1.0f, "" }, { "Wet Mix", 0.5f, 0.0f, 1.0f, "" } };
	const AudioMixer::ParamInfo kReverb[] = { { "Room", 0.0f, 0.0f, 18.0f, "" }, { "Wet Mix", 40.0f, 0.0f, 100.0f, "%" } };
	const char* kEffectNames[] = { "Lowpass", "Highpass", "Echo", "Reverb" };

	struct ReverbPreset { const char* Name; XAUDIO2FX_REVERB_I3DL2_PARAMETERS P; };
	const ReverbPreset kReverbPresets[] = {
		{ "Room", XAUDIO2FX_I3DL2_PRESET_ROOM }, { "Small Room", XAUDIO2FX_I3DL2_PRESET_SMALLROOM }, { "Medium Room", XAUDIO2FX_I3DL2_PRESET_MEDIUMROOM },
		{ "Large Room", XAUDIO2FX_I3DL2_PRESET_LARGEROOM }, { "Bathroom", XAUDIO2FX_I3DL2_PRESET_BATHROOM }, { "Living Room", XAUDIO2FX_I3DL2_PRESET_LIVINGROOM },
		{ "Stone Room", XAUDIO2FX_I3DL2_PRESET_STONEROOM }, { "Auditorium", XAUDIO2FX_I3DL2_PRESET_AUDITORIUM }, { "Concert Hall", XAUDIO2FX_I3DL2_PRESET_CONCERTHALL },
		{ "Large Hall", XAUDIO2FX_I3DL2_PRESET_LARGEHALL }, { "Cave", XAUDIO2FX_I3DL2_PRESET_CAVE }, { "Arena", XAUDIO2FX_I3DL2_PRESET_ARENA },
		{ "Hangar", XAUDIO2FX_I3DL2_PRESET_HANGAR }, { "Hallway", XAUDIO2FX_I3DL2_PRESET_HALLWAY }, { "Stone Corridor", XAUDIO2FX_I3DL2_PRESET_STONECORRIDOR },
		{ "Alley", XAUDIO2FX_I3DL2_PRESET_ALLEY }, { "Forest", XAUDIO2FX_I3DL2_PRESET_FOREST }, { "Underwater", XAUDIO2FX_I3DL2_PRESET_UNDERWATER },
		{ "Plate", XAUDIO2FX_I3DL2_PRESET_PLATE },
	};

	std::map<std::string, std::shared_ptr<AudioMixer>>& Registry()
	{
		static std::map<std::string, std::shared_ptr<AudioMixer>> mixers;
		return mixers;
	}

	std::string NormalizePath(std::string path)
	{
		std::replace(path.begin(), path.end(), '/', '\\');
		// "Assets\\\\A.mixer" 처럼 겹친 구분자도 같은 믹서로
		path.erase(std::unique(path.begin(), path.end(), [](char a, char b) { return a == '\\' && b == '\\'; }), path.end());
		const size_t first = path.find_first_not_of('\\');
		path.erase(0, first == std::string::npos ? path.size() : first);
		return path;
	}

	std::wstring FilePath(const std::string& path)
	{
		std::filesystem::path p(string_to_wstring(path));
		return p.is_absolute() ? p.wstring() : PathManager::GetI()->GetMovePathW(string_to_wstring(path));
	}

	float DbToLinear(float db) { return db <= -79.9f ? 0.0f : powf(10.0f, db / 20.0f); }
}

// ---------------------------------------------------------------------- 정보
const char* AudioMixer::EffectName(int type) { return type >= 0 && type < EffectTypeCount ? kEffectNames[type] : "?"; }

int AudioMixer::EffectParamCount(int type)
{
	switch (type) { case Lowpass: case Highpass: return 2; case Echo: return 3; case Reverb: return 2; default: return 0; }
}

const AudioMixer::ParamInfo& AudioMixer::EffectParam(int type, int index)
{
	static const ParamInfo none = { "", 0.0f, 0.0f, 0.0f, "" };
	const ParamInfo* table = type == Lowpass ? kLowpass : type == Highpass ? kHighpass : type == Echo ? kEcho : type == Reverb ? kReverb : nullptr;
	return table && index >= 0 && index < EffectParamCount(type) ? table[index] : none;
}

int AudioMixer::ReverbPresetCount() { return (int)std::size(kReverbPresets); }
const char* AudioMixer::ReverbPresetName(int index) { return index >= 0 && index < ReverbPresetCount() ? kReverbPresets[index].Name : "?"; }

std::string AudioMixer::Name() const { return std::filesystem::path(Path).stem().string(); }

AudioMixer::~AudioMixer() {}

// ---------------------------------------------------------------------- 저장 / 읽기
std::string AudioMixer::ToJsonString() const
{
	json j;
	json groups = json::array();
	for (const Group& g : Groups)
	{
		json effects = json::array();
		for (const Effect& e : g.Effects)
			effects.push_back({ { "id", e.Id }, { "type", EffectName(e.Type) }, { "bypass", e.Bypass } });
		groups.push_back({ { "id", g.Id }, { "name", g.Name }, { "parent", g.Parent }, { "mute", g.Mute }, { "solo", g.Solo },
			{ "bypassEffects", g.BypassEffects }, { "effects", effects } });
	}
	j["groups"] = groups;
	json snaps = json::array();
	for (const Snapshot& s : Snapshots)
		snaps.push_back({ { "name", s.Name }, { "values", s.Values } });
	j["snapshots"] = snaps;
	json exposed = json::array();
	for (const Exposed& e : ExposedParams)
		exposed.push_back({ { "name", e.Name }, { "key", e.Key } });
	j["exposed"] = exposed;
	j["nextId"] = NextId;
	return j.dump(4);
}

void AudioMixer::ApplyJson(const std::string& text)
{
	json j = json::parse(text, nullptr, false);
	if (j.is_discarded())
		return;
	Groups.clear();
	Snapshots.clear();
	ExposedParams.clear();
	NextId = j.value("nextId", 1);
	if (j.contains("groups"))
		for (const auto& gj : j["groups"])
		{
			Group g;
			g.Id = gj.value("id", NextId);
			g.Name = gj.value("name", std::string("Group"));
			g.Parent = gj.value("parent", -1);
			g.Mute = gj.value("mute", false);
			g.Solo = gj.value("solo", false);
			g.BypassEffects = gj.value("bypassEffects", false);
			if (gj.contains("effects"))
				for (const auto& ej : gj["effects"])
				{
					Effect e;
					e.Id = ej.value("id", 0);
					const std::string type = ej.value("type", std::string("Lowpass"));
					for (int t = 0; t < EffectTypeCount; ++t)
						if (type == kEffectNames[t]) e.Type = t;
					e.Bypass = ej.value("bypass", false);
					g.Effects.push_back(e);
				}
			Groups.push_back(g);
		}
	if (j.contains("snapshots"))
		for (const auto& sj : j["snapshots"])
		{
			Snapshot s;
			s.Name = sj.value("name", std::string("Snapshot"));
			if (sj.contains("values") && sj["values"].is_object())
				for (auto it = sj["values"].begin(); it != sj["values"].end(); ++it)
					if (it.value().is_number())
						s.Values[it.key()] = it.value().get<float>();
			Snapshots.push_back(s);
		}
	if (j.contains("exposed"))
		for (const auto& ej : j["exposed"])
			ExposedParams.push_back({ ej.value("name", std::string()), ej.value("key", std::string()) });
	// 최소 구성: Master 와 스냅숏 하나
	if (Groups.empty())
	{
		Group master;
		master.Id = NextId++;
		master.Name = "Master";
		Groups.push_back(master);
	}
	Groups[0].Parent = -1;
	for (size_t i = 1; i < Groups.size(); ++i)
		if (Groups[i].Parent < 0 || Groups[i].Parent >= (int)i)
			Groups[i].Parent = 0;   // 부모는 늘 앞에 (저장 순서)
	if (Snapshots.empty())
		Snapshots.push_back({ "Snapshot", {} });
	int maxId = 0;
	for (const Group& g : Groups)
	{
		maxId = (std::max)(maxId, g.Id);
		for (const Effect& e : g.Effects)
			maxId = (std::max)(maxId, e.Id);
	}
	NextId = (std::max)(NextId, maxId + 1);
	EditSnapshot = std::clamp(EditSnapshot, 0, (int)Snapshots.size() - 1);
	StructureChanged();
}

bool AudioMixer::Save() const
{
	const std::wstring file = FilePath(Path);
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
	std::ofstream os(file, std::ios::binary | std::ios::trunc);
	if (!os)
		return false;
	os << ToJsonString();
	return true;
}

std::shared_ptr<AudioMixer> AudioMixer::Load(const std::string& rawPath)
{
	const std::string path = NormalizePath(rawPath);
	if (path.empty())
		return nullptr;
	auto it = Registry().find(path);
	if (it != Registry().end())
		return it->second;
	std::ifstream in(FilePath(path), std::ios::binary);
	if (!in)
		return nullptr;
	std::stringstream ss;
	ss << in.rdbuf();
	auto m = std::make_shared<AudioMixer>();
	m->Path = path;
	m->ApplyJson(ss.str());
	Registry()[path] = m;
	return m;
}

std::shared_ptr<AudioMixer> AudioMixer::Create(const std::string& rawPath)
{
	const std::string path = NormalizePath(rawPath);
	auto m = std::make_shared<AudioMixer>();
	m->Path = path;
	m->ApplyJson("{}");
	m->Save();
	Registry()[path] = m;
	return m;
}

std::vector<std::string> AudioMixer::FindAll()
{
	std::vector<std::string> out;
	const std::wstring root = PathManager::GetI()->GetMovePathW(L"Assets\\");
	std::error_code ec;
	if (!std::filesystem::exists(root, ec))
		return out;
	for (const auto& e : std::filesystem::recursive_directory_iterator(root, ec))
		if (e.is_regular_file() && e.path().extension() == L".mixer")
			out.push_back(wstring_to_string(L"Assets\\" + std::filesystem::relative(e.path(), root, ec).wstring()));
	return out;
}

// ---------------------------------------------------------------------- 편집
int AudioMixer::FindGroup(const std::string& name) const
{
	for (size_t i = 0; i < Groups.size(); ++i)
		if (Groups[i].Name == name)
			return (int)i;
	return -1;
}

int AudioMixer::AddGroup(int parent, const std::string& base)
{
	parent = std::clamp(parent, 0, (int)Groups.size() - 1);
	std::string name = base;
	for (int n = 1; FindGroup(name) >= 0; ++n)
		name = base + " " + std::to_string(n);
	Group g;
	g.Id = NextId++;
	g.Name = name;
	g.Parent = parent;
	// 부모 바로 뒤 (자식 묶음의 끝) 에 넣어 "부모는 늘 앞" 을 지킨다
	int at = parent + 1;
	while (at < (int)Groups.size())
	{
		int p = Groups[at].Parent;
		bool under = false;
		while (p >= 0) { if (p == parent) { under = true; break; } p = Groups[p].Parent; }
		if (!under)
			break;
		++at;
	}
	for (Group& other : Groups)
		if (other.Parent >= at)
			++other.Parent;
	Groups.insert(Groups.begin() + at, g);
	StructureChanged();
	return at;
}

void AudioMixer::RemoveGroup(int index)
{
	if (index <= 0 || index >= (int)Groups.size())
		return;
	// 지울 그룹과 그 자손
	std::vector<bool> remove(Groups.size(), false);
	for (size_t i = 0; i < Groups.size(); ++i)
		for (int p = (int)i; p >= 0; p = Groups[p].Parent)
			if (p == index) { remove[i] = true; break; }
	std::vector<int> newIndex(Groups.size(), -1);
	std::vector<Group> kept;
	for (size_t i = 0; i < Groups.size(); ++i)
		if (!remove[i])
		{
			newIndex[i] = (int)kept.size();
			kept.push_back(Groups[i]);
		}
	for (Group& g : kept)
		g.Parent = g.Parent >= 0 ? newIndex[g.Parent] : -1;
	Groups.swap(kept);
	StructureChanged();
}

bool AudioMixer::RenameGroup(int index, const std::string& name)
{
	if (index < 0 || index >= (int)Groups.size() || name.empty() || FindGroup(name) >= 0)
		return false;
	Groups[index].Name = name;
	return true;
}

void AudioMixer::AddEffect(int group, int type)
{
	if (group < 0 || group >= (int)Groups.size() || type < 0 || type >= EffectTypeCount)
		return;
	Effect e;
	e.Id = NextId++;
	e.Type = type;
	Groups[group].Effects.push_back(e);
	StructureChanged();
}

void AudioMixer::RemoveEffect(int group, int effect)
{
	if (group < 0 || group >= (int)Groups.size() || effect < 0 || effect >= (int)Groups[group].Effects.size())
		return;
	Groups[group].Effects.erase(Groups[group].Effects.begin() + effect);
	StructureChanged();
}

int AudioMixer::AddSnapshot(const std::string& base)
{
	std::string name = base;
	for (int n = 1; FindSnapshot(name) >= 0; ++n)
		name = base + " " + std::to_string(n);
	Snapshot s = Snapshots[std::clamp(EditSnapshot, 0, (int)Snapshots.size() - 1)];
	s.Name = name;
	Snapshots.push_back(s);
	return (int)Snapshots.size() - 1;
}

void AudioMixer::RemoveSnapshot(int index)
{
	if (Snapshots.size() <= 1 || index < 0 || index >= (int)Snapshots.size())
		return;
	Snapshots.erase(Snapshots.begin() + index);
	EditSnapshot = std::clamp(EditSnapshot, 0, (int)Snapshots.size() - 1);
}

int AudioMixer::FindSnapshot(const std::string& name) const
{
	for (size_t i = 0; i < Snapshots.size(); ++i)
		if (Snapshots[i].Name == name)
			return (int)i;
	return -1;
}

std::string AudioMixer::Expose(const std::string& key)
{
	for (const Exposed& e : ExposedParams)
		if (e.Key == key)
			return e.Name;
	std::string name = "MyExposedParam";
	for (int n = 1; FindExposed(name) >= 0; ++n)
		name = "MyExposedParam " + std::to_string(n);
	ExposedParams.push_back({ name, key });
	return name;
}

void AudioMixer::Unexpose(int index)
{
	if (index >= 0 && index < (int)ExposedParams.size())
		ExposedParams.erase(ExposedParams.begin() + index);
}

int AudioMixer::FindExposed(const std::string& name) const
{
	for (size_t i = 0; i < ExposedParams.size(); ++i)
		if (ExposedParams[i].Name == name)
			return (int)i;
	return -1;
}

const AudioMixer::Effect* AudioMixer::FindEffect(int id, int* group) const
{
	for (size_t g = 0; g < Groups.size(); ++g)
		for (const Effect& e : Groups[g].Effects)
			if (e.Id == id)
			{
				if (group) *group = (int)g;
				return &e;
			}
	return nullptr;
}

std::string AudioMixer::KeyLabel(const std::string& key) const
{
	int id = 0, p = 0;
	if (sscanf_s(key.c_str(), "g%d/vol", &id) == 1)
		for (const Group& g : Groups)
			if (g.Id == id)
				return g.Name + " Volume";
	if (sscanf_s(key.c_str(), "fx%d/p%d", &id, &p) == 2)
	{
		int group = -1;
		if (const Effect* e = FindEffect(id, &group))
			return Groups[group].Name + " " + EffectName(e->Type) + " " + EffectParam(e->Type, p).Name;
	}
	return key;
}

float AudioMixer::DefaultValue(const std::string& key) const
{
	int id = 0, p = 0;
	if (sscanf_s(key.c_str(), "fx%d/p%d", &id, &p) == 2)
		if (const Effect* e = FindEffect(id))
			return EffectParam(e->Type, p).Default;
	return 0.0f;   // 볼륨 0 dB
}

float AudioMixer::GetValue(int snapshot, const std::string& key) const
{
	if (snapshot >= 0 && snapshot < (int)Snapshots.size())
	{
		auto it = Snapshots[snapshot].Values.find(key);
		if (it != Snapshots[snapshot].Values.end())
			return it->second;
	}
	return DefaultValue(key);
}

void AudioMixer::SetValue(int snapshot, const std::string& key, float value)
{
	if (snapshot < 0 || snapshot >= (int)Snapshots.size())
		return;
	Snapshots[snapshot].Values[key] = value;
	if (m_LivePlaying)
		m_Live[key] = value;   // Play 중 편집은 바로 들리게
}

std::vector<std::string> AudioMixer::AllKeys() const
{
	std::vector<std::string> keys;
	for (const Group& g : Groups)
	{
		keys.push_back(VolumeKey(g));
		for (const Effect& e : g.Effects)
			for (int p = 0; p < EffectParamCount(e.Type); ++p)
				keys.push_back(EffectKey(e, p));
	}
	return keys;
}

// ---------------------------------------------------------------------- 실행 값 (스냅숏 · 전환 · 노출 파라미터)
float AudioMixer::LiveValue(const std::string& key) const
{
	if (!m_LivePlaying)
		return GetValue(EditSnapshot, key);
	auto o = m_Overrides.find(key);
	if (o != m_Overrides.end())
		return o->second;
	auto it = m_Live.find(key);
	return it != m_Live.end() ? it->second : GetValue(0, key);
}

bool AudioMixer::SetFloat(const std::string& name, float value)
{
	const int i = FindExposed(name);
	if (i < 0)
		return false;
	m_Overrides[ExposedParams[i].Key] = value;
	return true;
}

bool AudioMixer::GetFloat(const std::string& name, float& value) const
{
	const int i = FindExposed(name);
	if (i < 0)
		return false;
	value = LiveValue(ExposedParams[i].Key);
	return true;
}

bool AudioMixer::ClearFloat(const std::string& name)
{
	const int i = FindExposed(name);
	if (i < 0)
		return false;
	m_Overrides.erase(ExposedParams[i].Key);
	return true;
}

bool AudioMixer::TransitionTo(const std::string& snapshot, float seconds)
{
	const int s = FindSnapshot(snapshot);
	if (s < 0)
		return false;
	m_From.clear();
	m_To.clear();
	for (const std::string& key : AllKeys())
	{
		auto it = m_Live.find(key);
		m_From[key] = it != m_Live.end() ? it->second : GetValue(0, key);
		m_To[key] = GetValue(s, key);
	}
	m_TransDuration = (std::max)(0.0f, seconds);
	m_TransTime = 0.0f;
	if (m_TransDuration <= 0.0f)
	{
		for (auto& [k, v] : m_To) m_Live[k] = v;
		m_To.clear();
	}
	return true;
}

void AudioMixer::Tick(float dt)
{
	const bool playing = Application::IsPlaying();
	if (playing != m_LivePlaying)
	{
		// Play 시작: 시작 스냅숏에서, 끝: 실행 중 바꾼 값(전환·노출 파라미터)을 버린다 (Unity 와 같음)
		m_LivePlaying = playing;
		m_Live.clear();
		m_Overrides.clear();
		m_From.clear();
		m_To.clear();
		if (playing)
			for (const std::string& key : AllKeys())
				m_Live[key] = GetValue(0, key);
	}
	if (playing && !m_To.empty())
	{
		m_TransTime += dt;
		const float t = m_TransDuration > 0.0f ? std::clamp(m_TransTime / m_TransDuration, 0.0f, 1.0f) : 1.0f;
		for (auto& [key, to] : m_To)
		{
			const float from = m_From.count(key) ? m_From[key] : to;
			m_Live[key] = from + (to - from) * t;   // 볼륨은 dB 로 (Unity 와 같음)
		}
		if (t >= 1.0f)
			m_To.clear();
	}
}

// ---------------------------------------------------------------------- 보이스
IXAudio2Voice* AudioMixer::GroupVoice(int group)
{
	if (group < 0 || group >= (int)Groups.size() || !AudioManager::Init())
		return nullptr;
	if (m_BuiltStructure != m_Structure)
		BuildVoices();
	return group < (int)m_Runtime.size() ? m_Runtime[group].Voice : nullptr;
}

void AudioMixer::BuildVoices()
{
	IXAudio2* engine = AudioManager::Engine();
	if (engine == nullptr)
		return;
	for (GroupRuntime& r : m_Runtime)
		if (r.Voice)
			m_Retired.push_back(r.Voice);
	m_RetireFrames = 0;
	m_Runtime.assign(Groups.size(), GroupRuntime());
	const UINT32 rate = AudioManager::SampleRate();
	for (size_t i = 0; i < Groups.size(); ++i)   // 부모가 늘 앞에 있다
	{
		const Group& g = Groups[i];
		GroupRuntime& r = m_Runtime[i];
		int depth = 0;
		for (int p = g.Parent; p >= 0; p = Groups[p].Parent)
			++depth;
		// 효과 체인: [0] 레벨 측정기, 그다음 Echo · Reverb (Lowpass · Highpass 는 보이스 필터)
		std::vector<XAUDIO2_EFFECT_DESCRIPTOR> chain;
		std::vector<IUnknown*> made;
		IUnknown* meter = nullptr;
		if (SUCCEEDED(::XAudio2CreateVolumeMeter(&meter)))
		{
			chain.push_back({ meter, TRUE, kGroupChannels });
			made.push_back(meter);
		}
		r.ChainIndex.assign(g.Effects.size(), -1);
		r.Enabled.assign(g.Effects.size(), true);
		r.Applied.assign(g.Effects.size(), {});
		for (size_t e = 0; e < g.Effects.size(); ++e)
		{
			IUnknown* apo = nullptr;
			if (g.Effects[e].Type == Echo)
			{
				FXECHO_INITDATA init = { FXECHO_MAX_DELAY };
				::CreateFX(__uuidof(FXEcho), &apo, &init, sizeof(init));
			}
			else if (g.Effects[e].Type == Reverb)
				::XAudio2CreateReverb(&apo);
			if (apo == nullptr)
				continue;
			r.ChainIndex[e] = (int)chain.size();
			chain.push_back({ apo, TRUE, kGroupChannels });
			made.push_back(apo);
		}
		XAUDIO2_EFFECT_CHAIN effects = { (UINT32)chain.size(), chain.data() };
		XAUDIO2_SEND_DESCRIPTOR send = { 0, i == 0 ? nullptr : m_Runtime[g.Parent].Voice };
		XAUDIO2_VOICE_SENDS sends = { 1, &send };
		// 자식이 부모보다 먼저 처리되도록 (ProcessingStage 가 작을수록 먼저)
		const HRESULT hr = engine->CreateSubmixVoice(&r.Voice, kGroupChannels, rate, XAUDIO2_VOICE_USEFILTER, 1000 - depth,
			i == 0 || send.pOutputVoice == nullptr ? nullptr : &sends, chain.empty() ? nullptr : &effects);
		for (IUnknown* u : made)
			u->Release();   // 보이스가 들고 있다
		if (FAILED(hr))
		{
			r.Voice = nullptr;
			EditorLog::Write("Audio", "mixer %s: group %s submix failed hr=0x%08X", Name().c_str(), g.Name.c_str(), (unsigned)hr);
		}
	}
	m_BuiltStructure = m_Structure;
	++m_Generation;
	EditorLog::Write("Audio", "mixer %s: %d groups ready (generation %u)", Name().c_str(), (int)Groups.size(), m_Generation);
}

void AudioMixer::DestroyRetired(bool force)
{
	// 소스가 새 그룹으로 다시 연결할 시간을 준 뒤 (몇 프레임) 지운다
	if (m_Retired.empty() || (!force && ++m_RetireFrames < 3))
		return;
	for (IXAudio2SubmixVoice* v : m_Retired)
		v->DestroyVoice();
	m_Retired.clear();
}

void AudioMixer::ApplyToVoices()
{
	if (m_Runtime.size() != Groups.size())
		return;
	bool anySolo = false;
	for (const Group& g : Groups)
		anySolo |= g.Solo;
	const UINT32 rate = AudioManager::SampleRate();
	const double now = ImGui::GetTime();
	for (size_t i = 0; i < Groups.size(); ++i)
	{
		const Group& g = Groups[i];
		GroupRuntime& r = m_Runtime[i];
		if (r.Voice == nullptr)
			continue;
		// Solo: 솔로인 그룹과 그 조상·자손만 들린다
		bool audible = true;
		if (anySolo)
		{
			audible = false;
			for (int p = (int)i; p >= 0 && !audible; p = Groups[p].Parent)
				audible = Groups[p].Solo;
			for (size_t c = 0; c < Groups.size() && !audible; ++c)
				if (Groups[c].Solo)
					for (int p = (int)c; p >= 0 && !audible; p = Groups[p].Parent)
						audible = p == (int)i;
		}
		r.Voice->SetVolume(g.Mute || !audible ? 0.0f : DbToLinear(LiveValue(VolumeKey(g))));

		// 필터: 처음으로 켜진 Lowpass / Highpass (없으면 다 통과)
		XAUDIO2_FILTER_PARAMETERS filter = { LowPassFilter, XAUDIO2_MAX_FILTER_FREQUENCY, 1.0f };
		for (size_t e = 0; e < g.Effects.size(); ++e)
		{
			const Effect& fx = g.Effects[e];
			const bool on = !fx.Bypass && !g.BypassEffects;
			if (fx.Type == Lowpass || fx.Type == Highpass)
			{
				if (on && filter.Frequency >= XAUDIO2_MAX_FILTER_FREQUENCY)
				{
					const float cutoff = std::clamp(LiveValue(EffectKey(fx, 0)), 10.0f, rate * 0.45f);
					filter.Type = fx.Type == Lowpass ? LowPassFilter : HighPassFilter;
					// XAudio2CutoffFrequencyToRadians 와 같다 (그 함수는 XAUDIO2_HELPER_FUNCTIONS 를 켜야 보인다)
					filter.Frequency = (std::min)(XAUDIO2_MAX_FILTER_FREQUENCY, 2.0f * sinf(XM_PI * cutoff / (float)rate));
					filter.OneOverQ = 1.0f / std::clamp(LiveValue(EffectKey(fx, 1)), 0.5f, 10.0f);
				}
				continue;
			}
			const int ci = r.ChainIndex[e];
			if (ci < 0)
				continue;
			if (r.Enabled[e] != on)
			{
				if (on) r.Voice->EnableEffect(ci); else r.Voice->DisableEffect(ci);
				r.Enabled[e] = on;
			}
			std::vector<float> values;
			for (int p = 0; p < EffectParamCount(fx.Type); ++p)
				values.push_back(LiveValue(EffectKey(fx, p)));
			if (values == r.Applied[e])
				continue;
			r.Applied[e] = values;
			if (fx.Type == Echo)
			{
				FXECHO_PARAMETERS ep;
				ep.Delay = std::clamp(values[0], FXECHO_MIN_DELAY, FXECHO_MAX_DELAY);
				ep.Feedback = std::clamp(values[1], FXECHO_MIN_FEEDBACK, FXECHO_MAX_FEEDBACK);
				ep.WetDryMix = std::clamp(values[2], FXECHO_MIN_WETDRYMIX, FXECHO_MAX_WETDRYMIX);
				r.Voice->SetEffectParameters(ci, &ep, sizeof(ep));
			}
			else if (fx.Type == Reverb)
			{
				XAUDIO2FX_REVERB_PARAMETERS native;
				const int preset = std::clamp((int)lroundf(values[0]), 0, ReverbPresetCount() - 1);
				ReverbConvertI3DL2ToNative(&kReverbPresets[preset].P, &native);
				native.WetDryMix = std::clamp(values[1], XAUDIO2FX_REVERB_MIN_WET_DRY_MIX, XAUDIO2FX_REVERB_MAX_WET_DRY_MIX);
				r.Voice->SetEffectParameters(ci, &native, sizeof(native));
			}
		}
		r.Voice->SetFilterParameters(&filter);

		// 레벨 측정기 (0.5 초 최대값 유지)
		float peaks[kGroupChannels] = {}, rms[kGroupChannels] = {};
		XAUDIO2FX_VOLUMEMETER_LEVELS levels = {};
		levels.pPeakLevels = peaks;
		levels.pRMSLevels = rms;
		levels.ChannelCount = kGroupChannels;
		if (SUCCEEDED(r.Voice->GetEffectParameters(0, &levels, sizeof(levels))))
		{
			const float peak = (std::max)(peaks[0], peaks[1]);
			if (peak >= r.Level || now - r.LevelTime > 0.5)
			{
				r.Level = peak;
				r.LevelTime = now;
			}
		}
	}
}

float AudioMixer::GroupLevelDb(int group) const
{
	if (group < 0 || group >= (int)m_Runtime.size())
		return -80.0f;
	const float l = m_Runtime[group].Level;
	return l > 1e-4f ? 20.0f * log10f(l) : -80.0f;
}

void AudioMixer::UpdateAll(float dt)
{
	if (!AudioManager::IsAvailable())
		return;
	for (auto& [path, mixer] : Registry())
	{
		mixer->Tick(dt);
		if (mixer->m_BuiltStructure != mixer->m_Structure)
			mixer->BuildVoices();
		mixer->ApplyToVoices();
		mixer->DestroyRetired(false);
	}
}

void AudioMixer::ForgetAllVoices()
{
	for (auto& [path, mixer] : Registry())
	{
		mixer->m_Runtime.clear();
		mixer->m_Retired.clear();
		mixer->m_BuiltStructure = 0;
	}
}
