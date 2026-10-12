#include "pch.h"
#include "TerrainTextureGen.h"
#include "TerrainGraphNoise.h"
#include "JobSystem.h"

namespace N = TerrainGraphNoise;

namespace TerrainTextureGen
{
	const char* KindName(Kind k)
	{
		static const char* kNames[] = { "Grass", "Dirt", "Rock", "Snow", "Sand" };
		return kNames[std::clamp((int)k, 0, (int)Kind::Count - 1)];
	}

	Kind KindFromName(const std::string& raw)
	{
		std::string n = raw;
		std::transform(n.begin(), n.end(), n.begin(), ::tolower);
		if (n.find("rock") != std::string::npos || n.find("cliff") != std::string::npos || n.find("stone") != std::string::npos) return Kind::Rock;
		if (n.find("snow") != std::string::npos || n.find("ice") != std::string::npos) return Kind::Snow;
		if (n.find("sand") != std::string::npos || n.find("desert") != std::string::npos) return Kind::Sand;
		if (n.find("dirt") != std::string::npos || n.find("soil") != std::string::npos || n.find("mud") != std::string::npos || n.find("gravel") != std::string::npos) return Kind::Dirt;
		return Kind::Grass;
	}

	namespace
	{
		inline float Sat(float v) { return std::clamp(v, 0.0f, 1.0f); }
		inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
		inline float Smooth(float a, float b, float x) { const float t = Sat((x - a) / (b - a)); return t * t * (3 - 2 * t); }
		inline int Wrap(int v, int p) { return ((v % p) + p) % p; }
		inline float R(uint32_t h) { return N::Rand01(h); }
		struct Rgb { float R, G, B; };
		inline Rgb Mix(const Rgb& a, const Rgb& b, float t) { return { Lerp(a.R, b.R, t), Lerp(a.G, b.G, t), Lerp(a.B, b.B, t) }; }
		inline Rgb Mul(const Rgb& a, float k) { return { a.R * k, a.G * k, a.B * k }; }

		// 주기 기울기 노이즈 (period 칸마다 반복 — 텍스처 가장자리가 이어진다)
		float PNoise(float x, float y, int periodX, int periodY, uint32_t seed)
		{
			const int ix = (int)floorf(x), iy = (int)floorf(y);
			const float fx = x - ix, fy = y - iy;
			auto grad = [&](int gx, int gy, float dx, float dy) {
				const float a = R(N::Hash(Wrap(gx, periodX), Wrap(gy, periodY), seed)) * 6.2831853f;
				return cosf(a) * dx + sinf(a) * dy;
			};
			const float u = fx * fx * fx * (fx * (fx * 6 - 15) + 10), v = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
			const float a = grad(ix, iy, fx, fy), b = grad(ix + 1, iy, fx - 1, fy), c = grad(ix, iy + 1, fx, fy - 1), d = grad(ix + 1, iy + 1, fx - 1, fy - 1);
			return ((a + (b - a) * u) * (1 - v) + (c + (d - c) * u) * v) * 1.41421356f;
		}
		// 주기 fBm: u, v = 0..1 (텍스처 좌표, 감싼 좌표도 된다), base = 첫 옥타브 칸 수
		float PFbm(float u, float v, int base, int octaves, uint32_t seed, float gain = 0.5f)
		{
			float sum = 0.0f, amp = 1.0f, norm = 0.0f;
			int p = base;
			for (int o = 0; o < octaves; ++o)
			{
				sum += amp * PNoise(u * p, v * p, p, p, seed + (uint32_t)o * 131u);
				norm += amp;
				amp *= gain;
				p *= 2;
			}
			return sum / norm;
		}
		// 주기 리지 (날카로운 등성이 0..1)
		float PRidged(float u, float v, int base, int octaves, uint32_t seed)
		{
			float sum = 0.0f, amp = 1.0f, norm = 0.0f, w = 1.0f;
			int p = base;
			for (int o = 0; o < octaves; ++o)
			{
				float n = 1.0f - fabsf(PNoise(u * p, v * p, p, p, seed + (uint32_t)o * 173u));
				n *= n;
				sum += amp * n * w;
				w = Sat(n * 1.5f);
				norm += amp;
				amp *= 0.5f;
				p *= 2;
			}
			return sum / norm;
		}
		// 주기 보로노이: 가장 가까운 두 점 (거리 · 칸 아이디 · 점 위치)
		struct Cell { float F1 = 9.0f, F2 = 9.0f; uint32_t Id = 0; float Px = 0.0f, Py = 0.0f; };
		Cell PVoronoi(float u, float v, int cells, uint32_t seed, float jitter = 0.9f)
		{
			const float x = u * cells, y = v * cells;
			const int ix = (int)floorf(x), iy = (int)floorf(y);
			Cell c;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int cx = ix + dx, cy = iy + dy;
					const uint32_t h = N::Hash(Wrap(cx, cells), Wrap(cy, cells), seed);
					const float px = cx + 0.5f + (R(h) - 0.5f) * jitter, py = cy + 0.5f + (R(h * 747796405u + 1u) - 0.5f) * jitter;
					const float d = sqrtf((px - x) * (px - x) + (py - y) * (py - y));
					if (d < c.F1) { c.F2 = c.F1; c.F1 = d; c.Id = h; c.Px = px - x; c.Py = py - y; }
					else if (d < c.F2) c.F2 = d;
				}
			return c;
		}

