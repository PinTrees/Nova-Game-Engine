#include "pch.h"
#include "BuildingTextures.h"
#include "BuildingGenerator.h"
#include "UMaterial.h"
#include "EditorLog.h"
#include "ImageIO.h"
#include "TextureLibrary.h"
#include "PathManager.h"
#include <filesystem>
#include <future>
#include <thread>
#include <atomic>
#include <array>
#include <map>
#include <mutex>
#include <chrono>
#include <execution>
#include <numeric>

namespace
{
	using namespace BuildingTextures;
	constexpr int kSize = 512;

	float Saturate(float x) { return std::clamp(x, 0.0f, 1.0f); }
	float Lerp(float a, float b, float t) { return a + (b - a) * t; }
	float Smooth(float e0, float e1, float x) { const float t = Saturate((x - e0) / (e1 - e0)); return t * t * (3.0f - 2.0f * t); }
	float Frac(float x) { return x - floorf(x); }

	uint32_t HashU(int x, int y, int s)
	{
		uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2246822519u;
		h = (h ^ (h >> 13)) * 1274126177u;
		return h ^ (h >> 16);
	}
	float Hash(int x, int y, int s = 0) { return (HashU(x, y, s) & 0xFFFFFF) / 16777215.0f; }
	int Wrap(int i, int p) { const int m = i % p; return m < 0 ? m + p : m; }

	// 이어지는 (주기 px · py 칸) 값 노이즈. x · y 는 칸 단위
	float Noise(float x, float y, int px, int py, int seed)
	{
		const int ix = (int)floorf(x), iy = (int)floorf(y);
		const float fx = x - ix, fy = y - iy;
		const float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
		auto H = [&](int a, int b) { return Hash(Wrap(a, px), Wrap(b, py), seed); };
		return Lerp(Lerp(H(ix, iy), H(ix + 1, iy), ux), Lerp(H(ix, iy + 1), H(ix + 1, iy + 1), ux), uy);
	}
	// 타일 좌표 (u, v 0..1) 의 fbm — 기본 주기 fx · fy 칸, 옥타브마다 두 배
	float Fbm(float u, float v, int fx, int fy, int oct, int seed)
	{
		float sum = 0.0f, amp = 0.5f, norm = 0.0f;
		for (int o = 0; o < oct; ++o)
		{
			sum += amp * Noise(u * fx, v * fy, fx, fy, seed + o * 31);
			norm += amp;
			amp *= 0.5f;
			fx *= 2;
			fy *= 2;
		}
		return sum / norm;
	}

	// 이어지는 보로노이 (nx · ny 칸): 가장 가까운 점 거리 f1, 둘째 f2 와의 경계 거리 edge, 칸 번호
	struct Cell { float F1, Edge; int Id; float Cx, Cy; };
	Cell Voronoi(float u, float v, int nx, int ny, int seed, float stretchX = 1.0f)
	{
		const float x = u * nx, y = v * ny;
		const int ix = (int)floorf(x), iy = (int)floorf(y);
		float best = 1e9f, second = 1e9f;
		int id = 0;
		float bx = 0, by = 0;
		struct P { float x, y; };
		P pts[25];
		int n = 0;
		for (int j = -2; j <= 2; ++j)
			for (int i = -2; i <= 2; ++i)
			{
				const int cx = ix + i, cy = iy + j;
				const int wx = Wrap(cx, nx), wy = Wrap(cy, ny);
				const float px = cx + 0.15f + 0.7f * Hash(wx, wy, seed), py = cy + 0.15f + 0.7f * Hash(wx, wy, seed + 7);
				pts[n++] = { px, py };
				const float dx = (px - x) * stretchX, dy = py - y;
				const float d = sqrtf(dx * dx + dy * dy);
				if (d < best) { second = best; best = d; id = wx + wy * nx; bx = px; by = py; }
				else if (d < second) second = d;
			}
		// 경계까지 거리 (두 점의 수직 이등분선 — f2 - f1 보다 고르다)
		float edge = 1e9f;
		for (int k = 0; k < n; ++k)
		{
			const float ox = (pts[k].x - bx) * stretchX, oy = pts[k].y - by;
			const float l2 = ox * ox + oy * oy;
			if (l2 < 1e-8f)
				continue;
			const float mx = ((pts[k].x + bx) * 0.5f - x) * stretchX, my = (pts[k].y + by) * 0.5f - y;
			const float dist = (mx * ox + my * oy) / sqrtf(l2);
			edge = (std::min)(edge, dist);
		}
		(void)second;
		return { best, edge, id, bx, by };
	}

	// 텍셀 한 점의 값
	struct Sample
	{
		float H = 0.5f;               // 높이 0..1
		float R = 0.9f, G = 0.9f, B = 0.9f;   // 감마 알베도 (중성 — Base Color 를 곱한다)
		float Metal = 0.0f, Smooth = 0.3f;
		float Ao = -1.0f;             // < 0 이면 높이에서
		void Grey(float l) { R = G = B = l; }
		void Tint(float l, float warm) { R = l * (1.0f + warm); G = l; B = l * (1.0f - warm); }
	};

	struct Spec { const char* Name; float Tile; float Depth; };   // Depth = 높이 0 → 1 의 실제 깊이 (m)
	const Spec kSpecs[] = {
		{ "Rubble", 2.0f, 0.06f }, { "Ashlar", 2.4f, 0.025f }, { "Brick", 1.6f, 0.02f }, { "Plaster", 3.0f, 0.008f },
		{ "Stucco", 2.0f, 0.004f }, { "Planks", 1.6f, 0.02f }, { "Logs", 2.0f, 0.12f }, { "Marble", 3.2f, 0.01f },
		{ "Clay Tiles", 2.0f, 0.06f }, { "Slate", 1.5f, 0.02f }, { "Thatch", 2.0f, 0.08f }, { "Shingles", 1.6f, 0.025f },
		{ "Copper", 2.4f, 0.02f }, { "Lead", 2.4f, 0.025f }, { "Turf", 2.0f, 0.06f },
		{ "Timber", 1.2f, 0.01f }, { "Glass", 1.0f, 0.002f }, { "Leaded Glass", 0.6f, 0.006f }, { "Door Planks", 1.2f, 0.012f },
		{ "Metal", 1.0f, 0.004f }, { "Fabric", 1.0f, 0.002f },
		{ "Ornament", 1.2f, 0.006f }, { "Banner", 1.0f, 0.003f }, { "Foliage", 0.5f, 0.02f } };

	// 해상도: 넓게 보이는 벽 · 지붕 · 들보는 1024², 나머지 512²
	int SizeOf(Kind k)
	{
		switch (k)
		{
		case Kind::Rubble: case Kind::Ashlar: case Kind::Brick: case Kind::Plaster: case Kind::Planks: case Kind::Logs:
		case Kind::ClayTiles: case Kind::Slate: case Kind::Thatch: case Kind::Shingles: case Kind::Timber: case Kind::Ornament:
			return 1024;
		default:
			return 512;
		}
	}
	static_assert(sizeof(kSpecs) / sizeof(kSpecs[0]) == (size_t)Kind::Count, "kSpecs must cover every Kind");

	// 나뭇결 (결 방향 = along, 가로 = across): 0..1 결 무늬 · 옹이
	float Grain(float along, float across, int px, int seed)
	{
		const float warp = Fbm(along, across, px / 4, 2, 3, seed) * 6.0f;
		const float rings = 0.5f + 0.5f * sinf((across * 40.0f + warp) * XM_PI);
		const float fine = Fbm(along, across, (std::max)(1, px / 2), 64, 2, seed + 5);
		return Saturate(rings * 0.6f + fine * 0.4f);
	}

