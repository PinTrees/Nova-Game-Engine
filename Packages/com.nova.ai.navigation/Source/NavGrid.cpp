#include "pch.h"
#include "NavGrid.h"
#include <cmath>
#include <fstream>
#include <queue>
#include <thread>
#include <atomic>

namespace
{
	const float kNaN = std::numeric_limits<float>::quiet_NaN();
	constexpr uint32_t kMagic = 0x474E564E;   // 'NVNG'
	constexpr uint32_t kVersion = 1;

	const int kDX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	const int kDZ[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
}

bool NavGrid::Valid(int x, int z, int l) const
{
	return x >= 0 && z >= 0 && x < Width && z < Depth && l >= 0 && l < kLayers && !std::isnan(Height(x, z, l));
}

int NavGrid::WalkableCount() const
{
	int n = 0;
	for (float h : Heights)
		if (!std::isnan(h)) ++n;
	return n;
}

int NavGrid::ConnectedLayer(int x, int z, float h) const
{
	if (x < 0 || z < 0 || x >= Width || z >= Depth)
		return -1;
	int best = -1;
	float bestD = Settings.StepHeight + 1e-4f;
	for (int l = 0; l < kLayers; ++l)
	{
		const float v = Height(x, z, l);
		if (std::isnan(v))
			continue;
		const float d = fabsf(v - h);
		if (d <= bestD)
		{
			bestD = d;
			best = l;
		}
	}
	return best;
}

Vec3 NavGrid::CellCenter(int x, int z, int l) const
{
	return Vec3(Origin.x + (x + 0.5f) * Settings.CellSize, Height(x, z, l), Origin.z + (z + 0.5f) * Settings.CellSize);
}

// ---------------------------------------------------------------------- 굽기
bool NavGrid::Bake(const Vec3& bmin, const Vec3& bmax, const NavGridSettings& s, std::string& log)
{
	PhysicsManager* pm = PhysicsManager::GetI();
	if (!pm->IsSimulating())
	{
		log = "no physics world";
		return false;
	}
	Settings = s;
	Settings.CellSize = (std::max)(0.05f, s.CellSize);
	const Vec3 size = bmax - bmin;
	if (size.x <= 0.0f || size.z <= 0.0f)
	{
		log = "empty bounds";
		return false;
	}
	// 칸 수가 너무 많으면 칸을 키운다 (굽기 시간·메모리)
	const double kMaxCells = 1024.0 * 1024.0;   // 큰 지형: 칸을 키워 굽기를 몇 초 안에 (메인 스레드가 기다린다)
	float cell = Settings.CellSize;
	if ((double)size.x * size.z / ((double)cell * cell) > kMaxCells)
	{
		cell = (float)std::sqrt((double)size.x * size.z / kMaxCells);
		Settings.CellSize = cell;
	}
	Width = (std::max)(1, (int)std::ceil(size.x / cell));
	Depth = (std::max)(1, (int)std::ceil(size.z / cell));
	Origin = bmin;
	Heights.assign((size_t)Width * Depth * kLayers, kNaN);

	const float cosSlope = cosf(XMConvertToRadians(std::clamp(Settings.MaxSlope, 0.0f, 89.0f)));
	const float top = bmax.y + 1.0f;
	const float rayLength = size.y + 2.0f;
	const auto t0 = std::chrono::steady_clock::now();

	// 1) 칸마다 위에서 아래로 레이: 맞은 면 중 완만하고 머리 위가 빈 곳 = 바닥 (여러 스레드)
	std::atomic<int> nextRow{ 0 };
	auto work = [&]() {
		RaycastHit hits[16];
		RaycastHit up[1];
		for (int z = nextRow++; z < Depth; z = nextRow++)
			for (int x = 0; x < Width; ++x)
			{
				const Vec3 o(Origin.x + (x + 0.5f) * cell, top, Origin.z + (z + 0.5f) * cell);
				const int n = pm->RaycastAll(o, Vec3(0, -1, 0), rayLength, hits, 16, true);
				int l = 0;
				for (int i = 0; i < n && l < kLayers; ++i)
				{
					const RaycastHit& h = hits[i];
					if (h.normal.y < cosSlope)
						continue;   // 벽·가파른 면
					const Vec3 head = h.point + Vec3(0.0f, 0.05f, 0.0f);
					if (pm->RaycastAll(head, Vec3(0, 1, 0), (std::max)(0.1f, Settings.AgentHeight - 0.05f), up, 1, true) > 0)
						continue;   // 머리 위가 막힘 (낮은 천장, 물체 안)
					Heights[((size_t)z * Width + x) * kLayers + l++] = h.point.y;
				}
			}
	};
	const int threads = (std::max)(1, (int)std::thread::hardware_concurrency() - 1);
	{
		std::vector<std::thread> pool;
		for (int i = 0; i < threads; ++i)
			pool.emplace_back(work);
		for (auto& t : pool)
			t.join();
	}

	// 2) 가장자리(이어지지 않는 이웃)에서 Agent Radius 안쪽은 깎는다 — 여러 시작점 다익스트라로 가장자리까지 거리
	const int total = Width * Depth * kLayers;
	std::vector<float> dist(total, FLT_MAX);
	using Item = std::pair<float, int>;
	std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
	for (int z = 0; z < Depth; ++z)
		for (int x = 0; x < Width; ++x)
			for (int l = 0; l < kLayers; ++l)
			{
				const float h = Height(x, z, l);
				if (std::isnan(h))
					continue;
				bool edge = false;
				for (int k = 0; k < 4 && !edge; ++k)
					edge = ConnectedLayer(x + kDX[k], z + kDZ[k], h) < 0;
				if (edge)
				{
					const int i = Index(x, z, l);
					dist[i] = 0.5f * cell;
					open.push({ dist[i], i });
				}
			}
	const float radius = Settings.AgentRadius;
	while (!open.empty())
	{
		const auto [d, i] = open.top();
		open.pop();
		if (d > dist[i] || d >= radius)
			continue;
		const int l = i % kLayers, c = i / kLayers, x = c % Width, z = c / Width;
		const float h = Height(x, z, l);
		for (int k = 0; k < 8; ++k)
		{
			const int nx = x + kDX[k], nz = z + kDZ[k];
			const int nl = ConnectedLayer(nx, nz, h);
			if (nl < 0)
				continue;
			const int ni = Index(nx, nz, nl);
			const float nd = d + (k < 4 ? cell : cell * 1.41421356f);
			if (nd < dist[ni])
			{
				dist[ni] = nd;
				open.push({ nd, ni });
			}
		}
	}
	int removed = 0;
	for (int i = 0; i < total; ++i)
		if (!std::isnan(Heights[i]) && dist[i] < radius)
		{
			Heights[i] = kNaN;
			++removed;
		}

	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	char buf[256];
	snprintf(buf, sizeof(buf), "%d x %d cells (%.2f m), %d walkable, %d eroded by radius, %.0f ms", Width, Depth, cell, WalkableCount(), removed, ms);
	log = buf;
	return WalkableCount() > 0;
}

// ---------------------------------------------------------------------- 저장 / 읽기
bool NavGrid::Save(const std::wstring& path) const
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	auto put = [&](const void* p, size_t n) { out.write((const char*)p, (std::streamsize)n); };
	put(&kMagic, 4);
	put(&kVersion, 4);
	put(&Origin, sizeof(Vec3));
	put(&Width, 4);
	put(&Depth, 4);
	put(&Settings, sizeof(NavGridSettings));
	// 칸마다 층 수(1 바이트) + 높이들
	for (int c = 0; c < Width * Depth; ++c)
	{
		uint8_t n = 0;
		float hs[kLayers];
		for (int l = 0; l < kLayers; ++l)
			if (!std::isnan(Heights[(size_t)c * kLayers + l]))
				hs[n++] = Heights[(size_t)c * kLayers + l];
		put(&n, 1);
		put(hs, n * sizeof(float));
	}
	return (bool)out;
}