		// 풀잎: 칸마다 잎 하나 (뿌리 · 방향 · 길이) — 점에서 가장 가까운 잎 줄기까지 거리. 돌려주는 값: 잎 높이 (뿌리 0 → 끝 1), 아이디
		bool Blade(float u, float v, int cells, uint32_t seed, float& along, uint32_t& id, float& cover)
		{
			const float x = u * cells, y = v * cells;
			const int ix = (int)floorf(x), iy = (int)floorf(y);
			cover = 0.0f;
			bool hit = false;
			for (int dy = -2; dy <= 2; ++dy)
				for (int dx = -2; dx <= 2; ++dx)
				{
					const int cx = ix + dx, cy = iy + dy;
					const uint32_t h = N::Hash(Wrap(cx, cells), Wrap(cy, cells), seed);
					const float rx = cx + R(h), ry = cy + R(h * 3u + 7u);
					// 위에서 본 풀: 잎이 여러 방향으로 눕는다 (조금 한쪽으로 — 바람 결)
					const float ang = (R(h * 5u + 11u) - 0.5f) * 3.6f + 0.6f;
					const float len = 1.1f + R(h * 9u + 3u) * 1.4f;
					const float wid = 0.10f + R(h * 13u + 5u) * 0.07f;
					const float dxr = cosf(ang), dyr = sinf(ang);
					const float px = x - rx, py = y - ry;
					const float t = px * dxr + py * dyr;
					if (t < 0.0f || t > len)
						continue;
					const float s = t / len;
					// 살짝 휜 잎 (끝으로 갈수록 옆으로)
					const float bend = (R(h * 17u + 1u) - 0.5f) * 0.6f * s * s;
					const float side = fabsf(-px * dyr + py * dxr - bend);
					const float w = wid * (1.0f - s * 0.85f);
					if (side > w)
						continue;
					const float c = 1.0f - side / w;
					// 겹치면 더 위 (끝에 가까운) 잎
					const float top = s * 0.7f + R(h * 19u) * 0.3f;
					if (!hit || top > along)
					{
						along = top;
						id = h;
						hit = true;
					}
					cover = (std::max)(cover, c);
				}
			return hit;
		}

