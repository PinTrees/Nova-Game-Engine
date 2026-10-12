#include "pch.h"
#include "TerrainGraph.h"
#include "JobSystem.h"

// 지형 그래프의 색: SatMap 팔레트 (Gaea 처럼 위성 사진에서 뽑은 듯한 색 띠) · 3D 미리 보기 (CPU 광선 행진)
namespace TerrainGraph
{
	const std::vector<Palette>& Palettes()
	{
		// 아래 (0) → 위 (1). 실제 지표 색 (위성 · 항공 사진의 바위 · 풀 · 모래 · 눈) 을 손으로 고른 멈춤점
		static const std::vector<Palette> kPalettes = {
			{ "Granite Grey", "Rock", { 0x3b3a38, 0x55524d, 0x6e6a63, 0x8a857c, 0xa39d93, 0xbdb7ac, 0xd8d3c9 } },
			{ "Alpine Rock", "Rock", { 0x2e2f31, 0x4a4a48, 0x66625a, 0x7d776c, 0x958e81, 0xb2ab9f, 0xe3e1dc } },
			{ "Basalt", "Rock", { 0x1d1d1f, 0x2c2b2b, 0x3c3a37, 0x4f4b45, 0x625c53, 0x777064 } },
			{ "Red Canyon", "Rock", { 0x4a2618, 0x6b3a22, 0x8c4f2c, 0xa8673a, 0xbf8050, 0xd39f72, 0xe2c19a } },
			{ "Sandstone", "Rock", { 0x6e4a2f, 0x8d623f, 0xa87b52, 0xc09469, 0xd2ad85, 0xe0c7a5 } },
			{ "Limestone", "Rock", { 0x7d7568, 0x958c7d, 0xada392, 0xc2b9a7, 0xd6cfbf, 0xe7e2d6 } },
			{ "Slate", "Rock", { 0x2f3236, 0x41454a, 0x565a5e, 0x6b6e70, 0x7f8282, 0x979894 } },
			{ "Volcanic", "Rock", { 0x1a1515, 0x2e2420, 0x4a3428, 0x6b4a35, 0x3a2e2a, 0x8a7d70 } },
			{ "Dolomite", "Rock", { 0x5e5850, 0x7a7266, 0x958c7e, 0xb0a796, 0xc9c1b0, 0xe6e2da } },
			{ "Desert", "Sand", { 0x8a6a45, 0xa6814f, 0xbf9a62, 0xd1b07a, 0xddc294, 0xe8d5b0 } },
			{ "Dunes", "Sand", { 0xa0703f, 0xbb8a52, 0xcfa066, 0xddb47c, 0xe8c896 } },
			{ "Beach", "Sand", { 0x9a8a6c, 0xb5a383, 0xcbb998, 0xddcdb0, 0xece0c8 } },
			{ "Arid Mountains", "Sand", { 0x5a4634, 0x7a5f45, 0x967857, 0xad9070, 0xc4aa8c, 0xdcc8ad } },
			{ "Badlands", "Sand", { 0x6b4b3a, 0x8f6249, 0xa97b5a, 0x9a7a62, 0xc29a78, 0xdbbf9f } },
			{ "Alpine Meadow", "Green", { 0x2f3a1c, 0x46552a, 0x5f6e36, 0x7c8546, 0x8f8f5a, 0xa49c78, 0xc7c2b2 } },
			{ "Temperate Forest", "Green", { 0x1f2a14, 0x2c3a1b, 0x3c4b24, 0x4f5c30, 0x66703f, 0x7f8153 } },
			{ "Tundra", "Green", { 0x4e5040, 0x66674f, 0x7e7d62, 0x948f76, 0xaaa28c, 0xc3bca9 } },
			{ "Highlands", "Green", { 0x34401f, 0x4b5628, 0x646a35, 0x7d7a4a, 0x8f8566, 0x9d9688, 0xbdb8b0, 0xeeeeec } },
			{ "Mossy", "Green", { 0x263018, 0x37451f, 0x4a5a2a, 0x5f6d38, 0x76804a } },
			{ "Savanna", "Green", { 0x5d5a2c, 0x7b733b, 0x978a4b, 0xae9d5f, 0xc2b076 } },
			{ "Iceland", "Green", { 0x22261f, 0x3a4430, 0x5b6a3c, 0x7d8a54, 0xa9ab8c, 0xdcdcd6 } },
			{ "Patagonia", "Green", { 0x4b4a3c, 0x6a6650, 0x8b8465, 0xa99f7f, 0xc6bea4, 0xeceae6 } },
			{ "Glacier", "Blue", { 0x5d6a78, 0x7c8a99, 0x9eabb8, 0xbfc9d3, 0xdde4ea, 0xf4f7f9 } },
			{ "Lake", "Blue", { 0x1c2c38, 0x24404f, 0x2e5566, 0x3e6b7a, 0x5a8590 } },
			{ "Snow", "Blue", { 0x9aa3ad, 0xb7bfc8, 0xcfd6dd, 0xe3e8ec, 0xf5f7f8 } },
			{ "Autumn", "Color", { 0x3d2a17, 0x6b3f1c, 0x9a5a22, 0xc27a2c, 0xd6a046, 0xe2c27a } },
			{ "Earth", "Color", { 0x3a2f22, 0x5a4632, 0x7a6045, 0x978066, 0xb39f86, 0xd1c3ad } },
			{ "Loam", "Color", { 0x2a2219, 0x3f3324, 0x574631, 0x6f5a40, 0x87704f } },
			{ "Ochre", "Color", { 0x5a3a1a, 0x7f5323, 0xa16d2e, 0xbf8a3e, 0xd5a85b } },
			{ "Heather", "Color", { 0x3a2f35, 0x564553, 0x6e5a62, 0x857467, 0x9d907a } },
			{ "Grey Scale", "Color", { 0x000000, 0xffffff } },
			{ "Heat", "Color", { 0x000004, 0x420a68, 0x932667, 0xdd513a, 0xfca50a, 0xfcffa4 } },
		};
		return kPalettes;
	}

