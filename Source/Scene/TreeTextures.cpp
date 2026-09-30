#include "pch.h"
#include "TreeTextures.h"
#include <chrono>
#include <map>
#include <execution>
#include <numeric>

namespace
{
	struct Texel { float r, g, b, a; };

	float Saturate(float x) { return std::clamp(x, 0.0f, 1.0f); }
	float Lerp(float a, float b, float t) { return a + (b - a) * t; }
	float Smoothstep(float e0, float e1, float x) { const float t = Saturate((x - e0) / (e1 - e0)); return t * t * (3.0f - 2.0f * t); }
	float Hash11(float n) { const float x = sinf(n * 12.9898f) * 43758.5453f; return x - floorf(x); }

	// 밉맵 포함 RGBA8 텍스처. levels[0] = 원본, 이후 절반씩
	ComPtr<ID3D11ShaderResourceView> Upload(const std::vector<std::vector<Texel>>& levels, int size)
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
		ComPtr<ID3D11Texture2D> tex;
		ComPtr<ID3D11ShaderResourceView> srv;
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

	// 카드 한 장 (셀 해상도 res)
	void BakeLeafCard(std::vector<Texel>& atlas, int atlasSize, int ox, int oy, int res, int shape, int count, float size, float seed)
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
	ID3D11ShaderResourceView* Leaf(int shape, int leavesPerCard, float leafLength)
	{
		shape = std::clamp(shape, 0, 2);
		leavesPerCard = std::clamp(leavesPerCard, 1, 16);
		const int lengthKey = (int)roundf(std::clamp(leafLength, 0.05f, 0.6f) * 100.0f);
		const int key = shape * 100000 + leavesPerCard * 1000 + lengthKey;

		static std::map<int, ComPtr<ID3D11ShaderResourceView>> cache;
		static std::vector<int> order;
		if (auto it = cache.find(key); it != cache.end())
			return it->second.Get();

		const auto t0 = std::chrono::steady_clock::now();
		const int res = shape == 2 ? 512 : 256;   // 바늘잎은 가늘어 더 촘촘하게
		const int size = res * 2;                  // 2x2 변형
		std::vector<Texel> atlas((size_t)size * size, Texel{ 0.5f, 0.0f, 0.0f, 0.0f });
		static const float kSeeds[4] = { 0.13f, 0.37f, 0.61f, 0.89f };
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
		ComPtr<ID3D11ShaderResourceView> srv = Upload(levels, size);
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

	ID3D11ShaderResourceView* Bark()
	{
		static ComPtr<ID3D11ShaderResourceView> srv;
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
