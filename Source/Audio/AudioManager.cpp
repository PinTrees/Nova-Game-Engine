#include "pch.h"
#include "AudioManager.h"
#include "AudioClip.h"
#include "AudioListener.h"
#include "AudioMixer.h"
#include "AudioDecoder.h"
#include <xaudio2.h>
#include <xaudio2fx.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

#pragma comment(lib, "xaudio2.lib")

namespace
{
	IXAudio2* s_Engine = nullptr;
	IXAudio2MasteringVoice* s_Master = nullptr;
	bool s_Tried = false;
	bool s_HasMeter = false;
	UINT32 s_OutChannels = 2;
	bool s_Muted = false;
	bool s_EngineStopped = false;
	bool s_WasPlaying = false;

	struct OneShot { IXAudio2SourceVoice* Voice; std::shared_ptr<AudioClip> Clip; AudioStream* Stream; };
	std::vector<OneShot> s_OneShots;
	IXAudio2SourceVoice* s_Preview = nullptr;
	AudioStream* s_PreviewStream = nullptr;
	std::shared_ptr<AudioClip> s_PreviewClip;

	Vec3 s_ListenerPos(0, 0, 0), s_ListenerRight(1, 0, 0), s_ListenerVel(0, 0, 0);
	UINT32 s_SampleRate = 48000;
	constexpr float kSpeedOfSound = 343.0f;   // m/s (Unity 와 같음)

	// 통계 (최근 값)
	float s_PeakHold = 0.0f;
	double s_PeakTime = 0.0;
	int s_Frames = 0, s_ClipFrames = 0;
	AudioManager::Stats s_Stats;

	// ---- 스트리밍: 스레드 하나가 모든 스트림의 보이스에 버퍼를 3 개 (1.5 초) 까지 채워 둔다
	constexpr int kStreamBuffers = 4;      // 고리 버퍼 (보이스에는 3 개까지 → 다시 채우는 칸은 이미 재생이 끝났다)
	constexpr int kStreamQueued = 3;
	std::mutex s_StreamLock;
	std::vector<AudioStream*> s_Streams;
	std::thread s_StreamThread;
	std::atomic<bool> s_StreamQuit{ false };
	std::condition_variable s_StreamWake;
}

struct AudioStream
{
	IXAudio2SourceVoice* Voice = nullptr;
	std::shared_ptr<AudioClip> Clip;
	std::unique_ptr<AudioDecoder> Decoder;
	std::atomic<bool> Loop{ false };
	bool Ended = false;                    // 끝까지 풀어 넣었다 (반복이 아니면)
	std::vector<int16_t> Buffers[kStreamBuffers];
	int Next = 0;
	uint32_t FramesPerBuffer = 0;
};

namespace
{
	// 보이스에 버퍼가 kStreamQueued 개보다 적으면 풀어 넣는다 (s_StreamLock 안에서, 또는 아직 등록 전)
	void ServiceStream(AudioStream& s)
	{
		XAUDIO2_VOICE_STATE st = {};
		s.Voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
		UINT32 queued = st.BuffersQueued;
		const int ch = s.Clip->Channels;
		while (queued < (UINT32)kStreamQueued && !s.Ended)
		{
			std::vector<int16_t>& buf = s.Buffers[s.Next];
			buf.resize((size_t)s.FramesPerBuffer * ch);
			uint64_t got = 0;
			int rewinds = 0;
			while (got < s.FramesPerBuffer)
			{
				const uint64_t n = s.Decoder->Read(buf.data() + got * ch, s.FramesPerBuffer - got);
				got += n;
				if (n > 0)
					continue;
				if (s.Loop && rewinds++ < 2 && s.Decoder->Rewind())
					continue;   // 반복: 처음으로 돌아가 이어 채운다
				s.Ended = true;
				break;
			}
			if (got == 0)
			{
				s.Voice->Discontinuity();   // 남은 버퍼까지 재생하고 끝
				break;
			}
			XAUDIO2_BUFFER b = {};
			b.AudioBytes = (UINT32)(got * ch * sizeof(int16_t));
			b.pAudioData = reinterpret_cast<const BYTE*>(buf.data());
			b.Flags = s.Ended ? XAUDIO2_END_OF_STREAM : 0;
			if (FAILED(s.Voice->SubmitSourceBuffer(&b)))
				break;
			s.Next = (s.Next + 1) % kStreamBuffers;
			++queued;
		}
	}

