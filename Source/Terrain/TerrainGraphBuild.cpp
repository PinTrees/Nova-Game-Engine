#include "pch.h"
#include "TerrainGraph.h"
#include "TerrainGraphNoise.h"
#include "TerrainTextureGen.h"
#include "TerrainData.h"
#include "Terrain.h"
#include "GameObject.h"
#include "JobSystem.h"
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>

using json = nlohmann::json;
namespace N = TerrainGraphNoise;

namespace TerrainGraph
{
	namespace
	{
		std::wstring AbsPath(const std::string& path)
		{
			const std::filesystem::path p(string_to_wstring(path));
			return p.is_absolute() ? p.wstring() : PathManager::GetI()->GetMovePathW(string_to_wstring(path));
		}

		// Catmull-Rom 무게 (t = 0..1)
		inline void CubicW(float t, float w[4])
		{
			const float t2 = t * t, t3 = t2 * t;
			w[0] = -0.5f * t3 + t2 - 0.5f * t;
			w[1] = 1.5f * t3 - 2.5f * t2 + 1.0f;
			w[2] = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
			w[3] = 0.5f * t3 - 0.5f * t2;
		}

		// 빌드 파일 머리
		struct FileHeader
		{
			char Magic[4] = { 'N', 'T', 'G', 'B' };
			uint32_t Version = 2;   // 2: 뒤에 색 (RGB8) · 강 · 길 선
			int32_t Res = 0;
			float WorldSize = 0.0f, MaxHeight = 0.0f;
			uint32_t Pad = 0;
			uint64_t GraphHash = 0;
		};
	}

	// ================================================================ 빌드한 월드 (표본)
	float Built::BaseHeight01(float x, float z) const
	{
		const float cell = World.WorldSize / (Res - 1);
		const float fx = std::clamp((x + World.WorldSize * 0.5f) / cell, 0.0f, (float)(Res - 1));
		const float fz = std::clamp((z + World.WorldSize * 0.5f) / cell, 0.0f, (float)(Res - 1));
		const int ix = (std::min)((int)fx, Res - 2), iz = (std::min)((int)fz, Res - 2);
		float wx[4], wz[4];
		CubicW(fx - ix, wx);
		CubicW(fz - iz, wz);
		const std::vector<float>& h = *Height;
		float sum = 0.0f;
		for (int j = 0; j < 4; ++j)
		{
			const size_t row = (size_t)std::clamp(iz - 1 + j, 0, Res - 1) * Res;
			float r = 0.0f;
			for (int i = 0; i < 4; ++i)
				r += wx[i] * h[row + std::clamp(ix - 1 + i, 0, Res - 1)];
			sum += wz[j] * r;
		}
		return sum;
	}

	float Built::Sample8(const std::vector<uint8_t>& m, float x, float z) const
	{
		if (m.empty())
			return 0.0f;
		const float cell = World.WorldSize / (Res - 1);
		const float fx = std::clamp((x + World.WorldSize * 0.5f) / cell, 0.0f, (float)(Res - 1));
		const float fz = std::clamp((z + World.WorldSize * 0.5f) / cell, 0.0f, (float)(Res - 1));
		const int ix = (std::min)((int)fx, Res - 2), iz = (std::min)((int)fz, Res - 2);
		const float tx = fx - ix, tz = fz - iz;
		const uint8_t* r0 = &m[(size_t)iz * Res + ix];
		const uint8_t* r1 = r0 + Res;
		return ((r0[0] * (1 - tx) + r0[1] * tx) * (1 - tz) + (r1[0] * (1 - tx) + r1[1] * tx) * tz) * (1.0f / 255.0f);
	}

