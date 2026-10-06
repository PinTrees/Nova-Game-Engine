#include "pch.h"
#include "AndroidEngine.h"
#include "AudioReverb.h"
#include <xaudio2.h>
#include <xaudio2fx.h>
#include <xapofx.h>
#if defined(__ANDROID__)
#include <aaudio/AAudio.h>
#include <android/log.h>
#define NOVA_AUDIO_LOG(...) __android_log_print(ANDROID_LOG_INFO, "NOVA", __VA_ARGS__)
#else
#include <emscripten.h>
#define NOVA_AUDIO_LOG(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#endif
#include <atomic>
#include <chrono>
#include <mutex>

// XAudio2 의 안드로이드 · 웹 판: 소프트웨어 믹서 + 출력 (안드로이드 = AAudio, 웹 = Web Audio 의 ScriptProcessor 가 믹서를 부른다 — 한 스레드). 엔진의 AudioManager · AudioSource · AudioMixer 는 Windows 와 같은 코드를 그대로 쓴다
//  (그래픽의 GfxGLES 처럼 — 엔진이 쓰는 XAudio2 의 부분만).
//  - 소스 보이스: PCM 8 · 16 · 24 · 32 비트 · float, 버퍼 대기열 (PlayBegin/Length · 반복 구간 · LoopCount · END_OF_STREAM), 재생 속도 (SetFrequencyRatio)
//    와 샘플 레이트 차이는 선형 보간, 출력 행렬 (SetOutputMatrix — 팬 · 3D), 보낼 곳 (SetOutputVoices — 믹서 그룹)
//  - 서브믹스 (믹서 그룹): 처리 순서 (ProcessingStage), 필터 (XAudio2 의 상태 변수 필터), 효과 체인 = 레벨 측정기 · 에코 (FXEcho) · 리버브 (AudioReverb.h — I3DL2 프리셋)
//  - 마스터: 2 채널 · AAudio 장치 레이트 (보통 48000), 레벨 측정기
//  - 앱이 뒤로 가면 (NovaAndroid::SetAudioPaused) AAudio 스트림을 멈춘다
namespace
{
	std::mutex s_Lock;   // 믹서 상태 (게임 스레드의 보이스 호출 ↔ AAudio 콜백 스레드)

	// ---- 효과 (XAPO 자리)
	struct Effect : IUnknown
	{
		std::atomic<ULONG> Refs{ 1 };
		HRESULT QueryInterface(REFIID, void** out) override { *out = nullptr; return E_NOINTERFACE; }
		ULONG AddRef() override { return ++Refs; }
		ULONG Release() override { const ULONG r = --Refs; if (r == 0) delete this; return r; }
		virtual ~Effect() = default;
		virtual void Process(float* data, uint32_t frames, uint32_t channels, uint32_t rate) = 0;
		virtual void SetParameters(const void* params, uint32_t bytes) = 0;
		virtual bool GetParameters(void*, uint32_t) { return false; }
	};

	// 레벨 측정기: 마지막으로 물은 뒤의 최대 · RMS
	struct VolumeMeter final : Effect
	{
		float Peak[8] = {}, Sum[8] = {};
		uint32_t Count = 0, Channels = 0;
		void Process(float* data, uint32_t frames, uint32_t channels, uint32_t) override
		{
			Channels = (std::min)(channels, 8u);
			for (uint32_t f = 0; f < frames; ++f)
				for (uint32_t c = 0; c < Channels; ++c)
				{
					const float v = fabsf(data[f * channels + c]);
					Peak[c] = (std::max)(Peak[c], v);
					Sum[c] += v * v;
				}
			Count += frames;
		}
		void SetParameters(const void*, uint32_t) override {}
		bool GetParameters(void* params, uint32_t bytes) override
		{
			if (bytes < sizeof(XAUDIO2FX_VOLUMEMETER_LEVELS)) return false;
			auto* l = static_cast<XAUDIO2FX_VOLUMEMETER_LEVELS*>(params);
			for (uint32_t c = 0; c < l->ChannelCount && c < 8; ++c)
			{
				if (l->pPeakLevels) l->pPeakLevels[c] = Peak[c];
				if (l->pRMSLevels) l->pRMSLevels[c] = Count ? sqrtf(Sum[c] / Count) : 0.0f;
				Peak[c] = Sum[c] = 0.0f;
			}
			Count = 0;
			return true;
		}
	};