	void StreamThreadMain()
	{
		std::unique_lock<std::mutex> lock(s_StreamLock);
		while (!s_StreamQuit)
		{
			for (AudioStream* s : s_Streams)
				ServiceStream(*s);
			s_StreamWake.wait_for(lock, std::chrono::milliseconds(15));
		}
	}

	void UpdateListenerPose()
	{
		// AudioListener 가 있으면 그 Transform, 없으면 Game 뷰 카메라
		if (AudioListener* l = AudioListener::Active())
		{
			Transform* t = l->GetGameObject()->GetTransform();
			s_ListenerPos = t->GetPosition();
			s_ListenerRight = t->GetRight();
			return;
		}
		if (auto cam = DisplayManager::GetI()->GetActiveCamera())
		{
			const XMFLOAT3 p = cam->GetPosition();
			const XMFLOAT3 look = cam->GetLook();
			s_ListenerPos = Vec3(p.x, p.y, p.z);
			Vec3 right = Vec3(0, 1, 0).Cross(Vec3(look.x, look.y, look.z));   // 왼손 좌표계: up x forward = right
			if (right.LengthSquared() > 1e-6f)
			{
				right.Normalize();
				s_ListenerRight = right;
			}
		}
	}

	void UpdateListener()
	{
		// 속도 (도플러) = 이번 프레임 이동 / 시간, 순간 이동(100 m/s 넘음)은 무시
		const Vec3 before = s_ListenerPos;
		UpdateListenerPose();
		const float dt = ImGui::GetIO().DeltaTime;
		const Vec3 v = dt > 1e-4f ? (s_ListenerPos - before) / dt : Vec3::Zero;
		s_ListenerVel = v.Length() > 100.0f ? Vec3::Zero : v;
	}
}

namespace AudioManager
{
	bool Init()
	{
		if (s_Engine || s_Tried)
			return s_Engine != nullptr;
		s_Tried = true;
		HRESULT hr = ::XAudio2Create(&s_Engine, 0, XAUDIO2_DEFAULT_PROCESSOR);
		if (FAILED(hr))
		{
			EditorLog::Write("Audio", "XAudio2Create failed hr=0x%08X - audio disabled", (unsigned)hr);
			s_Engine = nullptr;
			return false;
		}
		hr = s_Engine->CreateMasteringVoice(&s_Master);
		if (FAILED(hr))
		{
			EditorLog::Write("Audio", "CreateMasteringVoice failed hr=0x%08X (no audio device?) - audio disabled", (unsigned)hr);
			s_Engine->Release();
			s_Engine = nullptr;
			return false;
		}
		XAUDIO2_VOICE_DETAILS details = {};
		s_Master->GetVoiceDetails(&details);
		s_OutChannels = details.InputChannels;
		s_SampleRate = details.InputSampleRate;

		// 출력 레벨 측정기 (Stats 의 Level / Clipping)
		IUnknown* meter = nullptr;
		if (SUCCEEDED(::XAudio2CreateVolumeMeter(&meter)))
		{
			XAUDIO2_EFFECT_DESCRIPTOR desc = { meter, TRUE, s_OutChannels };
			XAUDIO2_EFFECT_CHAIN chain = { 1, &desc };
			s_HasMeter = SUCCEEDED(s_Master->SetEffectChain(&chain));
			meter->Release();
		}
		s_Master->SetVolume(s_Muted ? 0.0f : 1.0f);
		EditorLog::Write("Audio", "XAudio2 ready: %u output channels, %u Hz, meter %s", s_OutChannels, details.InputSampleRate, s_HasMeter ? "on" : "off");
		return true;
	}

	void Shutdown()
	{
		StopAllOneShots();
		StopPreview();
		if (s_StreamThread.joinable())
		{
			s_StreamQuit = true;
			s_StreamWake.notify_all();
			s_StreamThread.join();
		}
		AudioMixer::ForgetAllVoices();   // 그룹 보이스는 엔진을 놓을 때 같이 지워진다
		if (s_Master) { s_Master->DestroyVoice(); s_Master = nullptr; }
		if (s_Engine) { s_Engine->Release(); s_Engine = nullptr; }
	}

	bool IsAvailable() { return s_Engine != nullptr; }
	IXAudio2* Engine() { return s_Engine; }
	unsigned SampleRate() { return s_SampleRate; }
	unsigned OutputChannels() { return s_OutChannels; }

