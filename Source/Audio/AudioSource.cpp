#include "pch.h"
#include "AudioSource.h"
#include "AudioClip.h"
#include "AudioManager.h"
#include "AudioMixer.h"
#include "UnityGUI.h"
#include "ObjectPicker.h"
#include <xaudio2.h>

AudioSource::AudioSource()
{
	m_InspectorTitleName = "Audio Source";
}

AudioSource::~AudioSource()
{
	AudioManager::StopStream(m_Stream, m_Voice);
}

void AudioSource::OnDestroy()
{
	AudioManager::StopStream(m_Stream, m_Voice);
	m_Playing = false;
}

void AudioSource::SetClip(const std::string& path)
{
	m_ClipPath = path;
	m_Clip = path.empty() ? nullptr : AudioClip::Load(path);
}

void AudioSource::SetOutput(const std::string& mixerPath, const std::string& group)
{
	m_OutputMixer = mixerPath;
	m_OutputGroup = group;
	m_Mixer = mixerPath.empty() ? nullptr : AudioMixer::Load(mixerPath);
	UpdateRouting();
}

IXAudio2Voice* AudioSource::OutputVoice()
{
	if (m_Mixer == nullptr)
		return nullptr;
	const int g = m_OutputGroup.empty() ? 0 : m_Mixer->FindGroup(m_OutputGroup);
	return g >= 0 ? m_Mixer->GroupVoice(g) : nullptr;
}

// 믹서를 고쳐 그룹 보이스가 새로 만들어졌거나 Output 을 바꿨으면 다시 연결
void AudioSource::UpdateRouting()
{
	if (m_Voice == nullptr)
		return;
	IXAudio2Voice* out = OutputVoice();
	if (out != m_RoutedTo)
	{
		AudioManager::SetOutput(m_Voice, out);
		m_RoutedTo = out;
		UpdateMix();   // 출력 채널 수가 바뀌었을 수 있다
	}
}

// ------------------------------------------------------------------ 재생
void AudioSource::Start()
{
	if (m_PlayOnAwake && m_Clip && IsEnabled())
		Play();
}

void AudioSource::Play()
{
	if (m_Clip == nullptr)
		return;
	// 스트리밍 중이었으면 보이스째 끝낸다 (스트리밍은 Play 마다 새 보이스), 형식이 다른 클립이면 보이스를 다시 만든다
	if (m_Stream || (m_Voice && (m_VoiceClip != m_Clip || m_Clip->Streaming)))
		AudioManager::StopStream(m_Stream, m_Voice);
	if (m_Voice == nullptr)
	{
		m_RoutedTo = OutputVoice();
		m_Voice = AudioManager::CreateVoice(*m_Clip, m_RoutedTo);
		m_VoiceClip = m_Clip;
		if (m_Voice == nullptr)
			return;
	}
	UpdateRouting();
	m_Voice->Stop();
	m_Voice->FlushSourceBuffers();
	XAUDIO2_VOICE_STATE st = {};
	m_Voice->GetState(&st);
	m_SamplesAtStart = st.SamplesPlayed;
	UpdateMix();
	if (m_Clip->Streaming)
		m_Stream = AudioManager::StartStream(m_Voice, m_Clip, m_Loop);
	else
		AudioManager::Submit(m_Voice, *m_Clip, m_Loop);
	m_VoiceLoop = m_Loop;
	m_Voice->Start();
	m_Playing = true;
	m_Paused = false;
	EditorLog::Write("Audio", "AudioSource '%s' play %s (loop %d, volume %.2f, pitch %.2f, blend %.2f)",
		m_pGameObject ? m_pGameObject->GetName().c_str() : "?", m_Clip->Name().c_str(), m_Loop ? 1 : 0, m_Volume, m_Pitch, m_SpatialBlend);
}

void AudioSource::Stop()
{
	if (m_Stream)
		AudioManager::StopStream(m_Stream, m_Voice);
	if (m_Voice)
	{
		m_Voice->Stop();
		m_Voice->FlushSourceBuffers();
	}
	m_Playing = false;
	m_Paused = false;
}

void AudioSource::Pause()
{
	if (m_Voice && m_Playing && !m_Paused)
	{
		m_Voice->Stop();
		m_Paused = true;
	}
}

void AudioSource::UnPause()
{
	if (m_Voice && m_Playing && m_Paused)
	{
		m_Voice->Start();
		m_Paused = false;
	}
}

