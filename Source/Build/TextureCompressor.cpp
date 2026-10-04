#include "pch.h"
#include "TextureCompressor.h"
#include "Etc2Codec.h"
#include "MobileTextureFormats.h"
#include <astcenc.h>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

namespace TextureCompressor
{
	namespace
	{
		enum class Kind { Astc, Etc2, Bc, Rgba };
		struct Choice
		{
			Kind K = Kind::Rgba;
			int AstcBlock = 4;   // MobileTex::kAstcBlocks 의 번호 (6x6)
			int MaxSize = 2048;
		};

		// 가져오기 설정의 Android Format → ASTC 블록 번호 (MobileTex::kAstcBlocks)
		int AstcBlockOf(int androidFormat)
		{
			switch (androidFormat)
			{
			case AssetImport::TextureSettings::AndroidASTC4x4: return MobileTex::FindAstcBlock(4, 4);
			case AssetImport::TextureSettings::AndroidASTC5x5: return MobileTex::FindAstcBlock(5, 5);
			case AssetImport::TextureSettings::AndroidASTC6x6: return MobileTex::FindAstcBlock(6, 6);
			case AssetImport::TextureSettings::AndroidASTC8x8: return MobileTex::FindAstcBlock(8, 8);
			case AssetImport::TextureSettings::AndroidASTC10x10: return MobileTex::FindAstcBlock(10, 10);
			case AssetImport::TextureSettings::AndroidASTC12x12: return MobileTex::FindAstcBlock(12, 12);
			default: return -1;
			}
		}

		// 형식 고르기: Override for Android → 그 형식 · 크기, 아니면 프로젝트 기본 (Compression None = 압축 없음, High Quality = ASTC 4x4)
		Choice Resolve(const AssetImport::TextureSettings& ts, AndroidDefault def)
		{
			Choice c;
			c.MaxSize = ts.AndroidOverride ? ts.AndroidMaxSize : ts.MaxSize;
			const int f = ts.AndroidOverride ? ts.AndroidFormat : AssetImport::TextureSettings::AndroidAutomatic;
			if (f == AssetImport::TextureSettings::AndroidAutomatic)
			{
				if (ts.Compression == AssetImport::TextureSettings::None) { c.K = Kind::Rgba; return c; }
				switch (def)
				{
				case AndroidDefault::ASTC:
					c.K = Kind::Astc;
					c.AstcBlock = MobileTex::FindAstcBlock(ts.Compression == AssetImport::TextureSettings::HighQuality ? 4 : 6, ts.Compression == AssetImport::TextureSettings::HighQuality ? 4 : 6);
					break;
				case AndroidDefault::ETC2: c.K = Kind::Etc2; break;
				case AndroidDefault::DXT: c.K = Kind::Bc; break;
				default: c.K = Kind::Rgba; break;
				}
				return c;
			}
			if (f == AssetImport::TextureSettings::AndroidETC2) c.K = Kind::Etc2;
			else if (f == AssetImport::TextureSettings::AndroidRGBA32) c.K = Kind::Rgba;
			else { c.K = Kind::Astc; c.AstcBlock = AstcBlockOf(f); }
			return c;
		}

		DXGI_FORMAT ToLinear(DXGI_FORMAT f)
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