	Sample Eval(Kind k, float u, float v)
	{
		Sample s;
		switch (k)
		{
		case Kind::Rubble:
		{
			// 막돌: 가로로 긴 돌 (보로노이를 가로로 줄여서) — 돌마다 둥근 배 · 기운 면 · 거친 표면, 줄눈은 깊고 자갈 섞인 회반죽
			const Cell c = Voronoi(u + (Fbm(u, v, 8, 8, 2, 4) - 0.5f) * 0.03f, v, 7, 10, 11, 0.6f);
			const float stone = Hash(c.Id, 3, 1);
			const float e = c.Edge / 10.0f;   // 경계까지 (타일 단위)
			const float mortar = 1.0f - Smooth(0.003f, 0.011f, e);
			const float bulge = sqrtf(Smooth(0.0f, 0.05f, e));
			const float rough = Fbm(u, v, 24, 24, 4, 3);
			const float tilt = ((u * 7.0f - c.Cx) * (Hash(c.Id, 7, 3) - 0.5f) + (v * 10.0f - c.Cy) * (Hash(c.Id, 8, 3) - 0.5f)) * 0.5f;
			const float gravel = Fbm(u, v, 96, 96, 2, 5);
			s.H = Lerp(0.08f + gravel * 0.12f, 0.45f + 0.35f * bulge + 0.12f * stone + tilt * 0.25f + (rough - 0.5f) * 0.3f, 1.0f - mortar);
			const float dark = Hash(c.Id, 9, 6) < 0.15f ? 0.72f : 1.0f;
			const float l = Lerp((0.58f + 0.38f * stone) * dark + (rough - 0.5f) * 0.22f, 0.62f + gravel * 0.12f, mortar);
			s.Tint(l, (Hash(c.Id, 5, 2) - 0.5f) * 0.16f);
			s.Smooth = Lerp(0.26f + 0.1f * stone, 0.06f, mortar);
			break;
		}
		case Kind::Ashlar:
		case Kind::Marble:
		{
			// 마름돌: 단마다 다른 길이의 블록 (반 칸 엇갈림), 모서리 깎임 · 정 자국
			const bool marble = k == Kind::Marble;
			const int rows = 6;
			const float y = v * rows;
			const int row = (int)floorf(y);
			const float fy = y - row;
			const int cols = marble ? 3 : 4;
			const float x = u * cols + (row & 1) * 0.5f;
			const int col = (int)floorf(x);
			const float fx = x - col;
			const int id = Wrap(col, cols) + Wrap(row, rows) * 16;
			const float joint = marble ? 0.003f : 0.006f;
			const float dx = (std::min)(fx, 1.0f - fx) / cols, dy = (std::min)(fy, 1.0f - fy) / rows;
			const float d = (std::min)(dx, dy);   // 이음매까지 (타일 단위)
			const float mortar = 1.0f - Smooth(joint * 0.5f, joint, d);
			const float bevel = Smooth(joint, joint * 3.0f, d);
			const float chisel = Fbm(u, v, 24, 24, 3, 17 + id);
			const float stone = Hash(id, 1, 4);
			s.H = Lerp(0.2f, 0.6f + 0.25f * bevel + (chisel - 0.5f) * (marble ? 0.05f : 0.2f), 1.0f - mortar);
			float l = 0.8f + 0.18f * stone + (chisel - 0.5f) * 0.12f;
			if (marble)
			{
				const float t = Fbm(u, v, 4, 4, 5, 51) * 7.0f;
				const float vein = powf(1.0f - fabsf(sinf((u * 3.0f + v * 1.3f + t) * XM_PI)), 14.0f);
				l = 0.9f + 0.08f * stone - vein * 0.35f;
				s.Smooth = Lerp(0.62f, 0.2f, mortar);
			}
			else
				s.Smooth = Lerp(0.22f, 0.08f, mortar);
			s.Tint(Lerp(l, 0.68f, mortar), (stone - 0.5f) * 0.06f);
			break;
		}
		case Kind::Brick:
		{
			const int rows = 24, cols = 8;   // 1.6 m: 단 6.7 cm, 벽돌 20 cm
			const float y = v * rows;
			const int row = (int)floorf(y);
			const float fy = y - row;
			const float x = u * cols + (row & 1) * 0.5f;
			const int col = (int)floorf(x);
			const float fx = x - col;
			const int id = Wrap(col, cols) + Wrap(row, rows) * 16;
			const float dx = (std::min)(fx, 1.0f - fx) / cols, dy = (std::min)(fy, 1.0f - fy) / rows;
			const float d = (std::min)(dx, dy);
			const float mortar = 1.0f - Smooth(0.0035f, 0.006f, d);
			const float n = Fbm(u, v, 32, 32, 3, 9);
			const float b = Hash(id, 2, 8);
			const float burnt = Hash(id, 3, 8) < 0.12f ? 0.65f : 1.0f;
			s.H = Lerp(0.15f + n * 0.1f, 0.7f + (n - 0.5f) * 0.25f + Smooth(0.006f, 0.012f, d) * 0.1f, 1.0f - mortar);
			s.Tint(Lerp((0.74f + 0.26f * b) * burnt + (n - 0.5f) * 0.1f, 0.62f, mortar), (b - 0.5f) * 0.1f);
			s.Smooth = Lerp(0.2f, 0.06f, mortar);
			break;
		}
		case Kind::Plaster:
		{
			// 손으로 바른 회벽: 흙손 자국 · 얼룩 · 실금 (몇 군데) · 드물게 벗겨져 돌이 보이는 자리
			const float blot = Fbm(u, v, 3, 3, 5, 21);
			const float fine = Fbm(u, v, 64, 64, 3, 22);
			const float trowel = Fbm(u, v, 10, 6, 3, 26);
			const float patch = Smooth(0.7f, 0.73f, Fbm(u, v, 5, 5, 4, 23));
			const float crackZone = Smooth(0.52f, 0.66f, Fbm(u, v, 3, 3, 3, 25));
			const float crack = (1.0f - Smooth(0.0f, 0.005f, fabsf(Fbm(u, v, 6, 6, 4, 24) - 0.5f))) * crackZone;
			s.H = 0.6f + (blot - 0.5f) * 0.3f + (trowel - 0.5f) * 0.35f + (fine - 0.5f) * 0.2f - patch * 0.45f - crack * 0.35f;
			const float stone = Voronoi(u, v, 14, 20, 25, 0.6f).Edge;
			const float wall = 0.86f + (blot - 0.5f) * 0.2f + (fine - 0.5f) * 0.06f + (trowel - 0.5f) * 0.05f - crack * 0.35f;
			s.Tint(Lerp(wall, Lerp(0.38f, 0.6f, Smooth(0.0f, 0.15f, stone)), patch), 0.012f);
			s.Smooth = 0.1f + (fine - 0.5f) * 0.06f;
			break;
		}
		case Kind::Stucco:
		{
			const float fine = Fbm(u, v, 64, 64, 3, 31);
			const float blot = Fbm(u, v, 3, 3, 4, 32);
			s.H = 0.5f + (fine - 0.5f) * 0.9f;
			s.Grey(0.92f + (blot - 0.5f) * 0.08f + (fine - 0.5f) * 0.05f);
			s.Smooth = 0.15f;
			break;
		}
		case Kind::Planks:
		case Kind::DoorPlanks:
		{
			// 세로 판자 (결 = 세로), 판 사이 틈
			const int boards = k == Kind::Planks ? 8 : 8;
			const float x = u * boards;
			const int b = (int)floorf(x);
			const float fx = x - b;
			const float gap = 1.0f - Smooth(0.0f, 0.03f, (std::min)(fx, 1.0f - fx));
			const float g = Grain(v, u * boards * 0.25f + b * 0.37f, 8, 40 + Wrap(b, boards));
			const float tone = Hash(Wrap(b, boards), 1, 41);
			const float round = sinf(fx * XM_PI);
			s.H = Lerp(0.0f, 0.55f + round * 0.2f + (g - 0.5f) * 0.2f, 1.0f - gap);
			s.Tint(Lerp(0.72f + 0.2f * tone + (g - 0.5f) * 0.3f, 0.3f, gap), 0.03f);
			s.Smooth = Lerp(0.3f, 0.05f, gap);
			// 판 가로대 못 (문짝)
			if (k == Kind::DoorPlanks)
			{
				const float nx = fx - 0.5f, ny = Frac(v * 4.0f) - 0.5f;
				const float nail = 1.0f - Smooth(0.03f, 0.05f, sqrtf(nx * nx * 0.25f + ny * ny * 4.0f));
				s.H = Lerp(s.H, 0.95f, nail);
				s.Grey(Lerp(s.R, 0.25f, nail));
				s.Metal = nail * 0.8f;
				s.Smooth = Lerp(s.Smooth, 0.45f, nail);
			}
			break;
		}
		case Kind::Logs:
		{
			const int rows = 7;
			const float y = v * rows;
			const int r = (int)floorf(y);
			const float fy = y - r;
			const float t = fy * 2.0f - 1.0f;
			const float round = sqrtf(Saturate(1.0f - t * t));
			const float g = Grain(u, fy * 0.2f + r * 0.13f, 8, 60 + Wrap(r, rows));
			const float chink = 1.0f - Smooth(0.0f, 0.25f, round);
			s.H = round * 0.9f + (g - 0.5f) * 0.08f;
			s.Tint(Lerp(0.7f + 0.2f * Hash(Wrap(r, rows), 0, 61) + (g - 0.5f) * 0.3f, 0.45f, chink), 0.04f);
			s.Smooth = Lerp(0.22f, 0.05f, chink);
			s.Ao = 0.5f + 0.5f * Smooth(0.0f, 0.7f, round);
			break;
		}
		case Kind::ClayTiles:
		{
			// 둥근 기와: 단 (아래로 겹침) + 가로로 둥근 골
			const int rows = 8, cols = 10;
			const float y = v * rows;
			const int r = (int)floorf(y);
			const float fy = y - r;
			const float x = u * cols;
			const int c = (int)floorf(x);
			const float fx = x - c;
			const float barrel = sinf(fx * XM_PI);
			const float course = fy;   // 단 아래쪽이 두껍다 (위 단 밑에서 나와 아래 끝이 들린다)
			const float n = Fbm(u, v, 24, 24, 3, 71);
			const int id = Wrap(c, cols) + Wrap(r, rows) * 16;
			const float lip = 1.0f - Smooth(0.9f, 1.0f, fy);
			s.H = Saturate(0.15f + course * 0.35f * lip + barrel * 0.45f + (n - 0.5f) * 0.06f);
			const float tone = Hash(id, 4, 72);
			s.Tint(0.72f + 0.26f * tone + (n - 0.5f) * 0.1f - (1.0f - barrel) * 0.12f, (tone - 0.5f) * 0.1f);
			s.Smooth = 0.3f;
			s.Ao = Lerp(0.45f, 1.0f, Smooth(0.0f, 0.5f, barrel)) * Lerp(0.6f, 1.0f, Smooth(0.0f, 0.25f, fy));
			break;
		}
		case Kind::Slate:
		case Kind::Shingles:
		{
			// 비늘 단: 단마다 반 칸 엇갈림, 아래 끝 = 너울 (슬레이트) · 둥근 비늘 (나무 너와)
			const bool wood = k == Kind::Shingles;
			const int rows = wood ? 12 : 12, cols = wood ? 8 : 6;
			const float y = v * rows;
			const int r = (int)floorf(y);
			auto bottomDip = [&](int row, float xx) {
				const float xr = xx * cols + (row & 1) * 0.5f;
				const float f = Frac(xr);
				if (wood)
					return 0.45f * sqrtf(Saturate(1.0f - (2.0f * f - 1.0f) * (2.0f * f - 1.0f)));
				return 0.06f * (Noise(xx * 60.0f, (float)row, 60, rows, 81) - 0.5f);
			};
			// 위 단 (r - 1) 의 비늘이 이 단으로 내려와 덮었는지
			const float fy = y - r;
			int owner = r;
			float local;   // 비늘 안 위치 (0 위 → 1 아래 끝)
			if (fy < bottomDip(r - 1, u))
			{
				owner = r - 1;
				local = (1.0f + fy) / (1.0f + bottomDip(r - 1, u));
			}
			else
				local = (fy + 0.0f) / (1.0f + bottomDip(r, u)) + 0.0f;
			const float xr = u * cols + (owner & 1) * 0.5f;
			const int c = (int)floorf(xr);
			const float fx = xr - c;
			const int id = Wrap(c, cols) + Wrap(owner, rows) * 32;
			const float gap = wood ? 1.0f - Smooth(0.0f, 0.04f, (std::min)(fx, 1.0f - fx)) : 1.0f - Smooth(0.0f, 0.015f, (std::min)(fx, 1.0f - fx));
			const float n = wood ? Grain(v, u * cols * 0.3f + c * 0.41f, 8, 90 + id % 7) : Fbm(u, v, 32, 32, 3, 91);
			const float tone = Hash(id, 6, 92);
			s.H = Saturate(0.1f + local * 0.75f + (n - 0.5f) * 0.1f - gap * 0.35f);
			s.Tint(0.7f + 0.26f * tone + (n - 0.5f) * (wood ? 0.25f : 0.08f) - gap * 0.3f, wood ? 0.04f : (tone - 0.5f) * 0.04f);
			s.Smooth = wood ? 0.18f : 0.42f;
			s.Ao = Lerp(0.5f, 1.0f, Smooth(0.0f, 0.3f, local)) * (1.0f - gap * 0.4f);
			break;
		}
		case Kind::Thatch:
		{
			// 이엉: 단마다 다발 (가로로 볼록), 아래 끝이 들쭉날쭉, 아래로 흐르는 짚 가닥, 다발 사이 깊은 틈
			const int rows = 5;
			const float edge = (Noise(u * 40.0f, 0.0f, 40, 1, 104) - 0.5f) * 0.18f;
			const float y = v * rows + edge;
			const int row = (int)floorf(y);
			const float fy = y - row;
			const float bx = u * 14.0f + Hash(Wrap(row, rows), 0, 105) * 3.0f;
			const float fb = Frac(bx);
			const float bundle = sinf(fb * XM_PI);
			const float strands = Fbm(u, v * 0.1f + row * 0.37f, 128, 2, 3, 101);
			const float clump = Fbm(u, v, 8, 4, 3, 102);
			const float course = Smooth(0.0f, 0.9f, fy) * (1.0f - Smooth(0.94f, 1.0f, fy));
			s.H = Saturate(0.12f + course * 0.42f + bundle * 0.16f + (strands - 0.5f) * 0.7f + (clump - 0.5f) * 0.2f);
			const float dark = Smooth(0.55f, 0.15f, strands) * 0.7f + (1.0f - bundle) * 0.2f;
			const float tone = Hash((int)floorf(bx) & 63, Wrap(row, rows), 106);
			s.Tint(0.8f + (clump - 0.5f) * 0.2f + (tone - 0.5f) * 0.12f - dark * 0.35f, 0.06f);
			s.Smooth = 0.04f;
			s.Ao = Lerp(0.45f, 1.0f, Smooth(0.0f, 0.4f, s.H));
			break;
		}
		case Kind::Copper:
		case Kind::Lead:
		{
			// 금속판: 세로 이음 (돋은 솔기) + 가로 겹침 + 녹 얼룩
			const bool copper = k == Kind::Copper;
			const int cols = copper ? 5 : 4;
			const float fx = Frac(u * cols);
			const float seam = 1.0f - Smooth(0.0f, copper ? 0.04f : 0.09f, (std::min)(fx, 1.0f - fx));
			const float lap = 1.0f - Smooth(0.0f, 0.01f, Frac(v * 3.0f));
			const float patina = Fbm(u, v, 6, 6, 5, 111);
			const float dent = Fbm(u, v, 20, 20, 3, 112);
			s.H = Saturate(0.45f + seam * 0.5f + lap * 0.15f + (dent - 0.5f) * 0.08f);
			const float stain = Smooth(0.45f, 0.75f, patina);
			s.Tint(0.82f + (patina - 0.5f) * 0.25f - stain * 0.1f, copper ? -0.03f : 0.0f);
			s.Metal = copper ? Lerp(0.15f, 0.55f, 1.0f - stain) : 0.45f;
			s.Smooth = copper ? Lerp(0.35f, 0.55f, 1.0f - stain) : 0.38f + (dent - 0.5f) * 0.1f;
			break;
		}
		case Kind::Turf:
		{
			const float blades = Fbm(u, v, 128, 48, 2, 121);
			const float clump = Fbm(u, v, 8, 8, 4, 122);
			const float moss = Smooth(0.45f, 0.7f, Fbm(u, v, 5, 5, 4, 123));
			s.H = Saturate(0.3f + (clump - 0.5f) * 0.6f + (blades - 0.5f) * 0.5f);
			s.R = 0.78f + (clump - 0.5f) * 0.2f - moss * 0.1f + (blades - 0.5f) * 0.1f;
			s.G = 0.9f + (clump - 0.5f) * 0.15f + (blades - 0.5f) * 0.12f;
			s.B = 0.7f + (clump - 0.5f) * 0.1f;
			s.Smooth = 0.06f;
			break;
		}
		case Kind::Timber:
		{
			// 손도끼로 다듬은 들보: 가로 결 (u 를 따라) + 갈라짐 + 자귀 자국
			const float g = Grain(u, v, 4, 131);
			const float crack = 1.0f - Smooth(0.0f, 0.012f, fabsf(Fbm(u, v, 2, 6, 3, 132) - 0.5f + (Noise(u * 3, v * 9, 3, 9, 133) - 0.5f) * 0.05f));
			const float adze = Fbm(u * 1.0f, v, 12, 4, 2, 134);
			s.H = Saturate(0.6f + (g - 0.5f) * 0.2f + (adze - 0.5f) * 0.35f - crack * 0.5f);
			s.Tint(0.76f + (g - 0.5f) * 0.3f - crack * 0.4f, 0.03f);
			s.Smooth = 0.2f + (g - 0.5f) * 0.1f;
			break;
		}
		case Kind::Glass:
		{
			const float wave = Fbm(u, v, 4, 4, 3, 141);
			s.H = wave;
			s.Grey(0.92f + (wave - 0.5f) * 0.1f);
			s.Smooth = 0.94f;
			s.Ao = 1.0f;
			break;
		}
		case Kind::LeadedGlass:
		{
			// 납틀 마름모 (12 cm) — 납 줄은 돋고 어둡다
			const float a = u * 5.0f + v * 3.0f, b = u * 5.0f - v * 3.0f;
			const float da = fabsf(Frac(a) - 0.5f), db = fabsf(Frac(b) - 0.5f);
			const float line = 1.0f - Smooth(0.42f, 0.47f, (std::max)(da, db));
			const int pane = (int)floorf(a) * 31 + (int)floorf(b);
			const float wave = Fbm(u, v, 5, 5, 2, 151);
			s.H = line > 0.5f ? 0.6f + 0.4f * (1.0f - line) : 0.5f + (wave - 0.5f) * 0.3f;
			s.H = Saturate(Lerp(0.45f + (wave - 0.5f) * 0.2f, 1.0f, 1.0f - line));
			const float tint = Hash(pane, 1, 152);
			s.Tint(Lerp(0.18f, 0.84f + tint * 0.16f, line), (tint - 0.5f) * 0.12f);
			s.Metal = Lerp(0.5f, 0.0f, line);
			s.Smooth = Lerp(0.4f, 0.95f, line);
			s.Ao = 1.0f;
			break;
		}
		case Kind::Metal:
		{
			const float ham = Fbm(u, v, 16, 16, 3, 161);
			const float rust = Smooth(0.55f, 0.8f, Fbm(u, v, 6, 6, 4, 162));
			s.H = ham;
			s.Tint(0.8f + (ham - 0.5f) * 0.15f - rust * 0.15f, rust * 0.12f);
			s.Metal = Lerp(0.85f, 0.2f, rust);
			s.Smooth = Lerp(0.5f, 0.15f, rust);
			break;
		}
		case Kind::Ornament:
		{
			// 북유럽 칠한 판: 세로 판자 + 네 띠 (마름모 사슬 · 지그재그 · 마름모 · 꽃 고리), 군데군데 칠이 벗겨져 나무가 보인다
			const float bxv = u * 8.0f;
			const int board = (int)floorf(bxv);
			const float fbx = bxv - board;
			const float gap = 1.0f - Smooth(0.0f, 0.03f, (std::min)(fbx, 1.0f - fbx));
			const float g = Grain(v, u * 2.0f + board * 0.37f, 8, 180 + Wrap(board, 8));
			const int band = (int)floorf(v * 4.0f);
			const float fv = Frac(v * 4.0f), fu = Frac(u * 6.0f);
			const float cream[3] = { 0.86f, 0.8f, 0.66f }, red[3] = { 0.62f, 0.17f, 0.12f }, teal[3] = { 0.16f, 0.45f, 0.44f }, ochre[3] = { 0.84f, 0.6f, 0.22f }, ink[3] = { 0.16f, 0.12f, 0.1f };
			const float* col = cream;
			float relief = 0.0f;
			if (fv < 0.05f || fv > 0.95f)
				col = red;
			else if (band == 0 || band == 2)
			{
				const float dd = fabsf(fu - 0.5f) * 2.0f + fabsf(fv - 0.5f) * 2.2f;
				if (fabsf(dd - 0.86f) < 0.06f) col = ink;
				else if (dd < 0.42f) col = band == 0 ? ochre : teal;
				else if (dd < 0.86f) col = band == 0 ? red : ochre;
				relief = dd < 0.86f ? 0.05f : 0.0f;
			}
			else if (band == 1)
			{
				const float zz = fabsf(Frac(u * 12.0f) - 0.5f) * 2.0f;
				const float line = fabsf(fv - (0.25f + 0.5f * zz));
				if (line < 0.08f) col = teal;
				else if (line < 0.11f) col = ink;
			}
			else
			{
				const float r = sqrtf((fu - 0.5f) * (fu - 0.5f) + (fv - 0.5f) * (fv - 0.5f) * 1.4f);
				if (r < 0.1f) col = ochre;
				else if (fabsf(r - 0.28f) < 0.06f) col = red;
				else if (fabsf(r - 0.28f) < 0.085f) col = ink;
			}
			const float wear = Smooth(0.62f, 0.72f, Fbm(u, v, 10, 10, 4, 181) + (g - 0.5f) * 0.2f);
			const float wood[3] = { 0.42f + (g - 0.5f) * 0.12f, 0.31f + (g - 0.5f) * 0.1f, 0.21f };
			const float fade = 0.88f + (Fbm(u, v, 4, 4, 3, 182) - 0.5f) * 0.18f;
			s.R = Lerp(Lerp(col[0] * fade, wood[0], wear), 0.12f, gap);
			s.G = Lerp(Lerp(col[1] * fade, wood[1], wear), 0.1f, gap);
			s.B = Lerp(Lerp(col[2] * fade, wood[2], wear), 0.08f, gap);
			s.H = Saturate(0.6f + relief - wear * 0.12f + (g - 0.5f) * 0.15f - gap * 0.55f);
			s.Smooth = Lerp(0.32f, 0.12f, wear);
			break;
		}
		case Kind::Banner:
		{
			// 깃발 (판 하나에 한 장 — u 가로 0..1, v 위 0 → 아래 1, 세로가 약 3 배 길다): 짠 천 · 테두리 · 해 문장
			const float wx = sinf(u * 220.0f * XM_PI), wy = sinf(v * 600.0f * XM_PI);
			const float weave = 0.5f + 0.2f * wx * (wy > 0 ? 1.0f : -1.0f);
			const float du = u - 0.5f, dv = (v - 0.36f) * 3.0f;
			const float r = sqrtf(du * du + dv * dv), ang = atan2f(dv, du);
			const float rays = 0.5f + 0.5f * cosf(ang * 8.0f);
			const bool sun = r < 0.16f || (r < 0.3f && r < 0.17f + 0.13f * rays);
			const bool ring = fabsf(r - 0.36f) < 0.025f;
			const bool border = u < 0.07f || u > 0.93f || v < 0.025f || (v > 0.55f && fabsf(v - 0.62f) < 0.012f);
			float l = 0.72f;
			if (sun || ring) l = 1.0f;
			if (border) l = 0.42f;
			const float stain = Fbm(u, v, 3, 6, 4, 191);
			l *= 0.9f + (stain - 0.5f) * 0.25f + (weave - 0.5f) * 0.08f;
			s.Grey(l);
			s.H = weave * 0.6f + (sun ? 0.15f : 0.0f);
			s.Smooth = 0.08f;
			s.Ao = 1.0f;
			break;
		}
		case Kind::Foliage:
		{
			// 꽃 상자: 잎 (진하고 옅은 초록) + 꽃 점 (빨강 · 분홍 · 노랑 · 흰색)
			const Cell leaf = Voronoi(u, v, 16, 16, 201, 1.0f);
			const float n = Fbm(u, v, 12, 12, 3, 202);
			const float lt = Hash(leaf.Id, 1, 203);
			s.R = 0.22f + 0.12f * lt + (n - 0.5f) * 0.1f;
			s.G = 0.42f + 0.18f * lt + (n - 0.5f) * 0.12f;
			s.B = 0.16f + 0.06f * lt;
			s.H = Saturate(0.3f + Smooth(0.0f, 0.12f, leaf.Edge) * 0.5f + (n - 0.5f) * 0.3f);
			const Cell fl = Voronoi(u, v, 10, 10, 204, 1.0f);
			const float pick = Hash(fl.Id, 2, 205);
			if (pick < 0.55f && fl.F1 < 0.28f)
			{
				const float petals[4][3] = { { 0.85f, 0.16f, 0.14f }, { 0.92f, 0.5f, 0.62f }, { 0.95f, 0.8f, 0.2f }, { 0.94f, 0.92f, 0.88f } };
				const float* p = petals[(int)(Hash(fl.Id, 3, 206) * 4.0f) & 3];
				const float center = fl.F1 < 0.08f ? 1.0f : 0.0f;
				s.R = Lerp(p[0], 0.9f, center * 0.5f); s.G = Lerp(p[1], 0.75f, center * 0.6f); s.B = Lerp(p[2], 0.2f, center);
				s.H = 0.85f;
			}
			s.Smooth = 0.2f;
			break;
		}
		case Kind::Fabric:
		default:
		{
			const float wx = sinf(u * 160.0f * XM_PI), wy = sinf(v * 160.0f * XM_PI);
			const float weave = 0.5f + 0.25f * (wx * (wy > 0 ? 1.0f : -1.0f)) + 0.25f * wy * 0.3f;
			const float blot = Fbm(u, v, 4, 4, 4, 171);
			s.H = weave;
			s.Grey(0.9f + (blot - 0.5f) * 0.12f + (weave - 0.5f) * 0.06f);
			s.Smooth = 0.1f;
			break;
		}
		}
		return s;
	}

