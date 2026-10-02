#include "pch.h"
#include "UIFont.h"
#include <filesystem>
#include <fstream>
#include <chrono>

// stb_truetype: ImGui 안의 것은 static 이라 이 파일에 따로 (같은 헤더, 이 파일 안에서만 보이게)
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

namespace fs = std::filesystem;

namespace
{
	constexpr int kMaxAtlas = 4096;

	struct FontFile
	{
		std::vector<unsigned char> Data;
		stbtt_fontinfo Info = {};
		float Scale = 0.0f;   // 기준 크기 (ascent - descent = BasePx, ImGui 와 같은 크기 규칙)
		bool Ok = false;
	};

	struct Entry
	{
		std::vector<FontFile> Files;        // 0 = 고른 글꼴, 나머지 = 대체 글꼴
		std::unordered_map<char32_t, UIFont::Glyph> Glyphs;
		std::vector<unsigned char> Pixels;  // R8 아틀라스
		int PenX = 0, PenY = 0, RowH = 0;   // 줄 단위 채우기 (shelf)
		ComPtr<GfxTexture2D> Tex;
		ComPtr<GfxShaderResourceView> SRV;
		std::vector<ComPtr<GfxShaderResourceView>> Retired;   // 키우기 전 텍스처 (이번 프레임 그리기 명령이 아직 가리킬 수 있다)
		bool Dirty = false;
		UIFont::Font Public;
	};

	std::map<std::pair<std::wstring, bool>, std::unique_ptr<Entry>> s_Cache;

