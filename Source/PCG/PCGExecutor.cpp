#include "pch.h"
#include "PCGExecutor.h"
#include <chrono>
#include <cmath>
#include <map>
#include <unordered_map>

namespace PCG
{
	namespace
	{
		inline uint32_t Hash3(int a, int b, uint32_t c)
		{
			uint32_t h = (uint32_t)a * 0x27d4eb2dU ^ (uint32_t)b * 0x165667b1U ^ c * 0x9e3779b9U;
			h ^= h >> 15; h *= 0x85ebca6bU;
			h ^= h >> 13; h *= 0xc2b2ae35U;
			h ^= h >> 16;
			return h;
		}
		inline uint32_t Mix(uint32_t h, uint32_t v) { return Hash3((int)h, (int)v, 0x51ed27u); }
		inline float Rand01(uint32_t h) { return (h & 0xFFFFFF) / 16777216.0f; }
		inline float Smooth(float e0, float e1, float x)
		{
			if (e1 == e0)
				return x >= e1 ? 1.0f : 0.0f;
			const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		// 셀 안의 지형 격자 높이 (한 번 계산해 점마다 보간)
		struct HeightCache
		{
			const WorldGen::Settings* World = nullptr;
			TerrainGrid Grid;
			int I0 = 0, J0 = 0, W = 0, H = 0;
			std::vector<float> H01;
			void Build(const WorldGen::Settings& w, const TerrainGrid& g, const Rect& r)
			{
				World = &w;
				Grid = g;
				I0 = (int)floorf((r.X0 - g.OriginX) / g.Spacing) - 2;
				J0 = (int)floorf((r.Z0 - g.OriginZ) / g.Spacing) - 2;
				W = (int)ceilf((r.X1 - g.OriginX) / g.Spacing) + 3 - I0;
				H = (int)ceilf((r.Z1 - g.OriginZ) / g.Spacing) + 3 - J0;
				H01.resize((size_t)W * H);
				for (int j = 0; j < H; ++j)
					for (int i = 0; i < W; ++i)
						H01[(size_t)j * W + i] = WorldGen::Height01(w, g.OriginX + (I0 + i) * g.Spacing, g.OriginZ + (J0 + j) * g.Spacing);
			}
			float Sample(int i, int j) const
			{
				const int li = i - I0, lj = j - J0;
				if (li < 0 || lj < 0 || li >= W || lj >= H)
					return WorldGen::Height01(*World, Grid.OriginX + i * Grid.Spacing, Grid.OriginZ + j * Grid.Spacing);
				return H01[(size_t)lj * W + li];
			}
			// TerrainData::GetHeight 와 같은 대각선 (0,0)-(1,1)
			float Height01(float x, float z) const
			{
				const float fx = (x - Grid.OriginX) / Grid.Spacing, fz = (z - Grid.OriginZ) / Grid.Spacing;
				const int ix = (int)floorf(fx), iz = (int)floorf(fz);
				const float tx = fx - ix, tz = fz - iz;
				const float h00 = Sample(ix, iz), h10 = Sample(ix + 1, iz), h01 = Sample(ix, iz + 1), h11 = Sample(ix + 1, iz + 1);
				return tz >= tx ? h00 + tz * (h01 - h00) + tx * (h11 - h01) : h00 + tx * (h10 - h00) + tz * (h11 - h10);
			}
			void Normal(float x, float z, float& nx, float& ny, float& nz) const
			{
				const float e = Grid.Spacing;
				const float s = World->MaxHeight;
				const float dx = (Height01(x + e, z) - Height01(x - e, z)) * s / (2.0f * e);
				const float dz = (Height01(x, z + e) - Height01(x, z - e)) * s / (2.0f * e);
				const float len = sqrtf(dx * dx + 1.0f + dz * dz);
				nx = -dx / len; ny = 1.0f / len; nz = -dz / len;
			}
		};

		struct Exec
		{
			const Graph& G;
			const WorldGen::Settings& World;
			TerrainGrid Grid;
			uint32_t Seed;
			std::map<std::tuple<int, float, float, float, float>, std::vector<Point>> Memo;
			std::map<std::tuple<float, float, float, float>, std::unique_ptr<HeightCache>> Heights;

			const HeightCache& HeightsFor(const Rect& r)
			{
				auto& slot = Heights[{ r.X0, r.Z0, r.X1, r.Z1 }];
				if (!slot)
				{
					slot = std::make_unique<HeightCache>();
					slot->Build(World, Grid, r);
				}
				return *slot;
			}

			// 입력 핀 pin 의 점 (여러 줄이면 이어 붙인다)
			std::vector<Point> Input(const Node& n, int pin, const Rect& r)
			{
				std::vector<Point> out;
				for (int from : G.InputsOf(n.Id, pin))
				{
					const std::vector<Point>& p = Eval(from, r);
					out.insert(out.end(), p.begin(), p.end());
				}
				return out;
			}

			const std::vector<Point>& Eval(int id, const Rect& r)
			{
				const auto key = std::make_tuple(id, r.X0, r.Z0, r.X1, r.Z1);
				auto it = Memo.find(key);
				if (it != Memo.end())
					return it->second;
				std::vector<Point> out;
				if (const Node* n = G.Find(id); n && n->Enabled)
					Run(*n, r, out);
				else if (n)   // 꺼진 노드: 입력을 그대로 (Unreal 의 Disable 처럼)
					out = Input(*n, 0, r);
				return Memo[key] = std::move(out);
			}

			void Run(const Node& n, const Rect& r, std::vector<Point>& out)
			{
				switch (n.Type)
				{
				case NodeType::SurfaceSampler: Sample(n, r, out); break;
				case NodeType::DensityNoise:
				{
					out = Input(n, 0, r);
					const float scale = (std::max)(1.0f, n.Get("scale"));
					const int octaves = std::clamp((int)n.Get("octaves"), 1, 6);
					const float contrast = n.Get("contrast"), offset = n.Get("offset");
					const int mode = (int)n.Get("mode");
					const bool invert = n.Get("invert") > 0.5f;
					const int seed = (int)Seed + (int)n.Get("seed") * 7919 + n.Id * 131;
					for (Point& p : out)
					{
						float v = 0.5f + 0.5f * WorldGen::Fbm(seed, p.X / scale, p.Z / scale, octaves);
						v = std::clamp((v - 0.5f) * contrast + 0.5f + offset, 0.0f, 1.0f);
						if (invert)
							v = 1.0f - v;
						switch (mode)
						{
						case 1: p.Density = v; break;
						case 2: p.Density = (std::min)(p.Density, v); break;
						case 3: p.Density = (std::max)(p.Density, v); break;
						default: p.Density *= v; break;
						}
					}
					break;
				}
				case NodeType::HeightFilter:
				{
					out = Input(n, 0, r);
					const float lo = n.Get("minHeight"), hi = n.Get("maxHeight"), f = (std::max)(0.001f, n.Get("falloff"));
					for (Point& p : out)
						p.Density *= Smooth(lo - f, lo, p.Y) * (1.0f - Smooth(hi, hi + f, p.Y));
					break;
				}
				case NodeType::SlopeFilter:
				{
					out = Input(n, 0, r);
					const float lo = n.Get("minAngle"), hi = n.Get("maxAngle"), f = (std::max)(0.001f, n.Get("falloff"));
					for (Point& p : out)
						p.Density *= Smooth(lo - f, lo, p.SlopeDeg) * (1.0f - Smooth(hi, hi + f, p.SlopeDeg));
					break;
				}
				case NodeType::BiomeFilter:
				{
					out = Input(n, 0, r);
					const int biome = (int)n.Get("biome");
					const float minW = n.Get("minWeight");
					const bool asDensity = n.Get("useAsDensity") > 0.5f;
					std::vector<Point> kept;
					kept.reserve(out.size());
					for (Point& p : out)
					{
						const float w = biome == 0 ? p.Biome.Forest : biome == 1 ? p.Biome.Meadow : biome == 2 ? p.Biome.Desert : p.Biome.Rock;
						if (w < minW)
							continue;
						if (asDensity)
							p.Density *= Smooth(minW, (std::min)(1.0f, minW + 0.25f), w);
						kept.push_back(p);
					}
					out.swap(kept);
					break;
				}
				case NodeType::DensityFilter:
				{
					const std::vector<Point> in = Input(n, 0, r);
					const float lo = n.Get("lowerBound"), hi = n.Get("upperBound");
					const bool randomize = n.Get("randomize") > 0.5f;
					out.reserve(in.size());
					for (const Point& p : in)
					{
						if (randomize)
						{
							// 남을 확률 = 밀도 (lower · upper 로 다시 펴서)
							const float d = std::clamp((p.Density - lo) / (std::max)(1e-4f, hi - lo), 0.0f, 1.0f);
							if (Rand01(Mix(p.Seed, 0xD3u + (uint32_t)n.Id)) >= d)
								continue;
						}
						else if (p.Density < lo || p.Density > hi)
							continue;
						out.push_back(p);
					}
					break;
				}
				case NodeType::SelfPruning:
				{
					std::vector<Point> in = Input(n, 0, r);
					const float radius = (std::max)(0.01f, n.Get("radius"));
					const bool withSize = n.Get("scaleWithSize") > 0.5f;
					// 밀도 큰 것부터 (같으면 해시) — 격자 칸 해시로 가까운 점만 본다
					std::sort(in.begin(), in.end(), [](const Point& a, const Point& b) { return a.Density != b.Density ? a.Density > b.Density : a.Seed < b.Seed; });
					const float cell = radius * (withSize ? 2.0f : 1.0f);
					std::unordered_map<uint64_t, std::vector<int>> buckets;
					auto keyOf = [&](int cx, int cz) { return ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz; };
					out.reserve(in.size());
					for (const Point& p : in)
					{
						const float rp = radius * (withSize ? p.Scale : 1.0f);
						const int cx = (int)floorf(p.X / cell), cz = (int)floorf(p.Z / cell);
						bool blocked = false;
						const int reach = (int)ceilf(rp * 2.0f / cell) + 1;
						for (int dz = -reach; dz <= reach && !blocked; ++dz)
							for (int dx = -reach; dx <= reach && !blocked; ++dx)
							{
								auto b = buckets.find(keyOf(cx + dx, cz + dz));
								if (b == buckets.end())
									continue;
								for (int k : b->second)
								{
									const Point& q = out[(size_t)k];
									const float rq = radius * (withSize ? q.Scale : 1.0f);
									const float ddx = q.X - p.X, ddz = q.Z - p.Z;
									const float minD = (rp + rq) * 0.5f;
									if (ddx * ddx + ddz * ddz < minD * minD)
									{
										blocked = true;
										break;
									}
								}
							}
						if (blocked)
							continue;
						buckets[keyOf(cx, cz)].push_back((int)out.size());
						out.push_back(p);
					}
					break;
				}
				case NodeType::Difference:
				{
					const float radius = n.Get("radius");
					const std::vector<Point> in = Input(n, 0, r);
					// 빼는 점: 이 셀 + 반지름만큼 넓힌 곳 (이웃 셀의 나무도 — 점이 월드 격자라 같은 점)
					const Rect wide{ r.X0 - radius, r.Z0 - radius, r.X1 + radius, r.Z1 + radius };
					const std::vector<Point> ex = Input(n, 1, wide);
					const float cell = (std::max)(0.5f, radius);
					std::unordered_map<uint64_t, std::vector<int>> buckets;
					auto keyOf = [&](int cx, int cz) { return ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz; };
					for (int i = 0; i < (int)ex.size(); ++i)
						buckets[keyOf((int)floorf(ex[i].X / cell), (int)floorf(ex[i].Z / cell))].push_back(i);
					out.reserve(in.size());
					for (const Point& p : in)
					{
						const int cx = (int)floorf(p.X / cell), cz = (int)floorf(p.Z / cell);
						bool hit = false;
						for (int dz = -1; dz <= 1 && !hit; ++dz)
							for (int dx = -1; dx <= 1 && !hit; ++dx)
							{
								auto b = buckets.find(keyOf(cx + dx, cz + dz));
								if (b == buckets.end())
									continue;
								for (int k : b->second)
								{
									const float ddx = ex[(size_t)k].X - p.X, ddz = ex[(size_t)k].Z - p.Z;
									if (ddx * ddx + ddz * ddz < radius * radius)
									{
										hit = true;
										break;
									}
								}
							}
						if (!hit)
							out.push_back(p);
					}
					break;
				}
				case NodeType::TransformPoints:
				{
					out = Input(n, 0, r);
					const float yaw0 = n.Get("yawMin"), yaw1 = n.Get("yawMax");
					const float s0 = n.Get("scaleMin"), s1 = (std::max)(n.Get("scaleMax"), s0);
					const float align = n.Get("alignToNormal"), tilt = n.Get("tiltMax");
					const float o0 = n.Get("offsetMin"), o1 = n.Get("offsetMax");
					for (Point& p : out)
					{
						const uint32_t h = Mix(p.Seed, 0x7A3u + (uint32_t)n.Id);
						p.Yaw = XMConvertToRadians(yaw0 + (yaw1 - yaw0) * Rand01(h));
						p.Scale *= s0 + (s1 - s0) * Rand01(Mix(h, 1));
						p.Align = align;
						p.TiltX = XMConvertToRadians(tilt * (Rand01(Mix(h, 2)) * 2.0f - 1.0f));
						p.TiltZ = XMConvertToRadians(tilt * (Rand01(Mix(h, 3)) * 2.0f - 1.0f));
						p.OffsetY = o0 + (o1 - o0) * Rand01(Mix(h, 4));
					}
					break;
				}
				case NodeType::Merge:
				case NodeType::StaticMeshSpawner:
					out = Input(n, 0, r);
					break;
				default:
					break;
				}
			}

			void Sample(const Node& n, const Rect& r, std::vector<Point>& out)
			{
				const float density = (std::max)(1e-6f, n.Get("pointsPerSquaredMeter"));
				const float spacing = 1.0f / sqrtf(density);
				const float loose = std::clamp(n.Get("looseness"), 0.0f, 1.0f);
				const float extents = n.Get("pointExtents");
				const uint32_t seed = Seed ^ ((uint32_t)n.Get("seed") * 0x9E3779B1u) ^ ((uint32_t)n.Id * 0x85EBCA6Bu);
				const int i0 = (int)floorf(r.X0 / spacing), i1 = (int)floorf(r.X1 / spacing);
				const int j0 = (int)floorf(r.Z0 / spacing), j1 = (int)floorf(r.Z1 / spacing);
				const float half = World.WorldSize * 0.5f;
				const HeightCache& hc = HeightsFor(r);
				out.reserve((size_t)(i1 - i0 + 1) * (j1 - j0 + 1));
				for (int j = j0; j <= j1; ++j)
					for (int i = i0; i <= i1; ++i)
					{
						const uint32_t h = Hash3(i, j, seed);
						// 격자 칸 안에서만 흩뜨린다 → 점은 정확히 한 셀에 속한다 (셀 경계가 맞는다)
						const float x = (i + 0.5f + (Rand01(h) - 0.5f) * loose) * spacing;
						const float z = (j + 0.5f + (Rand01(Mix(h, 9)) - 0.5f) * loose) * spacing;
						if (x < r.X0 || x >= r.X1 || z < r.Z0 || z >= r.Z1 || fabsf(x) > half || fabsf(z) > half)
							continue;
						Point p;
						p.X = x;
						p.Z = z;
						p.Height01 = hc.Height01(x, z);
						p.Y = p.Height01 * World.MaxHeight;
						hc.Normal(x, z, p.Nx, p.Ny, p.Nz);
						p.SlopeDeg = XMConvertToDegrees(acosf(std::clamp(p.Ny, -1.0f, 1.0f)));
						p.Biome = WorldGen::BiomeAt(World, x, z, p.Height01, p.Ny);
						p.Extent = extents;
						p.Seed = h;
						out.push_back(p);
					}
			}
		};
	}