		// PC 의 가져오기 (Utils::LoadTexture 의 ApplyTextureImport) 와 같은 순서 — 압축만 빼고: 읽기 → 색 공간 → RGBA8 → Max Size → 밉
		bool Prepare(const std::wstring& source, const AssetImport::TextureSettings& ts, int maxSize, DirectX::ScratchImage& img, std::string& error)
		{
			std::wstring ext = fs::path(source).extension().wstring();
			for (wchar_t& c : ext) c = (wchar_t)towlower(c);
			DirectX::TexMetadata md = {};
			HRESULT hr;
			if (ext == L".dds") hr = DirectX::LoadFromDDSFile(source.c_str(), DirectX::DDS_FLAGS_NONE, &md, img);
			else if (ext == L".tga") hr = DirectX::LoadFromTGAFile(source.c_str(), &md, img);
			else hr = DirectX::LoadFromWICFile(source.c_str(), DirectX::WIC_FLAGS_NONE, &md, img);
			if (FAILED(hr)) { error = "could not read the image"; return false; }
			if (md.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || md.arraySize != 1 || md.depth != 1) { error = "only 2D textures"; return false; }
			const bool linear = ts.TextureType == AssetImport::TextureSettings::NormalMap || !ts.SRGB;
			if (linear && DirectX::IsSRGB(md.format))
				img.OverrideFormat(ToLinear(md.format));
			md = img.GetMetadata();
			DirectX::ScratchImage top;
			if (DirectX::IsCompressed(md.format))
			{
				if (FAILED(DirectX::Decompress(*img.GetImage(0, 0, 0), DirectX::IsSRGB(md.format) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, top)))
				{ error = "could not decompress"; return false; }
			}
			else if (md.format != DXGI_FORMAT_R8G8B8A8_UNORM && md.format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
			{
				if (FAILED(DirectX::Convert(*img.GetImage(0, 0, 0), DirectX::IsSRGB(md.format) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM,
					DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, top)))
				{ error = "unsupported pixel format " + std::to_string((int)md.format); return false; }
			}
			else if (FAILED(top.InitializeFromImage(*img.GetImage(0, 0, 0)))) { error = "out of memory"; return false; }
			img = std::move(top);
			md = img.GetMetadata();
			if ((int)(std::max)(md.width, md.height) > maxSize)
			{
				const double k = (double)maxSize / (double)(std::max)(md.width, md.height);
				DirectX::ScratchImage resized;
				if (SUCCEEDED(DirectX::Resize(*img.GetImage(0, 0, 0), (std::max)((size_t)1, (size_t)std::lround(md.width * k)),
					(std::max)((size_t)1, (size_t)std::lround(md.height * k)), DirectX::TEX_FILTER_DEFAULT, resized)))
					img = std::move(resized);
				md = img.GetMetadata();
			}
			if (ts.MipMaps && md.width > 1 && md.height > 1)
			{
				DirectX::ScratchImage mipped;
				if (SUCCEEDED(DirectX::GenerateMipMaps(img.GetImages(), img.GetImageCount(), md, DirectX::TEX_FILTER_DEFAULT, 0, mipped)))
					img = std::move(mipped);
			}
			return true;
		}

		bool HasAlpha(const DirectX::ScratchImage& img)
		{
			const DirectX::Image* i = img.GetImage(0, 0, 0);
			for (size_t y = 0; y < i->height; ++y)
				for (size_t x = 0; x < i->width; ++x)
					if (i->pixels[y * i->rowPitch + x * 4 + 3] != 255) return true;
			return false;
		}

		// ETC2: 4x4 블록마다 (가장자리는 마지막 화소를 되풀이)
		std::vector<uint8_t> EncodeEtc2(const DirectX::Image& im, bool alpha, bool perceptual)
		{
			const size_t bw = (im.width + 3) / 4, bh = (im.height + 3) / 4, bytes = alpha ? 16 : 8;
			std::vector<uint8_t> out(bw * bh * bytes);
			auto work = [&](size_t from, size_t to) {
				uint8_t block[64];
				for (size_t b = from; b < to; ++b)
				{
					const size_t bx = b % bw, by = b / bw;
					for (int y = 0; y < 4; ++y)
						for (int x = 0; x < 4; ++x)
						{
							const size_t sx = (std::min)(bx * 4 + x, im.width - 1), sy = (std::min)(by * 4 + y, im.height - 1);
							memcpy(block + (y * 4 + x) * 4, im.pixels + sy * im.rowPitch + sx * 4, 4);
						}
					uint8_t* dst = out.data() + b * bytes;
					if (alpha)
					{
						Etc2Codec::EncodeAlpha(block, dst);
						Etc2Codec::EncodeRgb(block, dst + 8, perceptual);
					}
					else Etc2Codec::EncodeRgb(block, dst, perceptual);
				}
			};
			const size_t total = bw * bh;
			const unsigned threads = (std::max)(1u, (std::min)(std::thread::hardware_concurrency(), (unsigned)(total / 64 + 1)));
			std::vector<std::thread> pool;
			for (unsigned t = 0; t < threads; ++t)
				pool.emplace_back(work, total * t / threads, total * (t + 1) / threads);
			for (auto& th : pool) th.join();
			return out;
		}

		std::vector<uint8_t> DecodeEtc2(const std::vector<uint8_t>& data, size_t w, size_t h, bool alpha)
		{
			const size_t bw = (w + 3) / 4, bh = (h + 3) / 4, bytes = alpha ? 16 : 8;
			std::vector<uint8_t> rgba(w * h * 4);
			uint8_t block[64];
			for (size_t by = 0; by < bh; ++by)
				for (size_t bx = 0; bx < bw; ++bx)
				{
					const uint8_t* src = data.data() + (by * bw + bx) * bytes;
					if (alpha) { Etc2Codec::DecodeRgb(src + 8, block); Etc2Codec::DecodeAlpha(src, block); }
					else Etc2Codec::DecodeRgb(src, block);
					for (int y = 0; y < 4; ++y)
						for (int x = 0; x < 4; ++x)
							if (bx * 4 + x < w && by * 4 + y < h)
								memcpy(rgba.data() + ((by * 4 + y) * w + bx * 4 + x) * 4, block + (y * 4 + x) * 4, 4);
				}
			return rgba;
		}

		// ASTC: astcenc (밉마다, 스레드 여럿이 같은 그림을 나눠서)
		struct Astc
		{
			astcenc_context* Ctx = nullptr;
			unsigned Threads = 1;
			~Astc() { if (Ctx) astcenc_context_free(Ctx); }
		};

		bool EncodeAstc(Astc& a, const DirectX::Image& im, int bx, int by, std::vector<uint8_t>& out, std::string& error)
		{
			astcenc_image image = {};
			image.dim_x = (unsigned)im.width;
			image.dim_y = (unsigned)im.height;
			image.dim_z = 1;
			image.data_type = ASTCENC_TYPE_U8;
			std::vector<uint8_t> tight;
			void* slice = im.pixels;
			if (im.rowPitch != im.width * 4)
			{
				tight.resize(im.width * im.height * 4);
				for (size_t y = 0; y < im.height; ++y) memcpy(tight.data() + y * im.width * 4, im.pixels + y * im.rowPitch, im.width * 4);
				slice = tight.data();
			}
			image.data = &slice;
			const astcenc_swizzle swz = { ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };
			out.assign(((im.width + bx - 1) / bx) * ((im.height + by - 1) / by) * 16, 0);
			std::vector<astcenc_error> errs(a.Threads, ASTCENC_SUCCESS);
			std::vector<std::thread> pool;
			for (unsigned t = 0; t < a.Threads; ++t)
				pool.emplace_back([&, t]() { errs[t] = astcenc_compress_image(a.Ctx, &image, &swz, out.data(), out.size(), t); });
			for (auto& th : pool) th.join();
			astcenc_compress_reset(a.Ctx);
			for (astcenc_error e : errs)
				if (e != ASTCENC_SUCCESS) { error = std::string("astcenc: ") + astcenc_get_error_string(e); return false; }
			return true;
		}

		std::vector<uint8_t> DecodeAstc(Astc& a, const std::vector<uint8_t>& data, size_t w, size_t h)
		{
			std::vector<uint8_t> rgba(w * h * 4);
			astcenc_image image = {};
			image.dim_x = (unsigned)w;
			image.dim_y = (unsigned)h;
			image.dim_z = 1;
			image.data_type = ASTCENC_TYPE_U8;
			void* slice = rgba.data();
			image.data = &slice;
			const astcenc_swizzle swz = { ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };
			astcenc_decompress_image(a.Ctx, data.data(), data.size(), &image, &swz, 0);
			astcenc_decompress_reset(a.Ctx);
			return rgba;
		}

		double Psnr(const DirectX::Image& src, const uint8_t* decoded, bool alpha)
		{
			double sum = 0;
			const int channels = alpha ? 4 : 3;
			for (size_t y = 0; y < src.height; ++y)
				for (size_t x = 0; x < src.width; ++x)
					for (int c = 0; c < channels; ++c)
					{
						const double d = (double)src.pixels[y * src.rowPitch + x * 4 + c] - decoded[(y * src.width + x) * 4 + c];
						sum += d * d;
					}
			const double mse = sum / ((double)src.width * src.height * channels);
			return mse <= 1e-9 ? 99.0 : 10.0 * log10(255.0 * 255.0 / mse);
		}

		// DDS (DX10 머리): 2D, 밉 levels
		bool WriteDds(const std::wstring& file, unsigned dxgi, size_t w, size_t h, const std::vector<std::vector<uint8_t>>& levels, std::string& error)
		{
			struct PixelFormat { uint32_t size, flags, fourCC, rgbBits, r, g, b, a; };
			struct Header
			{
				uint32_t size, flags, height, width, pitchOrLinearSize, depth, mipMapCount, reserved1[11];
				PixelFormat pf;
				uint32_t caps, caps2, caps3, caps4, reserved2;
			};
			struct Dx10 { uint32_t dxgiFormat, resourceDimension, miscFlag, arraySize, miscFlags2; };
			Header hd = {};
			hd.size = sizeof(Header);
			hd.flags = 0x1 | 0x2 | 0x4 | 0x1000 | (levels.size() > 1 ? 0x20000 : 0);   // CAPS · HEIGHT · WIDTH · PIXELFORMAT · MIPMAPCOUNT
			hd.height = (uint32_t)h;
			hd.width = (uint32_t)w;
			hd.mipMapCount = (uint32_t)levels.size();
			hd.pf.size = sizeof(PixelFormat);
			hd.pf.flags = 0x4;   // FOURCC
			hd.pf.fourCC = 'D' | ('X' << 8) | ('1' << 16) | ('0' << 24);
			hd.caps = 0x1000 | (levels.size() > 1 ? 0x400008 : 0);   // TEXTURE (+ MIPMAP · COMPLEX)
			const Dx10 x = { dxgi, 3 /* TEXTURE2D */, 0, 1, 0 };
			std::error_code ec;
			fs::create_directories(fs::path(file).parent_path(), ec);
			std::ofstream out(fs::path(file), std::ios::binary | std::ios::trunc);
			out.write("DDS ", 4);
			out.write((const char*)&hd, sizeof(hd));
			out.write((const char*)&x, sizeof(x));
			for (const auto& l : levels) out.write((const char*)l.data(), (std::streamsize)l.size());
			if (!out) { error = "could not write " + wstring_to_string(file); return false; }
			return true;
		}
	}