void AudioSource::PlayOneShot(const std::shared_ptr<AudioClip>& clip, float volumeScale)
{
	if (clip == nullptr || m_Mute)
		return;
	float gain = 1.0f, pan3d = 0.0f;
	if (m_SpatialBlend > 0.0f && m_pGameObject)
		AudioManager::Spatialize(m_pGameObject->GetTransform()->GetPosition(), m_MinDistance, m_MaxDistance, m_Rolloff == 1 ? 1 : 0, gain, pan3d);
	pan3d *= 1.0f - m_Spread / 180.0f;
	const float vol = m_Volume * volumeScale * (1.0f + (gain - 1.0f) * m_SpatialBlend);
	const float pan = m_StereoPan + (pan3d - m_StereoPan) * m_SpatialBlend;
	AudioManager::PlayOneShot(clip, vol, pan, m_Pitch, OutputVoice());
}

bool AudioSource::IsPlaying()
{
	if (!m_Playing || m_Voice == nullptr)
		return false;
	if (m_Paused)
		return false;
	if (m_Stream)
	{
		if (AudioManager::IsStreamFinished(m_Stream))
			m_Playing = false;
		return m_Playing;
	}
	XAUDIO2_VOICE_STATE st = {};
	m_Voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
	if (st.BuffersQueued == 0)
		m_Playing = false;   // 끝까지 재생됨
	return m_Playing;
}

uint64_t AudioSource::GetSamplesPlayed()
{
	if (m_Voice == nullptr)
		return 0;
	XAUDIO2_VOICE_STATE st = {};
	m_Voice->GetState(&st);
	return st.SamplesPlayed - m_SamplesAtStart;
}

float AudioSource::GetTime()
{
	if (m_VoiceClip == nullptr || m_VoiceClip->Frequency <= 0 || m_VoiceClip->Samples == 0)
		return 0.0f;
	const uint64_t s = GetSamplesPlayed();
	const uint64_t pos = m_VoiceLoop ? s % m_VoiceClip->Samples : (std::min<uint64_t>)(s, m_VoiceClip->Samples);
	return (float)pos / (float)m_VoiceClip->Frequency;
}

void AudioSource::UpdateMix()
{
	if (m_Voice == nullptr || m_VoiceClip == nullptr)
		return;
	float gain = 1.0f, pan3d = 0.0f, doppler = 1.0f;
	if (m_SpatialBlend > 0.0f && m_pGameObject)
	{
		const Vec3 pos = m_pGameObject->GetTransform()->GetPosition();
		AudioManager::Spatialize(pos, m_MinDistance, m_MaxDistance, m_Rolloff == 1 ? 1 : 0, gain, pan3d);
		doppler = AudioManager::DopplerFactor(pos, m_Velocity, m_DopplerLevel);
	}
	// Spread: 0 = 한 점, 180 = 양쪽 같게, 360 = 좌우 반대 (Unity 와 같은 방향)
	pan3d *= 1.0f - m_Spread / 180.0f;
	// Spatial Blend: 0 = 2D(Stereo Pan 만), 1 = 3D(거리 감쇠 + 방향 + 도플러)
	const float vol = m_Mute ? 0.0f : m_Volume * (1.0f + (gain - 1.0f) * m_SpatialBlend);
	const float pan = m_StereoPan + (pan3d - m_StereoPan) * m_SpatialBlend;
	AudioManager::ApplyMix(m_Voice, m_VoiceClip->Channels, vol, pan, m_RoutedTo ? 2 : 0);
	AudioManager::SetPitch(m_Voice, m_Pitch * (1.0f + (doppler - 1.0f) * m_SpatialBlend));
}

void AudioSource::Update()
{
	// 도플러용 속도 (순간 이동은 무시)
	if (m_pGameObject)
	{
		const Vec3 pos = m_pGameObject->GetTransform()->GetPosition();
		const float dt = (float)DT;
		if (m_HavePosition && dt > 1e-5f)
		{
			const Vec3 v = (pos - m_LastPosition) / dt;
			m_Velocity = v.Length() > 100.0f ? Vec3::Zero : v;
		}
		m_LastPosition = pos;
		m_HavePosition = true;
	}
	UpdateRouting();
	if (m_Voice == nullptr || !m_Playing)
		return;
	if (!IsEnabled())
	{
		Stop();   // Unity: 컴포넌트를 끄면 멈춘다
		return;
	}
	if (m_Stream)
	{
		AudioManager::SetStreamLoop(m_Stream, m_Loop);   // 스트리밍은 끝에서 처음으로 되감을지만 바꾼다
		m_VoiceLoop = m_Loop;
	}
	else if (m_VoiceLoop && !m_Loop)
	{
		m_Voice->ExitLoop();   // 재생 중 Loop 해제 = 이번 반복까지만
		m_VoiceLoop = false;
	}
	UpdateMix();
}