	// 에코 (FXEcho): 채널마다 지연선 + 되먹임, WetDryMix 0..1
	struct Echo final : Effect
	{
		FXECHO_PARAMETERS P = { FXECHO_DEFAULT_WETDRYMIX, FXECHO_DEFAULT_FEEDBACK, FXECHO_DEFAULT_DELAY };
		std::vector<float> Line;
		uint32_t Pos = 0, LineFrames = 0, LineChannels = 0;
		void Process(float* data, uint32_t frames, uint32_t channels, uint32_t rate) override
		{
			const uint32_t need = (uint32_t)(rate * FXECHO_MAX_DELAY / 1000.0f) + 1;
			if (LineFrames != need || LineChannels != channels) { Line.assign((size_t)need * channels, 0.0f); LineFrames = need; LineChannels = channels; Pos = 0; }
			const uint32_t delay = std::clamp((uint32_t)(P.Delay * rate / 1000.0f), 1u, LineFrames - 1);
			for (uint32_t f = 0; f < frames; ++f)
			{
				const uint32_t read = (Pos + LineFrames - delay) % LineFrames;
				for (uint32_t c = 0; c < channels; ++c)
				{
					float& x = data[f * channels + c];
					const float d = Line[(size_t)read * channels + c];
					Line[(size_t)Pos * channels + c] = x + d * P.Feedback;
					x = x * (1.0f - P.WetDryMix) + d * P.WetDryMix;
				}
				Pos = (Pos + 1) % LineFrames;
			}
		}
		void SetParameters(const void* params, uint32_t bytes) override { if (bytes >= sizeof(P)) P = *static_cast<const FXECHO_PARAMETERS*>(params); }
	};

	// 리버브: AudioReverb.h (앞 지연 · 초기 반사 · 빗살 8 개 + 전역 통과 4 개 · 방 필터 — I3DL2 프리셋 값을 따른다)
	struct Reverb final : Effect
	{
		NovaReverb R;
		void Process(float* data, uint32_t frames, uint32_t channels, uint32_t rate) override { R.Process(data, frames, channels, rate); }
		void SetParameters(const void* params, uint32_t bytes) override
		{
			if (bytes < sizeof(XAUDIO2FX_REVERB_PARAMETERS)) return;
			const auto* n = static_cast<const XAUDIO2FX_REVERB_PARAMETERS*>(params);
			NovaReverb::Params p;
			p.WetDryMix = n->WetDryMix;
			p.ReflectionsDelayMs = (float)n->ReflectionsDelay;
			p.ReverbDelayMs = (float)n->ReverbDelay;
			p.ReflectionsGainDb = n->ReflectionsGain;
			p.ReverbGainDb = n->ReverbGain;
			p.RoomFilterMainDb = n->RoomFilterMain;
			p.RoomFilterHFDb = n->RoomFilterHF;
			p.RoomFilterFreq = n->RoomFilterFreq > 0.0f ? n->RoomFilterFreq : 5000.0f;
			p.DecayTime = n->DecayTime > 0.0f ? n->DecayTime : 1.0f;
			p.DecayHFRatio = n->HighEQGain / 4.0f;
			p.Density = n->Density;
			p.Diffusion = n->LateDiffusion / 0.15f;
			R.SetParams(p);
		}
	};

	class Engine;

	// ---- 보이스
	struct VoiceBase
	{
		enum Kind { Source, Submix, Master } Type;
		Engine* Owner = nullptr;
		uint32_t Channels = 2, Rate = 48000, Stage = 0;
		float Volume = 1.0f;
		VoiceBase* Dest = nullptr;           // nullptr = 마스터
		std::vector<float> Matrix;           // [출력 채널 * Channels + 입력 채널], 비면 기본
		bool UseFilter = false;
		XAUDIO2_FILTER_PARAMETERS Filter = { LowPassFilter, XAUDIO2_MAX_FILTER_FREQUENCY, 1.0f };
		float Low[8] = {}, Band[8] = {};
		struct Fx { Effect* E; bool On; };
		std::vector<Fx> Effects;
		std::vector<float> Mix;              // 서브믹스 · 마스터의 이번 콜백 입력 (frames * Channels)

		VoiceBase(Kind k) : Type(k) {}
		virtual ~VoiceBase() { for (Fx& f : Effects) f.E->Release(); }

		void SetChain(const XAUDIO2_EFFECT_CHAIN* chain)
		{
			for (Fx& f : Effects) f.E->Release();
			Effects.clear();
			if (!chain) return;
			for (UINT32 i = 0; i < chain->EffectCount; ++i)
			{
				Effect* e = static_cast<Effect*>(chain->pEffectDescriptors[i].pEffect);
				e->AddRef();
				Effects.push_back({ e, chain->pEffectDescriptors[i].InitialState != FALSE });
			}
		}

