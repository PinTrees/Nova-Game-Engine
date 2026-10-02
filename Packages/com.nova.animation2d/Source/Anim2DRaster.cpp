#include "pch.h"
#include "Anim2DRaster.h"
#include <filesystem>

namespace Anim2D
{
	namespace
	{
		constexpr float kDeg = 3.14159265358979f / 180.0f;
		struct Cached { Image Img; std::filesystem::file_time_type Time; };
		std::map<std::string, Cached>& Cache() { static std::map<std::string, Cached> c; return c; }

		inline float Ch(uint32 c, int k) { return ((c >> (k * 8)) & 255) / 255.0f; }
		inline uint32 Pack(float r, float g, float b, float a)
		{
			auto q = [](float v) { return (uint32)std::clamp((int)(v * 255.0f + 0.5f), 0, 255); };
			return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
		}
	}

	const Image* GetImage(const std::string& path)
	{
		if (path.empty()) return nullptr;
		const std::string full = FullPath(path);
		const std::filesystem::path p = std::filesystem::path(std::u8string(full.begin(), full.end()));
		std::error_code ec;
		const auto t = std::filesystem::last_write_time(p, ec);
		auto& cache = Cache();
		auto it = cache.find(full);
		if (it != cache.end() && (ec || it->second.Time == t)) return it->second.Img.Ok ? &it->second.Img : nullptr;
		Cached c;
		c.Time = t;
		DirectX::ScratchImage img, conv;
		if (!ec && SUCCEEDED(DirectX::LoadFromWICFile(p.wstring().c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, img)))
		{
			const DirectX::Image* src = img.GetImage(0, 0, 0);
			if (src->format != DXGI_FORMAT_R8G8B8A8_UNORM && SUCCEEDED(DirectX::Convert(*src, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, conv)))
				src = conv.GetImage(0, 0, 0);
			if (src->format == DXGI_FORMAT_R8G8B8A8_UNORM)
			{
				c.Img.W = (int)src->width;
				c.Img.H = (int)src->height;
				c.Img.Px.resize((size_t)c.Img.W * c.Img.H);
				for (int y = 0; y < c.Img.H; ++y) memcpy(&c.Img.Px[(size_t)y * c.Img.W], src->pixels + (size_t)y * src->rowPitch, (size_t)c.Img.W * 4);
				c.Img.Ok = true;
			}
		}
		cache[full] = std::move(c);
		return cache[full].Img.Ok ? &cache[full].Img : nullptr;
	}

	void ForgetImages() { Cache().clear(); }

	bool AttachmentCorners(const Document& d, const Slot& s, const Attachment& a, float out[4][2])
	{
		float w = a.Width, h = a.Height;
		if (w <= 0 || h <= 0)
			if (const Image* img = GetImage(a.Image)) { if (w <= 0) w = (float)img->W; if (h <= 0) h = (float)img->H; }
		if (w <= 0 || h <= 0) return false;
		const Bone& b = d.Bones[s.Bone];
		const float r = a.Rotation * kDeg, cr = cosf(r), sr = sinf(r);
		const float hx = w * 0.5f * a.ScaleX, hy = h * 0.5f * a.ScaleY;
		const float local[4][2] = { { -hx, -hy }, { hx, -hy }, { hx, hy }, { -hx, hy } };
		for (int i = 0; i < 4; ++i)
		{
			// 첨부 기준 → 본 기준 (회전 + 위치) → 월드 (본 행렬)
			const float lx = a.X + local[i][0] * cr - local[i][1] * sr, ly = a.Y + local[i][0] * sr + local[i][1] * cr;
			out[i][0] = b.A * lx + b.B * ly + b.WX;
			out[i][1] = b.C * lx + b.D * ly + b.WY;
		}
		return true;
	}

	void Raster::Resize(int w, int h)
	{
		Width = (std::max)(1, w);
		Height = (std::max)(1, h);
		Color.assign((size_t)Width * Height, 0);
		SlotId.assign((size_t)Width * Height, -1);
	}