	IXAudio2SourceVoice* CreateVoice(const AudioClip& clip, IXAudio2Voice* output)
	{
		if (!Init())
			return nullptr;
		IXAudio2SourceVoice* voice = nullptr;
		XAUDIO2_SEND_DESCRIPTOR send = { 0, output };
		XAUDIO2_VOICE_SENDS sends = { 1, &send };
		const HRESULT hr = s_Engine->CreateSourceVoice(&voice, &clip.Format, 0, 3.0f, nullptr, output ? &sends : nullptr);
		if (FAILED(hr))
		{
			EditorLog::Write("Audio", "CreateSourceVoice failed for %s hr=0x%08X", clip.Path.c_str(), (unsigned)hr);
			return nullptr;
		}
		return voice;
	}

	AudioStream* StartStream(IXAudio2SourceVoice* voice, const std::shared_ptr<AudioClip>& clip, bool loop)
	{
		if (voice == nullptr || clip == nullptr || !clip->Streaming)
			return nullptr;
		auto* s = new AudioStream();
		s->Voice = voice;
		s->Clip = clip;
		s->Decoder = clip->OpenDecoder();
		s->Loop = loop;
		s->FramesPerBuffer = (uint32_t)(std::max)(1024, clip->Frequency / 2);
		if (s->Decoder == nullptr)
		{
			delete s;
			return nullptr;
		}
		ServiceStream(*s);   // 첫 버퍼들을 바로 (Start 하자마자 소리가 나게)
		std::lock_guard<std::mutex> g(s_StreamLock);
		s_Streams.push_back(s);
		if (!s_StreamThread.joinable())
		{
			s_StreamQuit = false;
			s_StreamThread = std::thread(StreamThreadMain);
		}
		return s;
	}

	void StopStream(AudioStream*& stream, IXAudio2SourceVoice*& voice)
	{
		if (stream)
		{
			std::lock_guard<std::mutex> g(s_StreamLock);
			s_Streams.erase(std::remove(s_Streams.begin(), s_Streams.end(), stream), s_Streams.end());
		}
		DestroyVoice(voice);   // 동기: 돌아오면 보이스가 버퍼를 더 읽지 않는다
		delete stream;
		stream = nullptr;
	}

	void SetStreamLoop(AudioStream* stream, bool loop)
	{
		if (stream)
			stream->Loop = loop;
	}

	bool IsStreamFinished(AudioStream* stream)
	{
		if (stream == nullptr)
			return true;
		bool ended;
		{
			std::lock_guard<std::mutex> g(s_StreamLock);
			ended = stream->Ended;
		}
		if (!ended)
			return false;
		XAUDIO2_VOICE_STATE st = {};
		stream->Voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
		return st.BuffersQueued == 0;
	}

	void SetOutput(IXAudio2SourceVoice* voice, IXAudio2Voice* output)
	{
		if (voice == nullptr)
			return;
		XAUDIO2_SEND_DESCRIPTOR send = { 0, output ? output : s_Master };
		XAUDIO2_VOICE_SENDS sends = { 1, &send };
		voice->SetOutputVoices(&sends);
	}

	void DestroyVoice(IXAudio2SourceVoice*& voice)
	{
		if (voice)
		{
			voice->Stop();
			voice->FlushSourceBuffers();
			voice->DestroyVoice();
			voice = nullptr;
		}
	}

	bool Submit(IXAudio2SourceVoice* voice, const AudioClip& clip, bool loop)
	{
		if (voice == nullptr || clip.Data.empty())
			return false;
		XAUDIO2_BUFFER buffer = {};
		buffer.AudioBytes = (UINT32)clip.Data.size();
		buffer.pAudioData = clip.Data.data();
		buffer.Flags = XAUDIO2_END_OF_STREAM;
		buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
		return SUCCEEDED(voice->SubmitSourceBuffer(&buffer));
	}