	float GroundHeight(const WorldGen::Settings& world, const TerrainGrid& grid, float x, float z)
	{
		HeightCache hc;
		hc.World = &world;
		hc.Grid = grid;
		return hc.Height01(x, z) * world.MaxHeight;
	}

	void ExecuteSpawner(const Graph& graph, int spawnerId, const WorldGen::Settings& world, const TerrainGrid& grid, int volumeSeed, const Rect& rect, CellOutput& out)
	{
		const auto t0 = std::chrono::steady_clock::now();
		out = CellOutput();
		const Node* sp = graph.Find(spawnerId);
		if (sp == nullptr || sp->Meshes.empty())
			return;
		Exec ex{ graph, world, grid, (uint32_t)volumeSeed * 0x2545F491u + 0x1234567u, {}, {} };
		const std::vector<Point>& points = ex.Eval(spawnerId, rect);
		out.PerMesh.resize(sp->Meshes.size());
		float total = 0.0f;
		for (const MeshEntry& m : sp->Meshes)
			total += (std::max)(0.0f, m.Weight);
		if (total <= 0.0f)
			return;
		out.MinY = FLT_MAX;
		out.MaxY = -FLT_MAX;
		const uint32_t pick = (uint32_t)sp->Get("seed") * 0x632BE5ABu + (uint32_t)spawnerId;
		for (const Point& p : points)
		{
			// 메시: 가중치로 (점 해시)
			float t = Rand01(Mix(p.Seed, pick)) * total;
			size_t m = 0;
			for (; m + 1 < sp->Meshes.size(); ++m)
			{
				t -= (std::max)(0.0f, sp->Meshes[m].Weight);
				if (t < 0.0f)
					break;
			}
			// 위 = 법선과 (0,1,0) 사이 (Align), 기울기, 방향 (Yaw)
			XMVECTOR up = XMVector3Normalize(XMVectorLerp(XMVectorSet(0, 1, 0, 0), XMVectorSet(p.Nx, p.Ny, p.Nz, 0), p.Align));
			XMMATRIX rot = XMMatrixRotationY(p.Yaw);
			if (p.TiltX != 0.0f || p.TiltZ != 0.0f)
				rot = rot * XMMatrixRotationRollPitchYaw(p.TiltX, 0.0f, p.TiltZ);
			if (p.Align > 0.0f)
			{
				// (0,1,0) 을 up 으로 돌리는 회전
				const XMVECTOR y = XMVectorSet(0, 1, 0, 0);
				const XMVECTOR axis = XMVector3Cross(y, up);
				const float len = XMVectorGetX(XMVector3Length(axis));
				if (len > 1e-4f)
					rot = rot * XMMatrixRotationAxis(axis, asinf((std::min)(1.0f, len)));
			}
			const XMMATRIX world = XMMatrixScaling(p.Scale, p.Scale, p.Scale) * rot * XMMatrixTranslation(p.X, p.Y + p.OffsetY, p.Z);
			XMFLOAT4X4 w;
			XMStoreFloat4x4(&w, world);
			out.PerMesh[m].push_back(w);
			out.MinY = (std::min)(out.MinY, p.Y);
			out.MaxY = (std::max)(out.MaxY, p.Y);
		}
		if (out.MinY > out.MaxY)
			out.MinY = out.MaxY = 0.0f;
		out.Points = (int)points.size();
		out.Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	}
}