	bool LoadFile(const std::wstring& path, float basePx, FontFile& out)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
			return false;
		out.Data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		const int offset = stbtt_GetFontOffsetForIndex(out.Data.data(), 0);
		if (offset < 0 || !stbtt_InitFont(&out.Info, out.Data.data(), offset))
			return false;
		out.Scale = stbtt_ScaleForPixelHeight(&out.Info, basePx);
		out.Ok = true;
		return true;
	}

	// 아틀라스에 w x h 자리 (모자라면 키운다). 실패하면 false
	bool Allocate(Entry& e, int w, int h, int& x, int& y)
	{
		for (;;)
		{
			if (e.PenX + w + 1 > e.Public.AtlasW)
			{
				e.PenX = 1;
				e.PenY += e.RowH + 1;
				e.RowH = 0;
			}
			if (e.PenY + h + 1 <= e.Public.AtlasH)
			{
				x = e.PenX;
				y = e.PenY;
				e.PenX += w + 1;
				e.RowH = (std::max)(e.RowH, h);
				return true;
			}
			// 가득: 두 배로 (기존 칸은 왼쪽 위에 그대로 → 픽셀 좌표 유지)
			const int oldW = e.Public.AtlasW, oldH = e.Public.AtlasH;
			if (oldW >= kMaxAtlas)
				return false;
			const int nw = oldW * 2, nh = oldH * 2;
			std::vector<unsigned char> grown((size_t)nw * nh, 0);
			for (int row = 0; row < oldH; ++row)
				memcpy(&grown[(size_t)row * nw], &e.Pixels[(size_t)row * oldW], oldW);
			e.Pixels.swap(grown);
			e.Public.AtlasW = nw;
			e.Public.AtlasH = nh;
			++e.Public.Generation;
			// 새 줄은 오른쪽 빈 곳부터가 아니라 아래 빈 곳부터 (간단히)
			if (e.SRV)
				e.Retired.push_back(e.SRV);
			e.SRV.Reset();
			e.Tex.Reset();
			e.Dirty = true;
		}
	}

	// 1 차원 거리 변환 (Felzenszwalb & Huttenlocher): f = 제곱 거리 비용 → d = 가장 가까운 점까지 제곱 거리
	void Edt1D(const float* f, float* d, int n, int* v, float* z)
	{
		int k = 0;
		v[0] = 0;
		z[0] = -1e20f;
		z[1] = 1e20f;
		for (int q = 1; q < n; ++q)
		{
			float s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2.0f * q - 2.0f * v[k]);
			while (s <= z[k])
			{
				--k;
				s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2.0f * q - 2.0f * v[k]);
			}
			++k;
			v[k] = q;
			z[k] = s;
			z[k + 1] = 1e20f;
		}
		k = 0;
		for (int q = 0; q < n; ++q)
		{
			while (z[k + 1] < q)
				++k;
			d[q] = (q - v[k]) * (q - v[k]) + f[v[k]];
		}
	}

	// 2 차원: grid 의 0 인 칸(대상)까지 제곱 거리 (나머지는 INF 로 시작)
	void Edt2D(std::vector<float>& grid, int w, int h)
	{
		const int n = (std::max)(w, h);
		std::vector<float> f(n), d(n), z(n + 1);
		std::vector<int> v(n);
		for (int x = 0; x < w; ++x)
		{
			for (int y = 0; y < h; ++y) f[y] = grid[(size_t)y * w + x];
			Edt1D(f.data(), d.data(), h, v.data(), z.data());
			for (int y = 0; y < h; ++y) grid[(size_t)y * w + x] = d[y];
		}
		for (int y = 0; y < h; ++y)
		{
			Edt1D(&grid[(size_t)y * w], d.data(), w, v.data(), z.data());
			memcpy(&grid[(size_t)y * w], d.data(), sizeof(float) * w);
		}
	}

	const UIFont::Glyph* AddGlyph(Entry& e, char32_t c)
	{
		UIFont::Glyph g;
		int fi = -1, glyphIndex = 0;
		for (int i = 0; i < (int)e.Files.size(); ++i)
			if (e.Files[i].Ok && (glyphIndex = stbtt_FindGlyphIndex(&e.Files[i].Info, (int)c)) != 0)
			{
				fi = i;
				break;
			}
		if (fi < 0)
		{
			// 어느 글꼴에도 없다: 공백처럼 (□ 대신 비워 둔다)
			g.Advance = e.Public.BasePx * 0.25f;
			return &(e.Glyphs[c] = g);
		}
		FontFile& f = e.Files[fi];
		g.FontIndex = fi;
		int adv = 0, lsb = 0;
		stbtt_GetGlyphHMetrics(&f.Info, glyphIndex, &adv, &lsb);
		g.Advance = adv * f.Scale;
		// 거리장: 2 배 크기로 비트맵을 그린 뒤 (stb 래스터라이저는 OTF 의 3 차 곡선도 그린다 — stbtt_GetGlyphSDF 는 3 차 곡선을 빼먹는다)
		// 안 · 밖 각각 가장 가까운 반대쪽까지 거리 (EDT) → 기준 크기로 2x2 평균 → 0.5 = 가장자리
		constexpr int kSS = 2;
		const int pad = (int)ceilf(e.Public.Spread);
		int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
		stbtt_GetGlyphBitmapBox(&f.Info, glyphIndex, f.Scale * kSS, f.Scale * kSS, &bx0, &by0, &bx1, &by1);
		if (bx1 > bx0 && by1 > by0)
		{
			const int w = (int)ceilf((bx1 - bx0) / (float)kSS) + pad * 2;   // 기준 크기 칸 수
			const int h = (int)ceilf((by1 - by0) / (float)kSS) + pad * 2;
			const int W = w * kSS, H = h * kSS, off = pad * kSS;
			std::vector<unsigned char> cov((size_t)W * H, 0);
			stbtt_MakeGlyphBitmap(&f.Info, &cov[(size_t)off * W + off], bx1 - bx0, by1 - by0, W, f.Scale * kSS, f.Scale * kSS, glyphIndex);
			const float INF = 1e20f;
			std::vector<float> toIn((size_t)W * H), toOut((size_t)W * H);
			for (size_t i = 0; i < cov.size(); ++i)
			{
				const bool inside = cov[i] >= 128;
				toIn[i] = inside ? 0.0f : INF;    // 밖 → 가장 가까운 안
				toOut[i] = inside ? INF : 0.0f;   // 안 → 가장 가까운 밖
			}
			Edt2D(toIn, W, H);
			Edt2D(toOut, W, H);
			int ax = 0, ay = 0;
			if (Allocate(e, w, h, ax, ay))
			{
				const float scale = 128.0f / (e.Public.Spread * kSS);   // 2 배 픽셀 거리 → 값
				for (int y = 0; y < h; ++y)
					for (int x = 0; x < w; ++x)
					{
						float sum = 0.0f;
						for (int sy = 0; sy < kSS; ++sy)
							for (int sx = 0; sx < kSS; ++sx)
							{
								const size_t i = (size_t)(y * kSS + sy) * W + (x * kSS + sx);
								// 부호 있는 거리 (안 = +). 경계는 칸 사이라 0.5 칸씩 빼 준다
								sum += toOut[i] > 0.0f ? sqrtf(toOut[i]) - 0.5f : -(sqrtf(toIn[i]) - 0.5f);
							}
						const float dist = sum / (kSS * kSS);
						e.Pixels[(size_t)(ay + y) * e.Public.AtlasW + ax + x] = (unsigned char)std::clamp(128.0f + dist * scale, 0.0f, 255.0f);
					}
				g.AtlasX = ax;
				g.AtlasY = ay;
				g.W = w;
				g.H = h;
				g.X0 = bx0 / (float)kSS - pad;
				g.Y0 = by0 / (float)kSS - pad;
				g.X1 = g.X0 + w;
				g.Y1 = g.Y0 + h;
				g.Visible = true;
				e.Dirty = true;
			}
		}
		++e.Public.Revision;
		return &(e.Glyphs[c] = g);
	}

	bool Upload(Entry& e)
	{
		auto device = Application::GetI()->GetDevice();
		if (e.Tex == nullptr)
		{
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = e.Public.AtlasW;
			td.Height = e.Public.AtlasH;
			td.MipLevels = td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8_UNORM;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA data = { e.Pixels.data(), (UINT)e.Public.AtlasW, 0 };
			if (FAILED(device->CreateTexture2D(&td, &data, e.Tex.GetAddressOf())) ||
				FAILED(device->CreateShaderResourceView(e.Tex.Get(), nullptr, e.SRV.GetAddressOf())))
				return false;
		}
		else
			Application::GetI()->GetDeviceContext()->UpdateSubresource(e.Tex.Get(), 0, nullptr, e.Pixels.data(), (UINT)e.Public.AtlasW, 0);
		e.Public.Texture = e.SRV.Get();
		e.Dirty = false;
		return true;
	}
}

