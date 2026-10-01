#include "pch.h"
#include "TreeTextures.h"
#include <chrono>
#include <map>
#include <execution>
#include <numeric>
#include <mutex>

namespace
{
	struct Texel { float r, g, b, a; };

	float Saturate(float x) { return std::clamp(x, 0.0f, 1.0f); }
	float Lerp(float a, float b, float t) { return a + (b - a) * t; }
	float Smoothstep(float e0, float e1, float x) { const float t = Saturate((x - e0) / (e1 - e0)); return t * t * (3.0f - 2.0f * t); }
	float Hash11(float n) { const float x = sinf(n * 12.9898f) * 43758.5453f; return x - floorf(x); }
	const float kSeeds[4] = { 0.13f, 0.37f, 0.61f, 0.89f };   // 아틀라스 칸 4 개의 잎 배치 시드
	std::map<int, ComPtr<GfxShaderResourceView>> s_LeafCache;   // 잎 아틀라스 (모양·잎 수·길이마다)
	ComPtr<GfxShaderResourceView> s_BarkSrv;

	// 밉맵 포함 RGBA8 텍스처. levels[0] = 원본, 이후 절반씩
	ComPtr<GfxShaderResourceView> Upload(const std::vector<std::vector<Texel>>& levels, int size)
	{
		std::vector<std::vector<uint8_t>> bytes(levels.size());
		std::vector<D3D11_SUBRESOURCE_DATA> init(levels.size());
		int s = size;
		for (size_t m = 0; m < levels.size(); ++m, s = (std::max)(1, s / 2))
		{
			bytes[m].resize((size_t)s * s * 4);
			for (size_t i = 0; i < levels[m].size(); ++i)
			{
				const Texel& t = levels[m][i];
				bytes[m][i * 4 + 0] = (uint8_t)(Saturate(t.r) * 255.0f + 0.5f);
				bytes[m][i * 4 + 1] = (uint8_t)(Saturate(t.g) * 255.0f + 0.5f);
				bytes[m][i * 4 + 2] = (uint8_t)(Saturate(t.b) * 255.0f + 0.5f);
				bytes[m][i * 4 + 3] = (uint8_t)(Saturate(t.a) * 255.0f + 0.5f);
			}
			init[m] = { bytes[m].data(), (UINT)(s * 4), 0 };
		}
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = td.Height = size;
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

	// 2x2 상자 필터로 한 단계 줄이기. premultiplied: rgb 는 알파 가중 평균 (투명한 곳 값이 번지지 않게)
	std::vector<Texel> Downsample(const std::vector<Texel>& src, int size, bool alphaWeighted)
	{
		const int h = (std::max)(1, size / 2);
		std::vector<Texel> dst((size_t)h * h);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < h; ++x)
			{
				Texel sum = { 0, 0, 0, 0 };
				float wsum = 0.0f;
				for (int k = 0; k < 4; ++k)
				{
					const Texel& t = src[(size_t)(y * 2 + (k >> 1)) * size + (x * 2 + (k & 1))];
					const float w = alphaWeighted ? t.a + 1e-4f : 1.0f;
					sum.r += t.r * w; sum.g += t.g * w; sum.b += t.b * w;
					sum.a += t.a;
					wsum += w;
				}
				dst[(size_t)y * h + x] = { sum.r / wsum, sum.g / wsum, sum.b / wsum, sum.a * 0.25f };
			}
		return dst;
	}

	float Coverage(const std::vector<Texel>& t, float scale)
	{
		size_t n = 0;
		for (const Texel& x : t)
			n += x.a * scale > 0.5f;
		return (float)n / (float)t.size();
	}

	// ================================================================ 잎 (SDF) — 예전 셰이더 식을 그대로 옮김
	struct Leaf
	{
		float bx, by, dx, dy, len, width;
		float id;
	};

