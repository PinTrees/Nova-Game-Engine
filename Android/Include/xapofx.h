#pragma once
// 안드로이드: XAPO 효과의 선언. CreateFX 는 FXEcho 만 (XAudio2Android.cpp), 나머지는 E_NOTIMPL
#include "xaudio2.h"
#define FXECHO_MIN_WETDRYMIX 0.0f
#define FXECHO_MAX_WETDRYMIX 1.0f
#define FXECHO_DEFAULT_WETDRYMIX 0.5f
#define FXECHO_MIN_FEEDBACK 0.0f
#define FXECHO_MAX_FEEDBACK 1.0f
#define FXECHO_DEFAULT_FEEDBACK 0.5f
#define FXECHO_MIN_DELAY 1.0f
#define FXECHO_MAX_DELAY 2000.0f
#define FXECHO_DEFAULT_DELAY 500.0f
#define FXEQ_MIN_FRAMERATE 22000
#define FXEQ_MAX_FRAMERATE 48000
#define FXEQ_MIN_FREQUENCY_CENTER 20.0f
#define FXEQ_MAX_FREQUENCY_CENTER 20000.0f
#define FXEQ_MIN_GAIN 0.126f
#define FXEQ_MAX_GAIN 7.94f
#define FXEQ_MIN_BANDWIDTH 0.1f
#define FXEQ_MAX_BANDWIDTH 2.0f
struct FXECHO_INITDATA { float MaxDelay; };
struct FXECHO_PARAMETERS { float WetDryMix; float Feedback; float Delay; };
struct FXEQ_PARAMETERS
{
	float FrequencyCenter0, Gain0, Bandwidth0, FrequencyCenter1, Gain1, Bandwidth1, FrequencyCenter2, Gain2, Bandwidth2, FrequencyCenter3, Gain3, Bandwidth3;
};
struct FXMASTERINGLIMITER_PARAMETERS { UINT32 Release; UINT32 Loudness; };
struct FXREVERB_PARAMETERS { float Diffusion; float RoomSize; };
struct FXEcho {};
struct FXEQ {};
struct FXMasteringLimiter {};
struct FXReverb {};
HRESULT CreateFX(REFGUID clsid, IUnknown** out, const void* init = nullptr, UINT32 initBytes = 0);