	// 낡음: 잔 표면 (정 자국 · 결 · 구멍) · 깨진 자리 · 큰 색 얼룩 · 틈의 때 · 빗물 줄 — 종류마다 세기
	struct AgeSpec { float Micro, Chip, Macro, Cavity, Streak; };

	// 표면 물리 값: Smooth = 그 재질의 가장 반들한 곳 (무늬의 Smooth 0..1 을 이 범위로), Rough = 가장 거친 곳, Normal = 노멀 세기 배율, Metal = 금속 칸의 Metallic 상한
	//  Unity 처럼 거친 면도 비스듬히 보면 Smoothness 만큼 하늘을 비춘다 — 기와 · 이엉 · 흙 · 나무는 0.05 ~ 0.2 로 낮춰야 하늘빛이 번지지 않는다
	struct PbrSpec { float Rough, Smooth, Normal, Metal; };
	PbrSpec PbrOf(Kind k)
	{
		switch (k)
		{
		case Kind::Rubble:      return { 0.03f, 0.2f, 1.7f, 0.0f };    // 거친 돌 · 회반죽
		case Kind::Ashlar:      return { 0.04f, 0.22f, 1.6f, 0.0f };   // 정으로 다듬은 돌
		case Kind::Brick:       return { 0.03f, 0.16f, 1.6f, 0.0f };   // 구운 벽돌 (무광)
		case Kind::Plaster:     return { 0.02f, 0.1f, 1.5f, 0.0f };    // 석회 회벽 (분필처럼)
		case Kind::Stucco:      return { 0.03f, 0.14f, 1.4f, 0.0f };
		case Kind::Planks:      return { 0.04f, 0.18f, 1.6f, 0.0f };   // 비바람 맞은 나무
		case Kind::Logs:        return { 0.03f, 0.16f, 1.5f, 0.0f };
		case Kind::Marble:      return { 0.2f, 0.62f, 1.2f, 0.0f };    // 닦은 대리석 (줄눈은 거칠다)
		case Kind::ClayTiles:   return { 0.03f, 0.16f, 1.7f, 0.0f };   // 유약 없는 기와 — 반사 없음
		case Kind::Slate:       return { 0.06f, 0.3f, 1.6f, 0.0f };    // 슬레이트는 살짝 결 광택
		case Kind::Thatch:      return { 0.0f, 0.05f, 1.6f, 0.0f };    // 짚 — 무광
		case Kind::Shingles:    return { 0.02f, 0.14f, 1.7f, 0.0f };   // 나무 너와
		case Kind::Copper:      return { 0.18f, 0.5f, 1.2f, 1.0f };    // 녹 (무광 · 비금속) ↔ 맨 구리
		case Kind::Lead:        return { 0.2f, 0.42f, 1.2f, 0.9f };
		case Kind::Turf:        return { 0.0f, 0.08f, 1.5f, 0.0f };
		case Kind::Timber:      return { 0.04f, 0.2f, 1.6f, 0.0f };    // 손도끼 들보
		case Kind::Glass:       return { 0.85f, 0.97f, 1.0f, 0.0f };
		case Kind::LeadedGlass: return { 0.3f, 0.96f, 1.2f, 0.85f };   // 유리 반들 · 납 줄은 금속
		case Kind::DoorPlanks:  return { 0.06f, 0.3f, 1.5f, 1.0f };    // 칠한 판 + 쇠 못
		case Kind::Metal:       return { 0.12f, 0.55f, 1.3f, 1.0f };   // 두드린 쇠 ↔ 녹
		case Kind::Fabric:      return { 0.0f, 0.06f, 1.0f, 0.0f };
		case Kind::Ornament:    return { 0.06f, 0.28f, 1.3f, 0.0f };   // 칠 (반무광) ↔ 벗겨진 나무
		case Kind::Banner:      return { 0.0f, 0.06f, 1.0f, 0.0f };
		case Kind::Foliage:     return { 0.08f, 0.32f, 1.2f, 0.0f };   // 잎 (밀랍 광택)
		default:                return { 0.05f, 0.3f, 1.0f, 1.0f };
		}
	}
	AgeSpec AgeOf(Kind k)
	{
		switch (k)
		{
		case Kind::Rubble: case Kind::Ashlar: case Kind::Brick: return { 0.14f, 1.0f, 0.22f, 0.45f, 0.1f };
		case Kind::Marble: return { 0.05f, 0.4f, 0.1f, 0.25f, 0.06f };
		case Kind::Plaster: case Kind::Stucco: return { 0.08f, 0.5f, 0.2f, 0.25f, 0.14f };
		case Kind::Planks: case Kind::Logs: case Kind::DoorPlanks: case Kind::Timber: return { 0.08f, 0.6f, 0.2f, 0.4f, 0.1f };
		case Kind::ClayTiles: case Kind::Slate: case Kind::Shingles: return { 0.08f, 0.6f, 0.24f, 0.45f, 0.06f };
		case Kind::Thatch: case Kind::Turf: return { 0.06f, 0.0f, 0.2f, 0.4f, 0.0f };
		case Kind::Copper: case Kind::Lead: case Kind::Metal: return { 0.04f, 0.3f, 0.15f, 0.2f, 0.08f };
		case Kind::Ornament: return { 0.05f, 0.4f, 0.12f, 0.3f, 0.06f };
		default: return { 0.0f, 0.0f, 0.06f, 0.0f, 0.0f };
		}
	}

