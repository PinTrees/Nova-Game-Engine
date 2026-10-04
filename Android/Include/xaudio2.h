#pragma once
// 안드로이드: XAudio2 의 선언만 (구현 없음). XAudio2Create 가 E_NOTIMPL → AudioManager::Init 이 실패 → 소리 없이 조용히 돈다.
//  나중의 안드로이드 소리 (AAudio) 는 AudioManager 쪽에서 따로
#include "WinCompat.h"
#include "mmreg.h"
#define XAUDIO2_DEFAULT_PROCESSOR 0x1
#define XAUDIO2_END_OF_STREAM 0x40
#define XAUDIO2_LOOP_INFINITE 255
#define XAUDIO2_VOICE_NOSAMPLESPLAYED 0x100
#define XAUDIO2_VOICE_USEFILTER 0x8
#define XAUDIO2_MAX_FILTER_FREQUENCY 1.0f
#define XAUDIO2_MIN_FREQ_RATIO (1 / 1024.0f)
#define XAUDIO2_COMMIT_NOW 0
#define XAUDIO2_DEFAULT_CHANNELS 0
#define XAUDIO2_DEFAULT_SAMPLERATE 0
#define XAUDIO2_DEFAULT_FREQ_RATIO 2.0f
#define XAUDIO2_MAX_LOOP_COUNT 254
#define XAUDIO2_MAX_QUEUED_BUFFERS 64
#define XAUDIO2_HELPER_FUNCTIONS 1
#define XAUDIO2FX_REVERB_MIN_WET_DRY_MIX 0.0f
#define XAUDIO2FX_REVERB_MAX_WET_DRY_MIX 100.0f
#define XAUDIO2FX_I3DL2_PRESET_DEFAULT { 100, -10000, 0, 0.0f, 1.00f, 0.50f, -10000, 0.020f, -10000, 0.040f, 100.0f, 100.0f, 5000.0f }
#define XAUDIO2FX_I3DL2_PRESET_ALLEY XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_ARENA XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_AUDITORIUM XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_BATHROOM XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_CAVE XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_CONCERTHALL XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_FOREST XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_HALLWAY XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_HANGAR XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_LARGEHALL XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_LARGEROOM XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_LIVINGROOM XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_MEDIUMROOM XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_PLATE XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_ROOM XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_SMALLROOM XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_STONECORRIDOR XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_STONEROOM XAUDIO2FX_I3DL2_PRESET_DEFAULT
#define XAUDIO2FX_I3DL2_PRESET_UNDERWATER XAUDIO2FX_I3DL2_PRESET_DEFAULT

enum XAUDIO2_FILTER_TYPE { LowPassFilter, BandPassFilter, HighPassFilter, NotchFilter, LowPassOnePoleFilter, HighPassOnePoleFilter };
struct XAUDIO2_FILTER_PARAMETERS { XAUDIO2_FILTER_TYPE Type; float Frequency; float OneOverQ; };
struct XAUDIO2_BUFFER { UINT32 Flags; UINT32 AudioBytes; const BYTE* pAudioData; UINT32 PlayBegin; UINT32 PlayLength; UINT32 LoopBegin; UINT32 LoopLength; UINT32 LoopCount; void* pContext; };
struct XAUDIO2_VOICE_STATE { void* pCurrentBufferContext; UINT32 BuffersQueued; UINT64 SamplesPlayed; };
struct XAUDIO2_VOICE_DETAILS { UINT32 CreationFlags; UINT32 ActiveFlags; UINT32 InputChannels; UINT32 InputSampleRate; };
struct XAUDIO2_PERFORMANCE_DATA
{
	UINT64 AudioCyclesSinceLastQuery, TotalCyclesSinceLastQuery; UINT32 MinimumCyclesPerQuantum, MaximumCyclesPerQuantum, MemoryUsageInBytes, CurrentLatencyInSamples,
		GlitchesSinceEngineStarted, ActiveSourceVoiceCount, TotalSourceVoiceCount, ActiveSubmixVoiceCount, ActiveResamplerCount, ActiveMatrixMixCount,
		ActiveXmaSourceVoices, ActiveXmaStreams;
};
struct IXAudio2Voice;
struct XAUDIO2_SEND_DESCRIPTOR { UINT32 Flags; IXAudio2Voice* pOutputVoice; };
struct XAUDIO2_VOICE_SENDS { UINT32 SendCount; XAUDIO2_SEND_DESCRIPTOR* pSends; };
struct XAUDIO2_EFFECT_DESCRIPTOR { IUnknown* pEffect; BOOL InitialState; UINT32 OutputChannels; };
struct XAUDIO2_EFFECT_CHAIN { UINT32 EffectCount; XAUDIO2_EFFECT_DESCRIPTOR* pEffectDescriptors; };
struct XAUDIO2FX_VOLUMEMETER_LEVELS { float* pPeakLevels; float* pRMSLevels; UINT32 ChannelCount; };
struct XAUDIO2FX_REVERB_PARAMETERS
{
	float WetDryMix; UINT32 ReflectionsDelay; BYTE ReverbDelay, RearDelay, SideDelay, PositionLeft, PositionRight, PositionMatrixLeft, PositionMatrixRight,
		EarlyDiffusion, LateDiffusion, LowEQGain, LowEQCutoff, HighEQGain, HighEQCutoff;
	float RoomFilterFreq, RoomFilterMain, RoomFilterHF, ReflectionsGain, ReverbGain, DecayTime, Density, RoomSize;
	BOOL DisableLateField;
};
struct XAUDIO2FX_REVERB_I3DL2_PARAMETERS
{
	float WetDryMix; int Room, RoomHF; float RoomRolloffFactor, DecayTime, DecayHFRatio; int Reflections; float ReflectionsDelay; int Reverb;
	float ReverbDelay, Diffusion, Density, HFReference;
};
inline void ReverbConvertI3DL2ToNative(const XAUDIO2FX_REVERB_I3DL2_PARAMETERS*, XAUDIO2FX_REVERB_PARAMETERS* native, BOOL = FALSE) { *native = {}; }
inline float XAudio2CutoffFrequencyToRadians(float cutoff, UINT32 sampleRate) { return sampleRate ? 2.0f * sinf(3.14159265f * cutoff / sampleRate) : 1.0f; }