namespace UIFont
{
	const char* DefaultFontName() { return "Pretendard (Default)"; }

	const Glyph* Font::Find(char32_t c) const
	{
		const Entry* e = static_cast<const Entry*>(Impl);
		auto it = e->Glyphs.find(c);
		return it == e->Glyphs.end() ? nullptr : &it->second;
	}

	float Font::Kerning(const Glyph* a, char32_t ca, const Glyph* b, char32_t cb) const
	{
		if (a == nullptr || b == nullptr || a->FontIndex != b->FontIndex)
			return 0.0f;
		const Entry* e = static_cast<const Entry*>(Impl);
		const FontFile& f = e->Files[a->FontIndex];
		if (!f.Ok || f.Info.kern == 0 && f.Info.gpos == 0)
			return 0.0f;
		return stbtt_GetCodepointKernAdvance(&f.Info, (int)ca, (int)cb) * f.Scale;
	}

	std::wstring ResolveFile(const std::string& fontPath, bool bold)
	{
		std::error_code ec;
		if (!fontPath.empty() && fontPath.rfind("builtin:", 0) != 0)
		{
			const std::wstring full = PathManager::GetI()->GetMovePathW(string_to_wstring(fontPath));
			if (fs::exists(full, ec))
				return full;
		}
		// 기본 글꼴: 엔진의 Pretendard (한글 포함, SIL OFL), 없으면 맑은 고딕
		const std::wstring engine = PathManager::GetI()->GetEnginePathW() + L"ProjectSetting\\fonts\\";
		const std::wstring pretendard = engine + (bold ? L"Pretendard-SemiBold.otf" : L"Pretendard-Regular.otf");
		if (fs::exists(pretendard, ec))
			return pretendard;
		return bold ? L"C:\\Windows\\Fonts\\malgunbd.ttf" : L"C:\\Windows\\Fonts\\malgun.ttf";
	}

