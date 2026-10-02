#include "pch.h"
#include "SpriteSlicer.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace SpriteSlicer
{
	bool Pixels::Load(const std::wstring& fullPath)
	{
		DirectX::ScratchImage img;
		if (FAILED(DirectX::LoadFromWICFile(fullPath.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, img)))
			return false;
		const DirectX::Image* src = img.GetImage(0, 0, 0);
		DirectX::ScratchImage conv;
		if (src->format != DXGI_FORMAT_R8G8B8A8_UNORM)
		{
			if (FAILED(DirectX::Convert(*src, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, conv)))
				return false;
			src = conv.GetImage(0, 0, 0);
		}
		W = (int)src->width;
		H = (int)src->height;
		RGBA.resize((size_t)W * H);
		for (int y = 0; y < H; ++y)
			memcpy(&RGBA[(size_t)y * W], src->pixels + (size_t)y * src->rowPitch, (size_t)W * 4);
		return W > 0 && H > 0;
	}

	bool Pixels::Empty(int x, int yTop, int w, int h) const
	{
		for (int y = (std::max)(0, yTop); y < (std::min)(H, yTop + h); ++y)
			for (int i = (std::max)(0, x); i < (std::min)(W, x + w); ++i)
				if (Alpha(i, y) > 0)
					return false;
		return true;
	}

	namespace
	{
		AssetImport::SpriteRect Make(const Pixels& px, int x, int yTop, int w, int h, int index, const Options& o)
		{
			AssetImport::SpriteRect r;
			r.Name = o.BaseName + "_" + std::to_string(index);
			r.X = (float)x;
			r.Y = (float)(px.H - yTop - h);   // 왼쪽 아래 원점
			r.W = (float)w;
			r.H = (float)h;
			r.PivotX = o.PivotX;
			r.PivotY = o.PivotY;
			return r;
		}
	}

	std::vector<AssetImport::SpriteRect> GridBySize(const Pixels& px, int cellW, int cellH, int offX, int offY, int padX, int padY, const Options& o)
	{
		std::vector<AssetImport::SpriteRect> out;
		if (cellW <= 0 || cellH <= 0)
			return out;
		int index = 0;
		for (int y = offY; y + cellH <= px.H; y += cellH + padY)
			for (int x = offX; x + cellW <= px.W; x += cellW + padX)
				if (o.KeepEmpty || !px.Empty(x, y, cellW, cellH))
					out.push_back(Make(px, x, y, cellW, cellH, index++, o));
		return out;
	}

	std::vector<AssetImport::SpriteRect> GridByCount(const Pixels& px, int columns, int rows, const Options& o)
	{
		if (columns <= 0 || rows <= 0)
			return {};
		return GridBySize(px, px.W / columns, px.H / rows, 0, 0, 0, 0, o);
	}

	std::vector<AssetImport::SpriteRect> Automatic(const Pixels& px, int minSize, const Options& o)
	{
		// 투명이 아닌 픽셀 덩어리 (8 방향 이웃) 의 경계 상자
		struct Box { int x0, y0, x1, y1; };
		std::vector<Box> boxes;
		std::vector<uint8> seen((size_t)px.W * px.H, 0);
		std::vector<int> stack;
		for (int y = 0; y < px.H; ++y)
			for (int x = 0; x < px.W; ++x)
			{
				const size_t i = (size_t)y * px.W + x;
				if (seen[i] || px.Alpha(x, y) == 0)
					continue;
				Box b{ x, y, x, y };
				stack.clear();
				stack.push_back((int)i);
				seen[i] = 1;
				while (!stack.empty())
				{
					const int k = stack.back();
					stack.pop_back();
					const int cx = k % px.W, cy = k / px.W;
					b.x0 = (std::min)(b.x0, cx); b.x1 = (std::max)(b.x1, cx);
					b.y0 = (std::min)(b.y0, cy); b.y1 = (std::max)(b.y1, cy);
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							const int nx = cx + dx, ny = cy + dy;
							if (nx < 0 || ny < 0 || nx >= px.W || ny >= px.H)
								continue;
							const size_t n = (size_t)ny * px.W + nx;
							if (!seen[n] && px.Alpha(nx, ny) > 0)
							{
								seen[n] = 1;
								stack.push_back((int)n);
							}
						}
				}
				if (b.x1 - b.x0 + 1 >= minSize && b.y1 - b.y0 + 1 >= minSize)
					boxes.push_back(b);
			}
		// 겹치는 상자는 합친다 (떨어진 눈 · 그림자 등이 한 스프라이트가 되게)
		for (bool merged = true; merged;)
		{
			merged = false;
			for (size_t a = 0; a < boxes.size() && !merged; ++a)
				for (size_t c = a + 1; c < boxes.size(); ++c)
					if (boxes[a].x0 <= boxes[c].x1 && boxes[c].x0 <= boxes[a].x1 && boxes[a].y0 <= boxes[c].y1 && boxes[c].y0 <= boxes[a].y1)
					{
						boxes[a] = { (std::min)(boxes[a].x0, boxes[c].x0), (std::min)(boxes[a].y0, boxes[c].y0), (std::max)(boxes[a].x1, boxes[c].x1), (std::max)(boxes[a].y1, boxes[c].y1) };
						boxes.erase(boxes.begin() + c);
						merged = true;
						break;
					}
		}
		// 위 → 아래 (같은 줄 = 세로로 겹침), 왼쪽 → 오른쪽
		std::sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) {
			const bool sameRow = a.y0 <= b.y1 && b.y0 <= a.y1;
			if (!sameRow) return a.y0 < b.y0;
			return a.x0 < b.x0;
		});
		std::vector<AssetImport::SpriteRect> out;
		int index = 0;
		for (const Box& b : boxes)
			out.push_back(Make(px, b.x0, b.y0, b.x1 - b.x0 + 1, b.y1 - b.y0 + 1, index++, o));
		return out;
	}

	std::wstring SheetJsonFor(const std::wstring& imagePath)
	{
		fs::path p(imagePath);
		p.replace_extension(L".json");
		std::error_code ec;
		if (!fs::exists(p, ec))
			return std::wstring();
		std::ifstream in(p);
		const json j = json::parse(in, nullptr, false);
		return j.is_object() && j.contains("frames") && j["frames"].is_array() && j.contains("cell") ? p.wstring() : std::wstring();
	}

	bool FromSheetJson(const std::wstring& jsonPath, int texW, int texH, const Options& o, std::vector<AssetImport::SpriteRect>& out, float* fps)
	{
		std::ifstream in(jsonPath);
		const json j = json::parse(in, nullptr, false);
		if (!j.is_object() || !j.contains("frames") || !j["frames"].is_array())
			return false;
		float pivotPx[2] = { -1, -1 };
		if (j.contains("pivot") && j["pivot"].is_array() && j["pivot"].size() == 2)
		{
			pivotPx[0] = j["pivot"][0].get<float>();
			pivotPx[1] = j["pivot"][1].get<float>();
		}
		if (fps)
			*fps = j.value("fps", 12.0f);
		out.clear();
		int index = 0;
		for (const json& f : j["frames"])
		{
			const int x = f.value("x", 0), y = f.value("y", 0), w = f.value("w", 0), h = f.value("h", 0);
			if (w <= 0 || h <= 0)
				continue;
			AssetImport::SpriteRect r;
			r.Name = o.BaseName + "_" + std::to_string(index++);
			r.X = (float)x;
			r.Y = (float)(texH - y - h);
			r.W = (float)w;
			r.H = (float)h;
			// 시트의 pivot = 칸 안 픽셀 (위 기준) — 뼈대 원점 (발 밑)
			r.PivotX = pivotPx[0] >= 0 ? pivotPx[0] / w : o.PivotX;
			r.PivotY = pivotPx[1] >= 0 ? 1.0f - pivotPx[1] / h : o.PivotY;
			out.push_back(r);
		}
		(void)texW;
		return !out.empty();
	}
}