	void Raster::WorldToScreen(float wx, float wy, float& sx, float& sy) const
	{
		sx = Width * 0.5f + (wx - View.CX) * View.Zoom;
		sy = Height * 0.5f - (wy - View.CY) * View.Zoom;
	}

	void Raster::ScreenToWorld(float sx, float sy, float& wx, float& wy) const
	{
		wx = View.CX + (sx - Width * 0.5f) / View.Zoom;
		wy = View.CY - (sy - Height * 0.5f) / View.Zoom;
	}

	void Raster::Blend(int i, float r, float g, float b, float a)
	{
		if (a <= 0.0f) return;
		const uint32 d = Color[i];
		const float da = Ch(d, 3);
		if (!m_Transparent || da >= 0.999f)
		{
			Color[i] = Pack(r * a + Ch(d, 0) * (1 - a), g * a + Ch(d, 1) * (1 - a), b * a + Ch(d, 2) * (1 - a), m_Transparent ? 1.0f : 1.0f);
			return;
		}
		// 투명 배경 위 (over): 결과 알파 = a + da (1 - a)
		const float oa = a + da * (1 - a);
		if (oa <= 1e-6f) return;
		Color[i] = Pack((r * a + Ch(d, 0) * da * (1 - a)) / oa, (g * a + Ch(d, 1) * da * (1 - a)) / oa, (b * a + Ch(d, 2) * da * (1 - a)) / oa, oa);
	}

	void Raster::TexturedTriangle(const float s[3][2], const float uv[3][2], const Image& img, const float tint[4], int slot)
	{
		const float area = (s[1][0] - s[0][0]) * (s[2][1] - s[0][1]) - (s[1][1] - s[0][1]) * (s[2][0] - s[0][0]);
		if (fabsf(area) < 1e-6f) return;
		const int x0 = (std::max)(0, (int)floorf((std::min)({ s[0][0], s[1][0], s[2][0] })));
		const int x1 = (std::min)(Width - 1, (int)ceilf((std::max)({ s[0][0], s[1][0], s[2][0] })));
		const int y0 = (std::max)(0, (int)floorf((std::min)({ s[0][1], s[1][1], s[2][1] })));
		const int y1 = (std::min)(Height - 1, (int)ceilf((std::max)({ s[0][1], s[1][1], s[2][1] })));
		const float inv = 1.0f / area;
		for (int y = y0; y <= y1; ++y)
			for (int x = x0; x <= x1; ++x)
			{
				const float px = x + 0.5f, py = y + 0.5f;
				const float w0 = ((s[1][0] - px) * (s[2][1] - py) - (s[1][1] - py) * (s[2][0] - px)) * inv;
				const float w1 = ((s[2][0] - px) * (s[0][1] - py) - (s[2][1] - py) * (s[0][0] - px)) * inv;
				const float w2 = 1.0f - w0 - w1;
				if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
				const float u = w0 * uv[0][0] + w1 * uv[1][0] + w2 * uv[2][0];
				const float v = w0 * uv[0][1] + w1 * uv[1][1] + w2 * uv[2][1];
				// bilinear (가장자리 고정)
				const float fx = std::clamp(u * img.W - 0.5f, 0.0f, (float)img.W - 1.0f), fy = std::clamp(v * img.H - 0.5f, 0.0f, (float)img.H - 1.0f);
				const int ix = (int)fx, iy = (int)fy;
				const int ix1 = (std::min)(ix + 1, img.W - 1), iy1 = (std::min)(iy + 1, img.H - 1);
				const float tx = fx - ix, ty = fy - iy;
				const uint32 c00 = img.Px[(size_t)iy * img.W + ix], c10 = img.Px[(size_t)iy * img.W + ix1], c01 = img.Px[(size_t)iy1 * img.W + ix], c11 = img.Px[(size_t)iy1 * img.W + ix1];
				float col[4];
				for (int k = 0; k < 4; ++k)
				{
					// 알파를 곱한 채로 섞어야 가장자리에 검은 테가 생기지 않는다
					const float a00 = Ch(c00, 3), a10 = Ch(c10, 3), a01 = Ch(c01, 3), a11 = Ch(c11, 3);
					if (k == 3) col[3] = (a00 * (1 - tx) + a10 * tx) * (1 - ty) + (a01 * (1 - tx) + a11 * tx) * ty;
					else col[k] = (Ch(c00, k) * a00 * (1 - tx) + Ch(c10, k) * a10 * tx) * (1 - ty) + (Ch(c01, k) * a01 * (1 - tx) + Ch(c11, k) * a11 * tx) * ty;
				}
				const float a = col[3] * tint[3];
				if (a <= 0.002f) continue;
				const float ia = col[3] > 1e-6f ? 1.0f / col[3] : 0.0f;
				const int i = y * Width + x;
				Blend(i, col[0] * ia * tint[0], col[1] * ia * tint[1], col[2] * ia * tint[2], a);
				if (a > 0.3f) SlotId[i] = slot;
			}
	}