struct IXAudio2Voice
{
	virtual void GetVoiceDetails(XAUDIO2_VOICE_DETAILS* details) = 0;
	virtual HRESULT SetOutputVoices(const XAUDIO2_VOICE_SENDS* sends) = 0;
	virtual HRESULT SetEffectChain(const XAUDIO2_EFFECT_CHAIN* chain) = 0;
	virtual HRESULT SetEffectParameters(UINT32 index, const void* params, UINT32 bytes, UINT32 op = 0) = 0;
	virtual HRESULT GetEffectParameters(UINT32 index, void* params, UINT32 bytes) = 0;
	virtual HRESULT SetFilterParameters(const XAUDIO2_FILTER_PARAMETERS* params, UINT32 op = 0) = 0;
	virtual HRESULT SetVolume(float volume, UINT32 op = 0) = 0;
	virtual HRESULT SetOutputMatrix(IXAudio2Voice* dest, UINT32 src, UINT32 dst, const float* matrix, UINT32 op = 0) = 0;
	virtual void DestroyVoice() = 0;
	virtual HRESULT EnableEffect(UINT32 index, UINT32 op = 0) = 0;
	virtual HRESULT DisableEffect(UINT32 index, UINT32 op = 0) = 0;
};
struct IXAudio2SourceVoice : IXAudio2Voice
{
	virtual HRESULT Start(UINT32 flags = 0, UINT32 op = 0) = 0;
	virtual HRESULT Stop(UINT32 flags = 0, UINT32 op = 0) = 0;
	virtual HRESULT SubmitSourceBuffer(const XAUDIO2_BUFFER* buffer, const void* wma = nullptr) = 0;
	virtual HRESULT FlushSourceBuffers() = 0;
	virtual void GetState(XAUDIO2_VOICE_STATE* state, UINT32 flags = 0) = 0;
	virtual HRESULT SetFrequencyRatio(float ratio, UINT32 op = 0) = 0;
	virtual HRESULT ExitLoop(UINT32 op = 0) = 0;
	virtual HRESULT Discontinuity() = 0;
};
struct IXAudio2SubmixVoice : IXAudio2Voice {};
struct IXAudio2MasteringVoice : IXAudio2Voice {};
struct IXAudio2VoiceCallback {};
struct IXAudio2 : IUnknown
{
	virtual HRESULT CreateSourceVoice(IXAudio2SourceVoice** voice, const WAVEFORMATEX* format, UINT32 flags = 0, float maxRatio = 2.0f,
		IXAudio2VoiceCallback* callback = nullptr, const XAUDIO2_VOICE_SENDS* sends = nullptr, const XAUDIO2_EFFECT_CHAIN* chain = nullptr) = 0;
	virtual HRESULT CreateSubmixVoice(IXAudio2SubmixVoice** voice, UINT32 channels, UINT32 rate, UINT32 flags = 0, UINT32 stage = 0,
		const XAUDIO2_VOICE_SENDS* sends = nullptr, const XAUDIO2_EFFECT_CHAIN* chain = nullptr) = 0;
	virtual HRESULT CreateMasteringVoice(IXAudio2MasteringVoice** voice, UINT32 channels = 0, UINT32 rate = 0, UINT32 flags = 0,
		const wchar_t* device = nullptr, const XAUDIO2_EFFECT_CHAIN* chain = nullptr, int category = 0) = 0;
	virtual HRESULT StartEngine() = 0;
	virtual void StopEngine() = 0;
	virtual void GetPerformanceData(XAUDIO2_PERFORMANCE_DATA* data) = 0;
};
inline HRESULT XAudio2Create(IXAudio2** out, UINT32 = 0, UINT32 = XAUDIO2_DEFAULT_PROCESSOR) { *out = nullptr; return E_NOTIMPL; }
inline HRESULT XAudio2CreateReverb(IUnknown** out, UINT32 = 0) { *out = nullptr; return E_NOTIMPL; }
inline HRESULT XAudio2CreateVolumeMeter(IUnknown** out, UINT32 = 0) { *out = nullptr; return E_NOTIMPL; }
