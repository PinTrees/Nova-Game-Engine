#include "pch.h"
#include "UISprites.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
	using Info = UISprites::Info;
	std::map<std::string, Info> s_Builtins;
	std::vector<ComPtr<GfxShaderResourceView>> s_Keep;
	std::map<std::string, Info> s_Files;

	ComPtr<GfxShaderResourceView> MakeTexture(const std::vector<uint32>& rgba, int w, int h)
	{
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = w;
		td.Height = h;
		td.MipLevels = td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_IMMUTABLE;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA data = { rgba.data(), (UINT)w * 4, 0 };
		ComPtr<GfxTexture2D> tex;
		ComPtr<GfxShaderResourceView> srv;
		auto device = Application::GetI()->GetDevice();
		if (SUCCEEDED(device->CreateTexture2D(&td, &data, tex.GetAddressOf())))
			device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf());
		return srv;
	}

	uint32 Pack(float r, float g, float b, float a)
	{
		auto c = [](float v) { return (uint32)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
		return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
	}

	// 둥근 사각형의 부호 거리 (안쪽 음수)
	float RoundRectDistance(float x, float y, float w, float h, float radius)
	{
		const float qx = fabsf(x - w * 0.5f) - (w * 0.5f - radius);
		const float qy = fabsf(y - h * 0.5f) - (h * 0.5f - radius);
		const float ox = (std::max)(qx, 0.0f), oy = (std::max)(qy, 0.0f);
		return sqrtf(ox * ox + oy * oy) + (std::min)((std::max)(qx, qy), 0.0f) - radius;
	}

	// 가장자리 1 픽셀을 부드럽게 (안티앨리어싱), outline > 0 이면 안쪽 테두리를 조금 어둡게
	std::vector<uint32> RoundRect(int w, int h, float radius, float outline, float outlineShade)
	{
		std::vector<uint32> px(w * h);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				const float d = RoundRectDistance(x + 0.5f, y + 0.5f, (float)w, (float)h, radius);
				const float alpha = std::clamp(0.5f - d, 0.0f, 1.0f);
				float shade = 1.0f;
				if (outline > 0.0f)
				{
					const float inner = std::clamp(-d - outline + 0.5f, 0.0f, 1.0f);   // 1 = 안쪽, 0 = 테두리
					shade = outlineShade + (1.0f - outlineShade) * inner;
				}
				px[y * w + x] = Pack(shade, shade, shade, alpha);
			}
		return px;
	}

	void EnsureBuiltins()
	{
		if (!s_Builtins.empty())
			return;
		auto add = [&](const char* name, const std::vector<uint32>& px, int w, int h, float border) {
			auto srv = MakeTexture(px, w, h);
			s_Keep.push_back(srv);
			Info info;
			info.Texture = srv.Get();
			info.Size = Vec2((float)w, (float)h);
			info.Border = Vec4(border, border, border, border);
			s_Builtins[name] = info;
		};
		// Unity UISprite: 흰 둥근 사각형 + 연한 회색 테두리 (Button, Input Field)
		add("builtin:UISprite", RoundRect(32, 32, 7.0f, 1.2f, 0.72f), 32, 32, 10.0f);
		// Unity Background: 테두리 없는 둥근 사각형 (Panel)
		add("builtin:Background", RoundRect(32, 32, 7.0f, 0.0f, 1.0f), 32, 32, 10.0f);
		// Unity Knob: 원
		add("builtin:Knob", RoundRect(64, 64, 32.0f, 0.0f, 1.0f), 64, 64, 0.0f);
		// Unity InputFieldBackground: UISprite 와 같은 모양
		add("builtin:InputFieldBackground", RoundRect(32, 32, 7.0f, 1.2f, 0.72f), 32, 32, 10.0f);
		// Unity Checkmark: 굵은 체크 표시 (Toggle)
		{
			const int n = 64;
			const Vec2 pts[3] = { Vec2(0.18f, 0.52f), Vec2(0.42f, 0.76f), Vec2(0.84f, 0.26f) };   // v 는 아래로
			const float half = 0.075f * n;
			std::vector<uint32> px(n * n);
			auto segDist = [](const Vec2& p, const Vec2& a, const Vec2& b) {
				const Vec2 ab = b - a;
				const float t = std::clamp((p - a).Dot(ab) / (std::max)(1e-6f, ab.Dot(ab)), 0.0f, 1.0f);
				return (p - (a + ab * t)).Length();
			};
			for (int y = 0; y < n; ++y)
				for (int x = 0; x < n; ++x)
				{
					const Vec2 p(x + 0.5f, y + 0.5f);
					const float d = (std::min)(segDist(p, pts[0] * (float)n, pts[1] * (float)n), segDist(p, pts[1] * (float)n, pts[2] * (float)n));
					px[y * n + x] = Pack(1.0f, 1.0f, 1.0f, std::clamp(half - d + 0.5f, 0.0f, 1.0f));
				}
			add("builtin:Checkmark", px, n, n, 0.0f);
		}
	}
}

namespace UISprites
{
	bool IsImagePath(const std::string& path)
	{
		std::string ext = fs::path(path).extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
		return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".dds";
	}

	std::string DisplayName(const std::string& path)
	{
		if (path.rfind("builtin:", 0) == 0)
			return path.substr(8);
		return fs::path(path).stem().string();
	}

	bool Get(const std::string& path, Info& out)
	{
		if (path.empty())
			return false;
		if (path.rfind("builtin:", 0) == 0)
		{
			EnsureBuiltins();
			auto it = s_Builtins.find(path);
			if (it == s_Builtins.end())
				return false;
			out = it->second;
			return true;
		}
		auto it = s_Files.find(path);
		if (it == s_Files.end())
		{
			Info info;
			ComPtr<GfxShaderResourceView> srv = ResourceManager::GetI()->LoadTexture(string_to_wstring(path));   // 캐시됨
			if (srv)
			{
				s_Keep.push_back(srv);
				info.Texture = srv.Get();
				ComPtr<GfxResource> res;
				srv->GetResource(res.GetAddressOf());
				ComPtr<GfxTexture2D> tex;
				if (res && SUCCEEDED(res.As(&tex)))
				{
					D3D11_TEXTURE2D_DESC d;
					tex->GetDesc(&d);
					info.Size = Vec2((float)d.Width, (float)d.Height);
				}
			}
			it = s_Files.emplace(path, info).first;
		}
		out = it->second;
		return out.Texture != nullptr;
	}

	std::vector<std::string> FindAll()
	{
		std::vector<std::string> out = { "builtin:UISprite", "builtin:Background", "builtin:InputFieldBackground", "builtin:Knob", "builtin:Checkmark" };
		std::error_code ec;
		const fs::path assets = PathManager::GetI()->GetMovePathW(L"Assets\\");
		const fs::path root = PathManager::GetI()->GetMovePathW(L"");
		std::vector<std::string> files;
		for (const auto& e : fs::recursive_directory_iterator(assets, fs::directory_options::skip_permission_denied, ec))
			if (e.is_regular_file(ec) && IsImagePath(e.path().string()))
				files.push_back(wstring_to_string(fs::relative(e.path(), root, ec).wstring()));
		std::sort(files.begin(), files.end());
		out.insert(out.end(), files.begin(), files.end());
		return out;
	}
}