	void Raster::FillTriangle(const float a[2], const float b[2], const float c[2], uint32 color)
	{
		const float s[3][2] = { { a[0], a[1] }, { b[0], b[1] }, { c[0], c[1] } };
		const float area = (s[1][0] - s[0][0]) * (s[2][1] - s[0][1]) - (s[1][1] - s[0][1]) * (s[2][0] - s[0][0]);
		if (fabsf(area) < 1e-6f) return;
		const int x0 = (std::max)(0, (int)floorf((std::min)({ s[0][0], s[1][0], s[2][0] })));
		const int x1 = (std::min)(Width - 1, (int)ceilf((std::max)({ s[0][0], s[1][0], s[2][0] })));
		const int y0 = (std::max)(0, (int)floorf((std::min)({ s[0][1], s[1][1], s[2][1] })));
		const int y1 = (std::min)(Height - 1, (int)ceilf((std::max)({ s[0][1], s[1][1], s[2][1] })));
		const float inv = 1.0f / area;
		for (int y = y0; y <= y1; ++y)
			for (int x = x0; x <= x1; ++x)
			{
				const float px = x + 0.5f, py = y + 0.5f;
				const float w0 = ((s[1][0] - px) * (s[2][1] - py) - (s[1][1] - py) * (s[2][0] - px)) * inv;
				const float w1 = ((s[2][0] - px) * (s[0][1] - py) - (s[2][1] - py) * (s[0][0] - px)) * inv;
				if (w0 < 0 || w1 < 0 || 1.0f - w0 - w1 < 0) continue;
				Blend(y * Width + x, Ch(color, 0), Ch(color, 1), Ch(color, 2), Ch(color, 3));
			}
	}

	void Raster::Line(float x0, float y0, float x1, float y1, uint32 color, float width)
	{
		const float hw = width * 0.5f + 0.5f;
		const int bx0 = (std::max)(0, (int)floorf((std::min)(x0, x1) - hw)), bx1 = (std::min)(Width - 1, (int)ceilf((std::max)(x0, x1) + hw));
		const int by0 = (std::max)(0, (int)floorf((std::min)(y0, y1) - hw)), by1 = (std::min)(Height - 1, (int)ceilf((std::max)(y0, y1) + hw));
		const float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
		for (int y = by0; y <= by1; ++y)
			for (int x = bx0; x <= bx1; ++x)
			{
				const float px = x + 0.5f, py = y + 0.5f;
				const float t = l2 > 1e-9f ? std::clamp(((px - x0) * dx + (py - y0) * dy) / l2, 0.0f, 1.0f) : 0.0f;
				const float ex = x0 + dx * t - px, ey = y0 + dy * t - py;
				const float cov = std::clamp(width * 0.5f + 0.5f - sqrtf(ex * ex + ey * ey), 0.0f, 1.0f);
				if (cov > 0) Blend(y * Width + x, Ch(color, 0), Ch(color, 1), Ch(color, 2), Ch(color, 3) * cov);
			}
	}

