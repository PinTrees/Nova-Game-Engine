#include "pch.h"
#include "TerrainBiomes.h"
#include "TerrainGenerator.h"
#include "TerrainData.h"
#include <filesystem>
#include <fstream>
#include <future>
#include "JobSystem.h"
#include <map>

namespace
{
	const char* kDir = "Resources\\Packages\\Terrain\\Biomes";
	std::vector<TerrainBiomes::Preset> s_Presets;
	bool s_Loaded = false;

	struct Thumb
	{
		Jobs::Future<std::vector<uint8_t>> Job;   // Background 잡
		ComPtr<GfxShaderResourceView> Srv;
		bool Started = false;
	};
	std::map<std::string, Thumb> s_Thumbs;
	constexpr int kThumbRes = 129;

	// 레이어 평균 색 근사 (썸네일용, 색 규칙이 없는 곳)
	void LayerAverage(const std::string& path, float out[3])
	{
		const std::string n = std::filesystem::path(path).stem().string();
		if (n == "Grass") { out[0] = 0.33f; out[1] = 0.42f; out[2] = 0.2f; }
		else if (n == "Rock") { out[0] = 0.46f; out[1] = 0.45f; out[2] = 0.43f; }
		else if (n == "Dirt") { out[0] = 0.42f; out[1] = 0.33f; out[2] = 0.24f; }
		else if (n == "Sand") { out[0] = 0.76f; out[1] = 0.68f; out[2] = 0.5f; }
		else { out[0] = out[1] = out[2] = 0.5f; }
	}

	// 위에서 본 음영 이미지 (색 = 컬러 맵 또는 레이어 평균 색, 음영 = 북서쪽 빛). res x res, RGBA
	std::vector<uint8_t> RenderThumb(const TerrainBiomes::Preset& p, int res = kThumbRes, TerrainGenerator::Output* keep = nullptr)
	{
		const int kThumbRes = res;
		TerrainGenerator::Input in;
		in.Resolution = kThumbRes;
		in.SizeX = in.SizeZ = 1000.0f;
		in.SizeY = 600.0f;
		in.ControlResolution = kThumbRes;
		in.LayerCount = (int)p.Layers.size();
		in.Settings = TerrainBiomes::ToSettings(p, TerrainGenSettings());
		in.Settings.PaintMaterials = true;
		TerrainGenerator::Output out = TerrainGenerator::Generate(in);
		std::vector<uint8_t> img((size_t)kThumbRes * kThumbRes * 4, 255);
		float avg[4][3];
		for (int l = 0; l < 4; ++l)
			LayerAverage(l < (int)p.Layers.size() ? p.Layers[l] : std::string(), avg[l]);
		const float cell = in.SizeX / (kThumbRes - 1);
		Vec3 light(-0.5f, 0.75f, 0.45f);   // 북서쪽 위
		light.Normalize();
		for (int z = 0; z < kThumbRes; ++z)
			for (int x = 0; x < kThumbRes; ++x)
			{
				auto h = [&](int xx, int zz) {
					xx = std::clamp(xx, 0, kThumbRes - 1); zz = std::clamp(zz, 0, kThumbRes - 1);
					return out.Heights[(size_t)zz * kThumbRes + xx] * in.SizeY;
				};
				Vec3 n(-(h(x + 1, z) - h(x - 1, z)) / (2 * cell), 1.0f, -(h(x, z + 1) - h(x, z - 1)) / (2 * cell));
				n.Normalize();
				const float shade = 0.28f + 0.95f * (std::max)(0.0f, n.Dot(light));
				const size_t i = ((size_t)z * kThumbRes + x) * 4;
				float c[3] = { 0, 0, 0 };
				for (int l = 0; l < 4; ++l)
				{
					const float w = out.HasControl ? out.Control[i + l] / 255.0f : (l == 0 ? 1.0f : 0.0f);
					for (int k = 0; k < 3; ++k) c[k] += w * avg[l][k];
				}
				if (!out.ColorMap.empty())
				{
					const float a = out.ColorMap[i + 3] / 255.0f;
					for (int k = 0; k < 3; ++k) c[k] = c[k] * (1 - a) + out.ColorMap[i + k] / 255.0f * a;
				}
				// 위가 북(+z)이 되도록 세로를 뒤집는다
				const size_t o = ((size_t)(kThumbRes - 1 - z) * kThumbRes + x) * 4;
				for (int k = 0; k < 3; ++k)
					img[o + k] = (uint8_t)std::clamp((int)(c[k] * shade * 255.0f), 0, 255);
				img[o + 3] = 255;
			}
		if (keep)
			*keep = std::move(out);
		return img;
	}

	void Load()
	{
		s_Presets.clear();
		s_Loaded = true;
		std::error_code ec;
		const std::wstring dir = PathManager::GetI()->GetMovePathW(string_to_wstring(kDir));
		std::vector<std::filesystem::path> files;
		for (const auto& e : std::filesystem::directory_iterator(dir, ec))
			if (e.is_regular_file() && e.path().extension() == L".biome")
				files.push_back(e.path());
		std::sort(files.begin(), files.end());
		for (const auto& f : files)
		{
			std::ifstream is(f);
			const nlohmann::json j = nlohmann::json::parse(is, nullptr, false);
			if (!j.is_object())
			{
				EditorLog::Write("TerrainGen", "biome preset parse failed: %s", f.string().c_str());
				continue;
			}
			TerrainBiomes::Preset p;
			p.Name = j.value("name", f.stem().string());
			p.Description = j.value("description", std::string());
			p.Path = std::string(kDir) + "\\" + f.filename().string();
			p.Data = j;
			if (j.contains("layers") && j["layers"].is_array())
				for (const auto& l : j["layers"])
					p.Layers.push_back(l.get<std::string>());
			s_Presets.push_back(std::move(p));
		}
		EditorLog::Write("TerrainGen", "biome presets: %zu", s_Presets.size());
	}
}