	float LeafSDF(float px, float py, const Leaf& l, int shape, float& along, float& across)
	{
		const float dx = px - l.bx, dy = py - l.by;
		along = (dx * l.dx + dy * l.dy) / l.len;
		across = dx * -l.dy + dy * l.dx;
		const float a = Saturate(along);
		float profile;
		if (shape == 0)
			profile = powf(sinf(XM_PI * powf(a, 0.8f)), 0.65f) * (1.0f - 0.15f * a);   // Broad: 넓고 끝이 뾰족
		else if (shape == 1)
			profile = powf(sinf(XM_PI * a), 0.9f);                                        // Oval
		else
			profile = 1.0f - a * 0.6f;                                                     // Needle
		const float dAcross = fabsf(across) - l.width * profile;
		const float dAlong = (std::max)(-along, along - 1.0f) * l.len;
		return (std::max)(dAcross, dAlong);
	}

	// 카드 한 장의 잎 배치 (굽기와 카드 외곽(LeafCardHull)이 같이 쓴다)
	std::vector<Leaf> MakeLeaves(int shape, int count, float size, float seed)
	{
		const bool needle = shape == 2;
		const float twigTop = needle ? 0.95f : 0.85f;
		std::vector<Leaf> leaves;
		for (int i = 0; i <= count; ++i)
		{
			const float r = Hash11(seed * 91.7f + i * 7.13f);
			const bool tip = i == count;
			const float t = tip ? 1.0f : (i + 0.5f) / count;
			const float side = (i & 1) ? 1.0f : -1.0f;
			const float angle = tip ? 0.0f : side * XMConvertToRadians(Lerp(needle ? 62.0f : 58.0f, needle ? 40.0f : 32.0f, t) + (r - 0.5f) * 14.0f);
			Leaf l;
			l.dx = sinf(angle);
			l.dy = cosf(angle);
			l.len = size * (tip ? 0.9f : Lerp(1.0f, 0.78f, t)) * (0.88f + 0.24f * r) * (needle ? 0.8f : 1.0f);
			l.width = needle ? 0.012f : size * (shape == 0 ? 0.34f : 0.2f);
			l.bx = 0.5f;
			l.by = tip ? twigTop : Lerp(0.1f, twigTop - 0.06f, t);
			l.id = (float)i + r;
			leaves.push_back(l);
		}
		return leaves;
	}

	// 카드 한 장 (셀 해상도 res)
	void BakeLeafCard(std::vector<Texel>& atlas, int atlasSize, int ox, int oy, int res, int shape, int count, float size, float seed)
	{
		const bool needle = shape == 2;
		const float twigTop = needle ? 0.95f : 0.85f;
		const std::vector<Leaf> leaves = MakeLeaves(shape, count, size, seed);
		const float px1 = 1.0f / res;
		std::vector<int> rows(res);
		std::iota(rows.begin(), rows.end(), 0);
		// 줄마다 따로 계산 (여러 코어)
		std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int y) {
			for (int x = 0; x < res; ++x)
			{
				// 텍셀 중심의 카드 uv (v = 0 이 잔가지 쪽 = 텍스처 아래)
				const float u = (x + 0.5f) * px1;
				const float v = 1.0f - (y + 0.5f) * px1;
				float best = (std::max)(fabsf(u - 0.5f) - Lerp(0.014f, 0.006f, Saturate(v / twigTop)), (std::max)(-v, v - twigTop));
				float bestAlong = 0.0f, bestAcross = 0.0f, bestId = 0.0f;
				bool twig = true;
				for (const Leaf& l : leaves)
				{
					const float reach = l.len + l.width + 2.0f * px1;
					if (fabsf(u - l.bx) > reach || fabsf(v - l.by) > reach)
						continue;
					float along, across;
					const float d = LeafSDF(u, v, l, shape, along, across);
					if (d < best)
					{
						best = d;
						bestAlong = along;
						bestAcross = across / (std::max)(l.width, 0.001f);
						bestId = l.id;
						twig = false;
					}
				}
				Texel t;
				t.r = std::clamp(bestAcross, -1.0f, 1.0f) * 0.5f + 0.5f;
				t.g = Saturate(bestAlong);
				t.b = twig ? 0.0f : 0.2f + 0.8f * (bestId * 0.618f - floorf(bestId * 0.618f));
				t.a = Saturate(0.5f - best * res);   // 경계 1 픽셀을 부드럽게
				atlas[(size_t)(oy + y) * atlasSize + (ox + x)] = t;
			}
		});
	}

	// ================================================================ 수피 (주기 노이즈)
	uint32_t HashU(uint32_t x, uint32_t y, uint32_t z)
	{
		uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ z * 0xcb1ab31fu;
		h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
		return h;
	}

	float NoisePeriodic(float px, float py, int periodX, int periodY, uint32_t salt)
	{
		const float fx = floorf(px), fy = floorf(py);
		const float tx = px - fx, ty = py - fy;
		const float ux = tx * tx * (3.0f - 2.0f * tx), uy = ty * ty * (3.0f - 2.0f * ty);
		auto wrap = [](int v, int p) { v %= p; return v < 0 ? v + p : v; };
		const int x0 = wrap((int)fx, periodX), x1 = wrap((int)fx + 1, periodX);
		const int y0 = wrap((int)fy, periodY), y1 = wrap((int)fy + 1, periodY);
		auto h = [&](int x, int y) { return (HashU((uint32_t)x, (uint32_t)y, salt) & 0xffffff) / 16777215.0f; };
		return Lerp(Lerp(h(x0, y0), h(x1, y0), ux), Lerp(h(x0, y1), h(x1, y1), ux), uy);
	}

	// 균열 높이 (0 = 틈, 1 = 껍질 판). 좌표 = 칸 단위 (타일 한 장 = kBarkCells 칸)
	float BarkHeight(float cx, float cy)
	{
		const int K = TreeTextures::kBarkCells;
		const float n = NoisePeriodic(cx, cy, K, K, 1) * 0.65f + NoisePeriodic(cx * 2.0f, cy * 2.0f + 5.7f, K * 2, K * 2, 2) * 0.35f;
		const float crack = Smoothstep(0.012f, 0.08f, fabsf(n - 0.5f));
		const float plate = NoisePeriodic(cx * 3.0f, cy * 5.0f + 3.1f, K * 3, K * 5, 3);
		return crack * Lerp(0.7f, 1.0f, plate);
	}
}

