#include "pch.h"
#include "VirtualTexturing.h"
#include "AssetImportSettings.h"
#include "CliServer.h"
#include "JobSystem.h"
#include "LockFree.h"
#include "MeshRenderer.h"
#include "SceneCulling.h"
#include "RenderLayers.h"
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_set>

// Streaming Virtual Texturing — VirtualTexturing.h 의 설명
namespace VirtualTexturing
{
	namespace
	{
		constexpr int kPage = 128;            // 페이지 (테두리 뺀) 텍셀
		constexpr int kBorder = 4;            // 이웃 텍셀 (쌍선형 · 이방성 조금)
		constexpr int kTile = kPage + 2 * kBorder;
		constexpr UINT64 kTileBytes = (UINT64)kTile * kTile * 4;
		constexpr uint32_t kMagic = 0x3254564E;   // "NVT2" (sRGB 칸 추가)
		constexpr int kFeedbackScale = 8;     // 피드백 = 화면의 1/8
		constexpr int kReadbackLatency = 2;   // 이만큼 앞 프레임의 피드백을 읽는다 (GPU 를 기다리지 않게)
		constexpr int kUploadsPerFrame = 24;

		struct FileHeader
		{
			uint32_t Magic = kMagic;
			uint32_t Width = 0, Height = 0, Mips = 0;
			uint32_t Page = kPage, Border = kBorder;
			uint32_t Srgb = 0;   // 원본이 sRGB 색 (보통 텍스처처럼 하드웨어가 감마를 푼다 — 캐시의 sRGB 뷰로 읽는다)
		};

		struct VTex
		{
			int Id = 0;
			std::wstring Source, TileFile;
			UINT Width = 0, Height = 0, Mips = 0;
			bool Srgb = false;
			std::vector<UINT> PagesX, PagesY;
			std::vector<size_t> MipBase;   // 펼친 페이지 번호의 밉 시작
			std::vector<int> Slot;         // 페이지마다 캐시 칸 (-1 = 없음)
			std::vector<uint8_t> Loading;  // 읽는 중 (중복 요청을 막는다)
			std::vector<uint64_t> LastWanted;
			ComPtr<GfxTexture2D> TableTex;
			ComPtr<GfxShaderResourceView> TableSrv;
			std::vector<std::vector<uint32_t>> Table;   // 밉마다 RGBA8 (x = 칸 x, y = 칸 y, z = 실제 밉, w = 255)
			bool TableDirty = true;
			ComPtr<GfxShaderResourceView> Fallback;
			uint64_t Uploads = 0;
			size_t Pages() const { return MipBase.empty() ? 0 : MipBase.back() + (size_t)PagesX.back() * PagesY.back(); }
			size_t Index(UINT m, UINT x, UINT y) const { return MipBase[m] + (size_t)y * PagesX[m] + x; }
		};

		struct CacheSlot
		{
			int Tex = 0;         // 0 = 빈 칸
			size_t Page = 0;
			uint64_t LastUsed = 0;
			bool Locked = false; // 가장 거친 밉 (대체) — 비우지 않는다
		};

		struct Job { int Tex = 0; size_t Page = 0; UINT64 Offset = 0; std::wstring File; };
		struct Done { int Tex = 0; size_t Page = 0; std::vector<uint8_t> Pixels; };

		struct FeedbackView
		{
			UINT W = 0, H = 0;
			ComPtr<GfxTexture2D> Tex;
			ComPtr<GfxRenderTargetView> Rtv;
			ComPtr<GfxTexture2D> Staging[kReadbackLatency + 1];
			uint64_t StagingFrame[kReadbackLatency + 1] = {};
			int Next = 0;
		};

		struct State
		{
			std::vector<std::unique_ptr<VTex>> Textures;   // 번호 = 자리 + 1
			std::map<std::wstring, int> ByPath;
			int CacheTiles = 16;   // 캐시 = 칸 N x N
			ComPtr<GfxTexture2D> CacheTex;
			ComPtr<GfxShaderResourceView> CacheSrv, CacheSrvSrgb;   // 같은 캐시를 선형 · sRGB 로 (가상 텍스처마다 원본 색 공간)
			std::vector<CacheSlot> Slots;
			bool Enabled = true;
			bool Frozen = false;   // 피드백을 읽지 않는다 (검사: 지금 올라 있는 것만)
			uint64_t Frame = 1;
			// 피드백
			ComPtr<FxEffect> Fx;
			ComPtr<GfxInputLayout> Layout;
			bool FxFailed = false;
			FeedbackView Views[2];   // 0 Game · 1 Scene
			// 페이지 읽기 (Background 잡 — DrainRequests): 무잠금 SPSC 링 둘
			LockFree::SpscRing<Job*> Requests{ 4096 };   // 메인 → 읽는 잡 (가득하면 요청을 버린다 — 피드백이 다시 요청한다)
			LockFree::SpscRing<Done*> Finished{ 1024 };  // 읽는 잡 → 메인 (Update 가 프레임마다 올린다)
			std::atomic<bool> Draining{ false };         // 읽는 잡이 돌고 있다 (하나만)
			std::map<std::wstring, std::unique_ptr<std::ifstream>> Files;   // 읽는 잡만 (한 번에 하나)
			// 통계
			uint64_t Uploads = 0, Evictions = 0, LastRequests = 0, LastNewRequests = 0, FeedbackPixels = 0, LastObjects = 0;
		};
		State* s_S = nullptr;

