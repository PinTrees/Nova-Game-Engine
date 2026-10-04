#include "../../Android/Source/Engine/AudioReverb.h"
#include <cstdio>

// 안드로이드 리버브 (Android/Source/Engine/AudioReverb.h) 를 PC 에서 검사 — Tools/tests/android.ps1 가 cl 로 빌드해 돌린다.
// 임펄스 응답: 잔향만 (WetDryMix 100), 48 kHz 스테레오. Schroeder 적분으로 -5 → -35 dB 기울기 → RT60
struct Preset { const char* Name; float Room, RoomHF, Decay, HF, Refl, ReflDelay, Reverb, RevDelay, Diff, Dens; };
int main()
{
	const Preset ps[] = {
		{ "Room", -1000, -454, 0.40f, 0.83f, -1646, 0.002f, 53, 0.003f, 100, 100 },
		{ "Bathroom", -1000, -1200, 1.49f, 0.54f, -370, 0.007f, 1030, 0.011f, 100, 60 },
		{ "ConcertHall", -1000, -500, 3.92f, 0.70f, -1230, 0.020f, -2, 0.029f, 100, 100 },
		{ "Hangar", -1000, -1000, 10.05f, 0.23f, -602, 0.020f, 198, 0.030f, 100, 100 },
		{ "Plate", -1000, -200, 1.30f, 0.90f, 0, 0.002f, 0, 0.010f, 100, 75 },
	};
	const int rate = 48000, ch = 2;
	int fails = 0;
	for (const Preset& p : ps)
	{
		NovaReverb r;
		NovaReverb::Params q;
		q.WetDryMix = 100; q.ReflectionsDelayMs = p.ReflDelay * 1000; q.ReverbDelayMs = p.RevDelay * 1000;
		q.ReflectionsGainDb = p.Refl / 100; q.ReverbGainDb = p.Reverb / 100; q.RoomFilterMainDb = p.Room / 100; q.RoomFilterHFDb = p.RoomHF / 100;
		q.RoomFilterFreq = 5000; q.DecayTime = p.Decay; q.DecayHFRatio = p.HF; q.Density = p.Dens; q.Diffusion = p.Diff;
		r.SetParams(q);
		const int n = (int)(rate * (p.Decay * 1.6f + 0.5f));
		std::vector<float> buf((size_t)n * ch, 0.0f);
		buf[0] = buf[1] = 1.0f;
		for (int off = 0; off < n; off += 512)
			r.Process(buf.data() + (size_t)off * ch, (uint32_t)std::min(512, n - off), ch, rate);
		double peak = 0, total = 0;
		std::vector<double> e(n);
		bool finite = true;
		for (int i = 0; i < n; ++i)
		{
			const double v = buf[(size_t)i * ch];
			if (!std::isfinite(v)) finite = false;
			peak = std::max(peak, std::abs(v));
			e[i] = v * v;
		}
		// Schroeder 역적분
		std::vector<double> edc(n);
		double acc = 0;
		for (int i = n - 1; i >= 0; --i) { acc += e[i]; edc[i] = acc; }
		auto at = [&](double db) { for (int i = 0; i < n; ++i) if (10 * log10(edc[i] / edc[0] + 1e-30) <= db) return i; return n; };
		const int i5 = at(-5), i35 = at(-35);
		const double rt60 = (i35 - i5) / (double)rate * 2.0;   // 30 dB 기울기 × 2
		int first = 0;
		while (first < n && std::abs(buf[(size_t)first * ch]) < 1e-6) ++first;
		const bool ok = finite && peak < 1.0 && rt60 > p.Decay * 0.6 && rt60 < p.Decay * 1.4;
		fails += !ok;
		printf("%-12s decay %5.2f s -> RT60 %5.2f s, peak %.3f, first sound %.1f ms %s\n", p.Name, p.Decay, rt60, peak, first * 1000.0 / rate, ok ? "OK" : "FAIL");
	}
	// WetDryMix 0 = 원음 그대로
	NovaReverb r;
	float x[4] = { 0.5f, -0.25f, 0.1f, 0.2f };
	r.Process(x, 2, 2, rate);
	const bool dry = x[0] == 0.5f && x[1] == -0.25f;
	printf("wet 0 leaves the signal: %s\n", dry ? "OK" : "FAIL");
	return fails + !dry;
}