namespace TerrainBiomes
{
	const std::vector<Preset>& List()
	{
		if (!s_Loaded)
			Load();
		return s_Presets;
	}

	const Preset* Find(const std::string& name)
	{
		for (const Preset& p : List())
			if (p.Name == name)
				return &p;
		return nullptr;
	}

	void Reload()
	{
		Load();
		s_Thumbs.clear();
	}

	TerrainGenSettings ToSettings(const Preset& preset, const TerrainGenSettings& base)
	{
		TerrainGenSettings s;
		s.FromJson(preset.Data);
		s.Enabled = base.Enabled;
		s.AutoUpdate = base.AutoUpdate;
		s.PaintMaterials = preset.Data.value("paintMaterials", true);
		s.BiomePreset = preset.Name;
		return s;
	}

	void Apply(TerrainData& data, const Preset& preset)
	{
		data.Generator = ToSettings(preset, data.Generator);
		if (!preset.Layers.empty())
		{
			data.Layers.clear();
			for (const std::string& path : preset.Layers)
				if (auto layer = TerrainLayer::Load(path))
					data.Layers.push_back(layer);
		}
		data.Dirty = true;
		EditorLog::Write("TerrainGen", "applied biome preset '%s' to %s", preset.Name.c_str(), data.Name().c_str());
	}

	void DevDump()
	{
		// (개발/검증용) NOVA_DEV_BIOMEDUMP=<폴더>: 프리셋마다 513² 로 생성해 <폴더>/biome_<이름>.ppm (위에서 본 음영) + 높이 통계·튀는 점 수를 Editor.log 에
		static bool s_Done = false;
		char dir[MAX_PATH] = {};
		if (s_Done || ::GetEnvironmentVariableA("NOVA_DEV_BIOMEDUMP", dir, sizeof(dir)) == 0)
			return;
		s_Done = true;
		constexpr int res = 513;
		for (const Preset& p : List())
		{
			const auto t0 = std::chrono::steady_clock::now();
			TerrainGenerator::Output out;
			const std::vector<uint8_t> img = RenderThumb(p, res, &out);
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			// 튀는 점: 8 이웃 모두보다 2 m 넘게 높거나 낮은 칸
			int spikes = 0, pits = 0;
			float mn = 1e9f, mx = -1e9f;
			for (int z = 1; z < res - 1; ++z)
				for (int x = 1; x < res - 1; ++x)
				{
					const float h = out.Heights[(size_t)z * res + x] * 600.0f;
					mn = (std::min)(mn, h); mx = (std::max)(mx, h);
					float nmx = -1e9f, nmn = 1e9f;
					for (int dz = -1; dz <= 1; ++dz)
						for (int dx = -1; dx <= 1; ++dx)
							if (dx || dz)
							{
								const float v = out.Heights[(size_t)(z + dz) * res + x + dx] * 600.0f;
								nmx = (std::max)(nmx, v); nmn = (std::min)(nmn, v);
							}
					spikes += h > nmx + 2.0f;
					pits += h < nmn - 2.0f;
				}
			EditorLog::Write("TerrainGen", "biome dump '%s': %.0f ms, height %.1f ~ %.1f m, spikes %d, pits %d", p.Name.c_str(), ms, mn, mx, spikes, pits);
			std::string name = p.Name;
			name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
			std::ofstream os(std::filesystem::path(dir) / ("biome_" + name + ".ppm"), std::ios::binary);
			os << "P6\n" << res << " " << res << "\n255\n";
			for (size_t i = 0; i < img.size(); i += 4)
				os.write((const char*)&img[i], 3);
		}
	}

	GfxShaderResourceView* Thumbnail(const Preset& preset)
	{
		Thumb& t = s_Thumbs[preset.Path];
		if (t.Srv)
			return t.Srv.Get();
		if (!t.Started)
		{
			t.Started = true;
			Preset copy = preset;
			t.Job = Jobs::Async([copy]() { return RenderThumb(copy); }, Jobs::Priority::Background, "Biome Thumbnail");
			return nullptr;
		}
		if (t.Job.valid() && t.Job.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			const std::vector<uint8_t> img = t.Job.get();
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = td.Height = kThumbRes;
			td.MipLevels = td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_IMMUTABLE;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA init = { img.data(), kThumbRes * 4, 0 };
			ComPtr<GfxTexture2D> tex;
			auto device = Application::GetI()->GetDevice();
			if (SUCCEEDED(device->CreateTexture2D(&td, &init, tex.GetAddressOf())))
				device->CreateShaderResourceView(tex.Get(), nullptr, t.Srv.GetAddressOf());
		}
		return t.Srv.Get();
	}
}
