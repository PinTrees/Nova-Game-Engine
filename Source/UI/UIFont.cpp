#include "pch.h"
#include "UIFont.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
	struct Entry
	{
		std::wstring File;
		int PixelSize = 0;
		ImFontAtlas* FontAtlas = nullptr;
		std::set<unsigned int> Chars;
		ImVector<ImWchar> Ranges;          // Build 가 끝날 때까지 살아 있어야 한다
		ComPtr<ID3D11ShaderResourceView> SRV;
		UIFont::Atlas Public;
		bool Failed = false;

		~Entry() { if (FontAtlas) IM_DELETE(FontAtlas); }
	};

	std::map<std::pair<std::wstring, int>, std::unique_ptr<Entry>> s_Cache;

	bool Build(Entry& e)
	{
		if (e.FontAtlas)
			IM_DELETE(e.FontAtlas);
		e.FontAtlas = IM_NEW(ImFontAtlas)();
		e.SRV.Reset();
		e.Public = UIFont::Atlas();

		ImFontGlyphRangesBuilder builder;
		builder.AddRanges(e.FontAtlas->GetGlyphRangesDefault());   // 라틴 기본 + Latin-1
		for (unsigned int c : e.Chars)
			if (c < 0x10000)
				builder.AddChar((ImWchar)c);
		e.Ranges.clear();
		builder.BuildRanges(&e.Ranges);

		ImFontConfig cfg;
		cfg.OversampleH = 1;
		cfg.OversampleV = 1;
		cfg.PixelSnapH = true;
		cfg.RasterizerMultiply = 1.1f;
		const std::string file = wstring_to_string(e.File);
		ImFont* font = e.FontAtlas->AddFontFromFileTTF(file.c_str(), (float)e.PixelSize, &cfg, e.Ranges.Data);
		if (font == nullptr || !e.FontAtlas->Build())
		{
			EditorLog::Write("UI", "font build failed: %s (%d px)", file.c_str(), e.PixelSize);
			e.Failed = true;
			return false;
		}
		unsigned char* pixels = nullptr;
		int w = 0, h = 0;
		e.FontAtlas->GetTexDataAsRGBA32(&pixels, &w, &h);
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = w;
		td.Height = h;
		td.MipLevels = td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_IMMUTABLE;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA data = { pixels, (UINT)w * 4, 0 };
		ComPtr<ID3D11Texture2D> tex;
		auto device = Application::GetI()->GetDevice();
		if (FAILED(device->CreateTexture2D(&td, &data, tex.GetAddressOf())) ||
			FAILED(device->CreateShaderResourceView(tex.Get(), nullptr, e.SRV.GetAddressOf())))
		{
			e.Failed = true;
			return false;
		}
		e.Public.Font = font;
		e.Public.Texture = e.SRV.Get();
		e.Public.PixelSize = (float)e.PixelSize;
		e.Public.Ascent = font->Ascent;
		e.Public.Descent = font->Descent;
		EditorLog::Write("UI", "font atlas %s %dpx: %d glyphs, %dx%d", fs::path(e.File).filename().string().c_str(), e.PixelSize, font->Glyphs.Size, w, h);
		return true;
	}
}

namespace UIFont
{
	const char* DefaultFontName() { return "Pretendard (Default)"; }

	const ImFontGlyph* Atlas::Glyph(unsigned int codepoint) const
	{
		if (Font == nullptr)
			return nullptr;
		return Font->FindGlyph((ImWchar)(codepoint < 0x10000 ? codepoint : '?'));
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

	const Atlas* Get(const std::string& fontPath, bool bold, int pixelSize, const std::u32string& text)
	{
		pixelSize = std::clamp(pixelSize, 4, 256);
		const std::wstring file = ResolveFile(fontPath, bold);
		auto& slot = s_Cache[{ file, pixelSize }];
		if (!slot)
		{
			slot = std::make_unique<Entry>();
			slot->File = file;
			slot->PixelSize = pixelSize;
		}
		Entry& e = *slot;
		if (e.Failed)
			return nullptr;
		bool missing = e.FontAtlas == nullptr;
		for (char32_t c : text)
			if (c >= 0x80 && c != 0xFEFF && !e.Chars.count((unsigned int)c))
			{
				e.Chars.insert((unsigned int)c);
				missing = true;
			}
		if (missing && !Build(e))
			return nullptr;
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
}
