#pragma once
#include "Component.h"

struct IXAudio2SourceVoice;
struct IXAudio2Voice;
class AudioClip;
class AudioMixer;
struct AudioStream;

// Unity 의 Audio Source.
//  - Play On Awake / Loop / Mute / Volume / Pitch / Stereo Pan / Spatial Blend(2D↔3D) / 3D Sound Settings(거리 감쇠, 도플러, Spread)
//  - Output: Audio Mixer 그룹으로 보낸다 (없으면 바로 마스터)
//  - Bypass Effects·Listener Effects·Reverb Zones, Priority, Reverb Zone Mix 는 값만 저장한다
//  - 스크립트 API: Play, Stop, Pause, UnPause, PlayOneShot, IsPlaying, GetTime
class NOVA_API AudioSource : public Component
{
private:
	std::string m_ClipPath;
	std::shared_ptr<AudioClip> m_Clip;
	std::string m_OutputMixer;     // Assets\... .mixer
	std::string m_OutputGroup;     // 그룹 이름
	std::shared_ptr<AudioMixer> m_Mixer;

	bool m_Mute = false;
	bool m_BypassEffects = false;
	bool m_BypassListenerEffects = false;
	bool m_BypassReverbZones = false;
	bool m_PlayOnAwake = true;
	bool m_Loop = false;
	int m_Priority = 128;
	float m_Volume = 1.0f;
	float m_Pitch = 1.0f;
	float m_StereoPan = 0.0f;
	float m_SpatialBlend = 0.0f;
	float m_ReverbZoneMix = 1.0f;
	// 3D Sound Settings
	float m_DopplerLevel = 1.0f;
	float m_Spread = 0.0f;
	int m_Rolloff = 0;            // 0 Logarithmic, 1 Linear, 2 Custom(=Logarithmic)
	float m_MinDistance = 1.0f;
	float m_MaxDistance = 500.0f;

	// 재생 상태
	IXAudio2SourceVoice* m_Voice = nullptr;
	AudioStream* m_Stream = nullptr;          // 스트리밍 클립 재생 중 (긴 OGG / MP3)
	std::shared_ptr<AudioClip> m_VoiceClip;   // 보이스를 만든 클립 (형식이 다르면 다시 만든다)
	bool m_Playing = false;
	bool m_Paused = false;
	bool m_VoiceLoop = false;
	uint64_t m_SamplesAtStart = 0;
	IXAudio2Voice* m_RoutedTo = nullptr;   // 지금 보내는 그룹 보이스 (nullptr = 마스터)
	Vec3 m_LastPosition = Vec3::Zero;
	Vec3 m_Velocity = Vec3::Zero;          // 도플러용
	bool m_HavePosition = false;

public:
	AudioSource();
	virtual ~AudioSource();

	virtual void Start() override;
	virtual void Update() override;
	virtual void OnDestroy() override;
	virtual void OnHierarchyActiveChanged(bool active) override;   // Unity: 오브젝트를 끄면 멈추고, 다시 켜면 Play On Awake

	// ---- Unity API ----
	void Play();
	void Stop();
	void Pause();
	void UnPause();
	void PlayOneShot(const std::shared_ptr<AudioClip>& clip, float volumeScale = 1.0f);
	bool IsPlaying();
	float GetTime();                  // 현재 재생 위치(초)
	uint64_t GetSamplesPlayed();      // (검사용) 이번 Play 이후 재생된 샘플 수

	void SetClip(const std::string& path);
	std::shared_ptr<AudioClip> GetClip() const { return m_Clip; }
	const std::string& GetClipPath() const { return m_ClipPath; }
	void SetLoop(bool loop) { m_Loop = loop; }
	bool GetLoop() const { return m_Loop; }
	void SetVolume(float v) { m_Volume = std::clamp(v, 0.0f, 1.0f); }
	float GetVolume() const { return m_Volume; }
	void SetPitch(float p) { m_Pitch = std::clamp(p, -3.0f, 3.0f); }
	float GetPitch() const { return m_Pitch; }
	void SetMute(bool m) { m_Mute = m; }
	bool GetMute() const { return m_Mute; }
	void SetPlayOnAwake(bool b) { m_PlayOnAwake = b; }
	bool GetPlayOnAwake() const { return m_PlayOnAwake; }
	void SetSpatialBlend(float b) { m_SpatialBlend = std::clamp(b, 0.0f, 1.0f); }
	float GetSpatialBlend() const { return m_SpatialBlend; }
	void SetStereoPan(float p) { m_StereoPan = std::clamp(p, -1.0f, 1.0f); }
	float GetStereoPan() const { return m_StereoPan; }
	void SetDistances(float minD, float maxD) { m_MinDistance = (std::max)(0.01f, minD); m_MaxDistance = (std::max)(m_MinDistance + 0.01f, maxD); }
	float GetMinDistance() const { return m_MinDistance; }
	float GetMaxDistance() const { return m_MaxDistance; }
	void SetDopplerLevel(float v) { m_DopplerLevel = std::clamp(v, 0.0f, 5.0f); }
	float GetDopplerLevel() const { return m_DopplerLevel; }
	void SetSpread(float v) { m_Spread = std::clamp(v, 0.0f, 360.0f); }
	float GetSpread() const { return m_Spread; }
	// Unity 의 outputAudioMixerGroup (빈 경로 = 마스터로 바로)
	void SetOutput(const std::string& mixerPath, const std::string& group);
	const std::string& GetOutputMixer() const { return m_OutputMixer; }
	const std::string& GetOutputGroup() const { return m_OutputGroup; }

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "audio_source"; }

private:
	void UpdateMix();
	IXAudio2Voice* OutputVoice();
	void UpdateRouting();

	GENERATE_COMPONENT_BODY(AudioSource)
};

REGISTER_COMPONENT(AudioSource)