	Font* Get(const std::string& fontPath, bool bold, const std::u32string& text, bool complete)
	{
		const std::wstring file = ResolveFile(fontPath, bold);
		auto& slot = s_Cache[{ file, bold }];
		if (!slot)
		{
			slot = std::make_unique<Entry>();
			Entry& e = *slot;
			e.Public.Impl = &e;
			// 고른 글꼴 + 대체 글꼴
			std::vector<std::wstring> chain = { file, ResolveFile("", bold),
				bold ? L"C:\\Windows\\Fonts\\malgunbd.ttf" : L"C:\\Windows\\Fonts\\malgun.ttf", L"C:\\Windows\\Fonts\\seguisym.ttf" };
			for (size_t i = 0; i < chain.size(); ++i)
			{
				if (i > 0 && std::find(chain.begin(), chain.begin() + i, chain[i]) != chain.begin() + i)
					continue;   // 같은 파일 두 번
				FontFile f;
				std::error_code ec;
				if (fs::exists(chain[i], ec) && LoadFile(chain[i], e.Public.BasePx, f))
					e.Files.push_back(std::move(f));
				else if (i == 0)
					e.Files.push_back(FontFile());   // 0 번 자리는 비워 둔다 (기본 글꼴로 대신)
			}
			if (!e.Files.empty() && e.Files[0].Ok)
			{
				int ascent = 0, descent = 0, gap = 0;
				stbtt_GetFontVMetrics(&e.Files[0].Info, &ascent, &descent, &gap);
				e.Public.Ascent = ascent * e.Files[0].Scale;
				e.Public.Descent = descent * e.Files[0].Scale;
			}
			else
			{
				e.Public.Ascent = e.Public.BasePx * 0.8f;
				e.Public.Descent = -e.Public.BasePx * 0.2f;
			}
			e.Public.AtlasW = e.Public.AtlasH = 512;
			e.Pixels.assign((size_t)512 * 512, 0);
			// 꽉 찬 칸 (밑줄 등): 왼쪽 위 4x4 = 255
			for (int y = 1; y < 5; ++y)
				for (int x = 1; x < 5; ++x)
					e.Pixels[(size_t)y * 512 + x] = 255;
			e.PenX = 6;
			e.PenY = 1;
			e.RowH = 4;
			e.Dirty = true;
			// 자주 쓰는 글자 (라틴)
			for (char32_t c = 32; c < 127; ++c)
				AddGlyph(e, c);
			AddGlyph(e, U'\u2026');   // … (Ellipsis)
			EditorLog::Write("UI", "SDF font %s: %d fallback fonts", fs::path(file).filename().string().c_str(), (int)e.Files.size() - 1);
		}
		Entry& e = *slot;
		// 프레임마다 굽는 시간 (모든 글꼴 합) — 넘으면 다음 프레임에
		static double s_FrameTime = -1.0, s_Spent = 0.0;
		const double now = ImGui::GetTime();
		if (now != s_FrameTime) { s_FrameTime = now; s_Spent = 0.0; }
		e.Public.Pending = false;
		for (char32_t c : text)
			if (c >= 32 && c != 0xFEFF && !e.Glyphs.count(c))
			{
				if (!complete && s_Spent > 0.006)
				{
					e.Public.Pending = true;
					break;
				}
				const auto t0 = std::chrono::steady_clock::now();
				AddGlyph(e, c);
				s_Spent += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			}
		if (e.Dirty && !Upload(e))
			return nullptr;
		e.Public.SolidU = 3.0f / e.Public.AtlasW;
		e.Public.SolidV = 3.0f / e.Public.AtlasH;
		// 키우기 전 텍스처는 몇 개만 남긴다 (이미 지난 프레임에 그린 것)
		if (e.Retired.size() > 2)
			e.Retired.erase(e.Retired.begin(), e.Retired.end() - 2);
		return &e.Public;
	}

	bool IsFontPath(const std::string& path)
	{
		const std::string ext = fs::path(path).extension().string();
		return _stricmp(ext.c_str(), ".ttf") == 0 || _stricmp(ext.c_str(), ".otf") == 0;
	}

	std::vector<std::string> FindAll()
	{
		std::vector<std::string> out;
		std::error_code ec;
		const fs::path assets = PathManager::GetI()->GetMovePathW(L"Assets\\");
		const fs::path root = PathManager::GetI()->GetMovePathW(L"");
		for (const auto& e : fs::recursive_directory_iterator(assets, fs::directory_options::skip_permission_denied, ec))
			if (e.is_regular_file(ec) && IsFontPath(e.path().string()))
				out.push_back(wstring_to_string(fs::relative(e.path(), root, ec).wstring()));
		std::sort(out.begin(), out.end());
		return out;
	}

	std::u32string DecodeUtf8(const std::string& s)
	{
		std::u32string out;
		out.reserve(s.size());
		for (size_t i = 0; i < s.size();)
		{
			const unsigned char c = (unsigned char)s[i];
			char32_t cp = '?';
			int len = 1;
			if (c < 0x80) cp = c;
			else if ((c >> 5) == 6 && i + 1 < s.size()) { cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); len = 2; }
			else if ((c >> 4) == 14 && i + 2 < s.size()) { cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); len = 3; }
			else if ((c >> 3) == 30 && i + 3 < s.size()) { cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F); len = 4; }
			out.push_back(cp);
			i += len;
		}
		return out;
	}

	std::string EncodeUtf8(const std::u32string& s)
	{
		std::string out;
		for (char32_t c : s)
		{
			if (c < 0x80) out.push_back((char)c);
			else if (c < 0x800) { out.push_back((char)(0xC0 | (c >> 6))); out.push_back((char)(0x80 | (c & 0x3F))); }
			else if (c < 0x10000) { out.push_back((char)(0xE0 | (c >> 12))); out.push_back((char)(0x80 | ((c >> 6) & 0x3F))); out.push_back((char)(0x80 | (c & 0x3F))); }
			else { out.push_back((char)(0xF0 | (c >> 18))); out.push_back((char)(0x80 | ((c >> 12) & 0x3F))); out.push_back((char)(0x80 | ((c >> 6) & 0x3F))); out.push_back((char)(0x80 | (c & 0x3F))); }
		}
		return out;
	}
}
