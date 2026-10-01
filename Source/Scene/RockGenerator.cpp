#include "pch.h"
#include "RockGenerator.h"
#include <chrono>
#include <execution>
#include <random>

namespace
{
	float Saturate(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
	float Lerp(float a, float b, float t) { return a + (b - a) * t; }

	uint32_t Hash(int x, int y, int z, uint32_t seed)
	{
		uint32_t h = seed * 0x9E3779B1u;
		h ^= (uint32_t)x * 0x85EBCA6Bu; h = (h << 13) | (h >> 19);
		h ^= (uint32_t)y * 0xC2B2AE35u; h = (h << 17) | (h >> 15);
		h ^= (uint32_t)z * 0x27D4EB2Fu;
		h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
		return h;
	}

	float ValueNoise(float x, float y, float z, uint32_t seed)
	{
		const int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
		float fx = x - ix, fy = y - iy, fz = z - iz;
		fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy); fz = fz * fz * (3 - 2 * fz);
		auto h = [&](int a, int b, int c) { return (Hash(ix + a, iy + b, iz + c, seed) & 0xFFFFFF) / 16777215.0f * 2.0f - 1.0f; };
		const float x00 = Lerp(h(0, 0, 0), h(1, 0, 0), fx), x10 = Lerp(h(0, 1, 0), h(1, 1, 0), fx);
		const float x01 = Lerp(h(0, 0, 1), h(1, 0, 1), fx), x11 = Lerp(h(0, 1, 1), h(1, 1, 1), fx);
		return Lerp(Lerp(x00, x10, fy), Lerp(x01, x11, fy), fz);
	}

	// 리지 섞인 fbm (-1~1 근처): 바위 표면의 날카로운 골
	float RockFbm(float x, float y, float z, uint32_t seed)
	{
		float sum = 0, amp = 0.55f, f = 1.0f;
		for (int o = 0; o < 4; ++o)
		{
			const float n = ValueNoise(x * f, y * f, z * f, seed + o * 101);
			sum += amp * (o == 0 ? n : (0.5f - fabsf(n)) * 1.4f);
			f *= 2.07f;
			amp *= 0.5f;
		}
		return sum;
	}

	struct Plane { float Nx, Ny, Nz, D; };
	struct Crack { float Mx, Mz, O, W; };

	// 매개변수 → 미리 정한 무작위 값들 (SDF 가 점마다 다시 만들지 않게)
	struct Shaper
	{
		RockParams P;
		float Hx, Hy, Hz, R, Cy;
		std::vector<Plane> Planes;
		std::vector<float> LayerInset;
		std::vector<float> LayerTop;     // 층 경계 (0~1, 두께가 제각각)
		std::vector<Crack> Cracks;
		float TiltTan = 0.0f;
		uint32_t Seed;