		// XAudio2 의 상태 변수 필터 (Frequency = 2 sin(pi f / fs), OneOverQ)
		void ApplyFilter(float* data, uint32_t frames)
		{
			if (!UseFilter || (Filter.Type == LowPassFilter && Filter.Frequency >= XAUDIO2_MAX_FILTER_FREQUENCY)) return;
			const float F = Filter.Frequency, Q = Filter.OneOverQ;
			for (uint32_t f = 0; f < frames; ++f)
				for (uint32_t c = 0; c < Channels && c < 8; ++c)
				{
					float& x = data[f * Channels + c];
					Low[c] += F * Band[c];
					const float high = x - Low[c] - Q * Band[c];
					Band[c] += F * high;
					switch (Filter.Type)
					{
					case LowPassFilter: case LowPassOnePoleFilter: x = Low[c]; break;
					case HighPassFilter: case HighPassOnePoleFilter: x = high; break;
					case BandPassFilter: x = Band[c]; break;
					default: x = high + Low[c]; break;   // Notch
					}
				}
		}

		void ApplyEffects(float* data, uint32_t frames, uint32_t rate)
		{
			for (Fx& f : Effects)
				if (f.On) f.E->Process(data, frames, Channels, rate);
		}

		// data (frames * Channels) 를 볼륨 · 행렬로 to (frames * toChannels) 에 더한다
		void MixInto(const float* data, uint32_t frames, float* to, uint32_t toChannels, float gain) const
		{
			const uint32_t in = Channels;
			for (uint32_t o = 0; o < toChannels; ++o)
				for (uint32_t i = 0; i < in; ++i)
				{
					float m;
					if (!Matrix.empty() && Matrix.size() == (size_t)in * toChannels) m = Matrix[(size_t)o * in + i];
					else if (in == 1) m = 1.0f;                     // 모노 → 모든 채널 (XAudio2 기본)
					else if (in == toChannels) m = o == i ? 1.0f : 0.0f;
					else m = (i % toChannels) == o ? 1.0f : 0.0f;
					m *= gain;
					if (m == 0.0f) continue;
					for (uint32_t f = 0; f < frames; ++f)
						to[(size_t)f * toChannels + o] += data[(size_t)f * in + i] * m;
				}
		}
	};

	template <class Base>
	struct VoiceCommon : Base, VoiceBase
	{
		VoiceCommon(Kind k) : VoiceBase(k) {}
		void GetVoiceDetails(XAUDIO2_VOICE_DETAILS* d) override { d->CreationFlags = 0; d->ActiveFlags = 0; d->InputChannels = Channels; d->InputSampleRate = Rate; }
		HRESULT SetOutputVoices(const XAUDIO2_VOICE_SENDS* sends) override;
		HRESULT SetEffectChain(const XAUDIO2_EFFECT_CHAIN* chain) override { std::lock_guard<std::mutex> g(s_Lock); SetChain(chain); return S_OK; }
		HRESULT SetEffectParameters(UINT32 index, const void* params, UINT32 bytes, UINT32) override
		{
			std::lock_guard<std::mutex> g(s_Lock);
			if (index >= Effects.size()) return E_INVALIDARG;
			Effects[index].E->SetParameters(params, bytes);
			return S_OK;
		}
		HRESULT GetEffectParameters(UINT32 index, void* params, UINT32 bytes) override
		{
			std::lock_guard<std::mutex> g(s_Lock);
			return index < Effects.size() && Effects[index].E->GetParameters(params, bytes) ? S_OK : E_FAIL;
		}
		HRESULT SetFilterParameters(const XAUDIO2_FILTER_PARAMETERS* p, UINT32) override
		{
			if (!UseFilter) return E_FAIL;   // XAudio2: XAUDIO2_VOICE_USEFILTER 로 만든 보이스만
			std::lock_guard<std::mutex> g(s_Lock);
			Filter = *p;
			return S_OK;
		}
		HRESULT SetVolume(float volume, UINT32) override { std::lock_guard<std::mutex> g(s_Lock); Volume = volume; return S_OK; }
		HRESULT SetOutputMatrix(IXAudio2Voice*, UINT32 src, UINT32 dst, const float* matrix, UINT32) override
		{
			std::lock_guard<std::mutex> g(s_Lock);
			Matrix.assign(matrix, matrix + (size_t)src * dst);
			return S_OK;
		}
		HRESULT EnableEffect(UINT32 index, UINT32) override { std::lock_guard<std::mutex> g(s_Lock); if (index >= Effects.size()) return E_INVALIDARG; Effects[index].On = true; return S_OK; }
		HRESULT DisableEffect(UINT32 index, UINT32) override { std::lock_guard<std::mutex> g(s_Lock); if (index >= Effects.size()) return E_INVALIDARG; Effects[index].On = false; return S_OK; }
		void DestroyVoice() override;
	};

