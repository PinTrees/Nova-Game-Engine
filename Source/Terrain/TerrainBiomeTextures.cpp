#include "pch.h"
#include "TerrainBiomeTextures.h"
#include "TextureLibrary.h"
#include "ImageIO.h"
#include "EditorLog.h"
#include "JobSystem.h"
#include "PathManager.h"
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <chrono>

namespace TerrainBiomeTextures
{
	namespace
	{
		// 바이옴 (가운데 기온 · 습도와 폭) × 종류 (풀 · 흙 · 바위 · 눈 · 모래) → 텍스처 팩 재질. 다크 판타지 대륙 (붉은사막의 파이웰처럼)
		//  온대 숲 · 북방림 · 툰드라 · 고산 · 사막 · 사바나 · 열대 · 지중해 · 습지
		const Biome kBiomeTable[kBiomes] = {
			{ "Temperate Forest", 9.0f, 0.6f, 6.0f, 0.17f, { "ground/wild-grass", "ground/forest-floor", "rock/cliff-grey", "ground/snow", "ground/river-pebbles" } },
			{ "Boreal", -1.0f, 0.55f, 6.0f, 0.17f, { "ground/mossy-forest", "ground/pine-needles", "rock/cliff-dark", "ground/snow", "ground/gravel" } },
			{ "Tundra / Alpine", -9.0f, 0.45f, 6.0f, 0.2f, { "ground/sparse-grass", "ground/rocky-dirt", "rock/lichen-rock", "ground/snow", "ground/gravel" } },
			{ "Hot Desert", 27.0f, 0.08f, 6.0f, 0.12f, { "ground/dry-ground", "ground/desert-sand-gravel", "rock/red-desert", "ground/snow", "ground/sand" } },
			{ "Savanna / Steppe", 22.0f, 0.3f, 6.0f, 0.14f, { "ground/dry-grass", "ground/red-laterite", "rock/sandstone-cliff", "ground/snow", "ground/desert-sand-gravel" } },
			{ "Tropical Rainforest", 25.0f, 0.85f, 6.0f, 0.15f, { "ground/forest-leaves-green", "ground/mud", "rock/cliff-mossy", "ground/snow", "ground/wet-sand" } },
			{ "Mediterranean", 16.0f, 0.32f, 6.0f, 0.14f, { "ground/grass-dirt", "ground/soil", "rock/cliff-light", "ground/snow", "ground/sand" } },
			{ "Wetland", 11.0f, 0.95f, 7.0f, 0.12f, { "ground/mud-clover", "ground/mud", "rock/mossy-rock", "ground/snow", "ground/wet-sand" } },
		};

		// 재질마다 높이 변위 (m) · 타일 (m — 0 = 재질의 실제 크기)
		float AmplitudeOf(const std::string& id)
		{
			auto has = [&](const char* s) { return id.find(s) != std::string::npos; };
			if (has("rock/")) return 0.35f;
			if (has("rocky") || has("gravel") || has("pebbles")) return 0.1f;
			if (has("snow")) return 0.1f;
			if (has("mud") || has("forest") || has("needles") || has("leaves") || has("laterite")) return 0.09f;
			if (has("dry-ground") || has("cracked")) return 0.06f;
			if (has("sand")) return 0.05f;
			return 0.06f;
		}
		float TileOf(const TextureLibrary::Material& m)
		{
			if (m.Category == "rock")
				return (std::max)(5.0f, m.TileMeters * 2.0f);   // 절벽 면은 크게
			return (std::max)(2.5f, m.TileMeters * 1.25f);
		}

		struct Cpu
		{
			std::vector<std::string> Ids;
			std::vector<DirectX::ScratchImage> Albedo, Heights;   // 슬라이스마다 밉 사슬
			std::vector<std::array<float, 4>> Params;
		};
		struct Entry
		{
			std::shared_ptr<Cpu> Pending;    // 작업 스레드가 채운다
			std::atomic<bool> Ready{ false };
			bool Failed = false;
			Set Out;
			ComPtr<GfxShaderResourceView> AlbedoSRV, HeightsSRV;
			bool Gpu = false;
			std::string Error;
		};
		std::mutex s_Lock;
		std::map<std::string, std::shared_ptr<Entry>> s_Sets;
		std::string s_Status = "biome textures: not used";