	void ApplyMix(IXAudio2SourceVoice* voice, int inputChannels, float volume, float pan, int outputChannels)
	{
		if (voice == nullptr || inputChannels <= 0)
			return;
		const UINT32 outCh = outputChannels > 0 ? (UINT32)outputChannels : s_OutChannels;
		pan = std::clamp(pan, -1.0f, 1.0f);
		volume = (std::max)(0.0f, volume);
		// 밸런스 팬: 가운데 = 양쪽 그대로, 한쪽으로 갈수록 반대쪽이 줄어든다 (Unity 의 Stereo Pan)
		const float left = volume * (pan > 0.0f ? 1.0f - pan : 1.0f);
		const float right = volume * (pan < 0.0f ? 1.0f + pan : 1.0f);
		std::vector<float> m((size_t)inputChannels * outCh, 0.0f);
		auto at = [&](int in, int out) -> float& { return m[(size_t)out * inputChannels + in]; };   // [출력][입력]
		if (outCh >= 2)
		{
			if (inputChannels == 1)
			{
				at(0, 0) = left;
				at(0, 1) = right;
			}
			else
			{
				at(0, 0) = left;
				at(1, 1) = right;
			}
		}
		else
			for (int i = 0; i < inputChannels; ++i)
				at(i, 0) = volume / inputChannels;
		voice->SetOutputMatrix(nullptr, (UINT32)inputChannels, outCh, m.data());
	}

	void SetPitch(IXAudio2SourceVoice* voice, float pitch)
	{
		if (voice)
			voice->SetFrequencyRatio(std::clamp(fabsf(pitch), XAUDIO2_MIN_FREQ_RATIO, 3.0f));
	}

	void Spatialize(const Vec3& sourcePos, float minDistance, float maxDistance, int rolloff, float& gain, float& pan)
	{
		minDistance = (std::max)(0.01f, minDistance);
		maxDistance = (std::max)(minDistance + 0.01f, maxDistance);
		const Vec3 d = sourcePos - s_ListenerPos;
		const float dist = d.Length();
		if (rolloff == 1)   // Linear Rolloff
			gain = std::clamp(1.0f - (dist - minDistance) / (maxDistance - minDistance), 0.0f, 1.0f);
		else                // Logarithmic Rolloff: 최소 거리 밖에서 거리에 반비례, 최대 거리 뒤로는 더 줄지 않음
			gain = dist <= minDistance ? 1.0f : minDistance / (std::min)(dist, maxDistance);
		pan = dist > 1e-4f ? std::clamp(d.Dot(s_ListenerRight) / dist, -1.0f, 1.0f) : 0.0f;
	}

	Vec3 ListenerPosition() { return s_ListenerPos; }
	Vec3 ListenerVelocity() { return s_ListenerVel; }

	float DopplerFactor(const Vec3& sourcePos, const Vec3& sourceVel, float level)
	{
		if (level <= 0.0f)
			return 1.0f;
		Vec3 toListener = s_ListenerPos - sourcePos;
		const float dist = toListener.Length();
		if (dist < 1e-3f)
			return 1.0f;
		toListener /= dist;
		// f' = f · (c + 리스너가 소스 쪽으로 가는 속도) / (c − 소스가 리스너 쪽으로 가는 속도)
		const float vs = std::clamp(sourceVel.Dot(toListener), -kSpeedOfSound * 0.5f, kSpeedOfSound * 0.5f);
		const float vl = std::clamp(-s_ListenerVel.Dot(toListener), -kSpeedOfSound * 0.5f, kSpeedOfSound * 0.5f);
		const float f = (kSpeedOfSound + vl) / (kSpeedOfSound - vs);
		return std::clamp(1.0f + (f - 1.0f) * level, 0.25f, 3.0f);
	}

	void PlayOneShot(const std::shared_ptr<AudioClip>& clip, float volume, float pan, float pitch, IXAudio2Voice* output)
	{
		if (clip == nullptr)
			return;
		IXAudio2SourceVoice* voice = CreateVoice(*clip, output);
		if (voice == nullptr)
			return;
		ApplyMix(voice, clip->Channels, volume, pan, output ? 2 : 0);
		SetPitch(voice, pitch);
		AudioStream* stream = nullptr;
		if (clip->Streaming)
			stream = StartStream(voice, clip, false);
		else
			Submit(voice, *clip, false);
		voice->Start();
		s_OneShots.push_back({ voice, clip, stream });
	}

	void StopAllOneShots()
	{
		for (OneShot& o : s_OneShots)
			StopStream(o.Stream, o.Voice);
		s_OneShots.clear();
	}