	// light = 사진 재질 (이미 낡았다): 잔 표면 · 깨짐 · 틈 때는 빼고 큰 얼룩 (반복 가림) · 빗물 줄 반만 · 1 층 때
	void Age(Kind k, float u, float v, int rows, float grime, Sample& s, bool light = false)
	{
		AgeSpec a = AgeOf(k);
		if (light)
			a = { 0.0f, 0.0f, a.Macro * 0.6f, 0.0f, a.Streak * 0.5f };
		const int seed = 500 + (int)k * 13;
		if (a.Micro > 0.0f)
			s.H += (Fbm(u, v, 48, 48, 3, seed) - 0.5f) * a.Micro + (Fbm(u, v, 160, 160, 2, seed + 1) - 0.5f) * a.Micro * 0.5f;
		if (a.Chip > 0.0f)
		{
			const float pit = Smooth(0.7f, 0.8f, Fbm(u, v, 20, 20, 3, seed + 2)) * a.Chip;
			s.H -= pit * 0.18f;
			s.R *= 1.0f - pit * 0.12f; s.G *= 1.0f - pit * 0.12f; s.B *= 1.0f - pit * 0.1f;
			s.Smooth *= 1.0f - pit * 0.4f;
		}
		const float mac = Fbm(u, v, 3, 3 * (std::max)(1, rows), 4, seed + 3) - 0.5f, hue = Fbm(u, v, 2, 2 * (std::max)(1, rows), 3, seed + 4) - 0.5f;
		const float km = 1.0f + mac * a.Macro * 2.0f;
		s.R *= km * (1.0f + hue * a.Macro * 0.4f); s.G *= km; s.B *= km * (1.0f - hue * a.Macro * 0.5f);
		if (a.Cavity > 0.0f)
		{
			const float cav = Lerp(1.0f - a.Cavity, 1.0f, Smooth(0.05f, 0.5f, s.H));
			s.R *= cav; s.G *= cav; s.B *= cav;
		}
		if (a.Streak > 0.0f)
		{
			const float st = Smooth(0.55f, 0.8f, Fbm(u, v, 40, 2 * (std::max)(1, rows), 3, seed + 5)) * a.Streak;
			s.R *= 1.0f - st; s.G *= 1.0f - st * 0.95f; s.B *= 1.0f - st * 0.9f;
		}
		s.Smooth = Saturate(s.Smooth * (0.85f + 0.3f * Fbm(u, v, 8, 8, 2, seed + 6)));
		if (grime > 0.0f)
		{
			// v 가 1 에 가까울수록 땅 (튀어 오른 흙 · 물 얼룩), 맨 아래 · 틈에 이끼
			const float fromGround = 1.0f - v;
			const float splash = Fbm(u * (float)(std::max)(1, rows), v, 10, 10 * (std::max)(1, rows), 4, seed + 7);
			const float g = grime * Saturate(powf(Saturate(1.0f - fromGround / 0.42f), 1.3f) * (0.6f + 0.8f * splash));
			const float moss = grime * Smooth(0.0f, 1.0f, Saturate(1.0f - fromGround / 0.16f)) * Smooth(0.45f, 0.62f, Fbm(u, v, 14, 14 * (std::max)(1, rows), 3, seed + 8))
				* Lerp(1.0f, 0.4f, Smooth(0.3f, 0.7f, s.H));
			const float drip = grime * 0.5f * Smooth(0.6f, 0.85f, Fbm(u, v, 30, 3 * (std::max)(1, rows), 3, seed + 9)) * Saturate(1.0f - fromGround / 0.8f);
			const float dk = 1.0f - 0.5f * g - 0.25f * drip;
			s.R *= dk * (1.0f - 0.4f * moss); s.G *= dk * (1.0f + 0.08f * moss); s.B *= dk * (1.0f - 0.55f * moss);
			s.Smooth = Saturate(s.Smooth + 0.08f * g);
			s.H -= 0.04f * g;
			s.H += 0.06f * moss;
		}
		s.H = Saturate(s.H);
	}