		State& S()
		{
			if (!s_S) s_S = new State();
			return *s_S;
		}

		UINT NextPow2(UINT v) { UINT p = 1; while (p < v) p <<= 1; return p; }

		// sRGB 형식 → 같은 바이트의 선형 형식 (가져오기 설정 sRGB 끔)
		DXGI_FORMAT LinearOf(DXGI_FORMAT f)
		{
			switch (f)
			{
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
			case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;
			case DXGI_FORMAT_BC1_UNORM_SRGB: return DXGI_FORMAT_BC1_UNORM;
			case DXGI_FORMAT_BC2_UNORM_SRGB: return DXGI_FORMAT_BC2_UNORM;
			case DXGI_FORMAT_BC3_UNORM_SRGB: return DXGI_FORMAT_BC3_UNORM;
			case DXGI_FORMAT_BC7_UNORM_SRGB: return DXGI_FORMAT_BC7_UNORM;
			default: return f;
			}
		}

		// 페이지 읽기 = Background 잡 하나 (동시성 로드맵 2 단계 — 예전: 전용 스레드 + 잠금 + 조건 변수).
		//  요청 (메인 → 잡) · 다 읽은 페이지 (잡 → 메인) 는 무잠금 SPSC 링. 읽는 잡은 한 번에 하나만 (Draining) — 그래서 SPSC 가 맞다
		void DrainRequests()
		{
			State& st = S();
			for (;;)
			{
				// 메인이 아직 안 올린 결과가 가득하면 멈춘다 (Update 가 다시 시작한다)
				if (st.Finished.SizeApprox() + 1 >= st.Finished.Capacity())
					break;
				Job* job = nullptr;
				if (!st.Requests.Pop(job))
					break;
				auto& f = st.Files[job->File];
				if (!f) f = std::make_unique<std::ifstream>(job->File, std::ios::binary);
				Done* d = new Done();
				d->Tex = job->Tex;
				d->Page = job->Page;
				d->Pixels.resize((size_t)kTileBytes);
				f->clear();
				f->seekg((std::streamoff)job->Offset);
				f->read(reinterpret_cast<char*>(d->Pixels.data()), (std::streamsize)kTileBytes);
				if (!*f) d->Pixels.clear();
				delete job;
				st.Finished.Push(d);   // 위에서 자리를 확인했다
			}
			st.Draining.store(false, std::memory_order_release);
		}

		// 요청이 있고 읽는 잡이 없으면 하나 띄운다
		void KickReader()
		{
			State& st = S();
			if (st.Requests.SizeApprox() == 0 || st.Finished.SizeApprox() + 1 >= st.Finished.Capacity())
				return;
			if (st.Draining.exchange(true, std::memory_order_acq_rel))
				return;
			Jobs::Run([]() { DrainRequests(); }, nullptr, Jobs::Priority::Background, "VT Page Reads");
		}

		bool EnsureCache()
		{
			State& st = S();
			if (st.CacheTex) return true;
			D3D11_TEXTURE2D_DESC d = {};
			d.Width = d.Height = (UINT)(st.CacheTiles * kTile);
			d.MipLevels = 1;
			d.ArraySize = 1;
			d.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
			d.SampleDesc.Count = 1;
			d.Usage = D3D11_USAGE_DEFAULT;
			d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
			sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			sv.Texture2D.MipLevels = 1;
			sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			D3D11_SHADER_RESOURCE_VIEW_DESC svs = sv;
			svs.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			if (FAILED(Gfx::Device()->CreateTexture2D(&d, nullptr, st.CacheTex.GetAddressOf())) ||
				FAILED(Gfx::Device()->CreateShaderResourceView(st.CacheTex.Get(), &sv, st.CacheSrv.GetAddressOf())) ||
				FAILED(Gfx::Device()->CreateShaderResourceView(st.CacheTex.Get(), &svs, st.CacheSrvSrgb.GetAddressOf())))
			{
				EditorLog::Write("VT", "physical cache %u x %u could not be created", d.Width, d.Height);
				st.CacheTex.Reset();
				return false;
			}
			st.Slots.assign((size_t)st.CacheTiles * st.CacheTiles, CacheSlot());
			EditorLog::Write("VT", "physical cache %u x %u (%d x %d tiles of %d, %.1f MB)", d.Width, d.Height, st.CacheTiles, st.CacheTiles, kTile, d.Width * (double)d.Height * 4 / 1048576.0);
			return true;
		}

