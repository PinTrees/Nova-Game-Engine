#include "pch.h"
#include "WorldGen.h"
#include <cmath>

namespace WorldGen
{
	namespace
	{
		inline uint32_t Hash(int x, int z, int seed)
		{
			uint32_t h = (uint32_t)x * 0x27d4eb2dU ^ (uint32_t)z * 0x165667b1U ^ (uint32_t)seed * 0x9e3779b9U;
			h ^= h >> 15; h *= 0x85ebca6bU;
			h ^= h >> 13; h *= 0xc2b2ae35U;
			h ^= h >> 16;
			return h;
		}
		// 격자점의 기울기 (8 방향)
		inline float Grad(int x, int z, int seed, float dx, float dz)
		{
			static const float kG[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 0.7071f, 0.7071f }, { -0.7071f, 0.7071f }, { 0.7071f, -0.7071f }, { -0.7071f, -0.7071f } };
			const float* g = kG[Hash(x, z, seed) & 7];
			return g[0] * dx + g[1] * dz;
		}
		inline float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
		inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
		inline float Smooth(float e0, float e1, float x)
		{
			const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}
		// 리지 fBm (산등성이): 0 ~ 1
		float Ridged(int seed, float x, float z, int octaves)
		{
			float sum = 0.0f, amp = 0.5f, freq = 1.0f, weight = 1.0f, norm = 0.0f;
			for (int o = 0; o < octaves; ++o)
			{
				float n = 1.0f - fabsf(Noise(seed + o * 131, x * freq, z * freq));
				n *= n;
				n *= weight;
				weight = std::clamp(n * 2.0f, 0.0f, 1.0f);
				sum += n * amp;
				norm += amp;
				amp *= 0.5f;
				freq *= 2.03f;
			}
			return sum / norm;
		}
	}

	float Noise(int seed, float x, float z)
	{
		const float fx = floorf(x), fz = floorf(z);
		const int ix = (int)fx, iz = (int)fz;
		const float dx = x - fx, dz = z - fz;
		const float u = Fade(dx), v = Fade(dz);
		const float n00 = Grad(ix, iz, seed, dx, dz);
		const float n10 = Grad(ix + 1, iz, seed, dx - 1.0f, dz);
		const float n01 = Grad(ix, iz + 1, seed, dx, dz - 1.0f);
		const float n11 = Grad(ix + 1, iz + 1, seed, dx - 1.0f, dz - 1.0f);
		return Lerp(Lerp(n00, n10, u), Lerp(n01, n11, u), v) * 1.41421356f;
	}

	float Fbm(int seed, float x, float z, int octaves, float lacunarity, float gain)
	{
		float sum = 0.0f, amp = 1.0f, norm = 0.0f;
		for (int o = 0; o < octaves; ++o)
		{
			sum += Noise(seed + o * 977, x, z) * amp;
			norm += amp;
			amp *= gain;
			x = x * lacunarity + 17.13f;
			z = z * lacunarity - 9.71f;
		}
		return sum / norm;
	}

	uint64_t Settings::Hash() const
	{
		uint64_t h = 1469598103934665603ull;
		auto mix = [&](float f) { uint32_t u; memcpy(&u, &f, 4); h = (h ^ u) * 1099511628211ull; };
		h = (h ^ (uint32_t)Seed) * 1099511628211ull;
		for (float f : { WorldSize, MaxHeight, ContinentScale, MountainScale, Mountains, HillScale, Hills, DetailScale, Detail, Warp, BiomeScale, Desert, Forest })
			mix(f);
		return h;
	}

	float Height01(const Settings& s, float x, float z)
	{
		// 도메인 휨: 큰 주파수의 노이즈 두 개로 좌표를 민다 (산맥 · 해안이 곧게 늘어서지 않게)
		const float wscale = s.ContinentScale * 0.35f;
		const float wx = x + s.Warp * wscale * Fbm(s.Seed + 11, x / wscale, z / wscale, 3);
		const float wz = z + s.Warp * wscale * Fbm(s.Seed + 23, x / wscale + 5.2f, z / wscale + 1.3f, 3);
		const float c = 0.5f + 0.5f * Fbm(s.Seed + 1, wx / s.ContinentScale, wz / s.ContinentScale, 4);
		// 산맥: 대륙이 높은 곳 + 넓은 마스크 노이즈
		const float maskN = 0.5f + 0.5f * Fbm(s.Seed + 3, wx / (s.ContinentScale * 0.6f), wz / (s.ContinentScale * 0.6f), 3);
		const float mountainMask = Smooth(0.48f, 0.78f, c * 0.6f + maskN * 0.6f - 0.1f);
		const float ridge = Ridged(s.Seed + 5, wx / s.MountainScale, wz / s.MountainScale, 6);
		const float hills = 0.5f + 0.5f * Fbm(s.Seed + 7, wx / s.HillScale, wz / s.HillScale, 5);
		const float detail = Fbm(s.Seed + 9, x / s.DetailScale, z / s.DetailScale, 3);
		float h = 0.06f + 0.2f * c;
		h += s.Mountains * mountainMask * ridge * 0.72f;
		h += s.Hills * hills * 0.12f * (1.0f - 0.6f * mountainMask);
		h += s.Detail * detail * (0.4f + mountainMask);
		// 월드 가장자리: 천천히 낮아진다 (바깥이 절벽이 되지 않게)
		const float half = s.WorldSize * 0.5f;
		const float edge = (std::max)(fabsf(x), fabsf(z)) / half;
		h *= 1.0f - 0.7f * Smooth(0.9f, 1.0f, edge);
		return std::clamp(h, 0.0f, 1.0f);
	}

	void Normal(const Settings& s, float x, float z, float eps, float& nx, float& ny, float& nz)
	{
		const float hl = Height(s, x - eps, z), hr = Height(s, x + eps, z);
		const float hd = Height(s, x, z - eps), hu = Height(s, x, z + eps);
		float ax = -(hr - hl) / (2.0f * eps), az = -(hu - hd) / (2.0f * eps), ay = 1.0f;
		const float len = sqrtf(ax * ax + ay * ay + az * az);
		nx = ax / len; ny = ay / len; nz = az / len;
	}

	Biome BiomeAt(const Settings& s, float x, float z, float height01, float slopeCos)
	{
		const float t = 0.5f + 0.5f * Fbm(s.Seed + 101, x / s.BiomeScale, z / s.BiomeScale, 4) - height01 * 0.55f;   // 온도
		const float m = 0.5f + 0.5f * Fbm(s.Seed + 202, x / (s.BiomeScale * 0.8f) + 7.7f, z / (s.BiomeScale * 0.8f) - 3.1f, 4);   // 습도
		Biome b;
		b.Desert = Smooth(0.1f, 0.28f, t - m + (s.Desert - 0.3f) * 0.8f);
		b.Forest = (1.0f - b.Desert) * Smooth(0.42f, 0.58f, m + (s.Forest - 0.55f) * 0.8f);
		b.Meadow = (std::max)(0.0f, 1.0f - b.Desert - b.Forest);
		// 바위: 가파른 경사 (30 도 넘게) · 높은 곳
		const float rock = (std::max)(Smooth(0.86f, 0.74f, slopeCos), Smooth(0.62f, 0.78f, height01));
		b.Forest *= 1.0f - rock;
		b.Meadow *= 1.0f - rock;
		b.Desert *= 1.0f - rock;
		b.Rock = rock;
		return b;
	}

	void Splat(const Biome& b, float x, float z, int seed, uint8_t out[4])
	{
		// 같은 바이옴 안에서도 얼룩 (숲 바닥에 이끼 · 초원에 흙길)
		const float n = 0.5f + 0.5f * Noise(seed + 303, x / 23.0f, z / 23.0f);
		float w[4];
		w[0] = b.Meadow * (0.75f + 0.25f * n) + b.Forest * 0.3f * n;
		w[1] = b.Forest * (1.0f - 0.3f * n) + b.Meadow * 0.25f * (1.0f - n);
		w[2] = b.Rock;
		w[3] = b.Desert;
		const float sum = (std::max)(w[0] + w[1] + w[2] + w[3], 1e-5f);
		int total = 0;
		for (int i = 0; i < 4; ++i)
		{
			out[i] = (uint8_t)std::clamp((int)(w[i] / sum * 255.0f + 0.5f), 0, 255);
			total += out[i];
		}
		// 합 255 로 (반올림 오차는 가장 큰 칸에)
		int big = 0;
		for (int i = 1; i < 4; ++i)
			if (out[i] > out[big]) big = i;
		out[big] = (uint8_t)std::clamp((int)out[big] + (255 - total), 0, 255);
	}
}