	struct Baked
	{
		ComPtr<GfxShaderResourceView> Base, Normal, Mask, Height;
		float MeanSmooth = 0.3f;
	};
	std::map<int, Baked> s_Baked;
	std::map<std::string, std::shared_ptr<UMaterial>> s_Materials;
	std::mutex s_Lock;

	ComPtr<GfxShaderResourceView> UploadLevels(const std::vector<std::vector<uint8_t>>& levels, int size)
	{
		std::vector<D3D11_SUBRESOURCE_DATA> init(levels.size());
		int sz = size;
		for (size_t m = 0; m < levels.size(); ++m, sz = (std::max)(1, sz / 2))
			init[m] = { levels[m].data(), (UINT)(sz * 4), 0 };
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = td.Height = (UINT)size;
		td.MipLevels = (UINT)levels.size();
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_IMMUTABLE;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		auto device = Application::GetI()->GetDevice();
		ComPtr<GfxTexture2D> tex;
		ComPtr<GfxShaderResourceView> srv;
		if (SUCCEEDED(device->CreateTexture2D(&td, init.data(), tex.GetAddressOf())))
			device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf());
		return srv;
	}

	// float4 한 장 → 밉 단계 RGBA8 (2x2 평균, normal = 평균 뒤 다시 길이 1)
	std::vector<std::vector<uint8_t>> BuildMips(std::vector<XMFLOAT4> img, int size, bool normal)
	{
		std::vector<std::vector<uint8_t>> out;
		int sz = size;
		for (;;)
		{
			std::vector<uint8_t> bytes((size_t)sz * sz * 4);
			for (size_t i = 0; i < img.size(); ++i)
			{
				XMFLOAT4 t = img[i];
				if (normal)
				{
					float x = t.x * 2 - 1, y = t.y * 2 - 1, z = t.z * 2 - 1;
					const float l = sqrtf(x * x + y * y + z * z);
					if (l > 1e-5f) { x /= l; y /= l; z /= l; }
					t = XMFLOAT4(x * 0.5f + 0.5f, y * 0.5f + 0.5f, z * 0.5f + 0.5f, 1.0f);
				}
				bytes[i * 4 + 0] = (uint8_t)(Saturate(t.x) * 255.0f + 0.5f);
				bytes[i * 4 + 1] = (uint8_t)(Saturate(t.y) * 255.0f + 0.5f);
				bytes[i * 4 + 2] = (uint8_t)(Saturate(t.z) * 255.0f + 0.5f);
				bytes[i * 4 + 3] = (uint8_t)(Saturate(t.w) * 255.0f + 0.5f);
			}
			out.push_back(std::move(bytes));
			if (sz == 1)
				break;
			const int h = sz / 2;
			std::vector<XMFLOAT4> next((size_t)h * h);
			for (int y = 0; y < h; ++y)
				for (int x = 0; x < h; ++x)
				{
					XMFLOAT4 sum(0, 0, 0, 0);
					for (int k = 0; k < 4; ++k)
					{
						const XMFLOAT4& t = img[(size_t)(y * 2 + (k >> 1)) * sz + (x * 2 + (k & 1))];
						sum.x += t.x; sum.y += t.y; sum.z += t.z; sum.w += t.w;
					}
					next[(size_t)y * h + x] = XMFLOAT4(sum.x * 0.25f, sum.y * 0.25f, sum.z * 0.25f, sum.w * 0.25f);
				}
			img = std::move(next);
			sz = h;
		}
		return out;
	}


	// 뒤에서 도는 BC 압축 (떨어진 스레드, 하나씩 — s_CompressLock). 기다리지 않는다: 편집기를 닫거나 패키지를 내리면 취소 표시만 하고,
	//  DLL 은 처음 압축을 시작할 때 고정 (PIN) 해 두어 스레드가 도는 중에 내려가지 않는다 (프로세스가 끝나면 함께 끝난다)
	std::mutex s_CompressLock;
	std::atomic<bool> s_CancelCompress{ false };
	void PinModule()
	{
		static bool pinned = false;
		if (pinned)
			return;
		HMODULE h = nullptr;
		pinned = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&PinModule), &h) != 0;
	}

	// ================================================================ 텍스처 팩 (실제 사진 재질) → 엔진 텍스처
	//  WebP (색 · DirectX 노멀 · 거칠기 · AO · 높이 · 금속) 를 풀어 → 색은 평균으로 나눠 중성 (고른 색을 곱한다), 거칠기 → 재질 범위의 Smoothness,
	//  AO (없으면 높이에서) → 1 층 때 · 이끼 · 큰 얼룩 (타일 반복이 덜 보이게) → BC 압축 (색 BC1 · 노멀 BC7 · 마스크 BC3) 으로 프로젝트 Library 에 캐시
	struct Plane { int N = 0; std::vector<uint8_t> Px; };   // RGBA8 N x N

	bool LoadPlane(const std::wstring& path, int want, Plane& out)
	{
		DirectX::ScratchImage img, conv, sized;
		DirectX::TexMetadata md = {};
		if (FAILED(ImageIO::Load(path.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, &md, img)))
			return false;
		const DirectX::ScratchImage* cur = &img;
		if (md.format != DXGI_FORMAT_R8G8B8A8_UNORM)
		{
			if (FAILED(DirectX::Convert(*img.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, conv)))
				return false;
			cur = &conv;
		}
		if ((int)cur->GetMetadata().width != want || (int)cur->GetMetadata().height != want)
		{
			if (FAILED(DirectX::Resize(*cur->GetImage(0, 0, 0), (size_t)want, (size_t)want, DirectX::TEX_FILTER_CUBIC, sized)))
				return false;
			cur = &sized;
		}
		const DirectX::Image* im = cur->GetImage(0, 0, 0);
		out.N = want;
		out.Px.resize((size_t)want * want * 4);
		for (int y = 0; y < want; ++y)
			memcpy(out.Px.data() + (size_t)y * want * 4, im->pixels + (size_t)y * im->rowPitch, (size_t)want * 4);
		return true;
	}

	// 이어지는 이중 선형 표본 (u, v 0..1)
	XMFLOAT4 TexelOf(const Plane& p, int x, int y)
	{
		const uint8_t* q = &p.Px[((size_t)y * p.N + x) * 4];
		return XMFLOAT4(q[0] / 255.0f, q[1] / 255.0f, q[2] / 255.0f, q[3] / 255.0f);
	}

	XMFLOAT4 SamplePlane(const Plane& p, float u, float v)
	{
		const int n = p.N;
		const float x = u * n - 0.5f, y = v * n - 0.5f;
		const int x0 = (int)floorf(x), y0 = (int)floorf(y);
		const float fx = x - x0, fy = y - y0;
		auto at = [&](int xx, int yy, int c) { return p.Px[((size_t)Wrap(yy, n) * n + Wrap(xx, n)) * 4 + c] / 255.0f; };
		XMFLOAT4 r;
		float* o = &r.x;
		for (int c = 0; c < 4; ++c)
			o[c] = Lerp(Lerp(at(x0, y0, c), at(x0 + 1, y0, c), fx), Lerp(at(x0, y0 + 1, c), at(x0 + 1, y0 + 1, c), fx), fy);
		return r;
	}

	// 밉 단계들 → ScratchImage (RGBA8) → BC 압축
	bool Pack(const std::vector<std::vector<uint8_t>>& levels, int n, DXGI_FORMAT bc, DirectX::ScratchImage& out)
	{
		DirectX::ScratchImage raw;
		if (FAILED(raw.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, (size_t)n, (size_t)n, 1, levels.size())))
			return false;
		int sz = n;
		for (size_t m = 0; m < levels.size(); ++m, sz = (std::max)(1, sz / 2))
		{
			const DirectX::Image* im = raw.GetImage(m, 0, 0);
			for (int y = 0; y < sz; ++y)
				memcpy(im->pixels + (size_t)y * im->rowPitch, levels[m].data() + (size_t)y * sz * 4, (size_t)sz * 4);
		}
		DirectX::TEX_COMPRESS_FLAGS flags = DirectX::TEX_COMPRESS_PARALLEL;
		if (bc == DXGI_FORMAT_BC7_UNORM)
			flags |= DirectX::TEX_COMPRESS_BC7_QUICK;
		return SUCCEEDED(DirectX::Compress(raw.GetImages(), raw.GetImageCount(), raw.GetMetadata(), bc, flags, DirectX::TEX_THRESHOLD_DEFAULT, out));
	}

	ComPtr<GfxShaderResourceView> UploadScratch(const DirectX::ScratchImage& img)
	{
		const DirectX::TexMetadata& md = img.GetMetadata();
		std::vector<D3D11_SUBRESOURCE_DATA> init(md.mipLevels);
		for (size_t m = 0; m < md.mipLevels; ++m)
		{
			const DirectX::Image* im = img.GetImage(m, 0, 0);
			init[m] = { im->pixels, (UINT)im->rowPitch, (UINT)im->slicePitch };
		}
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = (UINT)md.width;
		td.Height = (UINT)md.height;
		td.MipLevels = (UINT)md.mipLevels;
		td.ArraySize = 1;
		td.Format = md.format;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_IMMUTABLE;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		auto device = Application::GetI()->GetDevice();
		ComPtr<GfxTexture2D> tex;
		ComPtr<GfxShaderResourceView> srv;
		if (SUCCEEDED(device->CreateTexture2D(&td, init.data(), tex.GetAddressOf())))
			device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf());
		return srv;
	}

	std::wstring CacheDir()
	{
		return string_to_wstring(PathManager::GetI()->GetContentPathS()) + L"Library\\NovaBuilding\\TextureCache\\";
	}

	// 캐시 열쇠: id · 1 층 때 · 원본 파일 (크기 · 시각) · 굽기 판
	std::wstring CacheKey(const TextureLibrary::Material& m, int rows, int level)
	{
		size_t h = std::hash<std::string>()(m.Id);
		for (const auto& kv : m.Maps)
		{
			std::error_code ec;
			const auto sz = std::filesystem::file_size(kv.second, ec);
			const auto t = std::filesystem::last_write_time(kv.second, ec).time_since_epoch().count();
			h ^= std::hash<std::string>()(kv.first) + (size_t)sz * 1315423911u + (size_t)t + (h << 6) + (h >> 2);
		}
		std::string id = m.Id;
		for (char& c : id)
			if (c == '/' || c == '\\' || c == ':') c = '_';
		char buf[64];
		snprintf(buf, sizeof(buf), "_r%d_g%d_%016llx_v4", rows, level, (unsigned long long)h);
		return string_to_wstring(id + buf);
	}

	// 텍스처 팩 재질을 그대로 (사진 원본 — 색 · 노멀 · 거칠기 · AO · 금속 · 높이를 고치지 않는다). 바꾸는 것은 형식뿐:
	//  WebP → RGBA8 (밉) → 지금 올려 쓰고, 뒤에서 BC 압축 (색 BC1 · 노멀 BC7 · 마스크 BC3 · 높이 BC4) 을 Library 캐시에
	//  마스크 = 엔진 Metallic / Occlusion 맵 칸 (r = Metallic, g = AO, a = Smoothness = 1 - 거칠기)
	bool BakeReal(Kind k, const TextureLibrary::Material& m, int rows, int level, Baked& out)
	{
		(void)k; (void)rows; (void)level;
		const auto t0 = std::chrono::steady_clock::now();
		const std::wstring dir = CacheDir(), key = CacheKey(m, 0, 0);
		const std::wstring files[4] = { dir + key + L"_base.dds", dir + key + L"_normal.dds", dir + key + L"_mask.dds", dir + key + L"_height.dds" };
		const bool wantHeight = m.Maps.count("height") > 0;
		{
			DirectX::ScratchImage c[4];
			bool ok = true;
			for (int i = 0; i < (wantHeight ? 4 : 3) && ok; ++i)
				ok = SUCCEEDED(DirectX::LoadFromDDSFile(files[i].c_str(), DirectX::DDS_FLAGS_NONE, nullptr, c[i]));
			if (ok)
			{
				out.Base = UploadScratch(c[0]);
				out.Normal = UploadScratch(c[1]);
				out.Mask = UploadScratch(c[2]);
				if (wantHeight)
					out.Height = UploadScratch(c[3]);
				if (out.Base && out.Normal && out.Mask)
				{
					EditorLog::Write("Building", "texture %s from cache (%.1f ms)", m.Id.c_str(), std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count());
					return true;
				}
			}
		}
		auto mapPath = [&](const char* name) { auto it = m.Maps.find(name); return it == m.Maps.end() ? std::wstring() : it->second; };
		Plane color, normal, rough, ao, height, metal;
		const int n0 = 1024;
		if (!LoadPlane(mapPath("color"), n0, color) || !LoadPlane(mapPath("normal"), n0, normal))
		{
			EditorLog::Write("Building", "texture %s: cannot read color / normal maps", m.Id.c_str());
			return false;
		}
		const int n = color.N;
		const bool hasRough = LoadPlane(mapPath("roughness"), n, rough), hasAo = LoadPlane(mapPath("ao"), n, ao);
		const bool hasHeight = wantHeight && LoadPlane(mapPath("height"), n, height), hasMetal = LoadPlane(mapPath("metalness"), n, metal);
		std::vector<XMFLOAT4> base((size_t)n * n), nrm((size_t)n * n), mask((size_t)n * n), hgt(hasHeight ? (size_t)n * n : 0);
		std::vector<int> lines(n);
		std::iota(lines.begin(), lines.end(), 0);
		std::for_each(std::execution::par, lines.begin(), lines.end(), [&](int y) {
			for (int x = 0; x < n; ++x)
			{
				const size_t i = (size_t)y * n + x;
				const XMFLOAT4 col = TexelOf(color, x, y), nm = TexelOf(normal, x, y);
				base[i] = XMFLOAT4(col.x, col.y, col.z, 1.0f);
				nrm[i] = XMFLOAT4(nm.x, nm.y, nm.z, 1.0f);
				const float r = hasRough ? TexelOf(rough, x, y).x : 0.8f;
				const float occ = hasAo ? TexelOf(ao, x, y).x : 1.0f;
				const float met = hasMetal ? TexelOf(metal, x, y).x : 0.0f;
				mask[i] = XMFLOAT4(met, occ, 0.0f, 1.0f - r);
				if (hasHeight)
				{
					const float h = TexelOf(height, x, y).x;
					hgt[i] = XMFLOAT4(h, h, h, 1.0f);
				}
			}
		});
		auto levels = std::make_shared<std::array<std::vector<std::vector<uint8_t>>, 4>>();
		(*levels)[0] = BuildMips(std::move(base), n, false);
		(*levels)[1] = BuildMips(std::move(nrm), n, true);
		(*levels)[2] = BuildMips(std::move(mask), n, false);
		if (hasHeight)
			(*levels)[3] = BuildMips(std::move(hgt), n, false);
		out.Base = UploadLevels((*levels)[0], n);
		out.Normal = UploadLevels((*levels)[1], n);
		out.Mask = UploadLevels((*levels)[2], n);
		if (hasHeight)
			out.Height = UploadLevels((*levels)[3], n);
		EditorLog::Write("Building", "texture %s: from the texture pack as is (%d², %.1f ms) — compressing to the Library cache in the background", m.Id.c_str(), n,
			std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count());
		const std::string id = m.Id;
		const std::array<std::wstring, 4> paths = { files[0], files[1], files[2], files[3] };
		const int count = hasHeight ? 4 : 3;
		PinModule();
		s_CancelCompress = false;
		std::thread([levels, n, dir, paths, id, count]() {
			std::lock_guard<std::mutex> one(s_CompressLock);
			const auto c0 = std::chrono::steady_clock::now();
			const DXGI_FORMAT fmt[4] = { DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC4_UNORM };
			DirectX::ScratchImage packed[4];
			for (int i = 0; i < count; ++i)
				if (s_CancelCompress)
					return;
				else if (!Pack((*levels)[i], n, fmt[i], packed[i]))
				{
					EditorLog::Write("Building", "texture %s: BC compression failed (stays uncompressed)", id.c_str());
					return;
				}
			if (s_CancelCompress)
				return;   // 끝나는 중 — 반쯤 쓴 캐시를 남기지 않는다
			std::error_code ec;
			std::filesystem::create_directories(dir, ec);
			for (int i = 0; i < count; ++i)   // 임시 파일에 쓰고 이름 바꾸기 (도중에 끝나도 깨진 캐시가 남지 않게)
				if (SUCCEEDED(DirectX::SaveToDDSFile(packed[i].GetImages(), packed[i].GetImageCount(), packed[i].GetMetadata(), DirectX::DDS_FLAGS_NONE, (paths[i] + L".tmp").c_str())))
					std::filesystem::rename(paths[i] + L".tmp", paths[i], ec);
			EditorLog::Write("Building", "texture %s: cached as BC (%.1f s)", id.c_str(), std::chrono::duration<float>(std::chrono::steady_clock::now() - c0).count());
		}).detach();
		return out.Base && out.Normal && out.Mask;
	}

	// 테셀레이션 변위 (가까운 곳 — 노멀맵만으로 모자란 깊은 돌 · 기와 · 이엉): 재질 id 마다 높이 (m). 0 = 끔 (회벽 · 나무 · 금속 · 대리석)
	float DisplacementOf(const std::string& id)
	{
		auto has = [&](const char* s) { return id.find(s) != std::string::npos; };
		if (has("cyclopean")) return 0.07f;
		if (has("rubble") || has("fieldstone")) return 0.055f;
		if (has("polygonal") || has("sandstone")) return 0.04f;
		if (has("thatch")) return 0.06f;
		if (has("barrel") || has("clay-tiles")) return 0.05f;
		if (has("roof/flat-tiles") || has("dark-tiles") || has("glazed-tiles")) return 0.03f;
		if (has("fish-scale") || has("stone-scale")) return 0.03f;
		if (has("roof/slate")) return 0.02f;
		if (has("shingles") || has("shakes")) return 0.03f;
		if (has("ashlar")) return 0.018f;
		if (has("brick/")) return 0.014f;
		return 0.0f;
	}

	// 남은 압축 취소 (기다리지 않는다 — 다음에 그 재질을 쓸 때 다시 굽는다)
	void WaitPending()
	{
		s_CancelCompress = true;
	}

	std::map<std::string, Baked> s_Real;   // 텍스처 팩 재질 (id · 1 층 때)

	// rows > 0 = 1 층 때 무늬 (세로로 무늬 rows 번, 맨 아래로 때가 진해진다). 노멀은 세로 텍셀 크기를 따로
	Baked& Bake(Kind k, int rows = 0, int grimeLevel = 0)
	{
		const int key = (int)k * 1000 + rows * 10 + grimeLevel;
		auto it = s_Baked.find(key);
		if (it != s_Baked.end())
			return it->second;
		const auto t0 = std::chrono::steady_clock::now();
		const int n = SizeOf(k);
		const int vr = (std::max)(1, rows);
		const float grime = grimeLevel / 4.0f;
		std::vector<Sample> px((size_t)n * n);
		std::vector<int> lines(n);
		std::iota(lines.begin(), lines.end(), 0);
		std::for_each(std::execution::par, lines.begin(), lines.end(), [&](int y) {
			for (int x = 0; x < n; ++x)
			{
				const float u = (x + 0.5f) / n, v = (y + 0.5f) / n;
				Sample smp = Eval(k, u, Frac(v * vr));
				Age(k, u, v, vr, rows > 0 ? grime : 0.0f, smp);
				const PbrSpec pb = PbrOf(k);
				// 무늬의 Smooth 를 0 (틈 · 거친 곳) ~ 1 (가장 반들) 로 보고 재질 범위로 — 틈은 AO 로도 반사가 줄어든다
				const float t = Saturate(smp.Smooth / 0.6f);
				smp.Smooth = Lerp(pb.Rough, pb.Smooth, t) * Lerp(0.55f, 1.0f, Smooth(0.0f, 0.5f, smp.H));
				smp.Metal = Saturate(smp.Metal) * pb.Metal;
				px[(size_t)y * n + x] = smp;
			}
		});
		// 높이 → 노멀 (이어지게 감싼 중앙 차분, 낡은 느낌이 나게 1.6 배). 녹색 = B 방향 (아래 = 행이 느는 쪽)
		const Spec& sp = kSpecs[(int)k];
		const float texelU = sp.Tile / n, texelV = sp.Tile * vr / n;
		const float nk = PbrOf(k).Normal;
		const float su = sp.Depth * nk / (2.0f * texelU), sv = sp.Depth * nk / (2.0f * texelV);
		std::vector<XMFLOAT4> base((size_t)n * n), nrm((size_t)n * n), mask((size_t)n * n);
		for (int y = 0; y < n; ++y)
			for (int x = 0; x < n; ++x)
			{
				const Sample& smp = px[(size_t)y * n + x];
				const float hl = px[(size_t)y * n + Wrap(x - 1, n)].H, hr = px[(size_t)y * n + Wrap(x + 1, n)].H;
				const float hu = px[(size_t)Wrap(y - 1, n) * n + x].H, hd = px[(size_t)Wrap(y + 1, n) * n + x].H;
				float nx = -(hr - hl) * su, ny = -(hd - hu) * sv, nz = 1.0f;
				const float l = sqrtf(nx * nx + ny * ny + nz * nz);
				nx /= l; ny /= l; nz /= l;
				nrm[(size_t)y * n + x] = XMFLOAT4(nx * 0.5f + 0.5f, ny * 0.5f + 0.5f, nz * 0.5f + 0.5f, 1.0f);
				base[(size_t)y * n + x] = XMFLOAT4(Saturate(smp.R), Saturate(smp.G), Saturate(smp.B), 1.0f);
				const float ao = smp.Ao >= 0.0f ? smp.Ao : Lerp(0.38f, 1.0f, Smooth(0.03f, 0.6f, smp.H));
				mask[(size_t)y * n + x] = XMFLOAT4(smp.Metal, ao, 0.0f, smp.Smooth);
			}
		Baked b;
		b.Base = UploadLevels(BuildMips(std::move(base), n, false), n);
		b.Normal = UploadLevels(BuildMips(std::move(nrm), n, true), n);
		b.Mask = UploadLevels(BuildMips(std::move(mask), n, false), n);
		const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
		EditorLog::Write("Building", "baked %s textures (%d x %d + mips%s, %.1f ms)", sp.Name, n, n, rows > 0 ? ", ground-floor grime" : "", ms);
		return s_Baked[key] = b;
	}
}