		// 원본 → 타일 파일 (밉마다 페이지, 테두리 = 감싸기). fallback = 가장 거친 밉 (보통 텍스처로)
		bool BuildTileFile(VTex& t, DirectX::ScratchImage& coarsest, std::string& error)
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			const fs::path src(t.Source);
			const uint64_t stamp = (uint64_t)fs::last_write_time(src, ec).time_since_epoch().count() ^ ((uint64_t)fs::file_size(src, ec) << 1);
			const fs::path dir = L"VirtualTextureCache";
			fs::create_directories(dir, ec);
			char hex[32];
			snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)(std::hash<std::wstring>{}(src.lexically_normal().wstring()) ^ stamp));
			t.TileFile = (dir / (src.stem().wstring() + L"_" + string_to_wstring(hex) + L".nvt")).wstring();

			// 이미 있으면 머리만 읽고 가장 거친 밉을 파일에서 다시 짓는다
			{
				std::ifstream in(t.TileFile, std::ios::binary);
				FileHeader h;
				if (in && in.read(reinterpret_cast<char*>(&h), sizeof(h)) && h.Magic == kMagic && h.Page == kPage && h.Border == kBorder)
				{
					t.Width = h.Width;
					t.Height = h.Height;
					t.Mips = h.Mips;
					t.Srgb = h.Srgb != 0;
				}
			}
			const bool cached = t.Mips > 0;
			DirectX::ScratchImage mips;
			if (!cached)
			{
				const auto t0 = std::chrono::steady_clock::now();
				DirectX::ScratchImage img;
				DirectX::TexMetadata md;
				const std::wstring ext = src.extension().wstring();
				HRESULT hr;
				if (_wcsicmp(ext.c_str(), L".dds") == 0) hr = DirectX::LoadFromDDSFile(src.c_str(), DirectX::DDS_FLAGS_NONE, &md, img);
				else if (_wcsicmp(ext.c_str(), L".tga") == 0) hr = DirectX::LoadFromTGAFile(src.c_str(), &md, img);
				else hr = DirectX::LoadFromWICFile(src.c_str(), DirectX::WIC_FLAGS_NONE, &md, img);
				if (FAILED(hr)) { error = "cannot read the source image"; return false; }
				// 색 공간: 보통 텍스처 (Utils::ApplyTextureImport) 와 같이 — 원본이 sRGB 이고 가져오기 설정 sRGB 이면 하드웨어가 감마를 푼다.
				//  sRGB 면 sRGB 형식 그대로 줄이기 · 밉 (감마를 고려한 거르기 — 보통 텍스처와 같은 값), 끄면 같은 바이트를 선형으로
				t.Srgb = DirectX::IsSRGB(md.format) && AssetImport::LoadTexture(t.Source).SRGB;
				if (!t.Srgb && DirectX::IsSRGB(md.format))
				{
					img.OverrideFormat(LinearOf(md.format));
					md = img.GetMetadata();
				}
				const DXGI_FORMAT target = t.Srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
				if (DirectX::IsCompressed(md.format))
				{
					DirectX::ScratchImage dec;
					if (FAILED(DirectX::Decompress(*img.GetImage(0, 0, 0), target, dec))) { error = "decompress failed"; return false; }
					img = std::move(dec);
				}
				else if (md.format != target)
				{
					DirectX::ScratchImage conv;
					if (FAILED(DirectX::Convert(*img.GetImage(0, 0, 0), target, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, conv))) { error = "convert failed"; return false; }
					img = std::move(conv);
				}
				// 2 의 거듭제곱 · 128 이상 (페이지가 밉마다 반씩)
				UINT w = (std::max)((UINT)kPage, NextPow2((UINT)img.GetMetadata().width)), h = (std::max)((UINT)kPage, NextPow2((UINT)img.GetMetadata().height));
				if (w != img.GetMetadata().width || h != img.GetMetadata().height)
				{
					DirectX::ScratchImage rs;
					if (FAILED(DirectX::Resize(*img.GetImage(0, 0, 0), w, h, DirectX::TEX_FILTER_CUBIC, rs))) { error = "resize failed"; return false; }
					img = std::move(rs);
				}
				UINT count = 1;
				while (((w >> count) >= (UINT)kPage) && ((h >> count) >= (UINT)kPage)) ++count;
				if (count > 1)
				{
					if (FAILED(DirectX::GenerateMipMaps(*img.GetImage(0, 0, 0), DirectX::TEX_FILTER_BOX, count, mips))) { error = "mip generation failed"; return false; }
				}
				else
					mips = std::move(img);
				t.Width = w;
				t.Height = h;
				t.Mips = count;
				// 쓰기: 머리 + 밉 0 부터 페이지 행 순서
				std::ofstream out(t.TileFile, std::ios::binary | std::ios::trunc);
				FileHeader hd;
				hd.Width = w; hd.Height = h; hd.Mips = count; hd.Srgb = t.Srgb ? 1 : 0;
				out.write(reinterpret_cast<const char*>(&hd), sizeof(hd));
				std::vector<uint8_t> tile((size_t)kTileBytes);
				for (UINT m = 0; m < count; ++m)
				{
					const DirectX::Image* mi = mips.GetImage(m, 0, 0);
					const int mw = (int)mi->width, mh = (int)mi->height;
					for (int py = 0; py < mh / kPage; ++py)
						for (int px = 0; px < mw / kPage; ++px)
						{
							for (int ty = 0; ty < kTile; ++ty)
							{
								const int sy = ((py * kPage + ty - kBorder) % mh + mh) % mh;
								const uint8_t* row = mi->pixels + (size_t)sy * mi->rowPitch;
								uint8_t* dst = tile.data() + (size_t)ty * kTile * 4;
								for (int tx = 0; tx < kTile; ++tx)
								{
									const int sx = ((px * kPage + tx - kBorder) % mw + mw) % mw;
									memcpy(dst + tx * 4, row + sx * 4, 4);
								}
							}
							out.write(reinterpret_cast<const char*>(tile.data()), (std::streamsize)tile.size());
						}
				}
				if (!out) { error = "tile file write failed"; return false; }
				const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
				EditorLog::Write("VT", "tile file %s: %u x %u, %u mips (%.0f ms)", wstring_to_string(fs::path(t.TileFile).filename().wstring()).c_str(), w, h, count, ms);
			}
			t.PagesX.clear(); t.PagesY.clear(); t.MipBase.clear();
			size_t base = 0;
			for (UINT m = 0; m < t.Mips; ++m)
			{
				t.MipBase.push_back(base);
				t.PagesX.push_back((t.Width >> m) / kPage);
				t.PagesY.push_back((t.Height >> m) / kPage);
				base += (size_t)t.PagesX.back() * t.PagesY.back();
			}
			// 대체 텍스처: 가장 거친 밉 (파일에 있으면 그 밉의 페이지를 이어 붙인다)
			const UINT lm = t.Mips - 1;
			const UINT cw = t.PagesX[lm] * kPage, ch = t.PagesY[lm] * kPage;
			if (FAILED(coarsest.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, cw, ch, 1, 1))) { error = "fallback image"; return false; }
			std::ifstream in(t.TileFile, std::ios::binary);
			std::vector<uint8_t> tile((size_t)kTileBytes);
			for (UINT py = 0; py < t.PagesY[lm]; ++py)
				for (UINT px = 0; px < t.PagesX[lm]; ++px)
				{
					in.seekg((std::streamoff)(sizeof(FileHeader) + t.Index(lm, px, py) * kTileBytes));
					in.read(reinterpret_cast<char*>(tile.data()), (std::streamsize)tile.size());
					const DirectX::Image* ci = coarsest.GetImage(0, 0, 0);
					for (int ty = 0; ty < kPage; ++ty)
						memcpy(ci->pixels + (size_t)(py * kPage + ty) * ci->rowPitch + (size_t)px * kPage * 4, tile.data() + ((size_t)(ty + kBorder) * kTile + kBorder) * 4, (size_t)kPage * 4);
				}
			return true;
		}

		void RebuildTable(VTex& t)
		{
			// 가장 거친 밉부터: 올라 있으면 자기 칸, 아니면 부모 (반 크기 밉) 의 항목
			if (t.Table.size() != t.Mips)
			{
				t.Table.resize(t.Mips);
				for (UINT m = 0; m < t.Mips; ++m) t.Table[m].assign((size_t)t.PagesX[m] * t.PagesY[m], 0);
			}
			const int tiles = S().CacheTiles;
			for (int m = (int)t.Mips - 1; m >= 0; --m)
				for (UINT y = 0; y < t.PagesY[m]; ++y)
					for (UINT x = 0; x < t.PagesX[m]; ++x)
					{
						const int slot = t.Slot[t.Index(m, x, y)];
						uint32_t e;
						if (slot >= 0)
							e = (uint32_t)(slot % tiles) | ((uint32_t)(slot / tiles) << 8) | ((uint32_t)m << 16) | 0xFF000000u;
						else if (m + 1 < (int)t.Mips)
							e = t.Table[m + 1][(size_t)(std::min)(y / 2, t.PagesY[m + 1] - 1) * t.PagesX[m + 1] + (std::min)(x / 2, t.PagesX[m + 1] - 1)];
						else
							e = 0;   // 올라 있는 것이 없다 (대체가 올라가기 전)
						t.Table[m][(size_t)y * t.PagesX[m] + x] = e;
					}
			GfxContext* ctx = Gfx::Context();
			for (UINT m = 0; m < t.Mips; ++m)
				ctx->UpdateSubresource(t.TableTex.Get(), m, nullptr, t.Table[m].data(), t.PagesX[m] * 4, 0);
			t.TableDirty = false;
		}

		int FindSlot()
		{
			State& st = S();
			// 빈 칸, 없으면 가장 오래 쓰이지 않은 칸 (이번 · 지난 프레임에 쓴 것은 비우지 않는다)
			int best = -1;
			uint64_t bestUsed = UINT64_MAX;
			for (int i = 0; i < (int)st.Slots.size(); ++i)
			{
				const CacheSlot& c = st.Slots[i];
				if (c.Tex == 0) return i;
				if (c.Locked || c.LastUsed + 1 >= st.Frame) continue;
				if (c.LastUsed < bestUsed) { bestUsed = c.LastUsed; best = i; }
			}
			if (best >= 0)
			{
				CacheSlot& c = st.Slots[best];
				VTex& old = *st.Textures[c.Tex - 1];
				old.Slot[c.Page] = -1;
				old.TableDirty = true;
				c = CacheSlot();
				++st.Evictions;
			}
			return best;
		}

		void Upload(int slot, const uint8_t* pixels)
		{
			State& st = S();
			const UINT x = (UINT)(slot % st.CacheTiles) * kTile, y = (UINT)(slot / st.CacheTiles) * kTile;
			const D3D11_BOX box = { x, y, 0, x + kTile, y + kTile, 1 };
			Gfx::Context()->UpdateSubresource(st.CacheTex.Get(), 0, &box, pixels, kTile * 4, 0);
			++st.Uploads;
		}

		void Request(VTex& t, size_t page)
		{
			State& st = S();
			t.LastWanted[page] = st.Frame;
			const int slot = t.Slot[page];
			if (slot >= 0)
			{
				st.Slots[slot].LastUsed = st.Frame;
				return;
			}
			if (t.Loading[page]) return;
			t.Loading[page] = 1;
			++st.LastNewRequests;
			Job* job = new Job{ t.Id, page, (UINT64)(sizeof(FileHeader) + page * kTileBytes), t.TileFile };
			if (!st.Requests.Push(job))
			{
				delete job;   // 요청이 밀렸다 — 다음 피드백에서 다시
				t.Loading[page] = 0;
				return;
			}
			KickReader();
		}

		bool LoadFx()
		{
			State& st = S();
			if (st.Fx || st.FxFailed) return st.Fx != nullptr;
			std::string error;
			st.Fx = FxEffect::Load(L"../Shaders/65. VirtualTexture.fx", error);
			if (!st.Fx)
			{
				st.FxFailed = true;
				EditorLog::Write("VT", "65. VirtualTexture.fx failed to load: %s", error.c_str());
				return false;
			}
			FxTechnique* t = st.Fx->GetTechniqueByName("FeedbackTech");
			D3DX11_PASS_DESC pd;
			t->GetPassByIndex(0)->GetDesc(&pd);
			Gfx::Device()->CreateInputLayout(InputLayoutDesc::PosNormalTexTan2, 4, pd.pIAInputSignature, pd.IAInputSignatureSize, st.Layout.GetAddressOf());
			return true;
		}

		bool EnsureFeedback(FeedbackView& v, UINT w, UINT h)
		{
			if (v.Tex && v.W == w && v.H == h) return true;
			v = FeedbackView();
			D3D11_TEXTURE2D_DESC d = {};
			d.Width = w;
			d.Height = h;
			d.MipLevels = 1;
			d.ArraySize = 1;
			d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			d.SampleDesc.Count = 1;
			d.Usage = D3D11_USAGE_DEFAULT;
			d.BindFlags = D3D11_BIND_RENDER_TARGET;
			auto dev = Gfx::Device();
			if (FAILED(dev->CreateTexture2D(&d, nullptr, v.Tex.GetAddressOf())) || FAILED(dev->CreateRenderTargetView(v.Tex.Get(), nullptr, v.Rtv.GetAddressOf())))
				return false;
			d.Usage = D3D11_USAGE_STAGING;
			d.BindFlags = 0;
			d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			for (auto& s : v.Staging)
				if (FAILED(dev->CreateTexture2D(&d, nullptr, s.GetAddressOf()))) return false;
			v.W = w;
			v.H = h;
			return true;
		}

		// 지난 프레임들의 피드백 (지금 지연만큼 앞) 을 읽어 요청한다
		void ReadFeedback(FeedbackView& v)
		{
			State& st = S();
			if (!v.Tex) return;
			// 가장 오래된 것: Next 가 다음에 쓸 자리 = 가장 오래 전에 쓴 자리
			const int idx = v.Next;
			if (v.StagingFrame[idx] == 0 || v.StagingFrame[idx] + kReadbackLatency > st.Frame) return;
			D3D11_MAPPED_SUBRESOURCE m;
			if (FAILED(Gfx::Context()->Map(v.Staging[idx].Get(), 0, D3D11_MAP_READ, 0, &m))) return;
			std::unordered_set<uint32_t> seen;
			for (UINT y = 0; y < v.H; ++y)
			{
				const uint32_t* row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(m.pData) + (size_t)y * m.RowPitch);
				for (UINT x = 0; x < v.W; ++x)
				{
					const uint32_t p = row[x];
					if ((p & 0xFF) == 0) continue;
					++st.FeedbackPixels;
					if (!seen.insert(p).second) continue;
					const int id = (int)(p & 0xFF);
					const UINT mip = (p >> 8) & 0xFF, px = (p >> 16) & 0xFF, py = (p >> 24) & 0xFF;
					if (id < 1 || id > (int)st.Textures.size()) continue;
					VTex& t = *st.Textures[id - 1];
					if (mip >= t.Mips || px >= t.PagesX[mip] || py >= t.PagesY[mip]) continue;
					// 그 페이지와 조상 (거친 밉) 들 — 조상이 먼저 올라가 이음새 없이 차츰 선명해진다
					for (UINT mm = mip; mm < t.Mips; ++mm)
						Request(t, t.Index(mm, (std::min)(px >> (mm - mip), t.PagesX[mm] - 1), (std::min)(py >> (mm - mip), t.PagesY[mm] - 1)));
				}
			}
			Gfx::Context()->Unmap(v.Staging[idx].Get(), 0);
			st.LastRequests += seen.size();
			v.StagingFrame[idx] = 0;
		}
	}

	bool IsVirtual(const std::wstring& fullPath)
	{
		return AssetImport::AppliesTo(fullPath) && AssetImport::LoadTexture(fullPath).VirtualTexture;
	}

	int Register(const std::wstring& fullPath)
	{
		State& st = S();
		const std::wstring key = std::filesystem::path(fullPath).lexically_normal().wstring();
		auto it = st.ByPath.find(key);
		if (it != st.ByPath.end()) return it->second;
		if (st.Textures.size() >= 255)
		{
			EditorLog::Write("VT", "%s", "more than 255 virtual textures - the rest are ordinary textures");
			return 0;
		}
		if (!EnsureCache()) return 0;
		auto t = std::make_unique<VTex>();
		t->Id = (int)st.Textures.size() + 1;
		t->Source = fullPath;
		DirectX::ScratchImage coarsest;
		std::string error;
		if (!BuildTileFile(*t, coarsest, error))
		{
			EditorLog::Write("VT", "%s: %s", wstring_to_string(fullPath).c_str(), error.c_str());
			st.ByPath[key] = 0;
			return 0;
		}
		const size_t pages = t->Pages();
		t->Slot.assign(pages, -1);
		t->Loading.assign(pages, 0);
		t->LastWanted.assign(pages, 0);
		// 페이지 표 (밉 사슬, 점 샘플링 — 셰이더는 Load)
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = t->PagesX[0];
		d.Height = t->PagesY[0];
		d.MipLevels = t->Mips;
		d.ArraySize = 1;
		d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		d.SampleDesc.Count = 1;
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(Gfx::Device()->CreateTexture2D(&d, nullptr, t->TableTex.GetAddressOf())) ||
			FAILED(Gfx::Device()->CreateShaderResourceView(t->TableTex.Get(), nullptr, t->TableSrv.GetAddressOf())))
		{
			EditorLog::Write("VT", "%s: page table could not be created", wstring_to_string(fullPath).c_str());
			return 0;
		}
		// 대체 텍스처 (밉 포함 보통 텍스처)
		{
			DirectX::ScratchImage withMips;
			if (t->Srgb) coarsest.OverrideFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);   // 보통 텍스처와 같은 색 공간
			const DirectX::TexMetadata& cm = coarsest.GetMetadata();
			if (SUCCEEDED(DirectX::GenerateMipMaps(*coarsest.GetImage(0, 0, 0), DirectX::TEX_FILTER_BOX, 0, withMips)))
				Gfx::CreateShaderResourceView(Gfx::Device(), withMips.GetImages(), withMips.GetImageCount(), withMips.GetMetadata(), t->Fallback.GetAddressOf());
			else
				Gfx::CreateShaderResourceView(Gfx::Device(), coarsest.GetImages(), coarsest.GetImageCount(), cm, t->Fallback.GetAddressOf());
		}
		// 가장 거친 밉의 페이지는 바로 (늘 올라 있다 — 피드백 전에도 그릴 수 있게)
		{
			const UINT lm = t->Mips - 1;
			std::ifstream in(t->TileFile, std::ios::binary);
			std::vector<uint8_t> tile((size_t)kTileBytes);
			for (UINT y = 0; y < t->PagesY[lm]; ++y)
				for (UINT x = 0; x < t->PagesX[lm]; ++x)
				{
					const size_t page = t->Index(lm, x, y);
					const int slot = FindSlot();
					if (slot < 0) break;
					in.seekg((std::streamoff)(sizeof(FileHeader) + page * kTileBytes));
					in.read(reinterpret_cast<char*>(tile.data()), (std::streamsize)tile.size());
					Upload(slot, tile.data());
					st.Slots[slot] = { t->Id, page, st.Frame, true };
					t->Slot[page] = slot;
				}
		}
		VTex& ref = *t;
		st.Textures.push_back(std::move(t));
		st.ByPath[key] = ref.Id;
		RebuildTable(ref);
		EditorLog::Write("VT", "registered %s as #%d (%u x %u, %u mips, %zu pages)", wstring_to_string(std::filesystem::path(fullPath).filename().wstring()).c_str(),
			ref.Id, ref.Width, ref.Height, ref.Mips, pages);
		return ref.Id;
	}

	bool GetBinding(int id, Binding& out)
	{
		State& st = S();
		if (!st.Enabled || id < 1 || id > (int)st.Textures.size()) return false;
		VTex& t = *st.Textures[id - 1];
		if (!t.TableSrv || !st.CacheSrv) return false;
		out.PageTable = t.TableSrv.Get();
		out.Cache = t.Srgb ? st.CacheSrvSrgb.Get() : st.CacheSrv.Get();
		out.Info0[0] = (float)t.Width; out.Info0[1] = (float)t.Height; out.Info0[2] = (float)t.Mips; out.Info0[3] = (float)t.Id;
		const float cachePx = (float)(st.CacheTiles * kTile);
		out.Info1[0] = cachePx; out.Info1[1] = cachePx; out.Info1[2] = (float)kTile; out.Info1[3] = (float)kBorder;
		return true;
	}

	GfxShaderResourceView* Fallback(int id)
	{
		State& st = S();
		return id >= 1 && id <= (int)st.Textures.size() ? st.Textures[id - 1]->Fallback.Get() : nullptr;
	}

	bool Enabled() { return S().Enabled; }
	bool HasWork() { return s_S && s_S->Enabled && !s_S->Textures.empty(); }

	void RenderFeedback(GfxContext* ctx, const Matrix& view, const Matrix& proj, UINT viewWidth, UINT viewHeight, GfxShaderResourceView* normalDepth, bool editorView)
	{
		State& st = S();
		if (!HasWork() || !LoadFx() || st.Frozen) return;
		FeedbackView& v = st.Views[editorView ? 1 : 0];
		const UINT w = (std::max)(1u, viewWidth / kFeedbackScale), h = (std::max)(1u, viewHeight / kFeedbackScale);
		if (!EnsureFeedback(v, w, h)) return;
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (!scene) return;
		auto var = [&](const char* n) { FxVar* x = st.Fx->GetVariableByName(n); return x && x->IsValid() ? x : nullptr; };
		auto setM = [&](const char* n, const Matrix& m) { if (FxVar* x = var(n)) { XMFLOAT4X4 f; XMStoreFloat4x4(&f, m); x->SetMatrix(&f._11); } };
		auto setV = [&](const char* n, float a, float b, float c, float d) { const float f[4] = { a, b, c, d }; if (FxVar* x = var(n)) x->SetFloatVector(f); };
		setM("gViewProj", view * proj);
		setM("gView", view);
		// 프레임마다 1/8 칸 안의 다른 자리 (4 x 4 고리) — 작은 물체 · 페이지 가장자리도 언젠가는 요청된다
		static const float jit[4] = { -0.375f, 0.125f, -0.125f, 0.375f };
		const float jx = jit[st.Frame % 4] * 2.0f / w, jy = jit[(st.Frame / 4) % 4] * 2.0f / h;
		setV("gFeedback", -log2f((float)kFeedbackScale), normalDepth ? 1.0f : 0.0f, jx, jy);
		setV("gDepthSize", (float)viewWidth, (float)viewHeight, (float)w, (float)h);
		if (FxVar* nd = var("gNormalDepth")) nd->SetResource(normalDepth);

		const float clear[4] = { 0, 0, 0, 0 };
		ctx->ClearRenderTargetView(v.Rtv.Get(), clear);
		GfxRenderTargetView* rtv[1] = { v.Rtv.Get() };
		ctx->OMSetRenderTargets(1, rtv, nullptr);
		const D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
		ctx->RSSetViewports(1, &vp);
		ctx->OMSetDepthStencilState(nullptr, 0);
		ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		ctx->RSSetState(nullptr);
		ctx->IASetInputLayout(st.Layout.Get());
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		FxTechnique* tech = st.Fx->GetTechniqueByName("FeedbackTech");
		uint64_t objects = 0;
		for (GameObject* go : scene->GetAllGameObjects())
		{
			if (go == nullptr || !go->IsActiveInHierarchy() || !RenderLayers::Visible(go))
				continue;
			MeshRenderer* mr = go->GetComponent<MeshRenderer>();
			if (!mr || !mr->IsEnabled() || !SceneCulling::IsVisible(mr)) continue;
			auto mesh = mr->GetMesh();
			if (!mesh || mesh->Subsets.empty()) continue;
			const auto& mats = mr->GetMaterials();
			XMFLOAT4X4 world;
			XMStoreFloat4x4(&world, go->GetTransform()->GetWorldMatrix());
			bool any = false;
			for (uint32 i = 0; i < (uint32)mesh->Subsets.size(); ++i)
			{
				UMaterial* mat = i < mats.size() ? mats[i].get() : (mats.empty() ? nullptr : mats.back().get());
				const int id = mat ? mat->VirtualTextureId() : 0;
				Binding b;
				if (!id || !GetBinding(id, b)) continue;
				if (FxVar* x = var("gWorld")) x->SetMatrix(&world._11);
				const XMFLOAT2 tiling = mat->Tiling(), offset = mat->Offset();
				setV("gTexST", tiling.x, tiling.y, offset.x, offset.y);
				setV("gVTInfo", b.Info0[0], b.Info0[1], b.Info0[2], b.Info0[3]);
				tech->GetPassByIndex(0)->Apply(0, ctx);
				mesh->ModelMesh.Draw(ctx, i, nullptr);
				any = true;
			}
			objects += any ? 1 : 0;
		}
		st.LastObjects = objects;
		// 읽기 버퍼로 (지연 뒤에 읽는다)
		ctx->CopyResource(v.Staging[v.Next].Get(), v.Tex.Get());
		v.StagingFrame[v.Next] = st.Frame;
		v.Next = (v.Next + 1) % (kReadbackLatency + 1);
	}

	void Update()
	{
		if (!s_S) return;
		State& st = S();
		st.LastRequests = st.LastNewRequests = st.FeedbackPixels = 0;
		if (st.Enabled && !st.Frozen)
			for (FeedbackView& v : st.Views)
				ReadFeedback(v);
		// 다 읽은 페이지 올리기 (프레임마다 정해진 수까지 — 나머지는 다음 프레임)
		std::deque<Done> done;
		{
			Done* d = nullptr;
			for (int i = 0; i < kUploadsPerFrame && st.Finished.Pop(d); ++i)
			{
				done.push_back(std::move(*d));
				delete d;
			}
			KickReader();   // 결과 칸이 비었으니 남은 요청을 이어 읽는다
		}
		for (Done& d : done)
		{
			VTex& t = *st.Textures[d.Tex - 1];
			t.Loading[d.Page] = 0;
			if (d.Pixels.empty() || t.Slot[d.Page] >= 0) continue;
			// 지난 몇 프레임 안에 원한 페이지만 (멀리 지나간 것은 버린다)
			if (t.LastWanted[d.Page] + 30 < st.Frame) continue;
			const int slot = FindSlot();
			if (slot < 0) continue;
			Upload(slot, d.Pixels.data());
			st.Slots[slot] = { t.Id, d.Page, st.Frame, false };
			t.Slot[d.Page] = slot;
			t.TableDirty = true;
			++t.Uploads;
		}
		for (auto& t : st.Textures)
			if (t->TableDirty) RebuildTable(*t);
		++st.Frame;
	}

	nlohmann::json Info()
	{
		State& st = S();
		nlohmann::json list = nlohmann::json::array();
		size_t used = 0;
		for (const CacheSlot& c : st.Slots) used += c.Tex ? 1 : 0;
		for (const auto& t : st.Textures)
		{
			nlohmann::json mips = nlohmann::json::array();
			for (UINT m = 0; m < t->Mips; ++m)
			{
				int resident = 0;
				for (size_t i = t->MipBase[m]; i < t->MipBase[m] + (size_t)t->PagesX[m] * t->PagesY[m]; ++i) resident += t->Slot[i] >= 0 ? 1 : 0;
				mips.push_back({ { "mip", m }, { "pages", t->PagesX[m] * t->PagesY[m] }, { "resident", resident } });
			}
			list.push_back({ { "id", t->Id }, { "source", wstring_to_string(t->Source) }, { "size", { t->Width, t->Height } }, { "mips", mips },
				{ "pages", t->Pages() }, { "uploads", t->Uploads }, { "srgb", t->Srgb }, { "tileFile", wstring_to_string(t->TileFile) } });
		}
		return { { "enabled", st.Enabled }, { "frozen", st.Frozen }, { "cacheTiles", st.CacheTiles * st.CacheTiles }, { "cacheUsed", used },
			{ "cacheMegabytes", st.CacheTiles * kTile * (double)st.CacheTiles * kTile * 4 / 1048576.0 }, { "uploads", st.Uploads }, { "evictions", st.Evictions },
			{ "lastRequests", st.LastRequests }, { "lastNewRequests", st.LastNewRequests }, { "feedbackPixels", st.FeedbackPixels }, { "feedbackObjects", st.LastObjects },
			{ "pendingLoads", st.Requests.SizeApprox() }, { "reading", st.Draining.load() }, { "frame", st.Frame }, { "textures", list } };
	}

	void RegisterEditor()
	{
		CliServer::Register("vt", "Virtual Texturing: {op: info|page|set|flush, texture?, uv?, mip?, enabled?, frozen?}",
			[](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
				State& st = S();
				const std::string op = args.value("op", std::string("info"));
				if (op == "info") { result = Info(); return true; }
				if (op == "set")
				{
					if (args.contains("enabled")) st.Enabled = args["enabled"].is_boolean() ? args["enabled"].get<bool>() : args["enabled"].dump() != "false";
					if (args.contains("frozen")) st.Frozen = args["frozen"].is_boolean() ? args["frozen"].get<bool>() : args["frozen"].dump() != "false";
					result = { { "enabled", st.Enabled }, { "frozen", st.Frozen } };
					return true;
				}
				if (op == "flush")
				{
					// 잠긴 (가장 거친) 페이지 밖을 모두 비운다 — 다시 피드백으로 채워지는지 검사
					int n = 0;
					for (CacheSlot& c : st.Slots)
						if (c.Tex && !c.Locked)
						{
							VTex& t = *st.Textures[c.Tex - 1];
							t.Slot[c.Page] = -1;
							t.TableDirty = true;
							c = CacheSlot();
							++n;
						}
					for (auto& t : st.Textures) if (t->TableDirty) RebuildTable(*t);
					result = { { "evicted", n } };
					return true;
				}
				if (op == "page")
				{
					// 가상 uv 의 페이지: 원한 밉에서 실제로 올라 있는 밉 (페이지 표 항목)
					const int id = args.value("texture", 1);
					if (id < 1 || id > (int)st.Textures.size()) { error = "no virtual texture #" + std::to_string(id); return false; }
					VTex& t = *st.Textures[id - 1];
					float u = 0.5f, v = 0.5f;
					if (args.contains("uv"))
					{
						const nlohmann::json& a = args["uv"];
						if (a.is_array() && a.size() >= 2) { u = a[0].get<float>(); v = a[1].get<float>(); }
						else if (a.is_string()) sscanf_s(a.get<std::string>().c_str(), "%f,%f", &u, &v);
					}
					const UINT mip = (UINT)std::clamp(args.value("mip", 0), 0, (int)t.Mips - 1);
					const UINT px = (std::min)((UINT)(u * t.PagesX[mip]), t.PagesX[mip] - 1), py = (std::min)((UINT)(v * t.PagesY[mip]), t.PagesY[mip] - 1);
					const uint32_t e = t.Table.empty() ? 0 : t.Table[mip][(size_t)py * t.PagesX[mip] + px];
					result = { { "mip", mip }, { "page", { px, py } }, { "resident", t.Slot[t.Index(mip, px, py)] >= 0 },
						{ "residentMip", (e >> 24) ? (int)((e >> 16) & 0xFF) : -1 }, { "slot", { (int)(e & 0xFF), (int)((e >> 8) & 0xFF) } } };
					return true;
				}
				error = "unknown op '" + op + "' (info, page, set, flush)";
				return false;
			});
	}
}