	struct SourceVoice final : VoiceCommon<IXAudio2SourceVoice>
	{
		WAVEFORMATEX Format = {};
		bool Float = false;
		float Ratio = 1.0f;
		bool Started = false;
		struct Buf { XAUDIO2_BUFFER B; uint32_t Frames; uint32_t Begin, End; uint32_t LoopBegin, LoopEnd; uint32_t LoopsLeft; };
		std::vector<Buf> Queue;
		double Pos = 0.0;                    // 지금 버퍼 안의 프레임 위치
		uint64_t Played = 0;
		std::vector<float> Scratch;

		SourceVoice() : VoiceCommon(Source) {}

		HRESULT Start(UINT32, UINT32) override { std::lock_guard<std::mutex> g(s_Lock); Started = true; return S_OK; }
		HRESULT Stop(UINT32, UINT32) override { std::lock_guard<std::mutex> g(s_Lock); Started = false; return S_OK; }
		HRESULT SubmitSourceBuffer(const XAUDIO2_BUFFER* b, const void*) override
		{
			const uint32_t frameBytes = Format.nBlockAlign ? Format.nBlockAlign : (Format.nChannels * Format.wBitsPerSample / 8);
			if (!b || !b->pAudioData || frameBytes == 0) return E_INVALIDARG;
			Buf q;
			q.B = *b;
			q.Frames = b->AudioBytes / frameBytes;
			q.Begin = (std::min)(b->PlayBegin, q.Frames);
			q.End = b->PlayLength ? (std::min)(q.Frames, b->PlayBegin + b->PlayLength) : q.Frames;
			q.LoopBegin = (std::min)(b->LoopBegin, q.End);
			q.LoopEnd = b->LoopLength ? (std::min)(q.End, b->LoopBegin + b->LoopLength) : q.End;
			q.LoopsLeft = b->LoopCount;
			std::lock_guard<std::mutex> g(s_Lock);
			if (Queue.size() >= XAUDIO2_MAX_QUEUED_BUFFERS) return E_FAIL;
			if (Queue.empty()) Pos = q.Begin;
			Queue.push_back(q);
			return S_OK;
		}
		HRESULT FlushSourceBuffers() override
		{
			std::lock_guard<std::mutex> g(s_Lock);
			// XAudio2: 재생 중인 버퍼는 남기고 (멈춰 있으면 모두) 지운다
			if (Started && !Queue.empty()) Queue.resize(1);
			else { Queue.clear(); Pos = 0.0; }
			return S_OK;
		}
		void GetState(XAUDIO2_VOICE_STATE* st, UINT32) override
		{
			std::lock_guard<std::mutex> g(s_Lock);
			st->BuffersQueued = (UINT32)Queue.size();
			st->pCurrentBufferContext = Queue.empty() ? nullptr : Queue[0].B.pContext;
			st->SamplesPlayed = Played;
		}
		HRESULT SetFrequencyRatio(float ratio, UINT32) override { std::lock_guard<std::mutex> g(s_Lock); Ratio = std::clamp(ratio, XAUDIO2_MIN_FREQ_RATIO, 3.0f); return S_OK; }
		HRESULT ExitLoop(UINT32) override { std::lock_guard<std::mutex> g(s_Lock); if (!Queue.empty()) Queue[0].LoopsLeft = 0; return S_OK; }
		HRESULT Discontinuity() override { return S_OK; }

		float Sample(const Buf& q, uint32_t frame, uint32_t ch) const
		{
			const uint8_t* p = q.B.pAudioData + (size_t)frame * Format.nBlockAlign + (size_t)ch * (Format.wBitsPerSample / 8);
			switch (Format.wBitsPerSample)
			{
			case 8: return ((int)*p - 128) / 128.0f;
			case 16: { int16_t v; memcpy(&v, p, 2); return v / 32768.0f; }
			case 24: { const int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8; return v / 8388608.0f; }
			case 32:
				if (Float) { float v; memcpy(&v, p, 4); return v; }
				{ int32_t v; memcpy(&v, p, 4); return v / 2147483648.0f; }
			default: return 0.0f;
			}
		}