		explicit Shaper(const RockParams& p) : P(p)
		{
			Hx = (std::max)(0.05f, p.SizeX * 0.5f);
			Hy = (std::max)(0.05f, p.SizeY * 0.5f);
			Hz = (std::max)(0.05f, p.SizeZ * 0.5f);
			R = Saturate(p.Roundness) * (std::min)(Hx, (std::min)(Hy, Hz)) * 0.9f;
			Cy = Hy * 0.9f;   // 바닥 10 % 는 땅 아래 (묻힌 듯)
			Seed = (uint32_t)p.Seed * 7919u + 17u;
			std::mt19937 rng(Seed);
			std::uniform_real_distribution<float> uni(0.0f, 1.0f);
			const bool vertical = p.Kind == RockParams::Shape::Cliff || p.Kind == RockParams::Shape::Spire;
			const float hexBase = uni(rng) * XM_2PI;
			for (int i = 0; i < p.Facets; ++i)
			{
				// 절벽·첨탑은 거의 수평 법선(세로 면), 나머지는 고르게. 아래쪽(-y)은 자르지 않는다 (땅에 닿는 면)
				//  첨탑: 처음 6 개는 60° 간격(육각 주상절리), 나머지는 비스듬히 깨진 면
				float az = uni(rng) * XM_2PI;
				if (p.Kind == RockParams::Shape::Spire && i < 6)
					az = hexBase + i * (XM_2PI / 6.0f) + (uni(rng) - 0.5f) * 0.25f;
				float el = vertical ? (uni(rng) * 2.0f - 1.0f) * 0.25f : (uni(rng) * 1.7f - 0.5f);
				if (i == 0 && vertical) el = 1.2f + uni(rng) * 0.3f;   // 윗면을 살짝 기울여 자른다
				Plane pl;
				pl.Nx = cosf(el) * cosf(az); pl.Ny = sinf(el); pl.Nz = cosf(el) * sinf(az);
				const float support = fabsf(pl.Nx) * Hx + fabsf(pl.Ny) * Hy + fabsf(pl.Nz) * Hz;
				pl.D = support * (1.0f - p.FacetDepth * (0.2f + 0.8f * uni(rng) * uni(rng) * 1.6f));   // 대부분 얕게, 가끔 깊게
				Planes.push_back(pl);
			}
			float acc = 0.0f;
			std::vector<float> thick;
			for (int i = 0; i < (std::max)(0, p.Strata); ++i)
			{
				LayerInset.push_back(uni(rng));
				thick.push_back(0.4f + uni(rng) * 1.2f);
				acc += thick.back();
			}
			float run = 0.0f;
			for (float t : thick)
			{
				run += t / acc;
				LayerTop.push_back(run);
			}
			TiltTan = tanf(XMConvertToRadians(p.StrataTilt));
			for (int i = 0; i < p.Cracks; ++i)
			{
				Crack c;
				const float a = uni(rng) * XM_PI;
				c.Mx = cosf(a); c.Mz = sinf(a);
				const float ext = fabsf(c.Mx) * Hx + fabsf(c.Mz) * Hz;
				c.O = (uni(rng) * 2.0f - 1.0f) * ext * 0.8f;
				c.W = p.CrackWidth * (0.6f + 0.8f * uni(rng));
				Cracks.push_back(c);
			}
		}