// ------------------------------------------------------------------ Inspector
void AudioSource::OnInspectorGUI()
{
	// Audio Generator (클립): ⊙ = Object Picker 창, Project 창에서 .wav 끌어 놓기
	{
		const std::string text = m_Clip ? m_Clip->Name() + " (Audio Clip)" : std::string("None (I Audio Generator)");
		ImVec2 fmin, fmax;
		const int pressed = UnityGUI::ObjectFieldButtons("Audio Generator", text.c_str(), "audio_clip", nullptr, 0, &fmin, &fmax);
		const ImVec2 after = ImGui::GetCursorScreenPos();
		ImGui::PushID("clipField");
		// ⊙ = Object Picker 창 (Select AudioClip)
		const std::string pickerKey = "clip:" + std::to_string((uintptr_t)this);
		if (pressed == -1)
		{
			ObjectPicker::Options opt;
			opt.TypeName = "AudioClip";
			opt.Icon = "audio_clip";
			opt.Items = AudioClip::FindAll();
			opt.Current = m_ClipPath;
			opt.Describe = [](const std::string& path) {
				auto c = AudioClip::Load(path);
				if (!c) return std::string("(cannot load)");
				char b[128];
				snprintf(b, sizeof(b), "%s, %d Hz, %.2f s%s", c->Channels == 1 ? "Mono" : "Stereo", c->Frequency, c->Length, c->Streaming ? ", streaming" : "");
				return std::string(b);
			};
			opt.Preview = [](const std::string& path) { AudioManager::PlayPreview(AudioClip::Load(path)); };
			ObjectPicker::Open(pickerKey, std::move(opt));
		}
		std::string picked;
		if (ObjectPicker::Poll(pickerKey, picked))
			SetClip(picked);
		ImGui::SetCursorScreenPos(fmin);
		ImGui::InvisibleButton("##drop", ImVec2((std::max)(1.0f, fmax.x - fmin.x - 22.0f), fmax.y - fmin.y));
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
			{
				const std::string dropped(static_cast<const char*>(payload->Data));
				if (AudioClip::IsAudioPath(dropped))
					SetClip(dropped);
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::IsItemClicked() && m_Clip)
			SelectionManager::SetSelectedFile(PathManager::GetI()->GetMovePathW(string_to_wstring(m_Clip->Path)));
		ImGui::SetCursorScreenPos(after);
		ImGui::PopID();
	}
	// Output: Audio Mixer 그룹 (프로젝트의 .mixer 들)
	{
		std::string text = "None (Audio Mixer Group)";
		if (m_Mixer)
			text = (m_OutputGroup.empty() ? std::string("Master") : m_OutputGroup) + " (" + m_Mixer->Name() + ")";
		if (UnityGUI::ObjectField("Output", text.c_str(), 0, m_Mixer ? "audio_mixer" : nullptr))
			ImGui::OpenPopup("##mixergroup");
		ImGui::SetNextWindowSizeConstraints(ImVec2(260, 0), ImVec2(480, 400));
		if (ImGui::BeginPopup("##mixergroup"))
		{
			if (ImGui::Selectable("None", m_Mixer == nullptr))
				SetOutput("", "");
			for (const std::string& path : AudioMixer::FindAll())
			{
				auto mixer = AudioMixer::Load(path);
				if (mixer == nullptr)
					continue;
				ImGui::SeparatorText(mixer->Name().c_str());
				for (const auto& g : mixer->Groups)
				{
					int depth = 0;
					for (int p = g.Parent; p >= 0; p = mixer->Groups[p].Parent)
						++depth;
					const std::string label = std::string(depth * 2, ' ') + g.Name + "##" + path;
					if (ImGui::Selectable(label.c_str(), m_Mixer == mixer && (m_OutputGroup == g.Name || (m_OutputGroup.empty() && depth == 0))))
						SetOutput(path, g.Name);
				}
			}
			ImGui::EndPopup();
		}
	}
	UnityGUI::Toggle("Mute", &m_Mute);
	UnityGUI::Toggle("Bypass Effects", &m_BypassEffects);
	UnityGUI::Toggle("Bypass Listener Effects", &m_BypassListenerEffects);
	UnityGUI::Toggle("Bypass Reverb Zones", &m_BypassReverbZones);
	UnityGUI::Toggle("Play On Awake", &m_PlayOnAwake);
	UnityGUI::Toggle("Loop", &m_Loop);
	UnityGUI::Spacing(6.0f);

	float priority = (float)m_Priority;
	if (UnityGUI::Slider("Priority", &priority, 0.0f, 256.0f))
		m_Priority = std::clamp((int)(priority + 0.5f), 0, 256);
	UnityGUI::SliderCaptions("High", "Low");
	if (UnityGUI::Slider("Volume", &m_Volume, 0.0f, 1.0f))
		m_Volume = std::clamp(m_Volume, 0.0f, 1.0f);
	UnityGUI::Spacing(4.0f);
	if (UnityGUI::Slider("Pitch", &m_Pitch, -3.0f, 3.0f))
		m_Pitch = std::clamp(m_Pitch, -3.0f, 3.0f);
	UnityGUI::Spacing(4.0f);
	if (UnityGUI::Slider("Stereo Pan", &m_StereoPan, -1.0f, 1.0f))
		m_StereoPan = std::clamp(m_StereoPan, -1.0f, 1.0f);
	UnityGUI::SliderCaptions("Left", "Right");
	if (UnityGUI::Slider("Spatial Blend", &m_SpatialBlend, 0.0f, 1.0f))
		m_SpatialBlend = std::clamp(m_SpatialBlend, 0.0f, 1.0f);
	UnityGUI::SliderCaptions("2D", "3D");
	if (UnityGUI::Slider("Reverb Zone Mix", &m_ReverbZoneMix, 0.0f, 1.1f))
		m_ReverbZoneMix = std::clamp(m_ReverbZoneMix, 0.0f, 1.1f);

	if (UnityGUI::FoldoutPlain("3D Sound Settings", 0, false))
	{
		static const char* kRolloff[] = { "Logarithmic Rolloff", "Linear Rolloff", "Custom Rolloff" };
		UnityGUI::Slider("Doppler Level", &m_DopplerLevel, 0.0f, 5.0f, 1);
		UnityGUI::Slider("Spread", &m_Spread, 0.0f, 360.0f, 1);
		UnityGUI::Dropdown("Volume Rolloff", &m_Rolloff, kRolloff, 3, 1);
		if (UnityGUI::Float("Min Distance", &m_MinDistance, 1))
			m_MinDistance = (std::max)(0.01f, m_MinDistance);
		if (UnityGUI::Float("Max Distance", &m_MaxDistance, 1))
			m_MaxDistance = (std::max)(m_MinDistance + 0.01f, m_MaxDistance);
	}

	// 재생 중이면 진행 표시 (Unity 에는 없지만 확인용)
	if (Application::IsPlaying() && m_Clip)
	{
		char buf[96];
		snprintf(buf, sizeof(buf), "%s  %.2f / %.2fs", IsPlaying() ? "Playing" : "Stopped", GetTime(), m_Clip->Length);
		UnityGUI::ValueLabel("State", buf);
	}
}