	float Built::Height01(float x, float z, float footprint) const
	{
		const float base = BaseHeight01(x, z);
		if (World.DetailAmplitude <= 0.0f)
			return std::clamp(base, 0.0f, 1.0f);
		// 디테일: 가파른 곳 (빌드 경사) 은 거칠게, 퇴적된 골 · 평지는 매끈하게. 표본 간격보다 잔 결은 건너뛴다
		const float slope = Slope ? Sample8(*Slope, x, z) : 0.0f;
		const float dep = Deposits ? Sample8(*Deposits, x, z) : 0.0f;
		const float amp = World.DetailAmplitude * (0.3f + World.DetailSlope * (std::min)(1.0f, slope * 2.5f)) * (1.0f - World.DetailDeposits * dep);
		// 표본 간격의 4 배보다 잔 결은 건너뛴다 (2 배 = 나이퀴스트 그대로면 먼 타일 (129² · 7.8 m) 에 등고선 같은 무아레 띠가 생겼다)
		const double minWl = (std::max)(0.6, 4.0 * footprint);
		// 침식 필터 (골이 빌드 비탈을 따라 내려가며 가지 친다) + 잔 침식 결 — 둘 다 위치의 순수 함수 (타일 경계가 맞는다)
		const float e = 0.5f * World.MaxHeight / (World.WorldSize / (Res - 1));
		const float gx = (BaseHeight01(x + 6.0f, z) - BaseHeight01(x - 6.0f, z)) * World.MaxHeight / 12.0f;
		const float gz = (BaseHeight01(x, z + 6.0f) - BaseHeight01(x, z - 6.0f)) * World.MaxHeight / 12.0f;
		(void)e;
		const float steep = (std::min)(1.0f, sqrtf(gx * gx + gz * gz) * 1.8f);
		const uint32_t seed = (uint32_t)World.Seed * 2654435761u + 977u;
		const double scale = (std::max)(World.DetailScale, 1.0f);
		float gully = 0.0f;
		if (steep > 0.02f)
		{
			const float ev = N::ErosionFilter(x, z, gx, gz, seed + 31u, scale * 2.5, 6, minWl, amp * 2.0f * steep, 1.2f);
			gully = (ev < 0.0f ? ev : ev * 0.4f) * steep;
		}
		const float d = N::Eroded(x, z, seed, scale, minWl, World.DetailErosion * 4.0f);
		float h = base * World.MaxHeight + amp * (d * (1.0f - 0.6f * steep) + gully * 1.6f);
		// 절벽 결: 가파를수록 (기울기 0.4 ~ 0.9 = 22° ~ 42°) — 매끈한 큰 비탈면을 깬다. 산맥 리지가 만든 100 m 넘는 면도 있어
		//  깊이는 경사에 따라 (45° = Cliff Detail × 2.5)
		if (World.CliffDetail > 0.0f)
		{
			const float g = sqrtf(gx * gx + gz * gz);
			const float c = std::clamp((g - 0.4f) / 0.5f, 0.0f, 1.0f);
			if (c > 0.0f)
			{
				const float cs = c * c * (3.0f - 2.0f * c);
				const float A = World.CliffDetail * cs * (1.0f + 1.5f * (std::min)(g, 1.5f));
				// 1) 깨진 바위 능 · 홈 (리지 60 m)
				const int oct = std::clamp((int)std::floor(std::log2(60.0 / (std::max)(minWl, 0.6))) + 1, 0, 6);
				if (oct > 0)
				{
					const float r = N::Ridged(x / 60.0, z / 60.0, seed + 71u, oct, 0.55f, 2.2f);
					h += A * (r - 0.35f) * 1.2f;
				}
				// 2) 비탈을 따라 내려가는 낙석 홈 · 바위 갈비 (침식 필터를 절벽 크기 80 m 로 — 골은 깊게, 능은 얕게)
				const float rib = N::ErosionFilter(x, z, gx, gz, seed + 113u, 80.0, 4, minWl, A * 1.5f, 1.4f);
				h += A * 1.4f * (rib < 0.0f ? rib : rib * 0.6f);
				// 2b) 큰 면의 버팀벽 · 골 (220 m 간격, 비탈 따라): 깊이는 면 높이 (경사 방향 ±100 m 의 빌드 높이 차) 의 22 % (40 m 까지)
				//  — 산맥 리지가 만든 높이 100 m 넘는 긴 면이 매끈한 벽으로 남지 않게. 작은 언덕은 면이 낮아 거의 그대로
				{
					const float ux = gx / g, uz = gz / g;
					const float relief = fabsf(BaseHeight01(x + ux * 100.0f, z + uz * 100.0f) - BaseHeight01(x - ux * 100.0f, z - uz * 100.0f)) * World.MaxHeight;
					const float B = (std::min)(relief * 0.22f, 40.0f) * cs * (World.CliffDetail / 6.0f);
					if (B > 1.0f)
						h += B * N::ErosionFilter(x, z, gx, gz, seed + 191u, 220.0, 2, minWl, B, 1.0f);
				}
				// 3) 지층 턱: 16 m 마다 평평한 턱 + 가파른 벽 (층은 300 m 물결로 기운다). 턱의 수평 간격 (16 / 경사) 이
				//  표본보다 넉넉할 때만 (먼 타일에서는 계단이 무아레 띠가 된다)
				const float step = 16.0f;
				const float fade = std::clamp((float)((step / g - 3.0 * minWl) / (3.0 * minWl)), 0.0f, 1.0f);
				if (fade > 0.0f)
				{
					const float wob = N::Fbm(x / 300.0, z / 300.0, seed + 151u, 3) * 6.0f;
					const float lv = (h + wob) / step;
					const float fr = lv - floorf(lv);
					h += (fr * fr * fr - fr) * step * 0.6f * cs * fade;
				}
			}
		}
		ApplyPaths(x, z, footprint, h);
		return std::clamp(h / World.MaxHeight, 0.0f, 1.0f);
	}

	// 강 · 길 (타일 해상도): 강바닥 (포물선) · 둑, 길 노면 (평평) · 어깨 (깎고 메움)
	void Built::ApplyPaths(float x, float z, float footprint, float& h) const
	{
		if (Segments.empty())
			return;
		Near river, road;
		NearestPath(x, z, river, road);
		if (river.Dist < 1e8f)
		{
			const float hw = river.Width * 0.5f, bank = river.Width * 0.6f + 12.0f, depth = 1.2f + 3.5f * river.Order;
			if (river.Dist < hw)
			{
				const float u = river.Dist / hw;
				h = (std::min)(h, river.Y - depth * (1.0f - u * u));
			}
			else if (river.Dist < hw + bank)
			{
				const float u = (river.Dist - hw) / bank;
				const float target = river.Y + 0.4f + (river.Dist - hw) * 0.3f;
				const float blended = target + (h - target) * u * u;
				h = (std::min)(h, blended);
			}
		}
		if (road.Dist < 1e8f)
		{
			// 어깨 = 기본 + 높이 차 × 1.5 (비탈 1:1.5 — 깎은 면이 수직 벽이 되지 않게), 길 찾기 격자 여유 (너비 × 2 + 16) 안
			const float hw = road.Width * 0.5f;
			const float shoulder = (std::min)(road.Width * 1.5f + 4.0f + fabsf(h - road.Y) * 1.5f, road.Width * 1.5f + 14.0f);
			const float d = road.Dist;
			if (d < hw + shoulder)
			{
				const float u = std::clamp((d - hw) / shoulder, 0.0f, 1.0f);
				const float s2 = u * u * (3.0f - 2.0f * u);
				// 노면은 살짝 볼록 (물빠짐 — 가운데 6 cm)
				const float crown = d < hw ? 0.06f * (1.0f - (d / hw) * (d / hw)) : 0.0f;
				h = road.Y + crown + (h - road.Y) * s2;
			}
		}
		(void)footprint;
	}

