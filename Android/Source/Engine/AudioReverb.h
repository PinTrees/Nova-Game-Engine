#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// 안드로이드 믹서 그룹의 리버브 (XAudio2 리버브 자리, 플랫폼 코드 없음 — PC 에서도 임펄스 응답을 검사할 수 있게).
//  I3DL2 값 (XAudio2 의 네이티브 리버브 값으로 바꾼 것) 을 따른다:
//   - 앞 지연 (ReflectionsDelay) 뒤 초기 반사 (탭 6 개, ReflectionsGain), 그 뒤 (ReverbDelay) 늦은 잔향 (ReverbGain)
//   - 늦은 잔향: 채널마다 빗살 8 개 (되먹임 감쇠 = DecayTime 의 60 dB, 고역 감쇠 = DecayHFRatio) + 전역 통과 4 개 (Diffusion)
//   - 방 필터 (RoomFilterMain / RoomFilterHF @ RoomFilterFreq), WetDryMix (0 = 원음만, 100 = 잔향만)
struct NovaReverb
{
	struct Params
	{
		float WetDryMix = 0.0f;             // %
		float ReflectionsDelayMs = 7.0f, ReverbDelayMs = 11.0f;
		float ReflectionsGainDb = -12.0f, ReverbGainDb = 0.0f;
		float RoomFilterMainDb = -10.0f, RoomFilterHFDb = -1.0f, RoomFilterFreq = 5000.0f;
		float DecayTime = 1.49f, DecayHFRatio = 0.83f;
		float Density = 100.0f, Diffusion = 100.0f;   // %
	} P;

	void SetParams(const Params& p) { P = p; Dirty = true; }

	// data = frames * channels (제자리)
	void Process(float* data, uint32_t frames, uint32_t channels, uint32_t rate)
	{
		if (P.WetDryMix <= 0.0f) return;
		if (Rate != rate || Channels != channels) Build(rate, channels);
		if (Dirty) Update();
		const float wet = std::clamp(P.WetDryMix / 100.0f, 0.0f, 1.0f);
		for (uint32_t f = 0; f < frames; ++f)
			for (uint32_t c = 0; c < channels; ++c)
			{
				Chan& ch = C[c];
				float& x = data[(size_t)f * channels + c];
				const float in = x;
				// 앞 지연선 (초기 반사 + 늦은 잔향의 입력)
				ch.Pre[ch.PrePos] = in;
				auto tap = [&](uint32_t delay) { return ch.Pre[(ch.PrePos + ch.Pre.size() - std::min<size_t>(delay, ch.Pre.size() - 1)) % ch.Pre.size()]; };
				float early = 0.0f;
				for (int t = 0; t < 6; ++t)
					early += tap(EarlyDelay[t] + c * 7) * EarlyGain[t];
				const float lateIn = tap(LateDelay) * kInputGain;
				ch.PrePos = (ch.PrePos + 1) % (uint32_t)ch.Pre.size();
				// 빗살: 되먹임 안에 1 차 저역 통과 (고역이 먼저 줄어든다)
				float late = 0.0f;
				for (int i = 0; i < 8; ++i)
				{
					Line& l = ch.Comb[i];
					const float y = l.B[l.P];
					l.Store = y * (1.0f - Damp) + l.Store * Damp;
					l.B[l.P] = lateIn + l.Store * CombGain[c][i];
					l.P = (l.P + 1) % (uint32_t)l.B.size();
					late += y;
				}
				for (int i = 0; i < 4; ++i)
				{
					Line& l = ch.All[i];
					const float b = l.B[l.P];
					l.B[l.P] = late + b * AllGain;
					late = b - late * AllGain;
					l.P = (l.P + 1) % (uint32_t)l.B.size();
				}
				float out = early * ReflGain + late * LateGain;
				// 방 필터: RoomFilterFreq 위는 RoomFilterHF 만큼 (1 차 저역 통과로 나눠 고역만 줄인다)
				ch.Lp += (out - ch.Lp) * LpCoef;
				out = (ch.Lp + (out - ch.Lp) * HfGain) * MainGain;
				x = in * (1.0f - wet) + out * wet;
			}
	}

private:
	static constexpr float kInputGain = 0.015f;   // Freeverb 의 고정 입력 이득 (빗살 8 개의 합을 맞춘다)
	struct Line { std::vector<float> B; uint32_t P = 0; float Store = 0.0f; };
	struct Chan { std::vector<float> Pre; uint32_t PrePos = 0; Line Comb[8], All[4]; float Lp = 0.0f; };
	std::vector<Chan> C;
	uint32_t Rate = 0, Channels = 0;
	bool Dirty = true;
	float CombGain[8][8] = {}, Damp = 0.2f, AllGain = 0.5f;
	uint32_t EarlyDelay[6] = {}, LateDelay = 0;
	float EarlyGain[6] = {};
	float ReflGain = 0.0f, LateGain = 0.0f, MainGain = 1.0f, HfGain = 1.0f, LpCoef = 1.0f;

