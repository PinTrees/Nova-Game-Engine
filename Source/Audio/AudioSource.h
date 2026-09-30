#pragma once
#include "Component.h"

struct IXAudio2SourceVoice;
class AudioClip;

// Unity 의 Audio Source.
//  - Play On Awake / Loop / Mute / Volume / Pitch / Stereo Pan / Spatial Blend(2D↔3D) / 3D Sound Settings(거리 감쇠)
//  - Bypass Effects·Listener Effects·Reverb Zones, Output(Mixer), Priority, Reverb Zone Mix, Doppler, Spread 는
//    값만 저장한다 (믹서/리버브/도플러 미구현).
//  - 스크립트 API: Play, Stop, Pause, UnPause, PlayOneShot, IsPlaying, GetTime
class AudioSource : public Component
{
private:
	std::string m_ClipPath;
	std::shared_ptr<AudioClip> m_Clip;

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
	std::shared_ptr<AudioClip> m_VoiceClip;   // 보이스를 만든 클립 (형식이 다르면 다시 만든다)
	bool m_Playing = false;
	bool m_Paused = false;
	bool m_VoiceLoop = false;
	uint64_t m_SamplesAtStart = 0;

public:
	AudioSource();
	virtual ~AudioSource();

	virtual void Start() override;
	virtual void Update() override;
	virtual void OnDestroy() override;

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
	void SetDistances(float minD, float maxD) { m_MinDistance = minD; m_MaxDistance = maxD; }

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "audio_source"; }

private:
	void UpdateMix();

	GENERATE_COMPONENT_BODY(AudioSource)
};

REGISTER_COMPONENT(AudioSource)