		// 출력 레이트로 frames 만큼 out (frames * Channels) 에 풀어 쓴다 (버퍼가 끝나면 0)
		void Render(float* out, uint32_t frames, uint32_t outRate)
		{
			std::fill(out, out + (size_t)frames * Channels, 0.0f);
			const double step = (double)Ratio * Format.nSamplesPerSec / outRate;
			for (uint32_t f = 0; f < frames; ++f)
			{
				while (!Queue.empty() && Pos >= (Queue[0].LoopsLeft > 0 ? Queue[0].LoopEnd : Queue[0].End) && (Queue[0].LoopsLeft == 0 || Queue[0].LoopEnd <= Queue[0].LoopBegin))
				{
					Queue.erase(Queue.begin());   // 빈 버퍼 (PlayLength 0 등) — 읽지 않고 넘긴다
					Pos = Queue.empty() ? 0.0 : Queue[0].Begin;
				}
				if (Queue.empty()) break;
				Buf& q = Queue[0];
				const uint32_t end = q.LoopsLeft > 0 ? q.LoopEnd : q.End;
				const uint32_t i0 = (uint32_t)Pos;
				const float t = (float)(Pos - i0);
				const uint32_t i1 = i0 + 1 < end ? i0 + 1 : (q.LoopsLeft > 0 ? q.LoopBegin : i0);
				for (uint32_t c = 0; c < Channels; ++c)
				{
					const float a = Sample(q, i0, c), b = Sample(q, i1, c);
					out[(size_t)f * Channels + c] = a + (b - a) * t;
				}
				Pos += step;
				const double before = Pos - step;
				Played += (uint64_t)((uint32_t)Pos - (uint32_t)before);
				while (!Queue.empty() && Pos >= (Queue[0].LoopsLeft > 0 ? Queue[0].LoopEnd : Queue[0].End))
				{
					Buf& cur = Queue[0];
					if (cur.LoopsLeft > 0)
					{
						Pos = cur.LoopBegin + (Pos - cur.LoopEnd);
						if (cur.LoopsLeft != XAUDIO2_LOOP_INFINITE) --cur.LoopsLeft;
						if (cur.LoopEnd <= cur.LoopBegin) { cur.LoopsLeft = 0; }
						continue;
					}
					const double over = Pos - cur.End;
					Queue.erase(Queue.begin());
					Pos = Queue.empty() ? 0.0 : Queue[0].Begin + over;
				}
			}
		}
	};

	struct SubmixVoice final : VoiceCommon<IXAudio2SubmixVoice> { SubmixVoice() : VoiceCommon(Submix) {} };
	struct MasterVoice final : VoiceCommon<IXAudio2MasteringVoice> { MasterVoice() : VoiceCommon(Master) {} };

	class Engine final : public IXAudio2
	{
	public:
		std::atomic<ULONG> Refs{ 1 };
		std::vector<VoiceBase*> Voices;
		MasterVoice* Master = nullptr;
#if defined(__ANDROID__)
		AAudioStream* Stream = nullptr;
#else
		void* Stream = nullptr;   // 웹: 출력이 열렸으면 this (Web Audio 노드는 JS 쪽)
#endif
		bool EnginePaused = false;          // StopEngine (게임 일시 정지)
		std::atomic<bool> AppPaused{ false };   // 앱이 뒤로 (SetAudioPaused)
		uint32_t OutRate = 48000;
		std::vector<float> SourceTemp;
		// 성능 (GetPerformanceData)
		uint64_t AudioNs = 0, TotalNs = 0;
		std::chrono::steady_clock::time_point LastQuery = std::chrono::steady_clock::now();
		uint64_t Rendered = 0;
		float LastPeak = 0.0f;

		HRESULT QueryInterface(REFIID, void** out) override { *out = nullptr; return E_NOINTERFACE; }
		ULONG AddRef() override { return ++Refs; }
		ULONG Release() override;

		HRESULT CreateSourceVoice(IXAudio2SourceVoice** voice, const WAVEFORMATEX* format, UINT32, float, IXAudio2VoiceCallback*, const XAUDIO2_VOICE_SENDS* sends, const XAUDIO2_EFFECT_CHAIN* chain) override
		{
			if (!voice || !format) return E_INVALIDARG;
			const bool isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
			if (format->wFormatTag != WAVE_FORMAT_PCM && !isFloat) return E_INVALIDARG;
			auto* v = new SourceVoice();
			v->Owner = this;
			v->Format = *format;
			v->Float = isFloat;
			v->Channels = format->nChannels;
			v->Rate = format->nSamplesPerSec;
			if (sends && sends->SendCount > 0) v->Dest = dynamic_cast<VoiceBase*>(sends->pSends[0].pOutputVoice);
			v->SetChain(chain);
			std::lock_guard<std::mutex> g(s_Lock);
			Voices.push_back(v);
			*voice = v;
			return S_OK;
		}