bool NavGrid::Load(const std::wstring& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return false;
	auto get = [&](void* p, size_t n) { in.read((char*)p, (std::streamsize)n); return (bool)in; };
	uint32_t magic = 0, version = 0;
	if (!get(&magic, 4) || !get(&version, 4) || magic != kMagic || version != kVersion)
		return false;
	int w = 0, d = 0;
	NavGridSettings s;
	Vec3 o;
	if (!get(&o, sizeof(Vec3)) || !get(&w, 4) || !get(&d, 4) || !get(&s, sizeof(NavGridSettings)) || w <= 0 || d <= 0 || (long long)w * d > 16000000)
		return false;
	std::vector<float> h((size_t)w * d * kLayers, kNaN);
	for (int c = 0; c < w * d; ++c)
	{
		uint8_t n = 0;
		if (!get(&n, 1) || n > kLayers)
			return false;
		for (int l = 0; l < n; ++l)
			if (!get(&h[(size_t)c * kLayers + l], sizeof(float)))
				return false;
	}
	Origin = o;
	Width = w;
	Depth = d;
	Settings = s;
	Heights = std::move(h);
	return true;
}

// ---------------------------------------------------------------------- 질의
bool NavGrid::Sample(const Vec3& p, float maxDistance, int& ox, int& oz, int& ol) const
{
	if (IsEmpty())
		return false;
	const float cell = Settings.CellSize;
	const int cx = (int)std::floor((p.x - Origin.x) / cell);
	const int cz = (int)std::floor((p.z - Origin.z) / cell);
	const int R = (std::max)(0, (int)std::ceil(maxDistance / cell));
	float best = maxDistance * maxDistance + 1e-4f;
	bool found = false;
	for (int r = 0; r <= R; ++r)
	{
		if (found && (r - 1) * cell * (r - 1) * cell > best)
			break;
		for (int dz = -r; dz <= r; ++dz)
			for (int dx = -r; dx <= r; ++dx)
			{
				if ((std::max)(std::abs(dx), std::abs(dz)) != r)
					continue;   // 고리만
				const int x = cx + dx, z = cz + dz;
				if (x < 0 || z < 0 || x >= Width || z >= Depth)
					continue;
				for (int l = 0; l < kLayers; ++l)
				{
					const float h = Height(x, z, l);
					if (std::isnan(h))
						continue;
					const float px = Origin.x + (x + 0.5f) * cell, pz = Origin.z + (z + 0.5f) * cell;
					// 칸 안이면 수평 거리 0
					const float hx = (dx == 0) ? 0.0f : px - p.x, hz = (dz == 0) ? 0.0f : pz - p.z;
					const float d2 = hx * hx + hz * hz + (h - p.y) * (h - p.y);
					if (d2 < best)
					{
						best = d2;
						ox = x; oz = z; ol = l;
						found = true;
					}
				}
			}
	}
	return found;
}