	void Built::BuildPathIndex()
	{
		Segments.clear();
		Grid.clear();
		for (const std::vector<Path>* list : { &Rivers, &Roads })
			for (const Path& p : *list)
				for (size_t i = 0; i + 1 < p.Points.size(); ++i)
				{
					const PathPoint& a = p.Points[i];
					const PathPoint& c = p.Points[i + 1];
					Segments.push_back({ a.X, a.Z, a.Y, a.Width, c.X, c.Z, c.Y, c.Width, p.Kind, p.Order });
				}
		if (Segments.empty())
			return;
		GridCell = 250.0f;
		GridN = (std::max)(1, (int)ceilf(World.WorldSize / GridCell) + 1);
		Grid.assign((size_t)GridN * GridN, {});
		const float x0 = -World.WorldSize * 0.5f;
		for (int si = 0; si < (int)Segments.size(); ++si)
		{
			const Segment& s = Segments[(size_t)si];
			const float reach = (std::max)(s.W0, s.W1) * (s.Kind == 0 ? 1.1f : 2.0f) + 16.0f;
			const int ix0 = std::clamp((int)floorf(((std::min)(s.X0, s.X1) - reach - x0) / GridCell), 0, GridN - 1);
			const int ix1 = std::clamp((int)floorf(((std::max)(s.X0, s.X1) + reach - x0) / GridCell), 0, GridN - 1);
			const int iz0 = std::clamp((int)floorf(((std::min)(s.Z0, s.Z1) - reach - x0) / GridCell), 0, GridN - 1);
			const int iz1 = std::clamp((int)floorf(((std::max)(s.Z0, s.Z1) + reach - x0) / GridCell), 0, GridN - 1);
			for (int iz = iz0; iz <= iz1; ++iz)
				for (int ix = ix0; ix <= ix1; ++ix)
					Grid[(size_t)iz * GridN + ix].push_back(si);
		}
	}

	void Built::NearestPath(float x, float z, Near& river, Near& road) const
	{
		river = Near();
		road = Near();
		if (Grid.empty())
			return;
		const float x0 = -World.WorldSize * 0.5f;
		const int ix = (int)floorf((x - x0) / GridCell), iz = (int)floorf((z - x0) / GridCell);
		if (ix < 0 || iz < 0 || ix >= GridN || iz >= GridN)
			return;
		for (int si : Grid[(size_t)iz * GridN + ix])
		{
			const Segment& s = Segments[(size_t)si];
			const float dx = s.X1 - s.X0, dz = s.Z1 - s.Z0;
			const float l2 = dx * dx + dz * dz;
			const float t = l2 > 1e-9f ? std::clamp(((x - s.X0) * dx + (z - s.Z0) * dz) / l2, 0.0f, 1.0f) : 0.0f;
			const float qx = s.X0 + dx * t - x, qz = s.Z0 + dz * t - z;
			const float d = sqrtf(qx * qx + qz * qz);
			Near& nb = s.Kind == 0 ? river : road;
			if (d < nb.Dist)
			{
				nb.Dist = d;
				nb.Y = s.Y0 + (s.Y1 - s.Y0) * t;
				nb.Width = s.W0 + (s.W1 - s.W0) * t;
				nb.Order = s.Order;
			}
		}
	}

	bool Built::Color(float x, float z, uint8_t out[4]) const
	{
		if (!ColorMap || ColorMap->empty())
			return false;
		const float cell = World.WorldSize / (Res - 1);
		const float fx = std::clamp((x + World.WorldSize * 0.5f) / cell, 0.0f, (float)(Res - 1));
		const float fz = std::clamp((z + World.WorldSize * 0.5f) / cell, 0.0f, (float)(Res - 1));
		const int ix = (std::min)((int)fx, Res - 2), iz = (std::min)((int)fz, Res - 2);
		const float tx = fx - ix, tz = fz - iz;
		const uint8_t* r0 = &(*ColorMap)[((size_t)iz * Res + ix) * 3];
		const uint8_t* r1 = r0 + (size_t)Res * 3;
		for (int ch = 0; ch < 3; ++ch)
			out[ch] = (uint8_t)std::clamp((int)((r0[ch] * (1 - tx) + r0[3 + ch] * tx) * (1 - tz) + (r1[ch] * (1 - tx) + r1[3 + ch] * tx) * tz + 0.5f), 0, 255);
		float a = World.ColorStrength;
		// 길 · 강가는 텍스처 (흙 · 자갈) 색 그대로
		if (!Segments.empty())
		{
			Near river, road;
			NearestPath(x, z, river, road);
			if (road.Dist < 1e8f)
				a *= std::clamp((road.Dist - road.Width * 0.5f) / (road.Width + 2.0f), 0.0f, 1.0f);
			if (river.Dist < 1e8f)
				a *= std::clamp((river.Dist - river.Width * 0.5f) / (river.Width * 0.6f + 12.0f), 0.0f, 1.0f);
		}
		out[3] = (uint8_t)std::clamp((int)(a * 255.0f + 0.5f), 0, 255);
		return true;
	}