		HRESULT CreateSubmixVoice(IXAudio2SubmixVoice** voice, UINT32 channels, UINT32 rate, UINT32 flags, UINT32 stage, const XAUDIO2_VOICE_SENDS* sends, const XAUDIO2_EFFECT_CHAIN* chain) override
		{
			if (!voice || channels == 0 || channels > 8) return E_INVALIDARG;
			auto* v = new SubmixVoice();
			v->Owner = this;
			v->Channels = channels;
			v->Rate = rate ? rate : OutRate;
			v->Stage = stage;
			v->UseFilter = (flags & XAUDIO2_VOICE_USEFILTER) != 0;
			if (sends && sends->SendCount > 0) v->Dest = dynamic_cast<VoiceBase*>(sends->pSends[0].pOutputVoice);
			v->SetChain(chain);
			std::lock_guard<std::mutex> g(s_Lock);
			Voices.push_back(v);
			*voice = v;
			return S_OK;
		}

		HRESULT CreateMasteringVoice(IXAudio2MasteringVoice** voice, UINT32, UINT32, UINT32, const wchar_t*, const XAUDIO2_EFFECT_CHAIN* chain, int) override;
		HRESULT StartEngine() override { std::lock_guard<std::mutex> g(s_Lock); EnginePaused = false; return S_OK; }
		void StopEngine() override { std::lock_guard<std::mutex> g(s_Lock); EnginePaused = true; }
		void GetPerformanceData(XAUDIO2_PERFORMANCE_DATA* d) override
		{
			std::lock_guard<std::mutex> g(s_Lock);
			*d = {};
			for (VoiceBase* v : Voices)
				if (v->Type == VoiceBase::Source)
				{
					++d->TotalSourceVoiceCount;
					auto* s = static_cast<SourceVoice*>(v);
					if (s->Started && !s->Queue.empty()) ++d->ActiveSourceVoiceCount;
				}
				else if (v->Type == VoiceBase::Submix) ++d->ActiveSubmixVoiceCount;
			d->AudioCyclesSinceLastQuery = AudioNs;
			d->TotalCyclesSinceLastQuery = TotalNs;
			AudioNs = TotalNs = 0;
		}

		void Remove(VoiceBase* v)
		{
			std::lock_guard<std::mutex> g(s_Lock);
			Voices.erase(std::remove(Voices.begin(), Voices.end(), v), Voices.end());
			for (VoiceBase* o : Voices)
				if (o->Dest == v) o->Dest = nullptr;   // 지운 그룹으로 보내던 보이스는 마스터로
			if (v == Master) Master = nullptr;
		}

		// AAudio 콜백 (오디오 스레드): 소스 → 서브믹스 (처리 순서대로) → 마스터 → out (frames * 2)
		void Render(float* out, uint32_t frames, uint32_t outChannels)
		{
			const auto t0 = std::chrono::steady_clock::now();
			std::fill(out, out + (size_t)frames * outChannels, 0.0f);
			std::lock_guard<std::mutex> g(s_Lock);
			if (!Master || EnginePaused || AppPaused) return;
			for (VoiceBase* v : Voices)
				if (v->Type != VoiceBase::Source)
					v->Mix.assign((size_t)frames * v->Channels, 0.0f);
			for (VoiceBase* v : Voices)
			{
				if (v->Type != VoiceBase::Source) continue;
				auto* s = static_cast<SourceVoice*>(v);
				if (!s->Started || s->Queue.empty()) continue;
				SourceTemp.resize((size_t)frames * s->Channels);
				s->Render(SourceTemp.data(), frames, OutRate);
				s->ApplyFilter(SourceTemp.data(), frames);
				s->ApplyEffects(SourceTemp.data(), frames, OutRate);
				VoiceBase* to = s->Dest ? s->Dest : Master;
				s->MixInto(SourceTemp.data(), frames, to->Mix.data(), to->Channels, s->Volume);
			}
			// 서브믹스: ProcessingStage 가 작은 것부터 (자식 그룹 → 부모 그룹)
			std::vector<VoiceBase*> subs;
			for (VoiceBase* v : Voices)
				if (v->Type == VoiceBase::Submix) subs.push_back(v);
			std::stable_sort(subs.begin(), subs.end(), [](const VoiceBase* a, const VoiceBase* b) { return a->Stage < b->Stage; });
			for (VoiceBase* v : subs)
			{
				v->ApplyFilter(v->Mix.data(), frames);
				v->ApplyEffects(v->Mix.data(), frames, OutRate);
				VoiceBase* to = v->Dest && v->Dest != v ? v->Dest : Master;
				v->MixInto(v->Mix.data(), frames, to->Mix.data(), to->Channels, v->Volume);
			}
			Master->ApplyEffects(Master->Mix.data(), frames, OutRate);
			float peak = 0.0f;
			for (uint32_t f = 0; f < frames; ++f)
				for (uint32_t c = 0; c < outChannels; ++c)
				{
					const float x = std::clamp(Master->Mix[(size_t)f * Master->Channels + (c % Master->Channels)] * Master->Volume, -1.0f, 1.0f);
					out[(size_t)f * outChannels + c] = x;
					peak = (std::max)(peak, fabsf(x));
				}
			Rendered += frames;
			LastPeak = (std::max)(LastPeak, peak);
			AudioNs += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
			TotalNs += (uint64_t)(1e9 * frames / OutRate);
		}
	};