		// 감싼 상자 흐림 (가로 · 세로) — 오목한 곳 (cavity) 어림
		std::vector<float> BlurWrap(const std::vector<float>& src, int size, int r)
		{
			std::vector<float> tmp(src.size()), out(src.size());
			const float inv = 1.0f / (2 * r + 1);
			Jobs::ParallelFor(size, 16, [&](int y0, int y1) {
				for (int y = y0; y < y1; ++y)
				{
					float acc = 0.0f;
					for (int k = -r; k <= r; ++k)
						acc += src[(size_t)y * size + Wrap(k, size)];
					for (int x = 0; x < size; ++x)
					{
						tmp[(size_t)y * size + x] = acc * inv;
						acc += src[(size_t)y * size + Wrap(x + r + 1, size)] - src[(size_t)y * size + Wrap(x - r, size)];
					}
				}
			}, "TerrainTextureGen Blur");
			Jobs::ParallelFor(size, 16, [&](int x0, int x1) {
				for (int x = x0; x < x1; ++x)
				{
					float acc = 0.0f;
					for (int k = -r; k <= r; ++k)
						acc += tmp[(size_t)Wrap(k, size) * size + x];
					for (int y = 0; y < size; ++y)
					{
						out[(size_t)y * size + x] = acc * inv;
						acc += tmp[(size_t)Wrap(y + r + 1, size) * size + x] - tmp[(size_t)Wrap(y - r, size) * size + x];
					}
				}
			}, "TerrainTextureGen Blur");
			return out;
		}
	}

	Result Generate(Kind kind, int size, int seed)
	{
		Result r;
		size = std::clamp(size, 64, 4096);
		r.Size = size;
		const size_t count = (size_t)size * size;
		std::vector<float> height(count);
		std::vector<Rgb> col(count);
		const uint32_t s = (uint32_t)seed * 2654435761u + (uint32_t)kind * 977u;
		// 오목한 곳을 얼마나 어둡게 (높이 맵에서 굽는 AO) · 노멀 세기 배수
		float cavityDark = 0.4f, normalBoost = 1.0f;
		switch (kind)
		{
		case Kind::Grass: r.TileSize = 3.0f; r.NormalScale = 1.0f; r.HeightAmplitude = 0.04f; cavityDark = 0.55f; break;
		case Kind::Dirt: r.TileSize = 4.0f; r.NormalScale = 1.0f; r.HeightAmplitude = 0.05f; cavityDark = 0.45f; break;
		case Kind::Rock: r.TileSize = 10.0f; r.NormalScale = 1.0f; r.HeightAmplitude = 0.3f; cavityDark = 0.5f; normalBoost = 0.8f; break;
		case Kind::Snow: r.TileSize = 8.0f; r.NormalScale = 0.8f; r.HeightAmplitude = 0.06f; cavityDark = 0.15f; break;
		case Kind::Sand: r.TileSize = 6.0f; r.NormalScale = 0.9f; r.HeightAmplitude = 0.03f; cavityDark = 0.2f; break;
		default: break;
		}
		Jobs::ParallelFor(size, 8, [&](int y0, int y1) {
			for (int y = y0; y < y1; ++y)
				for (int x = 0; x < size; ++x)
				{
					const float u = (x + 0.5f) / size, v = (y + 0.5f) / size;
					const size_t i = (size_t)y * size + x;
					float h = 0.0f;
					Rgb c = { 0.5f, 0.5f, 0.5f };
					switch (kind)
					{
					case Kind::Grass:
					{
						// 바닥 (마른 잎 · 흙) 위에 풀잎 두 겹 — 잎마다 색 (진한 · 연한 · 마른 노란 잎), 끝이 밝다
						const float patch = PFbm(u, v, 2, 4, s);
						const float dryZone = Smooth(0.1f, 0.55f, PFbm(u, v, 3, 3, s + 7u));
						const float thatchN = PFbm(u, v, 48, 4, s + 9u);
						const Rgb soil = Mix({ 0.13f, 0.10f, 0.055f }, { 0.24f, 0.19f, 0.11f }, Sat(0.5f + thatchN));
						c = soil;
						h = 0.12f + thatchN * 0.08f;
						for (int layer = 0; layer < 3; ++layer)
						{
							const int cells = layer == 0 ? 70 : (layer == 1 ? 110 : 160);
							float along = 0.0f, cover = 0.0f;
							uint32_t id = 0;
							if (!Blade(u, v, cells, s + 101u * (layer + 1), along, id, cover))
								continue;
							const float bh = 0.3f + layer * 0.22f + along * 0.35f;
							if (bh < h)
								continue;
							h = bh;
							const float hue = R(id * 23u);
							const bool dry = R(id * 29u) < 0.08f + dryZone * 0.22f;
							Rgb g = Mix({ 0.10f, 0.21f, 0.035f }, { 0.25f, 0.40f, 0.08f }, hue);
							g = Mix(g, { 0.30f, 0.30f, 0.08f }, Sat(patch * 0.6f + 0.2f) * 0.35f);
							if (dry)
								g = Mix({ 0.38f, 0.32f, 0.14f }, { 0.52f, 0.45f, 0.22f }, hue);
							// 끝이 밝고 (햇빛) 뿌리 쪽은 그늘, 잎 가운데 결이 밝다
							c = Mul(g, (0.55f + along * 0.65f) * (0.85f + cover * 0.25f));
						}
						break;
					}
					case Kind::Dirt:
					{
						// 고운 흙 + 자잘한 자갈 + 큰 돌 몇 개 + 축축한 얼룩
						const float wu = u + PFbm(u, v, 4, 3, s + 1u) * 0.02f, wv = v + PFbm(u + 0.37f, v, 4, 3, s + 2u) * 0.02f;
						const float soilN = PFbm(wu, wv, 6, 7, s, 0.55f);
						const float fine = PFbm(u, v, 128, 2, s + 3u);
						const float damp = Smooth(-0.1f, 0.35f, PFbm(u, v, 2, 4, s + 4u));
						h = 0.3f + soilN * 0.18f + fine * 0.04f;
						c = Mix({ 0.18f, 0.12f, 0.075f }, { 0.36f, 0.27f, 0.17f }, Sat(0.5f + soilN * 0.9f + fine * 0.3f));
						c = Mul(c, 1.0f - damp * 0.35f);
						for (int layer = 0; layer < 2; ++layer)
						{
							const int cells = layer == 0 ? 46 : 14;
							const Cell vc = PVoronoi(wu, wv, cells, s + 31u + layer * 17u);
							if (R(vc.Id * 3u + 1u) > (layer == 0 ? 0.26f : 0.14f))
								continue;
							const float radius = (layer == 0 ? 0.22f : 0.3f) + 0.18f * R(vc.Id * 5u);
							const float edge = vc.F1 + PFbm(u, v, cells * 2, 2, s + 41u) * 0.08f;
							if (edge > radius)
								continue;
							const float dome = sqrtf(Sat(1.0f - (edge / radius) * (edge / radius)));
							const float sh = 0.42f + dome * (layer == 0 ? 0.3f : 0.5f);
							if (sh < h)
								continue;
							h = sh;
							// 흙에 묻은 돌: 회갈색 (흙 색을 반쯤 띤다), 하나하나 밝기가 다르다
							const float g = 0.10f + 0.14f * R(vc.Id * 7u);
							Rgb stone = { g * 1.08f, g * 1.0f, g * 0.9f };
							stone = Mix(stone, c, 0.25f + R(vc.Id * 11u) * 0.35f);
							c = Mul(stone, 0.75f + dome * 0.35f + PFbm(u, v, 96, 2, s + 43u) * 0.15f);
						}
						break;
					}
					case Kind::Rock:
					{
						// 깨진 면 (보로노이 칸마다 기운 평면) + 휜 층리 + 금 (두 크기, 군데군데 끊긴다) + 거친 결 + 이끼 · 지의류
						const float wu = u + PFbm(u, v, 3, 4, s + 1u) * 0.06f, wv = v + PFbm(u + 0.5f, v, 3, 4, s + 2u) * 0.06f;
						const Cell big = PVoronoi(wu, wv, 5, s + 4u);
						const float fa = R(big.Id * 3u) * 6.2831853f, fs = 0.25f + R(big.Id * 7u) * 0.35f;
						const float facet = 0.5f + (-big.Px * cosf(fa) - big.Py * sinf(fa)) * fs + (R(big.Id * 11u) - 0.5f) * 0.3f;
						const Cell mid = PVoronoi(wu, wv, 13, s + 5u);
						const float ma = R(mid.Id * 3u) * 6.2831853f;
						const float facet2 = (-mid.Px * cosf(ma) - mid.Py * sinf(ma)) * 0.2f;
						const float strataWarp = PFbm(u, v, 2, 4, s + 6u);
						// 이음매 없게: u · v 의 계수는 정수
						const float sv = v * 5.0f + u * 1.0f + strataWarp * 1.0f;
						const float strata = sv - floorf(sv);
						const float ledge = Smooth(0.0f, 0.15f, strata) * (1.0f - Smooth(0.85f, 1.0f, strata));
						const float ridged = PRidged(wu, wv, 4, 5, s + 7u);
						const float grain = PFbm(u, v, 64, 4, s + 8u, 0.6f);
						const float gateA = Smooth(-0.05f, 0.2f, PFbm(u, v, 6, 2, s + 9u));
						const float gateB = Smooth(0.0f, 0.25f, PFbm(u, v, 10, 2, s + 10u));
						const float crackA = Smooth(0.035f, 0.0f, big.F2 - big.F1) * gateA;
						const float crackB = Smooth(0.03f, 0.0f, mid.F2 - mid.F1) * gateB * 0.7f;
						const float crack = (std::max)(crackA, crackB);
						h = facet * 0.38f + facet2 * 0.35f + ledge * 0.06f + ridged * 0.2f + grain * 0.06f - crack * 0.35f;
						// 색: 회색 바위에 따뜻한 · 차가운 얼룩, 층마다 조금 다르게
						const float tone = Sat(0.5f + PFbm(wu, wv, 3, 5, s + 12u) * 1.1f + grain * 0.35f + (facet - 0.5f) * 0.5f);
						c = Mix({ 0.20f, 0.19f, 0.18f }, { 0.46f, 0.43f, 0.39f }, tone);
						c = Mul(c, 0.95f + ledge * 0.07f);
						c = Mix(c, { c.R * 1.12f, c.G * 1.0f, c.B * 0.86f }, R(big.Id * 13u) * 0.6f);
						// 지의류 (노르스름 · 연두 점 무리)
						const Cell lc = PVoronoi(u, v, 40, s + 14u, 1.0f);
						const float lichenZone = Smooth(0.15f, 0.45f, PFbm(u, v, 3, 3, s + 15u));
						const float lichen = Smooth(0.38f, 0.2f, lc.F1 + PFbm(u, v, 80, 2, s + 16u) * 0.15f) * lichenZone * (R(lc.Id) < 0.5f ? 1.0f : 0.0f);
						c = Mix(c, R(lc.Id * 3u) < 0.6f ? Rgb{ 0.42f, 0.40f, 0.20f } : Rgb{ 0.25f, 0.30f, 0.14f }, lichen * 0.75f);
						c = Mul(c, 1.0f - crack * 0.6f);
						break;
					}
					case Kind::Snow:
					{
						// 바람 결 (사스트루기) + 눈더미 + 잔 결정 반짝임
						const float drift = PFbm(u, v, 2, 6, s, 0.55f);
						const float warp = PFbm(u, v, 3, 3, s + 3u);
						const float rv = u * 8.0f + v * 22.0f + warp * 4.0f;
						const float ripple = powf(Sat(0.5f + 0.5f * sinf(rv * 6.2831853f)), 3.0f) * Smooth(-0.2f, 0.4f, PFbm(u, v, 4, 2, s + 5u));
						const float fine = PFbm(u, v, 96, 3, s + 9u);
						h = 0.5f + drift * 0.35f + ripple * 0.12f + fine * 0.03f;
						const float shade = Sat(0.55f + drift * 0.7f + ripple * 0.15f);
						c = Mix({ 0.66f, 0.72f, 0.82f }, { 0.93f, 0.95f, 0.98f }, shade);
						if (R(N::Hash(x, y, s + 21u)) > 0.9975f)
							c = { 1.0f, 1.0f, 1.0f };
						break;
					}
					case Kind::Sand:
					default:
					{
						const float warp = PFbm(u, v, 2, 3, s + 1u);
						const float rv = u * 11.0f + v * 18.0f + warp * 4.5f;
						const float ripple = powf(Sat(0.5f + 0.5f * sinf(rv * 6.2831853f)), 1.5f);
						const float grain = PFbm(u, v, 128, 2, s + 3u);
						h = 0.45f + ripple * 0.3f + grain * 0.08f;
						c = Mix({ 0.50f, 0.40f, 0.26f }, { 0.74f, 0.62f, 0.43f }, Sat(0.5f + PFbm(u, v, 3, 4, s + 5u) * 0.6f + grain * 0.4f));
						c = Mul(c, 0.9f + ripple * 0.12f);
						if (R(N::Hash(x, y, s + 9u)) > 0.985f)
							c = Mul(c, 0.55f + R(N::Hash(y, x, s + 11u)) * 0.7f);   // 다른 색 알갱이
						break;
					}
					}
					height[i] = Sat(h);
					col[i] = { Sat(c.R), Sat(c.G), Sat(c.B) };
				}
		}, "TerrainTextureGen");

		// 오목한 곳 (흐린 높이보다 낮은 곳) 을 어둡게 — 텍스처에 구운 AO
		const std::vector<float> blurred = BlurWrap(height, size, (std::max)(2, size / 128));
		// 출력: 알베도 (선형 → sRGB), 노멀 (실제 크기의 기울기: 높이 범위 / 픽셀 크기), 높이
		r.Albedo.resize(count * 4);
		r.Normal.resize(count * 4);
		r.Height.resize(count * 4);
		const float strength = r.HeightAmplitude * size / (std::max)(0.01f, r.TileSize) * normalBoost;
		Jobs::ParallelFor(size, 16, [&](int y0, int y1) {
			for (int y = y0; y < y1; ++y)
				for (int x = 0; x < size; ++x)
				{
					const size_t i = (size_t)y * size + x;
					auto H = [&](int xx, int yy) { return height[(size_t)Wrap(yy, size) * size + Wrap(xx, size)]; };
					// 소벨 (3 x 3) — 한 픽셀 튐에 덜 흔들린다
					const float dx = ((H(x + 1, y - 1) + 2 * H(x + 1, y) + H(x + 1, y + 1)) - (H(x - 1, y - 1) + 2 * H(x - 1, y) + H(x - 1, y + 1))) * 0.125f * strength;
					const float dy = ((H(x - 1, y + 1) + 2 * H(x, y + 1) + H(x + 1, y + 1)) - (H(x - 1, y - 1) + 2 * H(x, y - 1) + H(x + 1, y - 1))) * 0.125f * strength;
					const float inv = 1.0f / sqrtf(dx * dx + dy * dy + 1.0f);
					const float nx = -dx * inv, ny = dy * inv, nz = inv;   // +Y 위 (그림의 위로 갈수록 v 감소)
					const float cavity = Sat((blurred[i] - height[i]) * 6.0f);
					const float ao = 1.0f - cavity * cavityDark;
					const float cc[3] = { col[i].R * ao, col[i].G * ao, col[i].B * ao };
					uint8_t* a = &r.Albedo[i * 4];
					for (int k = 0; k < 3; ++k)
						a[k] = (uint8_t)std::clamp((int)(powf(Sat(cc[k]), 1.0f / 2.2f) * 255.0f + 0.5f), 0, 255);
					a[3] = 255;
					uint8_t* n = &r.Normal[i * 4];
					n[0] = (uint8_t)std::clamp((int)((nx * 0.5f + 0.5f) * 255.0f + 0.5f), 0, 255);
					n[1] = (uint8_t)std::clamp((int)((ny * 0.5f + 0.5f) * 255.0f + 0.5f), 0, 255);
					n[2] = (uint8_t)std::clamp((int)((nz * 0.5f + 0.5f) * 255.0f + 0.5f), 0, 255);
					n[3] = 255;
					uint8_t* hp = &r.Height[i * 4];
					hp[0] = hp[1] = hp[2] = (uint8_t)std::clamp((int)(height[i] * 255.0f + 0.5f), 0, 255);
					hp[3] = 255;
				}
		}, "TerrainTextureGen Normal");
		return r;
	}
}