namespace BuildingTextures
{
	const char* KindName(Kind k) { return kSpecs[std::clamp((int)k, 0, (int)Kind::Count - 1)].Name; }
	float TileMeters(Kind k) { return kSpecs[std::clamp((int)k, 0, (int)Kind::Count - 1)].Tile; }
	Kind WallKind(BuildingDesc::WallMat m) { return (Kind)std::clamp((int)m, 0, (int)Kind::Marble); }
	Kind RoofKind(BuildingDesc::RoofMat m) { return (Kind)std::clamp((int)Kind::ClayTiles + (int)m, (int)Kind::ClayTiles, (int)Kind::Turf); }

	Kind SlotKind(const BuildingDesc& d, int slot)
	{
		using namespace BuildingGenerator;
		switch (slot)
		{
		case WallLower: return WallKind(d.GroundWall);
		case WallUpper: return WallKind(d.UpperWall);
		case Trim: return d.GroundWall == BuildingDesc::WallMat::Marble || d.UpperWall == BuildingDesc::WallMat::Marble ? Kind::Marble : Kind::Ashlar;
		case Timber: return Kind::Timber;
		case Roof: return RoofKind(d.RoofMaterial);
		case Glass: return d.Lattice ? Kind::LeadedGlass : Kind::Glass;
		case Frame: return Kind::Timber;
		case Shutter: return Kind::DoorPlanks;
		case Door: return Kind::DoorPlanks;
		case Metal: return Kind::Metal;
		case Ornament: return Kind::Ornament;
		case Banner: return Kind::Banner;
		case Plants: return Kind::Foliage;
		default: return Kind::Fabric;
		}
	}