	void Raster::Disc(float cx, float cy, float r, uint32 color)
	{
		for (int y = (std::max)(0, (int)(cy - r - 1)); y <= (std::min)(Height - 1, (int)(cy + r + 1)); ++y)
			for (int x = (std::max)(0, (int)(cx - r - 1)); x <= (std::min)(Width - 1, (int)(cx + r + 1)); ++x)
			{
				const float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
				const float cov = std::clamp(r + 0.5f - d, 0.0f, 1.0f);
				if (cov > 0) Blend(y * Width + x, Ch(color, 0), Ch(color, 1), Ch(color, 2), Ch(color, 3) * cov);
			}
	}

	void Raster::DrawGrid()
	{
		// 100 단위 격자 + 원점 축 (X 빨강 · Y 초록)
		float wx0, wy0, wx1, wy1;
		ScreenToWorld(0, (float)Height, wx0, wy0);
		ScreenToWorld((float)Width, 0, wx1, wy1);
		float step = 100.0f;
		while (step * View.Zoom < 40.0f) step *= 2.0f;
		while (step * View.Zoom > 200.0f) step *= 0.5f;
		for (float x = floorf(wx0 / step) * step; x <= wx1; x += step)
		{
			float sx, sy; WorldToScreen(x, 0, sx, sy);
			Line(sx, 0, sx, (float)Height, fabsf(x) < 1e-3f ? 0x9040A040 : 0x18FFFFFF, 1.0f);
		}
		for (float y = floorf(wy0 / step) * step; y <= wy1; y += step)
		{
			float sx, sy; WorldToScreen(0, y, sx, sy);
			Line(0, sy, (float)Width, sy, fabsf(y) < 1e-3f ? 0x904040C0 : 0x18FFFFFF, 1.0f);
		}
	}

	void Raster::DrawBones(const Document& d, const RenderOptions& opt)
	{
		for (int i = 0; i < (int)d.Bones.size(); ++i)
		{
			const Bone& b = d.Bones[i];
			float hx, hy;
			WorldToScreen(b.WX, b.WY, hx, hy);
			const uint32 col = i == opt.SelectedBone ? 0xFF2896FF : (i == opt.HoverBone ? 0xFFE0E0E0 : 0xFFB4B4B4);
			const uint32 fill = i == opt.SelectedBone ? 0x702896FF : 0x40B4B4B4;
			if (b.Length > 0.0f)
			{
				float tx, ty;
				WorldToScreen(b.A * b.Length + b.WX, b.C * b.Length + b.WY, tx, ty);
				const float dx = tx - hx, dy = ty - hy, len = sqrtf(dx * dx + dy * dy);
				if (len < 1.0f) continue;
				const float w = std::clamp(len * 0.12f, 2.0f, 9.0f);
				const float nx = -dy / len * w, ny = dx / len * w;
				const float m[2] = { hx + dx * 0.18f, hy + dy * 0.18f };
				const float p1[2] = { m[0] + nx, m[1] + ny }, p2[2] = { m[0] - nx, m[1] - ny }, h[2] = { hx, hy }, t[2] = { tx, ty };
				FillTriangle(h, p1, t, fill);
				FillTriangle(h, t, p2, fill);
				Line(hx, hy, p1[0], p1[1], col, 1.2f); Line(p1[0], p1[1], tx, ty, col, 1.2f);
				Line(hx, hy, p2[0], p2[1], col, 1.2f); Line(p2[0], p2[1], tx, ty, col, 1.2f);
			}
			Disc(hx, hy, i == opt.SelectedBone ? 4.5f : 3.5f, col);
		}
	}