	Engine* s_Engine = nullptr;

#if defined(__ANDROID__)
	aaudio_data_callback_result_t DataCallback(AAudioStream* stream, void* user, void* audioData, int32_t numFrames)
	{
		static_cast<Engine*>(user)->Render(static_cast<float*>(audioData), (uint32_t)numFrames, (uint32_t)AAudioStream_getChannelCount(stream));
		return AAUDIO_CALLBACK_RESULT_CONTINUE;
	}

	void ErrorCallback(AAudioStream*, void*, aaudio_result_t error)
	{
		__android_log_print(ANDROID_LOG_WARN, "NOVA", "[Audio] AAudio stream error %s", AAudio_convertResultToText(error));
	}
#else
	// 웹 출력: AudioContext + ScriptProcessor (2 채널, 1024 프레임) — 콜백이 nova_audio_render 로 믹서를 부른다 (메인 스레드).
	//  브라우저는 사용자 입력 (클릭 · 키 · 터치) 뒤에만 소리를 내므로 그때 resume. 반환 = 출력 레이트 (0 = 실패)
	EM_JS(int, NovaWebAudioOpen, (void* engine), {
		try {
			const Ctx = window.AudioContext || window.webkitAudioContext;
			if (!Ctx) return 0;
			const ctx = new Ctx();
			const frames = 1024;
			const node = ctx.createScriptProcessor(frames, 0, 2);
			const buf = _malloc(frames * 2 * 4);
			node.onaudioprocess = (e) => {
				const n = e.outputBuffer.length;
				_nova_audio_render(engine, buf, n);
				const f = HEAPF32.subarray(buf >> 2, (buf >> 2) + n * 2);
				const l = e.outputBuffer.getChannelData(0), r = e.outputBuffer.getChannelData(1);
				for (let i = 0; i < n; i++) { l[i] = f[2 * i]; r[i] = f[2 * i + 1]; }
			};
			node.connect(ctx.destination);
			Module.novaAudio = ctx;
			const resume = () => { if (ctx.state !== 'running') ctx.resume(); };
			['pointerdown', 'keydown', 'touchstart'].forEach(ev => window.addEventListener(ev, resume, { capture: true }));
			return ctx.sampleRate | 0;
		} catch (err) { console.warn('NOVA audio:', err); return 0; }
	});
	EM_JS(void, NovaWebAudioPause, (int paused), {
		if (Module.novaAudio) { if (paused) Module.novaAudio.suspend(); else Module.novaAudio.resume(); }
	});
#endif

	template <class B>
	HRESULT VoiceCommon<B>::SetOutputVoices(const XAUDIO2_VOICE_SENDS* sends)
	{
		std::lock_guard<std::mutex> g(s_Lock);
		Dest = sends && sends->SendCount > 0 ? dynamic_cast<VoiceBase*>(sends->pSends[0].pOutputVoice) : nullptr;
		if (Dest == Owner->Master) Dest = nullptr;
		Matrix.clear();   // XAudio2: 보낼 곳을 바꾸면 행렬은 기본으로
		return S_OK;
	}

	template <class B>
	void VoiceCommon<B>::DestroyVoice()
	{
		if (Owner) Owner->Remove(this);
		delete this;
	}