		float Sdf(float x, float y, float z) const
		{
			float qx = x, qy = y - Cy, qz = z;
			// 도메인 워프: 크기 비례 저주파로 덩어리를 비틀어 대칭·상자꼴을 없앤다
			//  절벽·판은 가로가 길어 짧은 반폭만으로는 위에서 본 윤곽이 거의 안 바뀌므로 긴 변도 반영
			const float minH = (std::min)(Hx, (std::min)(Hy, Hz));
			if (P.Warp > 0.0f)
			{
				const float wide = (P.Kind == RockParams::Shape::Cliff || P.Kind == RockParams::Shape::Slab) ? (std::max)(minH, 0.3f * (std::max)(Hx, Hz)) : minH;
				const float f = 0.9f / wide, a = P.Warp * wide;
				const float wx = ValueNoise(qx * f + 1.7f, qy * f, qz * f, Seed + 51);
				const float wy = ValueNoise(qx * f, qy * f + 5.3f, qz * f, Seed + 52);
				const float wz = ValueNoise(qx * f, qy * f, qz * f + 9.1f, Seed + 53);
				// 바닥 근처는 덜 비튼다 (땅에 닿는 면이 평평하게)
				const float floorKeep = Saturate((qy + Hy) / (Hy * 0.6f));
				// 절벽·판은 세로로 덜 비튼다 (지층이 물결치지 않고 거의 수평으로)
				const float yWarp = (P.Kind == RockParams::Shape::Cliff || P.Kind == RockParams::Shape::Slab) ? 0.25f : 1.0f;
				qx += wx * a; qz += wz * a; qy += wy * a * floorKeep * yWarp;
			}
			// 위로 갈수록 좁게 (첨탑·피라미드꼴)
			const float t = Saturate(qy / (2.0f * Hy) + 0.5f);
			const float k = 1.0f - Saturate(P.Taper) * t;
			qx /= k; qz /= k;
			// 둥근 상자
			const float dx = fabsf(qx) - (Hx - R), dy = fabsf(qy) - (Hy - R), dz = fabsf(qz) - (Hz - R);
			const float ox = (std::max)(dx, 0.0f), oy = (std::max)(dy, 0.0f), oz = (std::max)(dz, 0.0f);
			float d = sqrtf(ox * ox + oy * oy + oz * oz) + (std::min)((std::max)(dx, (std::max)(dy, dz)), 0.0f) - R;
			// 평면으로 잘라 각진 면
			// 부드러운 max: 잘린 모서리를 살짝 깎는다 (깨진 돌 모서리가 너무 칼 같지 않게)
			const float kSoft = (std::max)(0.001f, P.EdgeSoftness * minH);
			for (const Plane& pl : Planes)
			{
				const float b = qx * pl.Nx + qy * pl.Ny + qz * pl.Nz - pl.D;
				const float h = Saturate(0.5f + 0.5f * (b - d) / kSoft);
				d = Lerp(d, b, h) + kSoft * h * (1.0f - h);
			}
			d *= k;
			// 지층: 층마다 안으로 들어간 정도가 다르고(턱), 층 사이는 홈
			if (!LayerInset.empty())
			{
				const int n = (int)LayerInset.size();
				const float layerH = 2.0f * Hy / n;   // 평균 두께
				const float yy = (qy + Hy + TiltTan * qx + RockFbm(x * 0.15f, 0.0f, z * 0.15f, Seed + 7) * layerH * 0.25f) / (2.0f * Hy);
				int i = 0;
				while (i < n - 1 && yy > LayerTop[i]) ++i;
				const float lo = i > 0 ? LayerTop[i - 1] : 0.0f, hi = LayerTop[i];
				const float edge = (std::min)(yy - lo, hi - yy) * 2.0f * Hy;
				const float grooveW = 0.06f * layerH + 0.04f;
				const float ledgeVar = 0.6f + 0.8f * (ValueNoise(x * 0.12f, (float)i * 3.7f, z * 0.12f, Seed + 19) * 0.5f + 0.5f);
				d += LayerInset[i] * P.StrataDepth * ledgeVar + P.StrataGroove * expf(-(edge * edge) / (grooveW * grooveW));
			}
			// 기둥: 균열 평면들의 어느 쪽인지로 구역 번호 → 구역마다 들어가고 나온 정도 (절벽의 블록감)
			if (P.ColumnDepth > 0.0f && !Cracks.empty())
			{
				uint32_t region = 0;
				for (size_t ci = 0; ci < Cracks.size(); ++ci)
					if (qx * Cracks[ci].Mx + qz * Cracks[ci].Mz - Cracks[ci].O > 0.0f)
						region |= 1u << ci;
				d += ((Hash((int)region, 7, 3, Seed) & 0xFFFF) / 65535.0f) * P.ColumnDepth;
			}
			// 세로 균열: 겉에서 CrackDepth 까지만 판 틈 (폭은 노이즈로 들쭉날쭉)
			const float before = d;
			for (const Crack& c : Cracks)
			{
				const float w = c.W * (0.7f + 0.6f * ValueNoise(x * 0.8f, y * 0.8f, z * 0.8f, Seed + 31));
				const float slot = w - fabsf(qx * c.Mx + qz * c.Mz - c.O);
				d = (std::max)(d, (std::min)(slot, before + P.CrackDepth));
			}
			// 거칠기
			if (P.Noise > 0.0f)
			{
				const float s = 1.0f / (std::max)(0.05f, P.NoiseScale);
				d += P.Noise * RockFbm(x * s, y * s, z * s, Seed + 3);
			}
			return d;
		}
	};
}

namespace RockGenerator
{
	float Distance(const RockParams& p, const XMFLOAT3& pos)
	{
		return Shaper(p).Sdf(pos.x, pos.y, pos.z);
	}

