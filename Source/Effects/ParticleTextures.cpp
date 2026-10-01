#include "pch.h"
#include "ParticleTextures.h"
#include "UISprites.h"

namespace
{
	std::map<std::string, ComPtr<GfxShaderResourceView>> s_Builtins;

	const char* kNames[] = { "builtin:Default-Particle", "builtin:Glow", "builtin:Smoke", "builtin:Spark", "builtin:Flame-Sheet", "builtin:Trail" };

	// 밉맵까지 만든다 (멀리 있는 작은 입자가 반짝이지 않게)
	ComPtr<GfxShaderResourceView> MakeTexture(const std::vector<uint32>& rgba, int w, int h)
	{
		auto device = Application::GetI()->GetDevice();
		auto ctx = Application::GetI()->GetDeviceContext();
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = w;
		td.Height = h;
		td.MipLevels = 0;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
		ComPtr<GfxTexture2D> tex;
		ComPtr<GfxShaderResourceView> srv;
		if (FAILED(device->CreateTexture2D(&td, nullptr, tex.GetAddressOf())))
			return srv;
		ctx->UpdateSubresource(tex.Get(), 0, nullptr, rgba.data(), (UINT)w * 4, 0);
		if (SUCCEEDED(device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf())))
			ctx->GenerateMips(srv.Get());
		return srv;
	}

	uint32 Pack(float r, float g, float b, float a)
	{
		auto c = [](float v) { return (uint32)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
		return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
	}

	float Hash2(int x, int y, int seed)
	{
		uint32 h = (uint32)x * 374761393u + (uint32)y * 668265263u + (uint32)seed * 2246822519u;
		h = (h ^ (h >> 13)) * 1274126177u;
		h ^= h >> 16;
		return (h & 0xFFFFFF) / (float)0xFFFFFF;
	}

	// 타일 가능한 값 노이즈 (period 단위로 반복)
	float Noise2(float x, float y, int period, int seed)
	{
		const int ix = (int)floorf(x), iy = (int)floorf(y);
		const float fx = x - ix, fy = y - iy;
		auto s = [](float t) { return t * t * (3.0f - 2.0f * t); };
		auto h = [&](int a, int b) { return Hash2(((a % period) + period) % period, ((b % period) + period) % period, seed); };
		const float a = h(ix, iy), b = h(ix + 1, iy), c = h(ix, iy + 1), d = h(ix + 1, iy + 1);
		const float ux = s(fx), uy = s(fy);
		return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy;
	}

	float Fbm(float x, float y, int seed, int octaves = 5)
	{
		float sum = 0.0f, amp = 0.5f, norm = 0.0f;
		int period = 4;
		for (int o = 0; o < octaves; ++o)
		{
			sum += Noise2(x * period, y * period, period, seed + o * 17) * amp;
			norm += amp;
			amp *= 0.5f;
			period *= 2;
		}
		return sum / norm;
	}

	float Smooth(float e0, float e1, float x)
	{
		const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	std::vector<uint32> DefaultParticle(int n)
	{
		std::vector<uint32> px(n * n);
		for (int y = 0; y < n; ++y)
			for (int x = 0; x < n; ++x)
			{
				const float dx = (x + 0.5f) / n * 2.0f - 1.0f, dy = (y + 0.5f) / n * 2.0f - 1.0f;
				const float r = sqrtf(dx * dx + dy * dy);
				const float a = 1.0f - Smooth(0.0f, 1.0f, r);   // 가운데 불투명 → 가장자리 0
				px[y * n + x] = Pack(1, 1, 1, a * a * (3.0f - 2.0f * a));
			}
		return px;
	}

	std::vector<uint32> Glow(int n)
	{
		std::vector<uint32> px(n * n);
		for (int y = 0; y < n; ++y)
			for (int x = 0; x < n; ++x)
			{
				const float dx = (x + 0.5f) / n * 2.0f - 1.0f, dy = (y + 0.5f) / n * 2.0f - 1.0f;
				const float r = sqrtf(dx * dx + dy * dy);
				const float a = std::clamp(expf(-r * r * 9.0f) * 1.1f - 0.02f, 0.0f, 1.0f) * (1.0f - Smooth(0.85f, 1.0f, r));
				px[y * n + x] = Pack(1, 1, 1, a);
			}
		return px;
	}

	std::vector<uint32> Smoke(int n)
	{
		std::vector<uint32> px(n * n);
		for (int y = 0; y < n; ++y)
			for (int x = 0; x < n; ++x)
			{
				const float u = (x + 0.5f) / n, v = (y + 0.5f) / n;
				const float dx = u * 2.0f - 1.0f, dy = v * 2.0f - 1.0f;
				const float r = sqrtf(dx * dx + dy * dy);
				const float f = Fbm(u, v, 7);
				// 둥근 모양을 노이즈로 흐트러뜨림 + 위쪽이 조금 밝은 음영
				const float shape = 1.0f - Smooth(0.35f, 1.0f, r + (f - 0.5f) * 0.7f);
				const float a = std::clamp(shape * (0.55f + f * 0.75f), 0.0f, 1.0f);
				const float shade = std::clamp(0.72f + (1.0f - v) * 0.2f + (f - 0.5f) * 0.35f, 0.0f, 1.0f);
				px[y * n + x] = Pack(shade, shade, shade, a);
			}
		return px;
	}

	std::vector<uint32> Spark(int n)
	{
		std::vector<uint32> px(n * n);
		for (int y = 0; y < n; ++y)
			for (int x = 0; x < n; ++x)
			{
				const float dx = (x + 0.5f) / n * 2.0f - 1.0f, dy = (y + 0.5f) / n * 2.0f - 1.0f;
				const float r = sqrtf(dx * dx + dy * dy);
				const float core = expf(-r * r * 40.0f);
				const float rays = expf(-fabsf(dx) * 30.0f) * expf(-fabsf(dy) * 3.0f) + expf(-fabsf(dy) * 30.0f) * expf(-fabsf(dx) * 3.0f);
				const float a = std::clamp(core + rays * 0.6f, 0.0f, 1.0f) * (1.0f - Smooth(0.9f, 1.0f, r));
				px[y * n + x] = Pack(1, 1, 1, a);
			}
		return px;
	}

	// 꼬리: 길이 방향(u)은 그대로, 폭 방향(v)은 가운데가 밝고 가장자리로 부드럽게 사라진다
	std::vector<uint32> TrailTex(int n)
	{
		std::vector<uint32> px(n * n);
		for (int y = 0; y < n; ++y)
			for (int x = 0; x < n; ++x)
			{
				const float v = fabsf((y + 0.5f) / n * 2.0f - 1.0f);
				const float a = 1.0f - Smooth(0.25f, 1.0f, v);
				px[y * n + x] = Pack(1, 1, 1, a);
			}
		return px;
	}

	// 4x4 프레임: 아래가 넓고 위로 갈수록 가늘어지는 불꽃 (프레임마다 노이즈를 위로 흘려 일렁임)
	std::vector<uint32> FlameSheet(int frame)
	{
		const int n = frame * 4;
		std::vector<uint32> px(n * n);
		for (int fy = 0; fy < 4; ++fy)
			for (int fx = 0; fx < 4; ++fx)
			{
				const int f = fy * 4 + fx;
				const float phase = f / 16.0f;
				for (int y = 0; y < frame; ++y)
					for (int x = 0; x < frame; ++x)
					{
						const float u = (x + 0.5f) / frame, v = (y + 0.5f) / frame;   // v: 0 위, 1 아래
						const float up = 1.0f - v;                                     // 0 아래, 1 위
						const float nse = Fbm(u * 0.8f, fmodf(v * 0.8f + phase, 1.0f), 3);
						// 물방울 모양: 아래(up < 0.3)는 둥글고, 위로 갈수록 가늘어져 끝이 흔들린다
						const float baseY = 0.3f;
						const float width = 0.34f * (1.0f - (std::max)(0.0f, up - baseY) * 1.2f) + 0.02f;
						const float cx = 0.5f + (nse - 0.5f) * 0.4f * (std::max)(0.0f, up - baseY);
						const float nx = (u - cx) / (std::max)(0.02f, width);
						const float ny = up < baseY ? (baseY - up) / 0.24f : 0.0f;
						const float d = sqrtf(nx * nx + ny * ny);
						const float body = (1.0f - Smooth(0.45f, 1.0f, d + (nse - 0.5f) * 0.5f)) * (1.0f - Smooth(0.7f, 0.98f, up + (nse - 0.5f) * 0.3f));
						const float a = std::clamp(body * (0.55f + nse * 0.6f), 0.0f, 1.0f);
						const float heat = std::clamp(a * (1.1f - up * 0.5f) * (1.0f - d * 0.35f), 0.0f, 1.0f);   // 아래·가운데가 밝다
						px[(fy * frame + y) * n + fx * frame + x] = Pack(heat, heat, heat, a);
					}
			}
		return px;
	}

	void EnsureBuiltins()
	{
		if (!s_Builtins.empty())
			return;
		s_Builtins["builtin:Default-Particle"] = MakeTexture(DefaultParticle(64), 64, 64);
		s_Builtins["builtin:Glow"] = MakeTexture(Glow(64), 64, 64);
		s_Builtins["builtin:Smoke"] = MakeTexture(Smoke(128), 128, 128);
		s_Builtins["builtin:Spark"] = MakeTexture(Spark(64), 64, 64);
		s_Builtins["builtin:Flame-Sheet"] = MakeTexture(FlameSheet(64), 256, 256);
		s_Builtins["builtin:Trail"] = MakeTexture(TrailTex(32), 32, 32);
		EditorLog::Write("Particles", "builtin textures created (%zu)", s_Builtins.size());
	}
}

namespace ParticleTextures
{
	bool IsBuiltin(const std::string& path) { return path.rfind("builtin:", 0) == 0; }

	GfxShaderResourceView* Get(const std::string& path)
	{
		if (path.empty() || IsBuiltin(path))
		{
			EnsureBuiltins();
			auto it = s_Builtins.find(path.empty() ? "builtin:Default-Particle" : path);
			if (it == s_Builtins.end())
				it = s_Builtins.find("builtin:Default-Particle");
			return it->second.Get();
		}
		UISprites::Info info;
		if (UISprites::Get(path, info))
			return info.Texture;
		return Get("builtin:Default-Particle");
	}

	std::vector<std::string> FindAll()
	{
		std::vector<std::string> out(std::begin(kNames), std::end(kNames));
		for (const std::string& s : UISprites::FindAll())
			if (!IsBuiltin(s))
				out.push_back(s);
		return out;
	}

	std::string DisplayName(const std::string& path)
	{
		return UISprites::DisplayName(path);
	}
}
