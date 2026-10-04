#pragma once
// 안드로이드: 엔진이 쓰는 XAudio2 의 선언. 구현 = Android/Source/Engine/XAudio2Android.cpp (소프트웨어 믹서 + AAudio 출력)
#include "WinCompat.h"
#include "mmreg.h"
#include <algorithm>
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
// I3DL2 프리셋 (Windows SDK 의 xaudio2fx.h 와 같은 값 — 엔진 AudioMixer 의 Reverb Preset)
#define XAUDIO2FX_I3DL2_PRESET_DEFAULT { 100, -10000, 0, 0.0f, 1.00f, 0.50f, -10000, 0.020f, -10000, 0.040f, 100.0f, 100.0f, 5000.0f }
#define XAUDIO2FX_I3DL2_PRESET_ALLEY {100, -1000, -270,0.0f, 1.49f,0.86f, -1204,0.007f,    -4,0.011f,100.0f,100.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_ARENA {100, -1000, -698,0.0f, 7.24f,0.33f, -1166,0.020f,    16,0.030f,100.0f,100.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_AUDITORIUM {100, -1000, -476,0.0f, 4.32f,0.59f,  -789,0.020f,  -289,0.030f,100.0f,100.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_BATHROOM {100, -1000,-1200,0.0f, 1.49f,0.54f,  -370,0.007f,  1030,0.011f,100.0f, 60.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_CAVE {100, -1000,    0,0.0f, 2.91f,1.30f,  -602,0.015f,  -302,0.022f,100.0f,100.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_CONCERTHALL {100, -1000, -500,0.0f, 3.92f,0.70f, -1230,0.020f,    -2,0.029f,100.0f,100.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_FOREST {100, -1000,-3300,0.0f, 1.49f,0.54f, -2560,0.162f,  -613,0.088f, 79.0f,100.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_HALLWAY {100, -1000, -300,0.0f, 1.49f,0.59f, -1219,0.007f,   441,0.011f,100.0f,100.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_HANGAR {100, -1000,-1000,0.0f,10.05f,0.23f,  -602,0.020f,   198,0.030f,100.0f,100.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_LARGEHALL {100, -1000, -600,0.0f, 1.80f,0.70f, -2000,0.030f, -1400,0.060f,100.0f,100.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_LARGEROOM {100, -1000, -600,0.0f, 1.50f,0.83f, -1600,0.020f, -1000,0.040f,100.0f,100.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_LIVINGROOM {100, -1000,-6000,0.0f, 0.50f,0.10f, -1376,0.003f, -1104,0.004f,100.0f,100.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_MEDIUMROOM {100, -1000, -600,0.0f, 1.30f,0.83f, -1000,0.010f,  -200,0.020f,100.0f,100.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_PLATE {100, -1000, -200,0.0f, 1.30f,0.90f,     0,0.002f,     0,0.010f,100.0f, 75.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_ROOM {100, -1000, -454,0.0f, 0.40f,0.83f, -1646,0.002f,    53,0.003f,100.0f,100.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_SMALLROOM {100, -1000, -600,0.0f, 1.10f,0.83f,  -400,0.005f,   500,0.010f,100.0f,100.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_STONECORRIDOR {100, -1000, -237,0.0f, 2.70f,0.79f, -1214,0.013f,   395,0.020f,100.0f,100.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_STONEROOM {100, -1000, -300,0.0f, 2.31f,0.64f,  -711,0.012f,    83,0.017f,100.0f,100.0f,5000.0f}
#define XAUDIO2FX_I3DL2_PRESET_UNDERWATER {100, -1000,-4000,0.0f, 1.49f,0.10f,  -449,0.007f,  1700,0.011f,100.0f,100.0f,5000.0f}

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
// I3DL2 → 리버브 값 (안드로이드 리버브 AudioReverb.h 가 쓰는 것: 지연 · 이득 (mB → dB) · 감쇠 · 고역 감쇠 비 · 밀도 · 확산 · 방 필터)
//  DecayHFRatio 는 HighEQGain 에 ×4 로 담는다 (0..8 = 0..2 — 이 안드로이드 판끼리만 쓰는 약속)
inline void ReverbConvertI3DL2ToNative(const XAUDIO2FX_REVERB_I3DL2_PARAMETERS* i3dl2, XAUDIO2FX_REVERB_PARAMETERS* native, BOOL = FALSE)
{
	*native = {};
	native->WetDryMix = i3dl2->WetDryMix;
	native->ReflectionsDelay = (UINT32)(i3dl2->ReflectionsDelay * 1000.0f + 0.5f);
	native->ReverbDelay = (BYTE)(std::min)(85.0f, i3dl2->ReverbDelay * 1000.0f + 0.5f);
	native->ReflectionsGain = i3dl2->Reflections / 100.0f;
	native->ReverbGain = i3dl2->Reverb / 100.0f;
	native->RoomFilterMain = i3dl2->Room / 100.0f;
	native->RoomFilterHF = i3dl2->RoomHF / 100.0f;
	native->RoomFilterFreq = i3dl2->HFReference;
	native->DecayTime = i3dl2->DecayTime;
	native->HighEQGain = (BYTE)(std::min)(8.0f, (std::max)(0.0f, i3dl2->DecayHFRatio * 4.0f + 0.5f));
	native->Density = i3dl2->Density;
	native->LateDiffusion = (BYTE)(std::min)(15.0f, i3dl2->Diffusion * 0.15f + 0.5f);
	native->RoomSize = 100.0f;
}
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
HRESULT XAudio2Create(IXAudio2** out, UINT32 flags = 0, UINT32 processor = XAUDIO2_DEFAULT_PROCESSOR);
HRESULT XAudio2CreateReverb(IUnknown** out, UINT32 flags = 0);
HRESULT XAudio2CreateVolumeMeter(IUnknown** out, UINT32 flags = 0);
