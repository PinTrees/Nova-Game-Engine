#pragma once
#include <cmath>
#include <cstdint>

// 지형 그래프의 노이즈 (TerrainGraph.cpp · 빌드 타일 디테일이 같이 쓴다): 좌표는 double (50 km 끝에서도 1 m 결이 흔들리지 않게)
namespace TerrainGraphNoise
{
	inline uint32_t Hash(int x, int z, uint32_t seed)
	{
		uint32_t h = (uint32_t)x * 0x27d4eb2dU ^ (uint32_t)z * 0x165667b1U ^ seed * 0x9e3779b9U;
		h ^= h >> 15; h *= 0x85ebca6bU;
		h ^= h >> 13; h *= 0xc2b2ae35U;
		h ^= h >> 16;
		return h;
	}
	inline float Rand01(uint32_t h) { return (h & 0xFFFFFF) / 16777216.0f; }

	// 기울기 노이즈 + 도함수 (-1..1, 도함수는 같은 좌표 단위). 격자점 기울기 = 해시 각도
	inline float Gradient(double x, double z, uint32_t seed, float* ddx = nullptr, float* ddz = nullptr)
	{
		const double fx0 = std::floor(x), fz0 = std::floor(z);
		const int ix = (int)fx0, iz = (int)fz0;
		const float fx = (float)(x - fx0), fz = (float)(z - fz0);
		auto grad = [&](int gx, int gz, float& gxo, float& gzo) {
			const float a = Rand01(Hash(gx, gz, seed)) * 6.2831853f;
			gxo = std::cos(a); gzo = std::sin(a);
		};
		float g00x, g00z, g10x, g10z, g01x, g01z, g11x, g11z;
		grad(ix, iz, g00x, g00z); grad(ix + 1, iz, g10x, g10z); grad(ix, iz + 1, g01x, g01z); grad(ix + 1, iz + 1, g11x, g11z);
		const float va = g00x * fx + g00z * fz;
		const float vb = g10x * (fx - 1.0f) + g10z * fz;
		const float vc = g01x * fx + g01z * (fz - 1.0f);
		const float vd = g11x * (fx - 1.0f) + g11z * (fz - 1.0f);
		const float u = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
		const float v = fz * fz * fz * (fz * (fz * 6.0f - 15.0f) + 10.0f);
		const float k = va - vb - vc + vd;
		const float n = va + u * (vb - va) + v * (vc - va) + u * v * k;
		if (ddx)
		{
			const float du = 30.0f * fx * fx * (fx * (fx - 2.0f) + 1.0f);
			const float dv = 30.0f * fz * fz * (fz * (fz - 2.0f) + 1.0f);
			*ddx = (g00x + u * (g10x - g00x) + v * (g01x - g00x) + u * v * (g00x - g10x - g01x + g11x) + du * (vb - va + v * k)) * 1.41421356f;
			*ddz = (g00z + u * (g10z - g00z) + v * (g01z - g00z) + u * v * (g00z - g10z - g01z + g11z) + dv * (vc - va + u * k)) * 1.41421356f;
		}
		return n * 1.41421356f;
	}

	// fBm (-1..1)
	inline float Fbm(double x, double z, uint32_t seed, int octaves, float lacunarity = 2.0f, float gain = 0.5f)
	{
		float sum = 0.0f, amp = 1.0f, norm = 0.0f;
		double f = 1.0;
		for (int o = 0; o < octaves; ++o)
		{
			sum += amp * Gradient(x * f + o * 17.31, z * f - o * 9.73, seed + (uint32_t)o * 131u);
			norm += amp;
			amp *= gain;
			f *= lacunarity;
		}
		return sum / norm;
	}

	// 리지 (0..1): 1 - |n| 의 제곱, 앞 옥타브가 높은 곳만 다음 옥타브 (산등성이)
	inline float Ridged(double x, double z, uint32_t seed, int octaves, float gain = 0.5f, float sharpness = 2.0f)
	{
		float sum = 0.0f, amp = 0.5f, weight = 1.0f, norm = 0.0f;
		double f = 1.0;
		for (int o = 0; o < octaves; ++o)
		{
			float n = 1.0f - std::fabs(Gradient(x * f + o * 7.1, z * f + o * 3.3, seed + (uint32_t)o * 977u));
			n = std::pow((std::max)(n, 0.0f), sharpness);
			n *= weight;
			weight = (std::min)((std::max)(n * 2.0f, 0.0f), 1.0f);
			sum += n * amp;
			norm += amp;
			amp *= gain;
			f *= 2.03;
		}
		return sum / norm;
	}

	// 침식 결 fBm (Inigo Quilez — 기울기가 쌓인 곳은 다음 옥타브를 줄인다: 비탈은 매끈한 골, 평지 · 능선에 잔 결). -1..1 쯤
	//  minWavelength: 이보다 잔 옥타브는 건너뛴다 (먼 타일의 표본 간격)
	inline float Eroded(double x, double z, uint32_t seed, double scale, double minWavelength, float erosion)
	{
		float sum = 0.0f, amp = 0.5f, norm = 0.0f, dx = 0.0f, dz = 0.0f;
		double wl = scale;
		for (int o = 0; o < 14 && wl >= minWavelength; ++o)
		{
			float nx, nz;
			const float n = Gradient(x / wl + o * 13.7, z / wl - o * 5.9, seed + (uint32_t)o * 733u, &nx, &nz);
			dx += nx * amp;
			dz += nz * amp;
			sum += amp * n / (1.0f + erosion * (dx * dx + dz * dz));
			norm += amp;
			amp *= 0.5f;
			wl *= 0.5;
		}
		return norm > 0.0f ? sum / norm : 0.0f;
	}