	void SlotColor(const BuildingDesc& d, int slot, float out[3])
	{
		using namespace BuildingGenerator;
		static const float kFabric[3] = { 0.86f, 0.82f, 0.72f }, kWhite[3] = { 1.0f, 1.0f, 1.0f };
		const float* c = kFabric;
		switch (slot)
		{
		case WallLower: c = d.WallColor; break;
		case WallUpper: c = d.UpperColor; break;
		case Trim: c = d.TrimColor; break;
		case Timber: c = d.TimberColor; break;
		case Roof: c = d.RoofColor; break;
		case Glass: c = d.GlassColor; break;
		case Frame: c = d.FrameColor; break;
		case Shutter: c = d.ShutterColor; break;
		case Door: c = d.DoorColor; break;
		case Metal: c = d.MetalColor; break;
		case Ornament: c = d.OrnamentColor; break;
		case Banner: c = d.BannerColor; break;
		case Plants: c = kWhite; break;
		default: break;
		}
		out[0] = c[0]; out[1] = c[1]; out[2] = c[2];
	}

	// 텍스처 팩 id → 물리 값을 고를 종류 (지붕 재질을 벽 칸에 써도 그 재질답게)
	Kind KindForId(const std::string& id, Kind fallback)
	{
		auto has = [&](const char* s) { return id.find(s) != std::string::npos; };
		if (has("roof/slate") || has("fish-scale")) return Kind::Slate;
		if (has("roof/clay")) return Kind::ClayTiles;
		if (has("thatch")) return Kind::Thatch;
		if (has("shingle")) return Kind::Shingles;
		if (has("marble")) return Kind::Marble;
		if (has("ashlar")) return Kind::Ashlar;
		if (has("stone/")) return Kind::Rubble;
		if (has("brick/")) return Kind::Brick;
		if (has("stucco")) return Kind::Stucco;
		if (has("plaster/")) return Kind::Plaster;
		if (has("beam") || has("grey-old-wood")) return Kind::Timber;
		if (has("wood/")) return fallback == Kind::DoorPlanks ? Kind::DoorPlanks : Kind::Planks;
		if (has("copper")) return Kind::Copper;
		if (has("lead")) return Kind::Lead;
		if (has("metal/")) return Kind::Metal;
		if (has("ground/") || has("nature/")) return Kind::Turf;
		return fallback;
	}