		bool LoadRgba(const std::wstring& path, int size, DirectX::ScratchImage& out)
		{
			DirectX::ScratchImage img;
			if (path.empty() || FAILED(ImageIO::Load(path.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, img)))
				return false;
			const DirectX::Image* src = img.GetImage(0, 0, 0);
			DirectX::ScratchImage rgba;
			if (src->format != DXGI_FORMAT_R8G8B8A8_UNORM)
			{
				if (FAILED(DirectX::Convert(*src, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, rgba)))
					return false;
				src = rgba.GetImage(0, 0, 0);
			}
			if ((int)src->width != size || (int)src->height != size)
				return SUCCEEDED(DirectX::Resize(*src, size, size, DirectX::TEX_FILTER_LINEAR, out));
			return SUCCEEDED(out.InitializeFromImage(*src));
		}

		// 프로젝트 Library 캐시 (BC3 색 · RGBA8 높이 DDS — 다음부터 사진을 읽지도 압축하지도 않는다)
		std::wstring CachePath(const std::string& id, int size, const wchar_t* suffix)
		{
			std::string name = id;
			for (char& ch : name)
				if (ch == '/' || ch == '\\' || ch == ':')
					ch = '_';
			const std::wstring dir = PathManager::GetI()->GetMovePathW(L"Library\\NovaTerrain\\Biome\\");
			std::error_code ec;
			std::filesystem::create_directories(dir, ec);
			return dir + std::wstring(name.begin(), name.end()) + L"_" + std::to_wstring(size) + L"_v1" + suffix;
		}

		// 작업 스레드: 사진 → 색 (rgb + 거칠기) · 높이 (높이 + 노멀 xy) 한 슬라이스씩 (밉까지)
		bool BuildCpu(Cpu& c, int albedoSize, std::string& error)
		{
			const int hsize = (std::max)(64, albedoSize / 2);
			c.Albedo.resize(c.Ids.size());
			c.Heights.resize(c.Ids.size());
			c.Params.resize(c.Ids.size());
			std::atomic<bool> ok{ true };
			std::mutex errLock;
			Jobs::ParallelFor((int)c.Ids.size(), 1, [&](int b, int e) {
				for (int i = b; i < e; ++i)
				{
					const TextureLibrary::Material* m = TextureLibrary::Find(c.Ids[(size_t)i]);
					const std::wstring ca = CachePath(c.Ids[(size_t)i], albedoSize, L"_albedo.dds"), ch = CachePath(c.Ids[(size_t)i], albedoSize, L"_height.dds");
					if (m && SUCCEEDED(DirectX::LoadFromDDSFile(ca.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, c.Albedo[(size_t)i])) &&
						SUCCEEDED(DirectX::LoadFromDDSFile(ch.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, c.Heights[(size_t)i])))
					{
						const bool hasHeight = m->Maps.count("height") > 0;
						c.Params[(size_t)i] = { 1.0f / TileOf(*m), hasHeight ? AmplitudeOf(c.Ids[(size_t)i]) : 0.0f, 0.5f, 1.0f };
						continue;
					}
					auto map = [&](const char* k) { if (!m) return std::wstring(); auto it = m->Maps.find(k); return it == m->Maps.end() ? std::wstring() : it->second; };
					DirectX::ScratchImage color, rough, height, normal;
					if (!m || !LoadRgba(map("color"), albedoSize, color) || !LoadRgba(map("normal"), hsize, normal))
					{
						std::lock_guard<std::mutex> lock(errLock);
						error = "missing " + c.Ids[(size_t)i];
						ok = false;
						continue;
					}
					const bool hasRough = LoadRgba(map("roughness"), albedoSize, rough);
					const bool hasHeight = LoadRgba(map("height"), hsize, height);
					// 색 + 거칠기 (a)
					DirectX::Image* ci = const_cast<DirectX::Image*>(color.GetImage(0, 0, 0));
					const DirectX::Image* ri = hasRough ? rough.GetImage(0, 0, 0) : nullptr;
					for (size_t y = 0; y < ci->height; ++y)
						for (size_t x = 0; x < ci->width; ++x)
							ci->pixels[y * ci->rowPitch + x * 4 + 3] = ri ? ri->pixels[y * ri->rowPitch + x * 4] : 200;
					// 높이 (r) + 노멀 xy (g · b)
					DirectX::Image* ni = const_cast<DirectX::Image*>(normal.GetImage(0, 0, 0));
					const DirectX::Image* hi = hasHeight ? height.GetImage(0, 0, 0) : nullptr;
					for (size_t y = 0; y < ni->height; ++y)
						for (size_t x = 0; x < ni->width; ++x)
						{
							uint8_t* p = ni->pixels + y * ni->rowPitch + x * 4;
							const uint8_t nx = p[0], ny = p[1];
							p[0] = hi ? hi->pixels[y * hi->rowPitch + x * 4] : 128;
							p[1] = nx;
							p[2] = ny;
							p[3] = 255;
						}
					// 색은 BC3 (rgb + 거칠기 a — VRAM 1/4), 높이 · 노멀은 RGBA8 (절반 크기). 캐시에 쓴다 (임시 파일 → 이름 바꾸기)
					DirectX::ScratchImage mips;
					DirectX::GenerateMipMaps(*ci, DirectX::TEX_FILTER_BOX, 0, mips);
					if (FAILED(DirectX::Compress(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), DXGI_FORMAT_BC3_UNORM,
						DirectX::TEX_COMPRESS_PARALLEL, DirectX::TEX_THRESHOLD_DEFAULT, c.Albedo[(size_t)i])))
						c.Albedo[(size_t)i] = std::move(mips);
					DirectX::GenerateMipMaps(*ni, DirectX::TEX_FILTER_BOX, 0, c.Heights[(size_t)i]);
					std::error_code ec;
					if (SUCCEEDED(DirectX::SaveToDDSFile(c.Albedo[(size_t)i].GetImages(), c.Albedo[(size_t)i].GetImageCount(), c.Albedo[(size_t)i].GetMetadata(), DirectX::DDS_FLAGS_NONE, (ca + L".tmp").c_str())))
						std::filesystem::rename(ca + L".tmp", ca, ec);
					if (SUCCEEDED(DirectX::SaveToDDSFile(c.Heights[(size_t)i].GetImages(), c.Heights[(size_t)i].GetImageCount(), c.Heights[(size_t)i].GetMetadata(), DirectX::DDS_FLAGS_NONE, (ch + L".tmp").c_str())))
						std::filesystem::rename(ch + L".tmp", ch, ec);
					const std::string& id = c.Ids[(size_t)i];
					c.Params[(size_t)i] = { 1.0f / TileOf(*m), hasHeight ? AmplitudeOf(id) : 0.0f, 0.5f, 1.0f };
				}
			}, "Biome Textures", Jobs::Priority::Background);
			return ok;
		}