bool NavGrid::GroundHeight(const Vec3& p, float& y) const
{
	if (IsEmpty())
		return false;
	const int x = (int)std::floor((p.x - Origin.x) / Settings.CellSize);
	const int z = (int)std::floor((p.z - Origin.z) / Settings.CellSize);
	if (x < 0 || z < 0 || x >= Width || z >= Depth)
		return false;
	bool found = false;
	float bestD = FLT_MAX;
	for (int l = 0; l < kLayers; ++l)
	{
		const float h = Height(x, z, l);
		if (std::isnan(h))
			continue;
		const float d = fabsf(h - p.y);
		if (d < bestD)
		{
			bestD = d;
			y = h;
			found = true;
		}
	}
	return found;
}

bool NavGrid::LineOfSight(int x0, int z0, int l0, int x1, int z1) const
{
	// 칸 중심 → 칸 중심을 반 칸 간격으로 걸으며 이어지는 층을 따라간다
	const float fx0 = x0 + 0.5f, fz0 = z0 + 0.5f, fx1 = x1 + 0.5f, fz1 = z1 + 0.5f;
	const float len = std::sqrt((fx1 - fx0) * (fx1 - fx0) + (fz1 - fz0) * (fz1 - fz0));
	const int steps = (std::max)(1, (int)std::ceil(len * 2.0f));
	int cx = x0, cz = z0;
	float h = Height(x0, z0, l0);
	for (int i = 1; i <= steps; ++i)
	{
		const float t = (float)i / steps;
		const int x = (int)std::floor(fx0 + (fx1 - fx0) * t), z = (int)std::floor(fz0 + (fz1 - fz0) * t);
		if (x == cx && z == cz)
			continue;
		// 대각선으로 한 번에 두 칸을 건너면 사이 칸도 확인
		if (x != cx && z != cz && (ConnectedLayer(x, cz, h) < 0 || ConnectedLayer(cx, z, h) < 0))
			return false;
		const int l = ConnectedLayer(x, z, h);
		if (l < 0)
			return false;
		h = Height(x, z, l);
		cx = x;
		cz = z;
	}
	return true;
}