// ------------------------------------------------------------------ 저장
GENERATE_COMPONENT_FUNC_TOJSON(AudioSource)
{
	json j;
	SERIALIZE_TYPE(j, AudioSource);
	j["enabled"] = m_Enabled;
	j["clip"] = m_ClipPath;
	j["outputMixer"] = m_OutputMixer;
	j["outputGroup"] = m_OutputGroup;
	j["mute"] = m_Mute;
	j["bypassEffects"] = m_BypassEffects;
	j["bypassListenerEffects"] = m_BypassListenerEffects;
	j["bypassReverbZones"] = m_BypassReverbZones;
	j["playOnAwake"] = m_PlayOnAwake;
	j["loop"] = m_Loop;
	j["priority"] = m_Priority;
	j["volume"] = m_Volume;
	j["pitch"] = m_Pitch;
	j["stereoPan"] = m_StereoPan;
	j["spatialBlend"] = m_SpatialBlend;
	j["reverbZoneMix"] = m_ReverbZoneMix;
	j["dopplerLevel"] = m_DopplerLevel;
	j["spread"] = m_Spread;
	j["rolloffMode"] = m_Rolloff;
	j["minDistance"] = m_MinDistance;
	j["maxDistance"] = m_MaxDistance;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(AudioSource)
{
	m_Enabled = j.value("enabled", true);
	m_Mute = j.value("mute", false);
	m_BypassEffects = j.value("bypassEffects", false);
	m_BypassListenerEffects = j.value("bypassListenerEffects", false);
	m_BypassReverbZones = j.value("bypassReverbZones", false);
	m_PlayOnAwake = j.value("playOnAwake", true);
	m_Loop = j.value("loop", false);
	m_Priority = j.value("priority", 128);
	m_Volume = j.value("volume", 1.0f);
	m_Pitch = j.value("pitch", 1.0f);
	m_StereoPan = j.value("stereoPan", 0.0f);
	m_SpatialBlend = j.value("spatialBlend", 0.0f);
	m_ReverbZoneMix = j.value("reverbZoneMix", 1.0f);
	m_DopplerLevel = j.value("dopplerLevel", 1.0f);
	m_Spread = j.value("spread", 0.0f);
	m_Rolloff = j.value("rolloffMode", 0);
	m_MinDistance = j.value("minDistance", 1.0f);
	m_MaxDistance = j.value("maxDistance", 500.0f);
	SetClip(j.value("clip", std::string()));
	SetOutput(j.value("outputMixer", std::string()), j.value("outputGroup", std::string()));
}