	Mesh Generate(const RockParams& p, int lod)
	{
		const auto t0 = std::chrono::steady_clock::now();
		Mesh mesh;
		const Shaper sh(p);
		// 격자: 가장 긴 변을 따라 72 / 36 / 18 칸 (Detail 배율)
		const float pad = p.Noise * 2.5f + p.StrataDepth + p.StrataGroove + 0.2f;
		const float ex = sh.Hx * 1.05f + pad, ey = sh.Hy * 1.05f + pad, ez = sh.Hz * 1.05f + pad;
		const int base = std::clamp((int)((72 >> std::clamp(lod, 0, 2)) * (std::max)(0.25f, p.Detail)), 8, 160);
		const float cs = 2.0f * (std::max)(ex, (std::max)(ey, ez)) / base;
		const int nx = (int)ceilf(2.0f * ex / cs) + 1, ny = (int)ceilf(2.0f * ey / cs) + 1, nz = (int)ceilf(2.0f * ez / cs) + 1;
		const float x0 = -ex, y0 = sh.Cy - ey, z0 = -ez;
		std::vector<float> field((size_t)nx * ny * nz);
		auto idx = [&](int x, int y, int z) { return ((size_t)z * ny + y) * nx + x; };
		std::vector<int> rows(nz);
		for (int i = 0; i < nz; ++i) rows[i] = i;
		std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
			for (int y = 0; y < ny; ++y)
				for (int x = 0; x < nx; ++x)
					field[idx(x, y, z)] = sh.Sdf(x0 + x * cs, y0 + y * cs, z0 + z * cs);
		});

		// Surface Nets: 부호가 바뀌는 칸마다 정점 = 바뀌는 모서리 교점들의 평균
		std::vector<int> cellVert((size_t)(nx - 1) * (ny - 1) * (nz - 1), -1);
		auto cidx = [&](int x, int y, int z) { return ((size_t)z * (ny - 1) + y) * (nx - 1) + x; };
		static const int kEdges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
		std::vector<XMFLOAT3> positions;
		for (int z = 0; z < nz - 1; ++z)
			for (int y = 0; y < ny - 1; ++y)
				for (int x = 0; x < nx - 1; ++x)
				{
					float v[8];
					int inside = 0;
					for (int c = 0; c < 8; ++c)
					{
						v[c] = field[idx(x + (c & 1), y + ((c >> 1) & 1), z + ((c >> 2) & 1))];
						inside += v[c] < 0.0f;
					}
					if (inside == 0 || inside == 8)
						continue;
					float sx = 0, sy = 0, sz = 0;
					int n = 0;
					for (const auto& e : kEdges)
					{
						const float a = v[e[0]], b = v[e[1]];
						if ((a < 0.0f) == (b < 0.0f))
							continue;
						const float t = a / (a - b);
						const int ca = e[0], cb = e[1];
						sx += Lerp((float)(ca & 1), (float)(cb & 1), t);
						sy += Lerp((float)((ca >> 1) & 1), (float)((cb >> 1) & 1), t);
						sz += Lerp((float)((ca >> 2) & 1), (float)((cb >> 2) & 1), t);
						++n;
					}
					cellVert[cidx(x, y, z)] = (int)positions.size();
					positions.push_back(XMFLOAT3(x0 + (x + sx / n) * cs, y0 + (y + sy / n) * cs, z0 + (z + sz / n) * cs));
				}
		// 면: 부호가 바뀌는 격자 모서리마다 그 모서리를 둘러싼 칸 4 개의 정점으로 사각형
		auto quad = [&](int a, int b, int c, int d, bool flip) {
			if (a < 0 || b < 0 || c < 0 || d < 0)
				return;
			if (flip) std::swap(b, d);
			mesh.Indices.insert(mesh.Indices.end(), { (uint32_t)a, (uint32_t)b, (uint32_t)c, (uint32_t)a, (uint32_t)c, (uint32_t)d });
		};
		for (int z = 1; z < nz - 1; ++z)
			for (int y = 1; y < ny - 1; ++y)
				for (int x = 1; x < nx - 1; ++x)
				{
					const bool in0 = field[idx(x, y, z)] < 0.0f;
					if (x + 1 < nx && in0 != (field[idx(x + 1, y, z)] < 0.0f))
						quad(cellVert[cidx(x, y - 1, z - 1)], cellVert[cidx(x, y, z - 1)], cellVert[cidx(x, y, z)], cellVert[cidx(x, y - 1, z)], !in0);
					if (y + 1 < ny && in0 != (field[idx(x, y + 1, z)] < 0.0f))
						quad(cellVert[cidx(x - 1, y, z - 1)], cellVert[cidx(x - 1, y, z)], cellVert[cidx(x, y, z)], cellVert[cidx(x, y, z - 1)], !in0);
					if (z + 1 < nz && in0 != (field[idx(x, y, z + 1)] < 0.0f))
						quad(cellVert[cidx(x - 1, y - 1, z)], cellVert[cidx(x, y - 1, z)], cellVert[cidx(x, y, z)], cellVert[cidx(x - 1, y, z)], !in0);
				}

		// 정점: 법선 = SDF 기울기, AO = 법선 쪽 몇 걸음의 거리 부족분, 오목함 = 가까운 걸음의 부족분
		mesh.Vertices.resize(positions.size());
		std::vector<size_t> vi(positions.size());
		for (size_t i = 0; i < vi.size(); ++i) vi[i] = i;
		std::for_each(std::execution::par, vi.begin(), vi.end(), [&](size_t i) {
			const XMFLOAT3 q = positions[i];
			const float e = cs * 0.5f;
			XMFLOAT3 n(sh.Sdf(q.x + e, q.y, q.z) - sh.Sdf(q.x - e, q.y, q.z), sh.Sdf(q.x, q.y + e, q.z) - sh.Sdf(q.x, q.y - e, q.z),
				sh.Sdf(q.x, q.y, q.z + e) - sh.Sdf(q.x, q.y, q.z - e));
			const float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
			if (len > 1e-8f) { n.x /= len; n.y /= len; n.z /= len; } else n = XMFLOAT3(0, 1, 0);
			float occ = 0.0f, w = 1.0f;
			const float steps[4] = { 1.5f, 3.0f, 6.0f, 11.0f };
			for (float s : steps)
			{
				const float h = cs * s;
				occ += w * Saturate((h - sh.Sdf(q.x + n.x * h, q.y + n.y * h, q.z + n.z * h)) / h);
				w *= 0.6f;
			}
			const float hc = cs * 1.2f;
			const float cav = (hc - sh.Sdf(q.x + n.x * hc, q.y + n.y * hc, q.z + n.z * hc)) / hc;
			Vertex& v = mesh.Vertices[i];
			v.Pos = q;
			v.Normal = n;
			v.AO = Saturate(1.0f - occ * 0.55f);
			v.Cavity = std::clamp(cav * 1.5f, -1.0f, 1.0f);
		});
		// 감기 방향: D3D 기본(시계 방향 = 앞면)에서 cross(b-a, c-a) 가 바깥(SDF 기울기) 쪽이 되게
		for (size_t t = 0; t + 2 < mesh.Indices.size(); t += 3)
		{
			const XMFLOAT3& a = mesh.Vertices[mesh.Indices[t]].Pos;
			const XMFLOAT3& b = mesh.Vertices[mesh.Indices[t + 1]].Pos;
			const XMFLOAT3& c = mesh.Vertices[mesh.Indices[t + 2]].Pos;
			const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, wx = c.x - a.x, wy = c.y - a.y, wz = c.z - a.z;
			const float fx = uy * wz - uz * wy, fy = uz * wx - ux * wz, fz = ux * wy - uy * wx;
			const XMFLOAT3& n = mesh.Vertices[mesh.Indices[t]].Normal;
			if (fx * n.x + fy * n.y + fz * n.z < 0.0f)
				std::swap(mesh.Indices[t + 1], mesh.Indices[t + 2]);
		}
		XMFLOAT3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		for (const Vertex& v : mesh.Vertices)
		{
			mn.x = (std::min)(mn.x, v.Pos.x); mn.y = (std::min)(mn.y, v.Pos.y); mn.z = (std::min)(mn.z, v.Pos.z);
			mx.x = (std::max)(mx.x, v.Pos.x); mx.y = (std::max)(mx.y, v.Pos.y); mx.z = (std::max)(mx.z, v.Pos.z);
		}
		mesh.BoundsMin = mesh.Vertices.empty() ? XMFLOAT3(0, 0, 0) : mn;
		mesh.BoundsMax = mesh.Vertices.empty() ? XMFLOAT3(0, 0, 0) : mx;
		mesh.Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		return mesh;
	}
}