	void Raster::Render(const Document& d, const View2D& view, const RenderOptions& opt)
	{
		View = view;
		m_Transparent = !opt.Background;
		std::fill(Color.begin(), Color.end(), opt.Background ? opt.BgColor : 0u);
		std::fill(SlotId.begin(), SlotId.end(), -1);
		if (opt.Grid && opt.Background) DrawGrid();
		for (int si = 0; si < (int)d.Slots.size(); ++si)
		{
			const Slot& s = d.Slots[si];
			const Attachment* a = s.Find(s.Current);
			if (!a || s.PColor[3] <= 0.0f) continue;
			const Image* img = GetImage(a->Image);
			float c[4][2];
			if (!AttachmentCorners(d, s, *a, c)) continue;
			float sc[4][2];
			for (int k = 0; k < 4; ++k) WorldToScreen(c[k][0], c[k][1], sc[k][0], sc[k][1]);
			if (!img)
			{
				// 그림이 없으면 분홍 자리 표시
				FillTriangle(sc[0], sc[1], sc[2], 0x80FF40FF);
				FillTriangle(sc[0], sc[2], sc[3], 0x80FF40FF);
				continue;
			}
			// UV: 왼아래 (0,1) · 오른아래 (1,1) · 오른위 (1,0) · 왼위 (0,0) — 그림은 위가 v = 0
			const float uv[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
			const float t0[3][2] = { { sc[0][0], sc[0][1] }, { sc[1][0], sc[1][1] }, { sc[2][0], sc[2][1] } };
			const float u0[3][2] = { { uv[0][0], uv[0][1] }, { uv[1][0], uv[1][1] }, { uv[2][0], uv[2][1] } };
			const float t1[3][2] = { { sc[0][0], sc[0][1] }, { sc[2][0], sc[2][1] }, { sc[3][0], sc[3][1] } };
			const float u1[3][2] = { { uv[0][0], uv[0][1] }, { uv[2][0], uv[2][1] }, { uv[3][0], uv[3][1] } };
			TexturedTriangle(t0, u0, *img, s.PColor, si);
			TexturedTriangle(t1, u1, *img, s.PColor, si);
			if (si == opt.SelectedSlot)
				for (int k = 0; k < 4; ++k) Line(sc[k][0], sc[k][1], sc[(k + 1) % 4][0], sc[(k + 1) % 4][1], 0xC02896FF, 1.0f);
		}
		if (opt.Bones) DrawBones(d, opt);
	}

	bool Raster::Bounds(const Document& d, float& minX, float& minY, float& maxX, float& maxY, bool withBones)
	{
		minX = minY = FLT_MAX;
		maxX = maxY = -FLT_MAX;
		auto add = [&](float x, float y) { minX = (std::min)(minX, x); minY = (std::min)(minY, y); maxX = (std::max)(maxX, x); maxY = (std::max)(maxY, y); };
		for (const Slot& s : d.Slots)
		{
			const Attachment* a = s.Find(s.Current);
			float c[4][2];
			if (a && AttachmentCorners(d, s, *a, c)) for (int k = 0; k < 4; ++k) add(c[k][0], c[k][1]);
		}
		if (withBones)
			for (const Bone& b : d.Bones) { add(b.WX, b.WY); add(b.A * b.Length + b.WX, b.C * b.Length + b.WY); }
		return minX <= maxX;
	}

	bool Raster::SavePng(const std::string& path, std::string& error) const
	{
		DirectX::Image img = {};
		img.width = Width;
		img.height = Height;
		img.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		img.rowPitch = (size_t)Width * 4;
		img.slicePitch = img.rowPitch * Height;
		img.pixels = (uint8_t*)Color.data();
		const std::filesystem::path p = std::filesystem::path(std::u8string(path.begin(), path.end()));
		std::error_code ec;
		if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
		if (FAILED(DirectX::SaveToWICFile(img, DirectX::WIC_FLAGS_FORCE_SRGB, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), p.wstring().c_str())))
		{
			error = "PNG save failed: " + path;
			return false;
		}
		return true;
	}
}