	const char* DefaultMaterialId(Kind k)
	{
		switch (k)
		{
		case Kind::Rubble: return "stone/rubble-wall";
		case Kind::Ashlar: return "stone/ashlar-weathered";
		case Kind::Brick: return "brick/old-red-brick";
		case Kind::Plaster: return "plaster/lime-plaster";
		case Kind::Stucco: return "plaster/smooth-stucco";
		case Kind::Planks: return "wood/vertical-boards-weathered";
		case Kind::Marble: return "stone/marble-white";
		case Kind::ClayTiles: return "roof/clay-tiles-weathered";
		case Kind::Slate: return "roof/slate";
		case Kind::Thatch: return "roof/thatch";
		case Kind::Shingles: return "roof/wood-shingles";
		case Kind::Copper: return "metal/copper-verdigris";
		case Kind::Lead: return "metal/lead-sheet";
		case Kind::Turf: return "ground/wild-grass";
		case Kind::Timber: return "wood/weathered-beam";
		case Kind::DoorPlanks: return "wood/vertical-boards-warm";
		case Kind::Metal: return "metal/black-iron";
		default: return "procedural";   // 통나무 (둥근 단) · 유리 · 천 · 칠한 무늬 · 깃발 · 잎
		}
	}

	std::string SlotMaterialId(const BuildingDesc& d, int slot)
	{
		using namespace BuildingGenerator;
		auto it = d.Materials.find(SlotKey(slot));
		if (it != d.Materials.end() && !it->second.empty())
			return it->second;
		if (slot == Trim)
			return SlotKind(d, slot) == Kind::Marble ? "stone/marble-white" : "stone/ashlar-grey";
		if (slot == Frame)
			return "wood/weathered-beam";
		return DefaultMaterialId(SlotKind(d, slot));
	}

	std::vector<std::string> SlotCategories(int slot)
	{
		using namespace BuildingGenerator;
		switch (slot)
		{
		case WallLower: case WallUpper: return { "stone", "brick", "plaster", "wood" };
		case Trim: return { "stone", "brick" };
		case Timber: case Frame: case Shutter: case Door: return { "wood" };
		case Roof: return { "roof", "metal", "ground" };
		case Metal: return { "metal" };
		default: return {};
		}
	}

	std::shared_ptr<UMaterial> SlotMaterial(const BuildingDesc& d, int slot)
	{
		float c[3];
		SlotColor(d, slot, c);
		const Kind slotKind = SlotKind(d, slot);
		const std::string id = SlotMaterialId(d, slot);
		const TextureLibrary::Material* lib = id == "procedural" ? nullptr : TextureLibrary::Find(id);
		const float tile = lib ? lib->TileMeters : TileMeters(slotKind);
		int rows = 0;
		float grime = 0.0f;
		if (slot == BuildingGenerator::WallLower && d.Weathering > 0.01f)
		{
			// 1 층 벽: 땅에서 위로 때 · 물 얼룩 · 이끼 (텍스처 한 장 = 1 층 높이, 무늬 크기는 그대로)
			rows = std::clamp((int)roundf((d.Plinth + d.FloorHeight) / (std::max)(0.3f, tile)), 1, 6);
			grime = d.Weathering;
		}
		if (lib)
			if (auto m = MaterialById(*lib, KindForId(lib->Id, slotKind), c, rows, grime))
				return m;
		return Material(slotKind, c, rows, grime);
	}

	std::shared_ptr<UMaterial> MaterialById(const TextureLibrary::Material& lib, Kind k, const float color[3], int grimeRows, float grime)
	{
		std::lock_guard<std::mutex> lock(s_Lock);
		const int q[3] = { (int)roundf(Saturate(color[0]) * 255), (int)roundf(Saturate(color[1]) * 255), (int)roundf(Saturate(color[2]) * 255) };
		(void)grimeRows; (void)grime; (void)q;
		const int useRows = 0, level = 0;
		const std::string texKey = lib.Id;
		const std::string key = texKey + ":" + std::to_string((int)k);
		auto it = s_Materials.find(key);
		if (it != s_Materials.end())
			return it->second;
		auto bt = s_Real.find(texKey);
		if (bt == s_Real.end())
		{
			Baked b;
			if (!BakeReal(k, lib, useRows, level, b))
				return nullptr;
			bt = s_Real.emplace(texKey, b).first;
		}
		auto m = std::make_shared<UMaterial>();
		PbrMaterial p;
		p.BaseColor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);   // 사진 색 그대로 (양식 색을 곱하지 않는다)
		p.Smoothness = 1.0f;     // 마스크 a = 1 - 거칠기 그대로
		p.Metallic = 0.0f;
		p.NormalScale = 1.0f;
		p.OcclusionStrength = 1.0f;
		const float t = 1.0f / (std::max)(0.1f, lib.TileMeters);
		p.Tiling = XMFLOAT2(t, t);
		UMaterial::Tessellation tess;
		const float disp = DisplacementOf(lib.Id);
		tess.Enabled = disp > 0.0f && bt->second.Height;
		tess.Amplitude = disp;
		tess.Base = 0.5f;
		tess.MaxFactor = 16.0f;
		tess.TriangleSize = 10.0f;
		tess.FadeDistance = 22.0f;   // 가까운 곳만 (멀리서는 노멀맵)
		m->SetRuntime("builtin:Building/" + lib.Id, p, bt->second.Base.Get(), bt->second.Normal.Get(), bt->second.Mask.Get(), bt->second.Mask.Get(),
			bt->second.Height.Get(), &tess);
		if (s_Materials.size() > 512)
			s_Materials.clear();
		s_Materials[key] = m;
		return m;
	}

	std::shared_ptr<UMaterial> Material(Kind k, const float color[3], int grimeRows, float grime)
	{
		std::lock_guard<std::mutex> lock(s_Lock);
		char key[96];
		const int q[3] = { (int)roundf(Saturate(color[0]) * 255), (int)roundf(Saturate(color[1]) * 255), (int)roundf(Saturate(color[2]) * 255) };
		const int rows = std::clamp(grimeRows, 0, 6);
		const int level = rows > 0 ? std::clamp((int)roundf(grime * 4.0f), 0, 4) : 0;
		const int useRows = level > 0 ? rows : 0;
		snprintf(key, sizeof(key), "%d:%d,%d,%d:%d:%d", (int)k, q[0], q[1], q[2], useRows, level);
		auto it = s_Materials.find(key);
		if (it != s_Materials.end())
			return it->second;
		Baked& b = Bake(k, useRows, level);
		auto m = std::make_shared<UMaterial>();
		PbrMaterial p;
		p.BaseColor = XMFLOAT4(q[0] / 255.0f, q[1] / 255.0f, q[2] / 255.0f, 1.0f);
		p.Smoothness = 1.0f;     // 마스크 a 가 그대로
		p.Metallic = 0.0f;
		p.NormalScale = 1.0f;
		p.OcclusionStrength = 1.0f;
		const float t = 1.0f / TileMeters(k);
		p.Tiling = XMFLOAT2(t, t / (float)(std::max)(1, useRows));
		if (k == Kind::Banner)
			p.Tiling = XMFLOAT2(1.0f, 1.0f);   // 깃발은 판 하나에 한 장 (UV 0..1)
		m->SetRuntime(std::string("builtin:Building/") + KindName(k), p, b.Base.Get(), b.Normal.Get(), b.Mask.Get(), b.Mask.Get());
		// 슬라이더로 색을 바꾸면 조합이 많아진다 — 512 개가 넘으면 비운다 (쓰는 중인 재질은 렌더러가 잡고 있다)
		if (s_Materials.size() > 512)
			s_Materials.clear();
		s_Materials[key] = m;
		return m;
	}

	GfxShaderResourceView* BaseMap(Kind k)
	{
		std::lock_guard<std::mutex> lock(s_Lock);
		return Bake(k).Base.Get();
	}

	int BakedCount()
	{
		std::lock_guard<std::mutex> lock(s_Lock);
		return (int)s_Baked.size();
	}

	void ClearCache()
	{
		WaitPending();
		std::lock_guard<std::mutex> lock(s_Lock);
		s_Materials.clear();
		s_Baked.clear();
		s_Real.clear();
	}
}