	const char* AndroidDefaultName(AndroidDefault d)
	{
		switch (d)
		{
		case AndroidDefault::ASTC: return "ASTC";
		case AndroidDefault::ETC2: return "ETC2";
		case AndroidDefault::DXT: return "DXT";
		default: return "None";
		}
	}

	std::string AndroidFormatFor(const AssetImport::TextureSettings& ts, AndroidDefault projectDefault)
	{
		const Choice c = Resolve(ts, projectDefault);
		switch (c.K)
		{
		case Kind::Astc: return MobileTex::Name(MobileTex::AstcFormat(c.AstcBlock, false));
		case Kind::Etc2: return "ETC2";
		case Kind::Bc: return ts.Compression == AssetImport::TextureSettings::HighQuality ? "BC7" : "BC1 / BC3";
		default: return "RGBA32";
		}
	}

	bool BuildAndroid(const std::wstring& source, const AssetImport::TextureSettings& ts, AndroidDefault projectDefault, const std::wstring& outDds,
		Result& result, std::string& error)
	{
		const Choice c = Resolve(ts, projectDefault);
		DirectX::ScratchImage img;
		if (!Prepare(source, ts, c.MaxSize, img, error))
			return false;
		const DirectX::TexMetadata md = img.GetMetadata();
		const bool srgb = DirectX::IsSRGB(md.format);
		const bool alpha = HasAlpha(img);
		result = {};
		result.Width = (int)md.width;
		result.Height = (int)md.height;
		result.Mips = (int)md.mipLevels;
		result.Srgb = srgb;
		std::vector<std::vector<uint8_t>> levels(md.mipLevels);
		unsigned dxgi = 0;
		const DirectX::Image& top = *img.GetImage(0, 0, 0);
		switch (c.K)
		{
		case Kind::Astc:
		{
			const MobileTex::Block b = MobileTex::kAstcBlocks[c.AstcBlock];
			Astc a;
			a.Threads = (std::max)(1u, std::thread::hardware_concurrency());
			astcenc_config cfg = {};
			if (astcenc_config_init(srgb ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_LDR, (unsigned)b.X, (unsigned)b.Y, 1, ASTCENC_PRE_MEDIUM, 0, &cfg) != ASTCENC_SUCCESS ||
				astcenc_context_alloc(&cfg, a.Threads, &a.Ctx, nullptr) != ASTCENC_SUCCESS)
			{
				error = "astcenc init failed";
				return false;
			}
			for (size_t m = 0; m < md.mipLevels; ++m)
				if (!EncodeAstc(a, *img.GetImage(m, 0, 0), b.X, b.Y, levels[m], error)) return false;
			result.Psnr = Psnr(top, DecodeAstc(a, levels[0], top.width, top.height).data(), alpha);
			dxgi = MobileTex::AstcFormat(c.AstcBlock, srgb);
			break;
		}
		case Kind::Etc2:
			for (size_t m = 0; m < md.mipLevels; ++m)
				levels[m] = EncodeEtc2(*img.GetImage(m, 0, 0), alpha, ts.TextureType != AssetImport::TextureSettings::NormalMap && ts.SRGB);
			result.Psnr = Psnr(top, DecodeEtc2(levels[0], top.width, top.height, alpha).data(), alpha);
			dxgi = alpha ? (srgb ? MobileTex::kEtc2SRGB8A8 : MobileTex::kEtc2RGBA8) : (srgb ? MobileTex::kEtc2SRGB8 : MobileTex::kEtc2RGB8);
			break;
		case Kind::Bc:
		{
			// PC 와 같은 BC (MuMu 같은 에뮬레이터 · BC 를 받는 GPU 용): High = BC7, 알파 없음 = BC1, 있음 = BC3
			if (md.width % 4 != 0 || md.height % 4 != 0) { error = "BC needs a size that is a multiple of 4"; return false; }
			const DXGI_FORMAT bc = ts.Compression == AssetImport::TextureSettings::HighQuality ? (srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM)
				: !alpha ? (srgb ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM) : (srgb ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM);
			DirectX::ScratchImage packed, back;
			if (FAILED(DirectX::Compress(img.GetImages(), img.GetImageCount(), md, bc, DirectX::TEX_COMPRESS_PARALLEL, DirectX::TEX_THRESHOLD_DEFAULT, packed)))
			{
				error = "BC compression failed";
				return false;
			}
			for (size_t m = 0; m < md.mipLevels; ++m)
			{
				const DirectX::Image* im = packed.GetImage(m, 0, 0);
				levels[m].assign(im->pixels, im->pixels + im->slicePitch);
			}
			if (SUCCEEDED(DirectX::Decompress(*packed.GetImage(0, 0, 0), srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, back)))
				result.Psnr = Psnr(top, back.GetImage(0, 0, 0)->pixels, alpha);
			dxgi = (unsigned)bc;
			result.Format = bc == DXGI_FORMAT_BC7_UNORM || bc == DXGI_FORMAT_BC7_UNORM_SRGB ? "BC7" : (bc == DXGI_FORMAT_BC1_UNORM || bc == DXGI_FORMAT_BC1_UNORM_SRGB ? "BC1" : "BC3");
			break;
		}
		default:
			for (size_t m = 0; m < md.mipLevels; ++m)
			{
				const DirectX::Image* im = img.GetImage(m, 0, 0);
				levels[m].resize(im->width * im->height * 4);
				for (size_t y = 0; y < im->height; ++y) memcpy(levels[m].data() + y * im->width * 4, im->pixels + y * im->rowPitch, im->width * 4);
			}
			dxgi = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
			result.Format = "RGBA32";
			break;
		}
		if (result.Format.empty())
			result.Format = MobileTex::Name(dxgi);
		for (const auto& l : levels) result.Bytes += l.size();
		return WriteDds(outDds, dxgi, md.width, md.height, levels, error);
	}
}