namespace TreeTextures
{
	void CollectMemory(std::vector<MemoryStats::Item>& items)
	{
		for (const auto& [key, srv] : s_LeafCache)
			items.push_back({ "Leaf atlas " + std::to_string(key), MemoryStats::ViewBytes(srv.Get()) });
		if (s_BarkSrv)
			items.push_back({ "Bark tile", MemoryStats::ViewBytes(s_BarkSrv.Get()) });
	}

	const LeafHull& LeafCardHull(int shape, int leavesPerCard, float leafLength, int cell)
	{
		shape = std::clamp(shape, 0, 2);
		leavesPerCard = std::clamp(leavesPerCard, 1, 16);
		const int lengthKey = (int)roundf(std::clamp(leafLength, 0.05f, 0.6f) * 100.0f);
		cell &= 3;
		const int key = ((shape * 100000 + leavesPerCard * 1000 + lengthKey) << 2) | cell;
		static std::mutex s_Lock;
		static std::map<int, LeafHull> s_Cache;
		std::lock_guard<std::mutex> lock(s_Lock);
		if (auto it = s_Cache.find(key); it != s_Cache.end())
			return it->second;

		// 굽기와 같은 식으로 덮임을 낮은 해상도로 보고, 덮인 텍셀 모서리의 u, v, u+v, u-v 최솟값·최댓값 (8 방향 k-DOP)
		const bool needle = shape == 2;
		const float twigTop = needle ? 0.95f : 0.85f;
		const auto leaves = MakeLeaves(shape, leavesPerCard, lengthKey / 100.0f, kSeeds[cell]);   // (이 이름공간에서 Leaf 는 함수)
		constexpr int R = 96;
		const float px1 = 1.0f / R;
		float lo[4] = { 1e9f, 1e9f, 1e9f, 1e9f }, hi[4] = { -1e9f, -1e9f, -1e9f, -1e9f };
		for (int y = 0; y < R; ++y)
			for (int x = 0; x < R; ++x)
			{
				const float u = (x + 0.5f) * px1, v = (y + 0.5f) * px1;
				float best = (std::max)(fabsf(u - 0.5f) - Lerp(0.014f, 0.006f, Saturate(v / twigTop)), (std::max)(-v, v - twigTop));
				for (const auto& l : leaves)
				{
					float along, across;
					best = (std::min)(best, LeafSDF(u, v, l, shape, along, across));
				}
				if (best > px1)   // 경계에서 한 텍셀 바깥까지 덮임으로 본다
					continue;
				for (int k = 0; k < 4; ++k)
				{
					const float cu = u + ((k & 1) ? 0.5f : -0.5f) * px1, cv = v + ((k & 2) ? 0.5f : -0.5f) * px1;
					const float p[4] = { cu, cv, cu + cv, cu - cv };
					for (int a = 0; a < 4; ++a)
					{
						lo[a] = (std::min)(lo[a], p[a]);
						hi[a] = (std::max)(hi[a], p[a]);
					}
				}
			}
		// 여유: 멀리서 쓰는 밉은 덮임을 맞추며 번지므로 카드 크기의 3% 더
		constexpr float m = 0.03f;
		for (int a = 0; a < 4; ++a)
		{
			const float ma = a < 2 ? m : m * 1.4142136f;
			lo[a] -= ma;
			hi[a] += ma;
		}
		// 사각형 [0,1]² ∩ u·v 범위에서 시작해 대각 반평면 4 개로 자른다 (Sutherland-Hodgman)
		std::vector<XMFLOAT2> poly = {
			{ (std::max)(0.0f, lo[0]), (std::max)(0.0f, lo[1]) }, { (std::min)(1.0f, hi[0]), (std::max)(0.0f, lo[1]) },
			{ (std::min)(1.0f, hi[0]), (std::min)(1.0f, hi[1]) }, { (std::max)(0.0f, lo[0]), (std::min)(1.0f, hi[1]) } };
		auto clipBy = [&](float a, float b, float c) {   // a*u + b*v <= c 쪽을 남긴다
			std::vector<XMFLOAT2> out;
			for (size_t i = 0; i < poly.size(); ++i)
			{
				const XMFLOAT2 p = poly[i], q = poly[(i + 1) % poly.size()];
				const float dp = a * p.x + b * p.y - c, dq = a * q.x + b * q.y - c;
				if (dp <= 0.0f)
					out.push_back(p);
				if ((dp <= 0.0f) != (dq <= 0.0f))
				{
					const float t = dp / (dp - dq);
					out.push_back({ p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t });
				}
			}
			poly = std::move(out);
		};
		clipBy(1.0f, 1.0f, hi[2]);
		clipBy(-1.0f, -1.0f, -lo[2]);
		clipBy(1.0f, -1.0f, hi[3]);
		clipBy(-1.0f, 1.0f, -lo[3]);

		LeafHull hull;
		if (poly.size() >= 3 && poly.size() <= 8)
		{
			hull.Count = (int)poly.size();
			for (int i = 0; i < hull.Count; ++i)
				hull.Points[i] = poly[i];
		}
		else
		{
			hull.Count = 4;
			hull.Points[0] = { 0, 0 }; hull.Points[1] = { 1, 0 }; hull.Points[2] = { 1, 1 }; hull.Points[3] = { 0, 1 };
		}
		float area = 0.0f;
		for (int i = 0; i < hull.Count; ++i)
		{
			const XMFLOAT2& p = hull.Points[i];
			const XMFLOAT2& q = hull.Points[(i + 1) % hull.Count];
			area += p.x * q.y - q.x * p.y;
		}
		hull.Area = fabsf(area) * 0.5f;
		EditorLog::Write("Tree", "leaf card hull: shape %d, %d leaves, length %.2f, cell %d -> %d points, %.0f%% of card", shape, leavesPerCard, lengthKey / 100.0f, cell, hull.Count, hull.Area * 100.0f);
		return s_Cache[key] = hull;
	}