	void Built::Splat(float x, float z, float height01, float slopeCos, uint8_t out[4]) const
	{
		const float slopeDeg = acosf(std::clamp(slopeCos, 0.0f, 1.0f)) * 57.29578f;
		const float flow = Flow ? Sample8(*Flow, x, z) : 0.0f;
		const float dep = Deposits ? Sample8(*Deposits, x, z) : 0.0f;
		const float curv = Curvature ? Sample8(*Curvature, x, z) * 2.0f - 1.0f : 0.0f;
		float w[4];
		SplatRules(Layers, height01 * World.MaxHeight, slopeDeg, flow, dep, curv, x, z, World.Seed, w);
		if (!Segments.empty())
		{
			Near river, road;
			NearestPath(x, z, river, road);
			int dirt = 1;
			for (int i = 0; i < 4; ++i)
				if (TerrainTextureGen::KindFromName(Layers[i].Name) == TerrainTextureGen::Kind::Dirt) { dirt = i; break; }
			float paint = 0.0f;
			// 가장자리가 너무 곧지 않게 (1 m 노이즈)
			const float jitter = N::Fbm(x / 3.0, z / 3.0, (uint32_t)World.Seed + 991u, 2) * 0.8f;
			if (road.Dist < 1e8f)
				paint = (std::max)(paint, std::clamp(1.0f - (road.Dist + jitter - road.Width * 0.5f) / 1.5f, 0.0f, 1.0f));
			if (river.Dist < 1e8f)
				paint = (std::max)(paint, std::clamp(1.0f - (river.Dist + jitter * 2.0f - river.Width * 0.5f - 4.0f) / 6.0f, 0.0f, 1.0f));
			if (paint > 0.0f)
				for (int i = 0; i < 4; ++i)
					w[i] = w[i] * (1.0f - paint) + (i == dirt ? paint : 0.0f);
		}
		int total = 0, big = 0;
		for (int i = 0; i < 4; ++i)
		{
			out[i] = (uint8_t)std::clamp((int)(w[i] * 255.0f + 0.5f), 0, 255);
			total += out[i];
			if (out[i] > out[big]) big = i;
		}
		out[big] = (uint8_t)std::clamp((int)out[big] + (255 - total), 0, 255);
	}

	WorldGen::Biome Built::BiomeAt(float x, float z, float height01, float slopeCos) const
	{
		// PCG 의 바이옴 (규칙 이름으로): 바위 칸 = 바위 (돌무더기), 눈 칸 = 아무것도 (눈밭에 나무 · 돌을 뿌리지 않는다), 모래 = 사막,
		//  나머지 식생은 습도 (노이즈 + 물길) 로 숲 · 초원
		uint8_t s[4];
		Splat(x, z, height01, slopeCos, s);
		WorldGen::Biome b;
		float rock = 0.0f, snow = 0.0f, sand = 0.0f;
		for (int i = 0; i < 4; ++i)
		{
			const TerrainTextureGen::Kind k = TerrainTextureGen::KindFromName(Layers[i].Name);
			const float w = s[i] / 255.0f;
			if (k == TerrainTextureGen::Kind::Rock) rock += w;
			else if (k == TerrainTextureGen::Kind::Snow) snow += w;
			else if (k == TerrainTextureGen::Kind::Sand) sand += w;
		}
		b.Rock = rock * 0.6f;
		b.Desert = sand;
		b.Clear = std::clamp(snow * 1.3f - 0.15f, 0.0f, 1.0f);   // 눈밭 (눈 칸이 많은 곳)
		const float veg = (std::max)(0.0f, 1.0f - rock - snow - sand);
		const float flow = Flow ? Sample8(*Flow, x, z) : 0.0f;
		const float moisture = 0.5f + 0.5f * N::Fbm(x / 4500.0, z / 4500.0, (uint32_t)World.Seed * 31u + 77u, 3) + flow * 0.35f;
		const float t = std::clamp((moisture - 0.42f) / 0.16f, 0.0f, 1.0f);
		b.Forest = veg * t * t * (3.0f - 2.0f * t);
		b.Meadow = veg - b.Forest;
		// 기후: 해수면 기온 - 6.5 °C/km + 남북 차 + 작은 노이즈, 습도 = 설정 + 동서 차 + (물길 · 노이즈 습도 - 0.5)
		const float u = x / (std::max)(World.WorldSize, 1.0f), v = z / (std::max)(World.WorldSize, 1.0f);   // -0.5 ~ 0.5
		b.Temperature = World.ClimateTemperature - 6.5f * height01 * World.MaxHeight / 1000.0f + World.ClimateGradient * v
			+ 1.5f * N::Fbm(x / 6000.0, z / 6000.0, (uint32_t)World.Seed * 17u + 5u, 2);
		b.Moisture = std::clamp(World.ClimateMoisture + World.ClimateMoistureGradient * u + (moisture - 0.5f) * 0.6f, 0.0f, 1.0f);
		if (!Segments.empty())
		{
			Near river, road;
			NearestPath(x, z, river, road);
			float keep = 1.0f;
			if (road.Dist < 1e8f)
				keep = (std::min)(keep, std::clamp((road.Dist - road.Width * 0.5f - 2.0f) / 6.0f, 0.0f, 1.0f));
			if (river.Dist < 1e8f)
			{
				keep = (std::min)(keep, std::clamp((river.Dist - river.Width * 0.5f - 3.0f) / 8.0f, 0.0f, 1.0f));
				b.Water = (std::max)(b.Water, std::clamp(1.0f - (river.Dist - river.Width * 0.5f) / 60.0f, 0.0f, 1.0f));   // 물가 60 m
			}
			b.Forest *= keep;
			b.Meadow *= keep;
			b.Rock *= keep;
			b.Desert *= keep;
			b.Clear = (std::max)(b.Clear, 1.0f - keep);   // 길 · 강가
		}
		return b;
	}

	uint64_t Built::Hash() const
	{
		return (GraphHash * 0x9E3779B97F4A7C15ull) ^ (RulesHash * 0xC2B2AE3D27D4EB4Full) ^ FileStamp;
	}

	std::vector<std::string> Built::LayerPaths() const
	{
		std::vector<std::string> out;
		for (const LayerRule& r : Layers)
			out.push_back(r.Layer);
		return out;
	}

	size_t Built::Bytes() const
	{
		size_t b = Height ? Height->size() * 4 : 0;
		for (const auto* m : { &Flow, &Deposits, &Wear, &Slope, &Curvature, &ColorMap })
			if (*m) b += (*m)->size();
		b += Segments.size() * sizeof(Segment);
		return b;
	}