	// ---- 침식 필터 (Clay John · Rune Skovbo Johansen 식): 비탈 방향 (기울기 gx, gz — 높이/좌표 단위) 을 따라 내려가는 골 무늬.
	//  칸마다 점 하나, 점에서 기울기에 수직인 방향의 코사인 물결 → 물결의 골이 비탈을 따라 흐른다. 옥타브마다 앞 옥타브의 골 기울기를
	//  더해 가지가 갈라진다 (프랙탈 물길). 순수 함수 (위치 · 기울기) — 타일 경계가 맞는다. 돌려주는 값 = 대략 -1..1 (골 = -)
	inline float ErosionCell(double x, double z, float dirX, float dirZ, uint32_t seed, float& dvx, float& dvz)
	{
		const double fx0 = std::floor(x), fz0 = std::floor(z);
		const int ix = (int)fx0, iz = (int)fz0;
		const float fx = (float)(x - fx0), fz = (float)(z - fz0);
		float v = 0.0f, wsum = 0.0f;
		dvx = dvz = 0.0f;
		for (int j = -1; j <= 1; ++j)
			for (int i = -1; i <= 1; ++i)
			{
				const uint32_t h = Hash(ix + i, iz + j, seed);
				const float px = fx - (float)i - Rand01(h) * 0.9f - 0.05f;
				const float pz = fz - (float)j - Rand01(h * 747796405u + 3u) * 0.9f - 0.05f;
				const float d2 = px * px + pz * pz;
				const float w = std::exp(-d2 * 2.0f);
				const float mag = (px * dirX + pz * dirZ) * 6.2831853f;
				const float c = std::cos(mag), sn = std::sin(mag);
				v += c * w;
				dvx += -sn * dirX * 6.2831853f * w;
				dvz += -sn * dirZ * 6.2831853f * w;
				wsum += w;
			}
		const float inv = 1.0f / (std::max)(wsum, 1e-6f);
		dvx *= inv; dvz *= inv;
		return v * inv;
	}
	// scale = 가장 큰 골 간격 (좌표 단위, 보통 m), slopeX/Z = 바탕 기울기 (높이 / 좌표 — m/m), depth = 골 깊이 (높이 단위 — 골의 옆 비탈이 다음
	//  옥타브의 방향을 바꾼다 = 가지), minWavelength 보다 잔 옥타브는 건너뛴다. 돌려주는 값 -1..1 (골 -, 등성이 +)
	inline float ErosionFilter(double x, double z, float slopeX, float slopeZ, uint32_t seed, double scale, int octaves, double minWavelength,
		float depth, float branch = 1.0f)
	{
		float sum = 0.0f, amp = 1.0f, norm = 0.0f;
		float gx = slopeX, gz = slopeZ;
		double wl = scale;
		for (int o = 0; o < octaves && wl >= minWavelength; ++o)
		{
			// 기울기에 수직 (물결의 마루 · 골이 비탈을 따라 나란히 내려간다)
			const float len = std::sqrt(gx * gx + gz * gz) + 1e-6f;
			const float dirX = gz / len, dirZ = -gx / len;
			float dvx, dvz;
			const float e = ErosionCell(x / wl, z / wl, dirX, dirZ, seed + (uint32_t)o * 6151u, dvx, dvz);
			sum += e * amp;
			norm += amp;
			// 다음 옥타브는 이 골의 옆 비탈도 따른다 (가지): 골의 기울기 = 깊이 × 진폭 × dv / 파장
			gx += dvx * amp * depth * branch / (float)wl;
			gz += dvz * amp * depth * branch / (float)wl;
			amp *= 0.5f;
			wl *= 0.5;
		}
		return norm > 0.0f ? sum / norm : 0.0f;
	}

	// 스위스 난류 (Giliam de Carpentier): 앞 옥타브 도함수로 다음 옥타브 자리를 밀고 (warp) 높은 곳에 잔 결을 몰아 —
	//  봉우리 · 능선이 프랙탈로 갈라지는 산맥. 0..1 쯤
	inline float Swiss(double x, double z, uint32_t seed, int octaves, float lacunarity = 2.0f, float gain = 0.5f, float warp = 0.15f)
	{
		float sum = 0.0f, freq = 1.0f, amp = 1.0f, dsx = 0.0f, dsz = 0.0f, norm = 0.0f;
		for (int o = 0; o < octaves; ++o)
		{
			float nx, nz;
			const float n = Gradient((x + warp * dsx) * freq + o * 11.7, (z + warp * dsz) * freq - o * 5.3, seed + (uint32_t)o * 811u, &nx, &nz);
			const float r = 1.0f - std::fabs(n);
			sum += amp * r;
			norm += amp;
			const float sgn = n < 0.0f ? -1.0f : 1.0f;
			dsx += amp * nx * -sgn;
			dsz += amp * nz * -sgn;
			freq *= lacunarity;
			amp *= gain * std::fmin(1.0f, std::fmax(0.0f, sum / norm));
		}
		return sum / (norm > 0.0f ? norm : 1.0f);
	}
}