	GfxShaderResourceView* Leaf(int shape, int leavesPerCard, float leafLength)
	{
		shape = std::clamp(shape, 0, 2);
		leavesPerCard = std::clamp(leavesPerCard, 1, 16);
		const int lengthKey = (int)roundf(std::clamp(leafLength, 0.05f, 0.6f) * 100.0f);
		const int key = shape * 100000 + leavesPerCard * 1000 + lengthKey;

		auto& cache = s_LeafCache;
		static std::vector<int> order;
		if (auto it = cache.find(key); it != cache.end())
			return it->second.Get();

		const auto t0 = std::chrono::steady_clock::now();
		const int res = shape == 2 ? 512 : 256;   // 바늘잎은 가늘어 더 촘촘하게
		const int size = res * 2;                  // 2x2 변형
		std::vector<Texel> atlas((size_t)size * size, Texel{ 0.5f, 0.0f, 0.0f, 0.0f });
		for (int c = 0; c < 4; ++c)
			BakeLeafCard(atlas, size, (c & 1) * res, (c >> 1) * res, res, shape, leavesPerCard, lengthKey / 100.0f, kSeeds[c]);

		// 밉맵: 알파 덮임 비율(> 0.5)을 원본과 같게 맞춘다 → 멀리서도 잎이 같은 양으로 보인다
		std::vector<std::vector<Texel>> levels;
		levels.push_back(atlas);
		const float target = Coverage(atlas, 1.0f);
		int s = size;
		while (s > 1)
		{
			std::vector<Texel> next = Downsample(levels.back(), s, true);
			s /= 2;
			float lo = 1.0f, hi = 8.0f;
			for (int it = 0; it < 12; ++it)
			{
				const float mid = (lo + hi) * 0.5f;
				(Coverage(next, mid) < target ? lo : hi) = mid;
			}
			for (Texel& t : next)
				t.a = Saturate(t.a * hi);
			levels.push_back(std::move(next));
		}
		ComPtr<GfxShaderResourceView> srv = Upload(levels, size);
		const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
		EditorLog::Write("Tree", "leaf texture baked: shape %d, %d leaves, length %.2f (%d x %d, %.1f ms)", shape, leavesPerCard, lengthKey / 100.0f, size, size, ms);

		// 슬라이더를 끌면 조합이 많이 생기므로 오래된 것부터 버린다 (쓰는 중인 SRV 는 효과 변수가 잡고 있다)
		cache[key] = srv;
		order.push_back(key);
		if (order.size() > 12)
		{
			cache.erase(order.front());
			order.erase(order.begin());
		}
		return srv.Get();
	}