	std::string BuildFilePath(const std::string& graphPath) { return graphPath + ".build"; }

	// ================================================================ 빌드 파일 → Built (캐시)
	namespace
	{
		struct BuiltEntry
		{
			std::shared_ptr<Built> Value;
			uint64_t Stamp = 0;
			uint64_t RulesHash = 0;
			// 기다리지 않는 읽기 (World Terrain · 창): 작업 스레드가 파일을 읽어 여기에 둔다
			std::shared_ptr<Built> Base;
			uint64_t BaseStamp = 0, FailedStamp = 0;
			bool Loading = false;
		};
		std::mutex s_BuiltLock;
		std::map<std::string, BuiltEntry> s_Built;

		uint64_t FileStampOf(const std::wstring& file)
		{
			std::error_code ec;
			const auto t = std::filesystem::last_write_time(file, ec);
			return ec ? 0 : (uint64_t)t.time_since_epoch().count();
		}

		// 경사 · 굴곡 맵 (빌드 해상도, 8 비트)
		void DeriveMaps(Built& b)
		{
			const int res = b.Res;
			const float cell = b.World.WorldSize / (res - 1);
			const std::vector<float>& h = *b.Height;
			auto slope = std::make_shared<std::vector<uint8_t>>((size_t)res * res);
			auto curv = std::make_shared<std::vector<uint8_t>>((size_t)res * res);
			const float k = b.World.MaxHeight / (2.0f * cell);
			Jobs::ParallelFor(res, 8, [&](int z0, int z1) {
				for (int z = z0; z < z1; ++z)
					for (int x = 0; x < res; ++x)
					{
						auto at = [&](int xx, int zz) { return h[(size_t)std::clamp(zz, 0, res - 1) * res + std::clamp(xx, 0, res - 1)]; };
						const float gx = (at(x + 1, z) - at(x - 1, z)) * k, gz = (at(x, z + 1) - at(x, z - 1)) * k;
						const float deg = atanf(sqrtf(gx * gx + gz * gz)) * 57.29578f;
						(*slope)[(size_t)z * res + x] = (uint8_t)std::clamp((int)(deg / 90.0f * 255.0f + 0.5f), 0, 255);
						// 굴곡: 둘레 (2 칸) 평균 - 나 (m) → 반 칸 높이를 1 로
						float avg = 0.0f;
						for (int dz = -2; dz <= 2; ++dz)
							for (int dx = -2; dx <= 2; ++dx)
								avg += at(x + dx, z + dz);
						avg /= 25.0f;
						const float c = std::clamp((avg - at(x, z)) * b.World.MaxHeight / (cell * 0.25f), -1.0f, 1.0f);
						(*curv)[(size_t)z * res + x] = (uint8_t)std::clamp((int)(128.0f + c * 127.0f), 0, 255);
					}
			}, "TerrainGraph Derive");
			b.Slope = slope;
			b.Curvature = curv;
		}

		std::shared_ptr<Built> LoadBuildFile(const std::wstring& file)
		{
			std::ifstream in(file, std::ios::binary);
			if (!in)
				return nullptr;
			FileHeader hdr;
			in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
			if (!in || memcmp(hdr.Magic, "NTGB", 4) != 0 || (hdr.Version != 1 && hdr.Version != 2) || hdr.Res < 16 || hdr.Res > 16385)
				return nullptr;
			auto b = std::make_shared<Built>();
			b->Res = hdr.Res;
			b->World.WorldSize = hdr.WorldSize;
			b->World.MaxHeight = hdr.MaxHeight;
			b->GraphHash = hdr.GraphHash;
			const size_t count = (size_t)hdr.Res * hdr.Res;
			std::vector<uint16_t> h16(count);
			in.read(reinterpret_cast<char*>(h16.data()), (std::streamsize)(count * 2));
			auto height = std::make_shared<std::vector<float>>(count);
			for (size_t i = 0; i < count; ++i)
				(*height)[i] = h16[i] * (1.0f / 65535.0f);
			b->Height = height;
			for (auto* m : { &b->Flow, &b->Deposits, &b->Wear })
			{
				auto v = std::make_shared<std::vector<uint8_t>>(count);
				in.read(reinterpret_cast<char*>(v->data()), (std::streamsize)count);
				*m = v;
			}
			if (!in)
				return nullptr;
			if (hdr.Version >= 2)
			{
				uint32_t flags = 0;
				in.read(reinterpret_cast<char*>(&flags), 4);
				if (flags & 1u)
				{
					auto col = std::make_shared<std::vector<uint8_t>>(count * 3);
					in.read(reinterpret_cast<char*>(col->data()), (std::streamsize)(count * 3));
					b->ColorMap = col;
				}
				uint32_t paths = 0;
				in.read(reinterpret_cast<char*>(&paths), 4);
				for (uint32_t i = 0; i < paths && in; ++i)
				{
					Path p;
					uint32_t n = 0;
					in.read(reinterpret_cast<char*>(&p.Kind), 4);
					in.read(reinterpret_cast<char*>(&p.Order), 4);
					in.read(reinterpret_cast<char*>(&n), 4);
					if (n > 1000000u)
						return nullptr;
					p.Points.resize(n);
					in.read(reinterpret_cast<char*>(p.Points.data()), (std::streamsize)(n * sizeof(PathPoint)));
					(p.Kind == 0 ? b->Rivers : p.Kind == 1 ? b->Roads : b->Lakes).push_back(std::move(p));
				}
				if (!in)
					return nullptr;
			}
			DeriveMaps(*b);
			b->BuildPathIndex();
			return b;
		}
	}