	static float Db(float db) { return powf(10.0f, db / 20.0f); }

	void Build(uint32_t rate, uint32_t channels)
	{
		static const int kComb[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 }, kAll[4] = { 556, 441, 341, 225 };
		Rate = rate;
		Channels = (std::min)(channels, 8u);
		C.assign(channels, Chan());
		const float scale = rate / 44100.0f;
		for (uint32_t c = 0; c < channels; ++c)
		{
			const int spread = (int)c * 23;   // 채널마다 조금 다른 길이 (넓은 잔향)
			for (int i = 0; i < 8; ++i) C[c].Comb[i].B.assign((size_t)((kComb[i] + spread) * scale), 0.0f);
			for (int i = 0; i < 4; ++i) C[c].All[i].B.assign((size_t)((kAll[i] + spread) * scale), 0.0f);
			C[c].Pre.assign((size_t)(rate * 0.5f) + 64, 0.0f);   // 앞 지연 최대 0.5 초 (반사 300 ms + 잔향 85 ms 보다 넉넉히)
		}
		Dirty = true;
	}

	void Update()
	{
		Dirty = false;
		const float rate = (float)Rate;
		// 늦은 잔향: 빗살 길이 L 샘플이 한 바퀴 돌 때 g = 10^(-3 L / (rate T)) → T 초에 60 dB
		const float decay = std::clamp(P.DecayTime, 0.1f, 20.0f);
		for (uint32_t c = 0; c < Channels; ++c)
			for (int i = 0; i < 8; ++i)
				CombGain[c][i] = powf(10.0f, -3.0f * (float)C[c].Comb[i].B.size() / (rate * decay));
		// 고역 감쇠: DecayHFRatio 1 = 고르게, 작을수록 고역이 빨리 (되먹임 저역 통과)
		Damp = std::clamp(0.05f + 0.6f * (1.0f - std::clamp(P.DecayHFRatio, 0.1f, 2.0f)), 0.0f, 0.7f);
		AllGain = 0.25f + 0.45f * std::clamp(P.Diffusion / 100.0f, 0.0f, 1.0f);
		// 초기 반사: ReflectionsDelay 부터 30 ms 안에 6 탭 (Density 가 낮을수록 듬성듬성)
		const float spacing = 0.005f * (1.5f - std::clamp(P.Density / 100.0f, 0.0f, 1.0f));
		for (int t = 0; t < 6; ++t)
		{
			EarlyDelay[t] = (uint32_t)((P.ReflectionsDelayMs / 1000.0f + spacing * t * (1.0f + 0.3f * (t % 2))) * rate);
			EarlyGain[t] = 0.6f / (1.0f + t * 0.6f) * (t % 2 ? -1.0f : 1.0f);
		}
		LateDelay = (uint32_t)((P.ReflectionsDelayMs + P.ReverbDelayMs) / 1000.0f * rate);
		ReflGain = Db(P.ReflectionsGainDb);
		LateGain = Db(P.ReverbGainDb);
		MainGain = Db(P.RoomFilterMainDb);
		HfGain = Db(P.RoomFilterHFDb);
		LpCoef = 1.0f - expf(-2.0f * 3.14159265f * std::clamp(P.RoomFilterFreq, 20.0f, rate * 0.45f) / rate);
	}
};