	void PlayPreview(const std::shared_ptr<AudioClip>& clip)
	{
		StopPreview();
		if (clip == nullptr)
			return;
		s_Preview = CreateVoice(*clip);
		if (s_Preview == nullptr)
			return;
		s_PreviewClip = clip;
		ApplyMix(s_Preview, clip->Channels, 1.0f, 0.0f);
		if (clip->Streaming)
			s_PreviewStream = StartStream(s_Preview, clip, false);
		else
			Submit(s_Preview, *clip, false);
		s_Preview->Start();
		EditorLog::Write("Audio", "preview %s", clip->Path.c_str());
	}

	void StopPreview()
	{
		StopStream(s_PreviewStream, s_Preview);
		s_PreviewClip.reset();
	}

	bool IsPreviewPlaying()
	{
		if (s_Preview == nullptr)
			return false;
		if (s_PreviewStream)
			return !IsStreamFinished(s_PreviewStream);
		XAUDIO2_VOICE_STATE st = {};
		s_Preview->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
		return st.BuffersQueued > 0;
	}

	void SetMuted(bool muted)
	{
		s_Muted = muted;
		if (s_Master)
			s_Master->SetVolume(muted ? 0.0f : 1.0f);
	}

	bool IsMuted() { return s_Muted; }

	void Update()
	{
		UpdateListener();
		if (s_Engine == nullptr)
			return;

		// 편집기 일시정지 = 오디오도 멈춤 (Unity 와 같음), 미리 듣기는 Play 중이 아닐 때만 쓰이므로 영향 없음
		const bool pause = Application::IsPlaying() && Application::IsPaused();
		if (pause && !s_EngineStopped) { s_Engine->StopEngine(); s_EngineStopped = true; }
		if (!pause && s_EngineStopped) { s_Engine->StartEngine(); s_EngineStopped = false; }

		// Play 가 끝나면 남은 One Shot 정리
		const bool playing = Application::IsPlaying();
		if (s_WasPlaying && !playing)
			StopAllOneShots();
		s_WasPlaying = playing;

		AudioMixer::UpdateAll(ImGui::GetIO().DeltaTime);

		// 끝난 One Shot 정리
		for (size_t i = 0; i < s_OneShots.size();)
		{
			XAUDIO2_VOICE_STATE st = {};
			s_OneShots[i].Voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
			if (s_OneShots[i].Stream ? IsStreamFinished(s_OneShots[i].Stream) : st.BuffersQueued == 0)
			{
				StopStream(s_OneShots[i].Stream, s_OneShots[i].Voice);
				s_OneShots.erase(s_OneShots.begin() + i);
			}
			else
				++i;
		}
		if (s_Preview && !IsPreviewPlaying())
			StopPreview();

		// 통계
		s_Stats.Available = true;
		XAUDIO2_PERFORMANCE_DATA perf = {};
		s_Engine->GetPerformanceData(&perf);
		s_Stats.ActiveVoices = (int)perf.ActiveSourceVoiceCount;
		if (perf.TotalCyclesSinceLastQuery > 0)
			s_Stats.DspLoadPercent = 100.0f * (float)perf.AudioCyclesSinceLastQuery / (float)perf.TotalCyclesSinceLastQuery;
		if (s_HasMeter)
		{
			float peaks[8] = {}, rms[8] = {};
			XAUDIO2FX_VOLUMEMETER_LEVELS levels = {};
			levels.pPeakLevels = peaks;
			levels.pRMSLevels = rms;
			levels.ChannelCount = (std::min)(s_OutChannels, 8u);
			if (SUCCEEDED(s_Master->GetEffectParameters(0, &levels, sizeof(levels))))
			{
				float peak = 0.0f;
				for (UINT32 c = 0; c < levels.ChannelCount; ++c)
					peak = (std::max)(peak, peaks[c]);
				const double now = ImGui::GetTime();
				if (peak >= s_PeakHold || now - s_PeakTime > 0.5)
				{
					s_PeakHold = peak;
					s_PeakTime = now;
				}
				++s_Frames;
				if (peak >= 1.0f)
					++s_ClipFrames;
				if (s_Frames >= 120)
				{
					s_Stats.ClippingPercent = 100.0f * s_ClipFrames / s_Frames;
					s_Frames = s_ClipFrames = 0;
				}
				s_Stats.LevelDb = s_PeakHold > 1e-4f ? 20.0f * log10f(s_PeakHold) : -80.0f;
			}
		}
	}

	Stats GetStats() { return s_Stats; }
}