	std::shared_ptr<const Built> GetBuilt(const std::string& graphPath, bool wait)
	{
		if (graphPath.empty())
			return nullptr;
		std::shared_ptr<Graph> g = Load(graphPath);
		if (!g)
			return nullptr;
		const std::wstring file = AbsPath(BuildFilePath(graphPath));
		const uint64_t stamp = FileStampOf(file);
		if (stamp == 0)
			return nullptr;
		const uint64_t rules = g->RulesHash();
		std::unique_lock<std::mutex> lock(s_BuiltLock);
		BuiltEntry& e = s_Built[graphPath];   // map 의 값은 자리가 바뀌지 않는다 (잠금을 풀었다 다시 잡아도)
		if (e.Value && e.Stamp == stamp && e.RulesHash == rules)
			return e.Value;
		std::shared_ptr<Built> base;
		if (e.Value && e.Stamp == stamp)
			base = e.Value;
		else if (e.Base && e.BaseStamp == stamp)
			base = e.Base;
		else if (!wait)
		{
			// 메인 스레드를 멈추지 않는다 (4097² 읽기 + 경사 · 굴곡 = Debug 3 s): 작업 스레드에서 읽고, 그동안은 옛 빌드 (없으면 nullptr)
			if (!e.Loading && e.FailedStamp != stamp)
			{
				e.Loading = true;
				Jobs::Run([graphPath, file, stamp]() {
					std::shared_ptr<Built> loaded = LoadBuildFile(file);
					std::lock_guard<std::mutex> l(s_BuiltLock);
					BuiltEntry& x = s_Built[graphPath];
					x.Loading = false;
					if (loaded) { x.Base = loaded; x.BaseStamp = stamp; }
					else x.FailedStamp = stamp;
				}, nullptr, Jobs::Priority::Background, "TerrainGraph Load Build");
			}
			return e.Value;
		}
		else
		{
			lock.unlock();
			base = LoadBuildFile(file);
			lock.lock();
		}
		if (!base)
			return nullptr;
		// 같은 맵 (공유) + 그래프의 지금 타일 값 · 규칙
		auto b = std::make_shared<Built>(*base);
		const float worldSize = base->World.WorldSize, maxHeight = base->World.MaxHeight;
		b->World = g->World;
		b->World.WorldSize = worldSize;      // 빌드한 값 (그래프를 바꿔도 다시 빌드 전까지는 그대로)
		b->World.MaxHeight = maxHeight;
		for (int i = 0; i < 4; ++i)
			b->Layers[i] = g->Layers[i];
		b->RulesHash = rules;
		b->FileStamp = stamp;
		e.Value = b;
		e.Stamp = stamp;
		e.RulesHash = rules;
		e.Base.reset();
		EditorLog::Write("TerrainGraph", "build loaded %s: %d², %.1f km, max height %.0f m, %.1f MB", graphPath.c_str(), b->Res, worldSize / 1000.0f, maxHeight, b->Bytes() / 1048576.0);
		return b;
	}

	// ================================================================ 빌드 (작업 스레드)
	namespace
	{
		struct BuildState
		{
			std::mutex Lock;
			BuildStatus Status;
			std::atomic<bool> Cancel{ false };
		};
		std::mutex s_BuildLock;
		std::map<std::string, std::shared_ptr<BuildState>> s_Builds;

		std::shared_ptr<BuildState> StateOf(const std::string& path)
		{
			std::lock_guard<std::mutex> lock(s_BuildLock);
			auto& s = s_Builds[path];
			if (!s)
				s = std::make_shared<BuildState>();
			return s;
		}