	HRESULT Engine::CreateMasteringVoice(IXAudio2MasteringVoice** voice, UINT32, UINT32, UINT32, const wchar_t*, const XAUDIO2_EFFECT_CHAIN* chain, int)
	{
		if (Master) return E_FAIL;
#if defined(__ANDROID__)
		AAudioStreamBuilder* b = nullptr;
		if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return E_FAIL;
		AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
		AAudioStreamBuilder_setChannelCount(b, 2);
		AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
		AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
		AAudioStreamBuilder_setDataCallback(b, DataCallback, this);
		AAudioStreamBuilder_setErrorCallback(b, ErrorCallback, this);
		const aaudio_result_t r = AAudioStreamBuilder_openStream(b, &Stream);
		AAudioStreamBuilder_delete(b);
		if (r != AAUDIO_OK || !Stream)
		{
			__android_log_print(ANDROID_LOG_WARN, "NOVA", "[Audio] AAudio openStream failed: %s", AAudio_convertResultToText(r));
			Stream = nullptr;
			return E_FAIL;
		}
		OutRate = (uint32_t)AAudioStream_getSampleRate(Stream);
#else
		const int rate = NovaWebAudioOpen(this);
		if (rate <= 0)
		{
			NOVA_AUDIO_LOG("[Audio] Web Audio is not available - no sound");
			return E_FAIL;
		}
		Stream = this;
		OutRate = (uint32_t)rate;
#endif
		auto* m = new MasterVoice();
		m->Owner = this;
		m->Channels = 2;
		m->Rate = OutRate;
		m->Stage = 0xFFFFFFFF;
		m->SetChain(chain);
		{
			std::lock_guard<std::mutex> g(s_Lock);
			Voices.push_back(m);
			Master = m;
		}
#if defined(__ANDROID__)
		if (!AppPaused) AAudioStream_requestStart(Stream);
		*voice = m;
		__android_log_print(ANDROID_LOG_INFO, "NOVA", "[Audio] AAudio stream: %d Hz, %d ch, burst %d frames, %s", (int)OutRate, AAudioStream_getChannelCount(Stream),
			AAudioStream_getFramesPerBurst(Stream), AAudioStream_getPerformanceMode(Stream) == AAUDIO_PERFORMANCE_MODE_LOW_LATENCY ? "low latency" : "normal");
#else
		*voice = m;
		NOVA_AUDIO_LOG("[Audio] Web Audio: %d Hz, 2 ch, 1024 frames (starts after the first click / key)", (int)OutRate);
#endif
		return S_OK;
	}

	ULONG Engine::Release()
	{
		const ULONG r = --Refs;
		if (r == 0)
		{
#if defined(__ANDROID__)
			if (Stream) { AAudioStream_requestStop(Stream); AAudioStream_close(Stream); Stream = nullptr; }
#else
			if (Stream) { NovaWebAudioPause(1); Stream = nullptr; }
#endif
			std::vector<VoiceBase*> left;
			{
				std::lock_guard<std::mutex> g(s_Lock);
				left.swap(Voices);
				Master = nullptr;
			}
			for (VoiceBase* v : left) { v->Owner = nullptr; delete v; }
			if (s_Engine == this) s_Engine = nullptr;
			delete this;
		}
		return r;
	}
}

#if !defined(__ANDROID__)
// Web Audio 콜백 → 믹서 (out = 2 채널 섞어 놓은 frames 개)
extern "C" EMSCRIPTEN_KEEPALIVE void nova_audio_render(void* engine, float* out, int frames)
{
	static_cast<Engine*>(engine)->Render(out, (uint32_t)frames, 2);
}
#endif

HRESULT XAudio2Create(IXAudio2** out, UINT32, UINT32)
{
	if (!out) return E_INVALIDARG;
	s_Engine = new Engine();
	*out = s_Engine;
	return S_OK;
}

HRESULT XAudio2CreateReverb(IUnknown** out, UINT32) { *out = new Reverb(); return S_OK; }
HRESULT XAudio2CreateVolumeMeter(IUnknown** out, UINT32) { *out = new VolumeMeter(); return S_OK; }

HRESULT CreateFX(REFGUID clsid, IUnknown** out, const void*, UINT32)
{
	if (clsid == __uuidof(FXEcho)) { *out = new Echo(); return S_OK; }
	*out = nullptr;
	return E_NOTIMPL;
}

namespace NovaAndroid
{
	void SetAudioPaused(bool paused)
	{
		Engine* e = s_Engine;
		if (!e) return;
		e->AppPaused = paused;
		if (e->Stream)
		{
#if defined(__ANDROID__)
			if (paused) AAudioStream_requestPause(e->Stream);
			else AAudioStream_requestStart(e->Stream);
#else
			NovaWebAudioPause(paused ? 1 : 0);
#endif
		}
	}

	bool AudioStats(uint64_t& framesRendered, float& peak)
	{
		Engine* e = s_Engine;
		if (!e || !e->Stream) return false;
		std::lock_guard<std::mutex> g(s_Lock);
		framesRendered = e->Rendered;
		peak = e->LastPeak;
		e->LastPeak = 0.0f;
		return true;
	}
}