	GfxShaderResourceView* Bark()
	{
		auto& srv = s_BarkSrv;
		if (srv)
			return srv.Get();
		const auto t0 = std::chrono::steady_clock::now();
		const int res = 256;
		const float cellPerTexel = (float)kBarkCells / res;
		std::vector<float> h((size_t)res * res);
		std::vector<int> rows(res);
		std::iota(rows.begin(), rows.end(), 0);
		std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int y) {
			for (int x = 0; x < res; ++x)
				h[(size_t)y * res + x] = BarkHeight((x + 0.5f) * cellPerTexel, (y + 0.5f) * cellPerTexel);
		});
		// 기울기 (칸 단위, 가로·세로 모두 이어지게 감싸서). 저장은 ÷8 후 0.5 중심
		std::vector<Texel> level((size_t)res * res);
		for (int y = 0; y < res; ++y)
			for (int x = 0; x < res; ++x)
			{
				const float hx = h[(size_t)y * res + (x + 1) % res] - h[(size_t)y * res + (x + res - 1) % res];
				const float hy = h[(size_t)((y + 1) % res) * res + x] - h[(size_t)((y + res - 1) % res) * res + x];
				const float gx = hx / (2.0f * cellPerTexel), gy = hy / (2.0f * cellPerTexel);
				level[(size_t)y * res + x] = { gx / 16.0f + 0.5f, gy / 16.0f + 0.5f, h[(size_t)y * res + x], 1.0f };
			}
		std::vector<std::vector<Texel>> levels;
		levels.push_back(level);
		int s = res;
		while (s > 1)
		{
			levels.push_back(Downsample(levels.back(), s, false));
			s /= 2;
		}
		srv = Upload(levels, res);
		const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
		EditorLog::Write("Tree", "bark texture baked (%d x %d, %.1f ms)", res, res, ms);
		return srv.Get();
	}
}