	const char* PaletteOptions()
	{
		static const std::string s = [] {
			std::string r;
			for (const Palette& p : Palettes())
			{
				if (!r.empty())
					r += '|';
				r += p.Name;
			}
			return r;
		}();
		return s.c_str();
	}

	void PaletteColor(int index, float t, float out[3])
	{
		const auto& all = Palettes();
		const Palette& p = all[(size_t)std::clamp(index, 0, (int)all.size() - 1)];
		const int n = (int)p.Stops.size();
		if (n == 1)
		{
			out[0] = ((p.Stops[0] >> 16) & 255) / 255.0f; out[1] = ((p.Stops[0] >> 8) & 255) / 255.0f; out[2] = (p.Stops[0] & 255) / 255.0f;
			return;
		}
		const float f = std::clamp(t, 0.0f, 1.0f) * (n - 1);
		const int i = (std::min)((int)f, n - 2);
		const float u = f - i;
		const uint32_t a = p.Stops[(size_t)i], b = p.Stops[(size_t)i + 1];
		for (int ch = 0; ch < 3; ++ch)
		{
			const int sh = 16 - ch * 8;
			const float ca = ((a >> sh) & 255) / 255.0f, cb = ((b >> sh) & 255) / 255.0f;
			out[ch] = ca + (cb - ca) * u;
		}
	}