bool NavGrid::FindPath(const Vec3& start, const Vec3& end, std::vector<Vec3>& corners) const
{
	corners.clear();
	int sx, sz, sl, ex, ez, el;
	const float snap = (std::max)(2.0f, Settings.AgentRadius * 4.0f);
	if (!Sample(start, snap, sx, sz, sl) || !Sample(end, snap, ex, ez, el))
		return false;
	const int startI = Index(sx, sz, sl), goalI = Index(ex, ez, el);
	const float cell = Settings.CellSize;

	// 칸마다 g·from 을 평면 배열에 (세대 번호가 다르면 비어 있는 것으로) — 해시맵보다 몇 배 빠르다
	const size_t total = (size_t)Width * Depth * kLayers;
	if (m_Stamp.size() != total)
	{
		m_G.assign(total, 0.0f);
		m_From.assign(total, -1);
		m_Stamp.assign(total, 0u);
		m_Generation = 0;
	}
	if (++m_Generation == 0)
	{
		std::fill(m_Stamp.begin(), m_Stamp.end(), 0u);
		m_Generation = 1;
	}
	const uint32_t gen = m_Generation;
	auto gOf = [&](int i) { return m_Stamp[i] == gen ? m_G[i] : FLT_MAX; };
	using Item = std::pair<float, int>;
	std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
	// 옥타일 거리 × 1.2 (가중 A*): 넓은 땅에서 고르게 퍼지지 않고 목표 쪽으로 — 길이는 최단보다 조금 길 수 있고 시선 다듬기가 펴 준다
	auto heuristic = [&](int x, int z) {
		const float dx = (float)std::abs(x - ex), dz = (float)std::abs(z - ez);
		return 1.2f * cell * ((dx + dz) + (1.41421356f - 2.0f) * (std::min)(dx, dz));
	};
	m_G[startI] = 0.0f;
	m_From[startI] = -1;
	m_Stamp[startI] = gen;
	open.push({ heuristic(sx, sz), startI });
	bool reached = startI == goalI;
	int expanded = 0;
	while (!open.empty() && !reached)
	{
		const auto [f, i] = open.top();
		open.pop();
		const float gi = gOf(i);
		const int l = i % kLayers, c = i / kLayers, x = c % Width, z = c / Width;
		if (f > gi + heuristic(x, z) + 1e-4f)
			continue;   // 낡은 항목
		if (++expanded > 4000000)
			break;
		const float h = Height(x, z, l);
		for (int k = 0; k < 8; ++k)
		{
			const int nx = x + kDX[k], nz = z + kDZ[k];
			const int nl = ConnectedLayer(nx, nz, h);
			if (nl < 0)
				continue;
			if (k >= 4 && (ConnectedLayer(nx, z, h) < 0 || ConnectedLayer(x, nz, h) < 0))
				continue;   // 모서리를 가로지르지 않는다
			const int ni = Index(nx, nz, nl);
			const float step = (k < 4 ? cell : cell * 1.41421356f) + fabsf(Height(nx, nz, nl) - h);
			const float ng = gi + step;
			if (gOf(ni) <= ng)
				continue;
			m_G[ni] = ng;
			m_From[ni] = i;
			m_Stamp[ni] = gen;
			if (ni == goalI)
			{
				reached = true;
				break;
			}
			open.push({ ng + heuristic(nx, nz), ni });
		}
	}
	if (!reached)
		return false;

	// 칸 목록 (시작 → 끝)
	std::vector<int> cells;
	for (int i = goalI; ; )
	{
		cells.push_back(i);
		if (i == startI)
			break;
		i = m_From[i];
	}
	std::reverse(cells.begin(), cells.end());

	auto unpack = [&](int i, int& x, int& z, int& l) { l = i % kLayers; const int c = i / kLayers; x = c % Width; z = c / Width; };
	// 시작 점: 시작 칸의 바닥 높이에 (칸 안이면 원래 x,z)
	int x, z, l;
	unpack(startI, x, z, l);
	const bool startInside = (int)std::floor((start.x - Origin.x) / cell) == x && (int)std::floor((start.z - Origin.z) / cell) == z;
	corners.push_back(startInside ? Vec3(start.x, Height(x, z, l), start.z) : CellCenter(x, z, l));
	// 보이는 곳까지 건너뛴다
	size_t anchor = 0;
	for (size_t k = 2; k < cells.size(); ++k)
	{
		int ax, az, al, bx, bz, bl;
		unpack(cells[anchor], ax, az, al);
		unpack(cells[k], bx, bz, bl);
		if (!LineOfSight(ax, az, al, bx, bz))
		{
			int px, pz, pl;
			unpack(cells[k - 1], px, pz, pl);
			corners.push_back(CellCenter(px, pz, pl));
			anchor = k - 1;
		}
	}
	unpack(goalI, x, z, l);
	const bool endInside = (int)std::floor((end.x - Origin.x) / cell) == x && (int)std::floor((end.z - Origin.z) / cell) == z;
	corners.push_back(endInside ? Vec3(end.x, Height(x, z, l), end.z) : CellCenter(x, z, l));
	return true;
}