		ComPtr<GfxShaderResourceView> MakeArray(const std::vector<DirectX::ScratchImage>& slices, size_t& bytes)
		{
			if (slices.empty())
				return nullptr;
			const DirectX::TexMetadata& md = slices[0].GetMetadata();
			std::vector<D3D11_SUBRESOURCE_DATA> init;
			for (const auto& s : slices)
				for (size_t mip = 0; mip < md.mipLevels; ++mip)
				{
					const DirectX::Image* im = s.GetImage(mip, 0, 0);
					init.push_back({ im->pixels, (UINT)im->rowPitch, (UINT)im->slicePitch });
					bytes += im->slicePitch;
				}
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = (UINT)md.width;
			td.Height = (UINT)md.height;
			td.MipLevels = (UINT)md.mipLevels;
			td.ArraySize = (UINT)slices.size();
			td.Format = md.format;
			for (const auto& s : slices)   // 모든 슬라이스가 같은 꼴이어야 한다 (압축이 실패한 것이 섞이면 배열을 만들지 않는다)
				if (s.GetMetadata().format != md.format || s.GetMetadata().width != md.width || s.GetMetadata().mipLevels != md.mipLevels)
					return nullptr;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_IMMUTABLE;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			ComPtr<GfxTexture2D> tex;
			ComPtr<GfxShaderResourceView> srv;
			GfxDevice* device = Application::GetI()->GetDevice();
			if (SUCCEEDED(device->CreateTexture2D(&td, init.data(), tex.GetAddressOf())))
				device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf());
			return srv;
		}
	}

	const Biome* Biomes() { return kBiomeTable; }

	const Set* Get(const std::array<int, 4>& kinds, int albedoSize)
	{
		if (TextureLibrary::All().empty())
			return nullptr;
		albedoSize = albedoSize <= 512 ? 512 : (albedoSize >= 2048 ? 2048 : 1024);
		std::string key = std::to_string(albedoSize);
		for (int k : kinds)
			key += ":" + std::to_string(k);
		std::shared_ptr<Entry> e;
		{
			std::lock_guard<std::mutex> lock(s_Lock);
			auto it = s_Sets.find(key);
			if (it == s_Sets.end())
			{
				e = std::make_shared<Entry>();
				s_Sets[key] = e;
				// 슬라이스 표: 바이옴 × 역할 → 재질 (같은 재질은 한 슬라이스)
				auto cpu = std::make_shared<Cpu>();
				std::map<std::string, int> index;
				for (int b = 0; b < kBiomes; ++b)
					for (int r = 0; r < 4; ++r)
					{
						const int kind = kinds[(size_t)r];
						const char* id = kind >= 0 && kind < kKinds ? kBiomeTable[b].Material[kind] : nullptr;
						if (!id || !TextureLibrary::Find(id))
						{
							e->Out.Slices[b][r] = -1.0f;
							continue;
						}
						auto f = index.find(id);
						if (f == index.end())
						{
							if ((int)cpu->Ids.size() >= kMaxSlices)
								continue;
							f = index.emplace(id, (int)cpu->Ids.size()).first;
							cpu->Ids.push_back(id);
						}
						e->Out.Slices[b][r] = (float)f->second;
					}
				if (cpu->Ids.empty())
				{
					e->Failed = true;
					e->Error = "no texture pack materials";
					return nullptr;
				}
				e->Pending = cpu;
				const auto t0 = std::chrono::steady_clock::now();
				Jobs::Run([e, cpu, albedoSize, t0]() {
					std::string err;
					if (!BuildCpu(*cpu, albedoSize, err))
						e->Error = err;
					EditorLog::Write("Terrain", "biome textures: %zu materials read (%d², %.1f s)%s", cpu->Ids.size(), albedoSize,
						std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), err.empty() ? "" : (" — " + err).c_str());
					e->Ready = true;
				}, nullptr, Jobs::Priority::Background, "Biome Textures");
				s_Status = "biome textures: reading " + std::to_string(cpu->Ids.size()) + " materials";
				return nullptr;
			}
			e = it->second;
		}
		if (e->Failed)
			return nullptr;
		if (!e->Gpu)
		{
			if (!e->Ready)
				return nullptr;
			std::shared_ptr<Cpu> cpu = e->Pending;
			if (!e->Error.empty() || !cpu)
			{
				e->Failed = true;
				s_Status = "biome textures: " + e->Error;
				return nullptr;
			}
			size_t bytes = 0;
			e->AlbedoSRV = MakeArray(cpu->Albedo, bytes);
			e->HeightsSRV = MakeArray(cpu->Heights, bytes);
			e->Gpu = true;
			if (!e->AlbedoSRV || !e->HeightsSRV)
			{
				e->Failed = true;
				s_Status = "biome textures: GPU array failed";
				return nullptr;
			}
			e->Out.Albedo = e->AlbedoSRV.Get();
			e->Out.Heights = e->HeightsSRV.Get();
			e->Out.SliceCount = (int)cpu->Ids.size();
			e->Out.GpuBytes = bytes;
			for (size_t i = 0; i < cpu->Params.size(); ++i)
			{
				for (int k = 0; k < 4; ++k)
					e->Out.Params[i][k] = cpu->Params[i][(size_t)k];
				e->Out.MaxAmplitude = (std::max)(e->Out.MaxAmplitude, cpu->Params[i][1]);
			}
			// 바이옴에 재질이 없는 역할 (-1) 은 바이옴 0 의 것을 쓴다
			for (int b = 0; b < kBiomes; ++b)
				for (int r = 0; r < 4; ++r)
					if (e->Out.Slices[b][r] < 0.0f)
						e->Out.Slices[b][r] = (std::max)(0.0f, e->Out.Slices[0][r]);
			e->Pending.reset();   // CPU 사본은 버린다
			char buf[160];
			snprintf(buf, sizeof(buf), "biome textures: %d materials, %d biomes, %.0f MB GPU", e->Out.SliceCount, kBiomes, bytes / 1048576.0);
			s_Status = buf;
			EditorLog::Write("Terrain", "%s", buf);
		}
		return &e->Out;
	}

	void Clear()
	{
		std::lock_guard<std::mutex> lock(s_Lock);
		s_Sets.clear();
		s_Status = "biome textures: cleared";
	}

	std::string Status()
	{
		std::lock_guard<std::mutex> lock(s_Lock);
		return s_Status;
	}
}