	// ---- 3D 미리 보기: 높이장 광선 행진 (픽셀마다 — 작업 스레드 여럿). 가로 · 세로 = -0.5..0.5, 높이 = 과장한 세로 배율
	void Render3D(const Map& height, const std::vector<uint8_t>& albedo, float maxHeight, float worldSize, float yaw, float pitch, float zoom, int w, int h,
		std::vector<uint8_t>& out, const std::atomic<bool>* cancel)
	{
		out.assign((size_t)w * h * 4, 0);
		const int res = height.Res;
		if (res < 2 || albedo.size() < (size_t)res * res * 4)
			return;
		// 세로 배율: 실제 비율 × 3 (넓은 월드는 납작해 보인다) — 0.06 ~ 0.3
		const float vs = std::clamp(maxHeight / (std::max)(worldSize, 1.0f) * 3.0f, 0.06f, 0.3f);
		auto H = [&](float u, float v) {
			// u, v = 0..1 (그림의 위 = +Z — PreviewImage 는 줄을 뒤집었다 → 높이 맵은 원래대로)
			const float fx = u * (res - 1), fz = v * (res - 1);
			return height.Bilinear(fx, fz) * vs;
		};
		auto A = [&](float u, float v, float rgb[3]) {
			// albedo 는 줄이 뒤집힌 그림 (위 = +Z)
			const int x = std::clamp((int)(u * (res - 1) + 0.5f), 0, res - 1), y = std::clamp((int)((1.0f - v) * (res - 1) + 0.5f), 0, res - 1);
			const uint8_t* p = &albedo[((size_t)y * res + x) * 4];
			rgb[0] = p[0] / 255.0f; rgb[1] = p[1] / 255.0f; rgb[2] = p[2] / 255.0f;
		};
		const float cy = yaw * 0.0174533f, cp = std::clamp(pitch, 5.0f, 89.0f) * 0.0174533f;
		const float dist = 1.55f / (std::max)(zoom, 0.2f);
		const float tx = 0.0f, ty = vs * 0.25f, tz = 0.0f;
		const float ex = tx + dist * cosf(cp) * sinf(cy), ey = ty + dist * sinf(cp), ez = tz + dist * cosf(cp) * cosf(cy);
		// 카메라 축
		float fx = tx - ex, fy = ty - ey, fz = tz - ez;
		const float fl = sqrtf(fx * fx + fy * fy + fz * fz);
		fx /= fl; fy /= fl; fz /= fl;
		float rx = fz, ry = 0.0f, rz = -fx;   // 오른쪽 = forward × up
		const float rl = sqrtf(rx * rx + rz * rz);
		rx /= rl; rz /= rl;
		const float ux = fy * rz - fz * ry, uy = fz * rx - fx * rz, uz = fx * ry - fy * rx;   // 위 = 앞 × 오른쪽
		const float tanHalf = tanf(0.3927f);   // 45° 화각
		const float aspect = (float)w / h;
		const float sunX = -0.45f, sunY = 0.65f, sunZ = 0.6f;
		const float step0 = 0.6f / res;
		Jobs::ParallelFor(h, 4, [&](int y0, int y1) {
			for (int py = y0; py < y1; ++py)
			{
				if (cancel && cancel->load(std::memory_order_relaxed))
					return;
				for (int px = 0; px < w; ++px)
				{
					const float sx = ((px + 0.5f) / w * 2.0f - 1.0f) * tanHalf * aspect, sy = (1.0f - (py + 0.5f) / h * 2.0f) * tanHalf;
					float dx = fx + rx * sx + ux * sy, dy = fy + ry * sx + uy * sy, dz = fz + rz * sx + uz * sy;
					const float dl = sqrtf(dx * dx + dy * dy + dz * dz);
					dx /= dl; dy /= dl; dz /= dl;
					// 하늘 (아래 = 밝은 안개, 위 = 파랑)
					const float skyT = std::clamp(dy * 1.5f + 0.3f, 0.0f, 1.0f);
					float col[3] = { 0.78f + (0.42f - 0.78f) * skyT, 0.82f + (0.56f - 0.82f) * skyT, 0.86f + (0.78f - 0.86f) * skyT };
					// 상자 [-0.5, 0.5] x [0, vs] x [-0.5, 0.5] 와 만나는 구간
					float t0 = 0.0f, t1 = 1e9f;
					const float o[3] = { ex, ey, ez }, d[3] = { dx, dy, dz }, lo[3] = { -0.5f, -0.01f, -0.5f }, hi[3] = { 0.5f, vs + 0.01f, 0.5f };
					bool hit = true;
					for (int a = 0; a < 3 && hit; ++a)
					{
						if (fabsf(d[a]) < 1e-8f) { if (o[a] < lo[a] || o[a] > hi[a]) hit = false; continue; }
						float ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
						if (ta > tb) std::swap(ta, tb);
						t0 = (std::max)(t0, ta); t1 = (std::min)(t1, tb);
						if (t0 > t1) hit = false;
					}
					if (hit)
					{
						float t = t0, prevT = t0, prevD = 1.0f;
						bool found = false;
						for (int it = 0; it < 600 && t <= t1; ++it)
						{
							const float qx = ex + dx * t, qy = ey + dy * t, qz = ez + dz * t;
							const float hh = H(qx + 0.5f, qz + 0.5f);
							const float dd = qy - hh;
							if (dd < 0.0f)
							{
								// 이전 점과 사이를 이분
								float a = prevT, b = t;
								for (int k = 0; k < 6; ++k)
								{
									const float m = (a + b) * 0.5f;
									const float my = ey + dy * m;
									if (my - H(ex + dx * m + 0.5f, ez + dz * m + 0.5f) < 0.0f) b = m; else a = m;
								}
								t = (a + b) * 0.5f;
								found = true;
								break;
							}
							prevT = t;
							prevD = dd;
							t += (std::max)(step0, dd * 0.45f);
						}
						(void)prevD;
						if (found)
						{
							const float qx = ex + dx * t + 0.5f, qz = ez + dz * t + 0.5f;
							const float e = 1.0f / res;
							const float nx = H(qx - e, qz) - H(qx + e, qz), nz = H(qx, qz - e) - H(qx, qz + e), ny = 2.0f * e;
							const float nl = sqrtf(nx * nx + ny * ny + nz * nz);
							const float ndl = (std::max)(0.0f, (nx * sunX + ny * sunY + nz * sunZ) / nl / sqrtf(sunX * sunX + sunY * sunY + sunZ * sunZ));
							float a[3];
							A(qx, qz, a);
							const bool wall = qx < 0.002f || qz < 0.002f || qx > 0.998f || qz > 0.998f;
							if (wall)
								a[0] = 0.28f, a[1] = 0.24f, a[2] = 0.2f;
							const float light = 0.35f + 0.85f * ndl;
							const float fog = std::clamp((t - dist * 0.6f) / (dist * 1.8f), 0.0f, 0.6f);
							for (int ch = 0; ch < 3; ++ch)
								col[ch] = (a[ch] * light) * (1.0f - fog) + col[ch] * fog;
						}
					}
					uint8_t* pp = &out[((size_t)py * w + px) * 4];
					for (int ch = 0; ch < 3; ++ch)
						pp[ch] = (uint8_t)std::clamp((int)(col[ch] * 255.0f + 0.5f), 0, 255);
					pp[3] = 255;
				}
			}
		}, "TerrainGraph 3D Preview");
	}
}