		bool WriteBuild(const std::wstring& file, const Graph& g, const EvalResult& r, int res, std::string& error)
		{
			FileHeader hdr;
			hdr.Res = res;
			hdr.WorldSize = g.World.WorldSize;
			hdr.MaxHeight = g.World.MaxHeight;
			hdr.GraphHash = g.ContentHash();
			const size_t count = (size_t)res * res;
			std::vector<uint16_t> h16(count);
			for (size_t i = 0; i < count; ++i)
				h16[i] = (uint16_t)std::clamp((int)(std::clamp(r.Height->V[i], 0.0f, 1.0f) * 65535.0f + 0.5f), 0, 65535);
			const std::wstring tmp = file + L".tmp";
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
			{
				std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
				if (!out)
				{
					error = "could not write " + wstring_to_string(tmp);
					return false;
				}
				out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
				out.write(reinterpret_cast<const char*>(h16.data()), (std::streamsize)(count * 2));
				std::vector<uint8_t> b8(count);
				for (const MapPtr& m : { r.Flow, r.Deposits, r.Wear })
				{
					if (m)
						for (size_t i = 0; i < count; ++i)
							b8[i] = (uint8_t)std::clamp((int)(m->V[i] * 255.0f + 0.5f), 0, 255);
					else
						std::fill(b8.begin(), b8.end(), (uint8_t)0);
					out.write(reinterpret_cast<const char*>(b8.data()), (std::streamsize)count);
				}
				// v2: 색 (RGB8 sRGB) · 강 · 길 선
				const uint32_t flags = r.Color ? 1u : 0u;
				out.write(reinterpret_cast<const char*>(&flags), 4);
				if (r.Color)
				{
					std::vector<uint8_t> rgb(count * 3);
					for (size_t i = 0; i < count * 3; ++i)
						rgb[i] = (uint8_t)std::clamp((int)(powf(std::clamp(r.Color->V[i], 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f + 0.5f), 0, 255);
					out.write(reinterpret_cast<const char*>(rgb.data()), (std::streamsize)rgb.size());
				}
				const uint32_t paths = (uint32_t)(r.Rivers.size() + r.Roads.size() + r.Lakes.size());
				out.write(reinterpret_cast<const char*>(&paths), 4);
				for (const std::vector<Path>* list : { &r.Rivers, &r.Roads, &r.Lakes })
					for (const Path& p : *list)
					{
						const uint32_t n = (uint32_t)p.Points.size();
						out.write(reinterpret_cast<const char*>(&p.Kind), 4);
						out.write(reinterpret_cast<const char*>(&p.Order), 4);
						out.write(reinterpret_cast<const char*>(&n), 4);
						out.write(reinterpret_cast<const char*>(p.Points.data()), (std::streamsize)(n * sizeof(PathPoint)));
					}
				if (!out)
				{
					error = "write failed";
					return false;
				}
			}
			std::filesystem::rename(tmp, file, ec);
			if (ec)
			{
				std::filesystem::remove(file, ec);
				std::filesystem::rename(tmp, file, ec);
			}
			if (ec)
			{
				error = "could not replace " + wstring_to_string(file);
				return false;
			}
			return true;
		}
	}

	bool StartBuild(const std::string& graphPath, int resolution)
	{
		std::shared_ptr<Graph> g = Load(graphPath);
		if (!g)
			return false;
		std::shared_ptr<BuildState> st = StateOf(graphPath);
		{
			std::lock_guard<std::mutex> lock(st->Lock);
			if (st->Status.Running)
				return false;
			st->Status = BuildStatus();
			st->Status.Running = true;
			st->Status.Stage = "Starting";
			st->Status.Resolution = resolution > 0 ? resolution : g->World.BuildResolution;
		}
		st->Cancel = false;
		auto snapshot = std::make_shared<Graph>(*g);
		const std::wstring file = AbsPath(BuildFilePath(graphPath));
		std::thread([st, snapshot, file, graphPath]() {
			const auto t0 = std::chrono::steady_clock::now();
			EvalContext ctx;
			{
				std::lock_guard<std::mutex> lock(st->Lock);
				ctx.Res = st->Status.Resolution;
			}
			ctx.Res = std::clamp(ctx.Res, 65, 16385);
			ctx.WorldSize = snapshot->World.WorldSize;
			ctx.MaxHeight = snapshot->World.MaxHeight;
			ctx.Seed = snapshot->World.Seed;
			ctx.Cancel = &st->Cancel;
			ctx.Progress = [st](float p, const char* stage) {
				std::lock_guard<std::mutex> lock(st->Lock);
				st->Status.Progress = (std::max)(st->Status.Progress, std::clamp(p, 0.0f, 1.0f) * 0.97f);
				st->Status.Stage = stage ? stage : "";
			};
			EditorLog::Write("TerrainGraph", "build start %s: %d² (%.1f km, cell %.1f m)", graphPath.c_str(), ctx.Res, ctx.WorldSize / 1000.0f, ctx.Cell());
			EvalResult r = Evaluate(*snapshot, ctx);
			std::string error = r.Error;
			if (!r.Cancelled && error.empty())
			{
				{
					std::lock_guard<std::mutex> lock(st->Lock);
					st->Status.Stage = "Writing";
				}
				WriteBuild(file, *snapshot, r, ctx.Res, error);
			}
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			std::lock_guard<std::mutex> lock(st->Lock);
			st->Status.Running = false;
			st->Status.Ms = ms;
			st->Status.Error = r.Cancelled ? "cancelled" : error;
			st->Status.Done = !r.Cancelled && error.empty();
			st->Status.Progress = st->Status.Done ? 1.0f : st->Status.Progress;
			st->Status.Stage = st->Status.Done ? "Done" : st->Status.Error;
			std::string nodes;
			for (const auto& [id, nms] : r.NodeMs)
				if (nms > 200.0)
				{
					const Node* n = snapshot->Find(id);
					const NodeDesc* d = n ? FindDesc(n->Type) : nullptr;
					char buf[96];
					snprintf(buf, sizeof(buf), " %s %.1fs", d ? d->Label : "?", nms / 1000.0);
					nodes += buf;
				}
			EditorLog::Write("TerrainGraph", "build %s %s in %.1f s (%d²)%s", graphPath.c_str(), st->Status.Done ? "done" : st->Status.Error.c_str(), ms / 1000.0, ctx.Res, nodes.c_str());
		}).detach();
		return true;
	}

	void CancelBuild(const std::string& graphPath)
	{
		StateOf(graphPath)->Cancel = true;
	}

	BuildStatus GetBuildStatus(const std::string& graphPath)
	{
		std::shared_ptr<BuildState> st = StateOf(graphPath);
		std::lock_guard<std::mutex> lock(st->Lock);
		return st->Status;
	}

	// ================================================================ 여러 Terrain 에 적용
	int ApplyToTerrains(const Built& b, const std::vector<Terrain*>& terrains, bool heights, bool splat)
	{
		std::vector<std::shared_ptr<TerrainLayer>> layers;
		if (splat)
			for (const LayerRule& r : b.Layers)
				if (auto l = TerrainLayer::Load(r.Layer))
					layers.push_back(l);
		int count = 0;
		for (Terrain* t : terrains)
		{
			std::shared_ptr<TerrainData> data = t ? t->GetTerrainData() : nullptr;
			if (!data)
				continue;
			const Vec3 pos = t->GetPosition();
			const int res = data->HeightmapResolution;
			const Vec3 size = data->Size;
			const float cx = size.x / (res - 1), cz = size.z / (res - 1);
			if (heights)
			{
				Jobs::ParallelFor(res, 8, [&](int z0, int z1) {
					for (int z = z0; z < z1; ++z)
						for (int x = 0; x < res; ++x)
						{
							const float h = b.Height01(pos.x + x * cx, pos.z + z * cz, (std::max)(cx, cz)) * b.World.MaxHeight;
							data->Heights[(size_t)z * res + x] = std::clamp((h - pos.y) / (std::max)(size.y, 1.0f), 0.0f, 1.0f);
						}
				}, "TerrainGraph Apply");
				data->OnHeightsChanged(0, 0, res - 1, res - 1);
			}
			if (splat && layers.size() == 4)
			{
				data->Layers = layers;
				const int cr = data->ControlResolution;
				data->Control.assign((size_t)cr * cr * 4, 0);
				Jobs::ParallelFor(cr, 8, [&](int z0, int z1) {
					for (int z = z0; z < z1; ++z)
						for (int x = 0; x < cr; ++x)
						{
							const float lx = (x + 0.5f) * size.x / cr, lz = (z + 0.5f) * size.z / cr;
							const float h01 = (data->GetHeight(lx, lz) + pos.y) / b.World.MaxHeight;
							const float ny = data->GetNormal(lx, lz).y;
							b.Splat(pos.x + lx, pos.z + lz, h01, ny, &data->Control[((size_t)z * cr + x) * 4]);
						}
				}, "TerrainGraph Apply Splat");
				data->OnControlChanged(0, 0, cr - 1, cr - 1);
				if (b.ColorMap)
				{
					std::vector<uint8_t> color((size_t)cr * cr * 4);
					for (int z = 0; z < cr; ++z)
						for (int x = 0; x < cr; ++x)
							b.Color(pos.x + (x + 0.5f) * size.x / cr, pos.z + (z + 0.5f) * size.z / cr, &color[((size_t)z * cr + x) * 4]);
					data->SetColorMap(std::move(color));
				}
			}
			data->Dirty = true;
			++count;
		}
		return count;
	}

	// ================================================================ 미리 보기 그림
	void PreviewImage(const Map& height, const Map* mask, PreviewMode mode, const Graph& g, const Map* flow, const Map* deposits, std::vector<uint8_t>& rgba,
		const Map* color)
	{
		const int res = height.Res;
		rgba.assign((size_t)res * res * 4, 255);
		const float cell = g.World.WorldSize / (res - 1);
		const float k = g.World.MaxHeight / (2.0f * cell);
		const float lx = -0.55f, ly = 0.7f, lz = 0.45f;   // 해: 왼쪽 위 (그림의 위 = 북쪽 +Z)
		static const float kLayerColor[4][3] = { { 0.33f, 0.45f, 0.20f }, { 0.47f, 0.37f, 0.25f }, { 0.50f, 0.48f, 0.46f }, { 0.93f, 0.95f, 0.98f } };
		Jobs::ParallelFor(res, 8, [&](int z0, int z1) {
			for (int z = z0; z < z1; ++z)
				for (int x = 0; x < res; ++x)
				{
					const float gx = (height.At(x + 1, z) - height.At(x - 1, z)) * k, gz = (height.At(x, z + 1) - height.At(x, z - 1)) * k;
					const float inv = 1.0f / sqrtf(gx * gx + gz * gz + 1.0f);
					const float nx = -gx * inv, ny = inv, nz = -gz * inv;
					const float shade = 0.3f + 0.7f * (std::max)(0.0f, nx * lx + ny * ly + nz * lz) / 0.95f;
					const float h = height.At(x, z);
					float c[3];
					if (mode == PreviewMode::Color && color && color->Channels == 3 && color->Res == res)
					{
						const size_t ci = ((size_t)z * res + x) * 3;
						for (int i = 0; i < 3; ++i)
							c[i] = powf(color->V[ci + i], 1.0f / 2.2f) * (0.45f + 0.75f * shade);
					}
					else if (mode == PreviewMode::Mask && mask)
					{
						const float m = std::clamp(mask->At(x, z), 0.0f, 1.0f);
						const float s = 0.75f + 0.25f * shade;
						c[0] = c[1] = c[2] = m * s;
					}
					else if (mode == PreviewMode::Splat)
					{
						float w[4];
						const float slopeDeg = atanf(sqrtf(gx * gx + gz * gz)) * 57.29578f;
						const double wx = -g.World.WorldSize * 0.5 + x * cell, wz = -g.World.WorldSize * 0.5 + z * cell;
						SplatRules(g.Layers, h * g.World.MaxHeight, slopeDeg, flow ? flow->At(x, z) : 0.0f, deposits ? deposits->At(x, z) : 0.0f, 0.0f, (float)wx, (float)wz, g.World.Seed, w);
						for (int i = 0; i < 3; ++i)
							c[i] = (w[0] * kLayerColor[0][i] + w[1] * kLayerColor[1][i] + w[2] * kLayerColor[2][i] + w[3] * kLayerColor[3][i]) * shade * 1.15f;
					}
					else
					{
						// 높이 색 (낮은 초록 → 갈색 → 회색 → 흰색) × 음영
						const float t = std::clamp(h, 0.0f, 1.0f);
						const float low[3] = { 0.30f, 0.40f, 0.22f }, mid[3] = { 0.52f, 0.45f, 0.33f }, high[3] = { 0.62f, 0.61f, 0.60f }, top[3] = { 0.95f, 0.96f, 0.98f };
						for (int i = 0; i < 3; ++i)
							c[i] = t < 0.4f ? low[i] + (mid[i] - low[i]) * (t / 0.4f) : t < 0.75f ? mid[i] + (high[i] - mid[i]) * ((t - 0.4f) / 0.35f) : high[i] + (top[i] - high[i]) * ((t - 0.75f) / 0.25f);
						for (float& v : c)
							v *= shade * 1.1f;
					}
					uint8_t* p = &rgba[((size_t)(res - 1 - z) * res + x) * 4];   // 위 = +Z
					for (int i = 0; i < 3; ++i)
						p[i] = (uint8_t)std::clamp((int)(powf(std::clamp(c[i], 0.0f, 1.0f), 1.0f / 1.1f) * 255.0f), 0, 255);
					p[3] = 255;
				}
		}, "TerrainGraph Preview");
	}
}
