#include "pch.h"
#include "WaterWaves.h"
#include <chrono>
#include <random>

namespace WaterWaves
{
	Set Build(const WaterProfile& p, float scale)
	{
		Set set;
		const int n = std::clamp(p.WaveCount, 0, kMaxWaves);
		const float u = (std::max)(0.5f, p.WindSpeed);
		// 다 자란 바다의 마루 파장 (Pierson-Moskowitz: 주기 ≈ 0.81·2π·U/g → 파장 = gT²/2π)
		const float tPeak = 0.81f * XM_2PI * u / kGravity;
		set.PeakWavelength = kGravity * tPeak * tPeak / XM_2PI;
		const float lMax = p.MaxWavelength > 0.0f ? p.MaxWavelength : set.PeakWavelength * 1.8f;
		const float lMin = std::clamp(p.MinWavelength, 0.1f, lMax * 0.9f);
		// 마루 파도 진폭: 유의 파고 Hs ≈ 0.21 U²/g, 마루 파도 하나 ≈ Hs/2 를 파도 수로 나눠 갖는다
		const float hs = 0.21f * u * u / kGravity;
		const float aPeak = hs * 0.5f * p.WaveScale * scale * 2.2f / sqrtf((std::max)(1.0f, n / 4.0f));
		std::mt19937 rng((uint32_t)p.Seed * 7919u + 13u);
		std::uniform_real_distribution<float> uni(0.0f, 1.0f);
		const float wind = XMConvertToRadians(p.WindDirection);
		for (int i = 0; i < n; ++i)
		{
			const float t = n > 1 ? (float)i / (n - 1) : 0.0f;   // 0 = 가장 긴 파도
			float l = lMax * powf(lMin / lMax, t);
			l *= 0.85f + 0.3f * uni(rng);
			const float r = l / set.PeakWavelength;
			// 마루보다 짧으면 진폭 ∝ r^1.5 (경사는 천천히 줄어듦), 길면 빠르게 줄어듦
			float amp = r < 1.0f ? aPeak * powf(r, 1.5f) : aPeak * expf(-2.5f * (r - 1.0f));
			if (!(amp > 0.0f))
				continue;
			Wave w;
			const float spread = XMConvertToRadians(p.Spread) * (0.75f + 0.6f * t);   // 잔 파도일수록 넓게 퍼진다 (긴 파도도 너무 나란하지 않게)
			const float a = wind + (uni(rng) * 2.0f - 1.0f) * spread;
			w.DirX = cosf(a);
			w.DirZ = sinf(a);
			w.K = XM_2PI / l;
			w.Amplitude = (std::min)(amp, 0.12f / w.K);   // 경사 kA ≤ 0.12 (부서지지 않게)
			w.Omega = sqrtf(kGravity * w.K);
			w.Phase = uni(rng) * XM_2PI;
			set.Waves.push_back(w);
			set.MaxAmplitude += w.Amplitude;
		}
		// 뾰족함: Σ Q·k·A = Choppiness (≤ 1 이면 마루가 고리처럼 꼬이지 않는다)
		const int count = (int)set.Waves.size();
		for (Wave& w : set.Waves)
			w.Q = count > 0 ? std::clamp(p.Choppiness, 0.0f, 1.0f) / (w.K * w.Amplitude * count + 1e-6f) : 0.0f;
		return set;
	}

	float Time()
	{
		static const auto s_Start = std::chrono::steady_clock::now();
		return std::chrono::duration<float>(std::chrono::steady_clock::now() - s_Start).count();
	}

	float Height(const Set& set, float x, float z, float time, Vec3* normal)
	{
		// 가로 이동을 되짚기: 점 p0 의 파도가 (x, z) 로 옮겨지도록 p0 를 찾는다
		float px = x, pz = z;
		for (int it = 0; it < 4; ++it)
		{
			float dx = 0, dz = 0;
			for (const Wave& w : set.Waves)
			{
				const float th = w.K * (w.DirX * px + w.DirZ * pz) - w.Omega * time + w.Phase;
				const float c = cosf(th) * w.Q * w.Amplitude;
				dx += w.DirX * c;
				dz += w.DirZ * c;
			}
			px = x - dx;
			pz = z - dz;
		}
		float h = 0, nx = 0, nz = 0, ny = 1;
		for (const Wave& w : set.Waves)
		{
			const float th = w.K * (w.DirX * px + w.DirZ * pz) - w.Omega * time + w.Phase;
			const float s = sinf(th), c = cosf(th);
			h += w.Amplitude * s;
			const float wa = w.K * w.Amplitude;
			nx -= w.DirX * wa * c;
			nz -= w.DirZ * wa * c;
			ny -= w.Q * wa * s;
		}
		if (normal)
		{
			*normal = Vec3(nx, ny, nz);
			normal->Normalize();
		}
		return h;
	}
}
