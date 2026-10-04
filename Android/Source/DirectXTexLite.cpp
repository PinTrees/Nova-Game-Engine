#include "pch.h"
#include <DirectXTex/DirectXTex.h>
#include <fstream>

// DirectXTex 의 일부 (안드로이드). 이미지 배치는 DirectXTex 와 같다:
//  1D · 2D (배열 · 큐브) = 조각마다 [밉 0 … 밉 n], 3D = 밉마다 깊이 조각들이 이어짐
namespace DirectX
{
	namespace
	{
		size_t BlockBytes(DXGI_FORMAT f)
		{
			switch (f)
			{
			case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
			case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM:
				return 8;
			case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
			case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
			case DXGI_FORMAT_BC5_TYPELESS: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
			case DXGI_FORMAT_BC6H_TYPELESS: case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
			case DXGI_FORMAT_BC7_TYPELESS: case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB:
				return 16;
			default:
				return 0;
			}
		}

		size_t ImageCount(const TexMetadata& m)
		{
			if (m.dimension == TEX_DIMENSION_TEXTURE3D)
			{
				size_t n = 0, d = m.depth;
				for (size_t mip = 0; mip < m.mipLevels; ++mip) { n += d; if (d > 1) d >>= 1; }
				return n;
			}
			return m.arraySize * m.mipLevels;
		}
	}

	size_t BitsPerPixel(DXGI_FORMAT f)
	{
		switch (f)
		{
		case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_UINT: case DXGI_FORMAT_R32G32B32A32_SINT:
			return 128;
		case DXGI_FORMAT_R32G32B32_TYPELESS: case DXGI_FORMAT_R32G32B32_FLOAT: case DXGI_FORMAT_R32G32B32_UINT: case DXGI_FORMAT_R32G32B32_SINT:
			return 96;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_UINT:
		case DXGI_FORMAT_R16G16B16A16_SNORM: case DXGI_FORMAT_R16G16B16A16_SINT: case DXGI_FORMAT_R32G32_TYPELESS: case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R32G32_SINT: case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			return 64;
		case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SNORM: case DXGI_FORMAT_R16_SINT: case DXGI_FORMAT_R8G8_TYPELESS: case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_R8G8_UINT:
		case DXGI_FORMAT_R8G8_SNORM: case DXGI_FORMAT_R8G8_SINT: case DXGI_FORMAT_B5G6R5_UNORM: case DXGI_FORMAT_B5G5R5A1_UNORM: case DXGI_FORMAT_B4G4R4A4_UNORM:
			return 16;
		case DXGI_FORMAT_R8_TYPELESS: case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_UINT: case DXGI_FORMAT_R8_SNORM: case DXGI_FORMAT_R8_SINT: case DXGI_FORMAT_A8_UNORM:
			return 8;
		case DXGI_FORMAT_R1_UNORM:
			return 1;
		case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
			return 4;
		case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB: case DXGI_FORMAT_BC5_TYPELESS: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM: case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16: case DXGI_FORMAT_BC7_TYPELESS: case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB:
			return 8;
		case DXGI_FORMAT_UNKNOWN:
			return 0;
		default:
			return 32;   // RGBA8 · BGRA8 · R10G10B10A2 · R11G11B10 · RG16 · R32 · D24S8 · R9G9B9E5 …
		}
	}

	bool IsCompressed(DXGI_FORMAT f) { return BlockBytes(f) != 0; }

	bool IsSRGB(DXGI_FORMAT f)
	{
		switch (f)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_BC1_UNORM_SRGB: case DXGI_FORMAT_BC2_UNORM_SRGB: case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: case DXGI_FORMAT_BC7_UNORM_SRGB:
			return true;
		default:
			return false;
		}
	}

	DXGI_FORMAT MakeSRGB(DXGI_FORMAT f)
	{
		switch (f)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case DXGI_FORMAT_BC1_UNORM: return DXGI_FORMAT_BC1_UNORM_SRGB;
		case DXGI_FORMAT_BC2_UNORM: return DXGI_FORMAT_BC2_UNORM_SRGB;
		case DXGI_FORMAT_BC3_UNORM: return DXGI_FORMAT_BC3_UNORM_SRGB;
		case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		case DXGI_FORMAT_B8G8R8X8_UNORM: return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
		case DXGI_FORMAT_BC7_UNORM: return DXGI_FORMAT_BC7_UNORM_SRGB;
		default: return f;
		}
	}

	HRESULT ComputePitch(DXGI_FORMAT fmt, size_t width, size_t height, size_t& rowPitch, size_t& slicePitch, CP_FLAGS)
	{
		if (const size_t block = BlockBytes(fmt))
		{
			const size_t bw = (std::max)(size_t(1), (width + 3) / 4), bh = (std::max)(size_t(1), (height + 3) / 4);
			rowPitch = bw * block;
			slicePitch = rowPitch * bh;
			return S_OK;
		}
		const size_t bpp = BitsPerPixel(fmt);
		if (!bpp) { rowPitch = slicePitch = 0; return E_INVALIDARG; }
		rowPitch = (width * bpp + 7) / 8;
		slicePitch = rowPitch * height;
		return S_OK;
	}

	size_t TexMetadata::ComputeIndex(size_t mip, size_t item, size_t slice) const
	{
		if (mip >= mipLevels) return size_t(-1);
		if (dimension == TEX_DIMENSION_TEXTURE3D)
		{
			if (item > 0) return size_t(-1);
			size_t index = 0, d = depth;
			for (size_t m = 0; m < mip; ++m) { index += d; if (d > 1) d >>= 1; }
			return slice < d ? index + slice : size_t(-1);
		}
		if (slice > 0 || item >= arraySize) return size_t(-1);
		return item * mipLevels + mip;
	}

	HRESULT ScratchImage::Initialize(const TexMetadata& meta, CP_FLAGS)
	{
		Release();
		if (!meta.width || !meta.height || !meta.mipLevels || BitsPerPixel(meta.format) == 0) return E_INVALIDARG;
		_meta = meta;
		if (!_meta.depth) _meta.depth = 1;
		if (!_meta.arraySize) _meta.arraySize = 1;
		_count = ImageCount(_meta);
		_images.reset(new Image[_count]);
		// 먼저 크기를 세고 한 덩어리로 잡는다
		size_t total = 0;
		std::vector<std::pair<size_t, size_t>> pitches;   // (row, slice) 이미지마다
		auto add = [&](size_t w, size_t h) {
			size_t row, slice;
			ComputePitch(_meta.format, w, h, row, slice);
			pitches.push_back({ row, slice });
			total += slice;
		};
		if (_meta.dimension == TEX_DIMENSION_TEXTURE3D)
		{
			size_t w = _meta.width, h = _meta.height, d = _meta.depth;
			for (size_t m = 0; m < _meta.mipLevels; ++m)
			{
				for (size_t s = 0; s < d; ++s) add(w, h);
				if (w > 1) w >>= 1;
				if (h > 1) h >>= 1;
				if (d > 1) d >>= 1;
			}
		}
		else
			for (size_t item = 0; item < _meta.arraySize; ++item)
			{
				size_t w = _meta.width, h = _meta.height;
				for (size_t m = 0; m < _meta.mipLevels; ++m)
				{
					add(w, h);
					if (w > 1) w >>= 1;
					if (h > 1) h >>= 1;
				}
			}
		_pixels.reset(new (std::nothrow) uint8_t[total]);
		if (!_pixels) { Release(); return E_OUTOFMEMORY; }
		memset(_pixels.get(), 0, total);
		_size = total;
		uint8_t* p = _pixels.get();
		size_t index = 0;
		auto place = [&](size_t w, size_t h) {
			Image& img = _images[index];
			img.width = w;
			img.height = h;
			img.format = _meta.format;
			img.rowPitch = pitches[index].first;
			img.slicePitch = pitches[index].second;
			img.pixels = p;
			p += img.slicePitch;
			++index;
		};
		if (_meta.dimension == TEX_DIMENSION_TEXTURE3D)
		{
			size_t w = _meta.width, h = _meta.height, d = _meta.depth;
			for (size_t m = 0; m < _meta.mipLevels; ++m)
			{
				for (size_t s = 0; s < d; ++s) place(w, h);
				if (w > 1) w >>= 1;
				if (h > 1) h >>= 1;
				if (d > 1) d >>= 1;
			}
		}
		else
			for (size_t item = 0; item < _meta.arraySize; ++item)
			{
				size_t w = _meta.width, h = _meta.height;
				for (size_t m = 0; m < _meta.mipLevels; ++m)
				{
					place(w, h);
					if (w > 1) w >>= 1;
					if (h > 1) h >>= 1;
				}
			}
		return S_OK;
	}

	HRESULT ScratchImage::Initialize1D(DXGI_FORMAT fmt, size_t length, size_t arraySize, size_t mipLevels, CP_FLAGS flags)
	{
		TexMetadata m;
		m.width = length; m.height = 1; m.depth = 1; m.arraySize = arraySize; m.mipLevels = mipLevels; m.format = fmt; m.dimension = TEX_DIMENSION_TEXTURE1D;
		return Initialize(m, flags);
	}

	HRESULT ScratchImage::Initialize2D(DXGI_FORMAT fmt, size_t width, size_t height, size_t arraySize, size_t mipLevels, CP_FLAGS flags)
	{
		TexMetadata m;
		m.width = width; m.height = height; m.depth = 1; m.arraySize = arraySize; m.mipLevels = mipLevels; m.format = fmt; m.dimension = TEX_DIMENSION_TEXTURE2D;
		return Initialize(m, flags);
	}

	HRESULT ScratchImage::Initialize3D(DXGI_FORMAT fmt, size_t width, size_t height, size_t depth, size_t mipLevels, CP_FLAGS flags)
	{
		TexMetadata m;
		m.width = width; m.height = height; m.depth = depth; m.arraySize = 1; m.mipLevels = mipLevels; m.format = fmt; m.dimension = TEX_DIMENSION_TEXTURE3D;
		return Initialize(m, flags);
	}

	HRESULT ScratchImage::InitializeCube(DXGI_FORMAT fmt, size_t width, size_t height, size_t nCubes, size_t mipLevels, CP_FLAGS flags)
	{
		TexMetadata m;
		m.width = width; m.height = height; m.depth = 1; m.arraySize = nCubes * 6; m.mipLevels = mipLevels; m.format = fmt;
		m.dimension = TEX_DIMENSION_TEXTURE2D; m.miscFlags = TEX_MISC_TEXTURECUBE;
		return Initialize(m, flags);
	}

	void ScratchImage::Release()
	{
		_meta = TexMetadata();
		_images.reset();
		_pixels.reset();
		_count = _size = 0;
	}

	const Image* ScratchImage::GetImage(size_t mip, size_t item, size_t slice) const
	{
		const size_t i = _meta.ComputeIndex(mip, item, slice);
		return i < _count ? &_images[i] : nullptr;
	}

	// ---- DDS
	namespace
	{
		constexpr uint32_t FourCC(char a, char b, char c, char d) { return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24); }

		struct DdsPixelFormat { uint32_t size, flags, fourCC, rgbBits, rMask, gMask, bMask, aMask; };
		struct DdsHeader
		{
			uint32_t size, flags, height, width, pitchOrLinearSize, depth, mipMapCount, reserved1[11];
			DdsPixelFormat ddspf;
			uint32_t caps, caps2, caps3, caps4, reserved2;
		};
		struct DdsHeaderDx10 { uint32_t dxgiFormat, resourceDimension, miscFlag, arraySize, miscFlags2; };

		DXGI_FORMAT LegacyFormat(const DdsPixelFormat& pf)
		{
			if (pf.flags & 0x4)   // DDPF_FOURCC
			{
				switch (pf.fourCC)
				{
				case FourCC('D', 'X', 'T', '1'): return DXGI_FORMAT_BC1_UNORM;
				case FourCC('D', 'X', 'T', '2'): case FourCC('D', 'X', 'T', '3'): return DXGI_FORMAT_BC2_UNORM;
				case FourCC('D', 'X', 'T', '4'): case FourCC('D', 'X', 'T', '5'): return DXGI_FORMAT_BC3_UNORM;
				case FourCC('A', 'T', 'I', '1'): case FourCC('B', 'C', '4', 'U'): return DXGI_FORMAT_BC4_UNORM;
				case FourCC('B', 'C', '4', 'S'): return DXGI_FORMAT_BC4_SNORM;
				case FourCC('A', 'T', 'I', '2'): case FourCC('B', 'C', '5', 'U'): return DXGI_FORMAT_BC5_UNORM;
				case FourCC('B', 'C', '5', 'S'): return DXGI_FORMAT_BC5_SNORM;
				case 36: return DXGI_FORMAT_R16G16B16A16_UNORM;
				case 110: return DXGI_FORMAT_R16G16B16A16_SNORM;
				case 111: return DXGI_FORMAT_R16_FLOAT;
				case 112: return DXGI_FORMAT_R16G16_FLOAT;
				case 113: return DXGI_FORMAT_R16G16B16A16_FLOAT;
				case 114: return DXGI_FORMAT_R32_FLOAT;
				case 115: return DXGI_FORMAT_R32G32_FLOAT;
				case 116: return DXGI_FORMAT_R32G32B32A32_FLOAT;
				default: return DXGI_FORMAT_UNKNOWN;
				}
			}
			if (pf.rgbBits == 32)
			{
				if (pf.rMask == 0xff && pf.gMask == 0xff00 && pf.bMask == 0xff0000) return DXGI_FORMAT_R8G8B8A8_UNORM;
				if (pf.rMask == 0xff0000 && pf.gMask == 0xff00 && pf.bMask == 0xff) return pf.aMask ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_B8G8R8X8_UNORM;
				if (pf.rMask == 0xffff && pf.gMask == 0xffff0000) return DXGI_FORMAT_R16G16_UNORM;
				if (pf.rMask == 0xffffffff) return DXGI_FORMAT_R32_FLOAT;
			}
			if (pf.rgbBits == 8 && (pf.rMask == 0xff || (pf.flags & 0x20000))) return DXGI_FORMAT_R8_UNORM;   // 휘도 8
			if (pf.rgbBits == 16 && pf.rMask == 0xffff) return DXGI_FORMAT_R16_UNORM;
			return DXGI_FORMAT_UNKNOWN;
		}
	}

	HRESULT LoadFromDDSMemory(const uint8_t* data, size_t size, DDS_FLAGS, TexMetadata* metaOut, ScratchImage& image)
	{
		image.Release();
		if (!data || size < 4 + sizeof(DdsHeader) || memcmp(data, "DDS ", 4) != 0) return E_FAIL;
		DdsHeader h;
		memcpy(&h, data + 4, sizeof(h));
		if (h.size != sizeof(DdsHeader) || h.ddspf.size != sizeof(DdsPixelFormat)) return E_FAIL;
		size_t offset = 4 + sizeof(DdsHeader);
		TexMetadata m;
		m.width = h.width;
		m.height = (std::max)(1u, h.height);
		m.depth = 1;
		m.mipLevels = (std::max)(1u, h.mipMapCount);
		m.arraySize = 1;
		if ((h.ddspf.flags & 0x4) && h.ddspf.fourCC == FourCC('D', 'X', '1', '0'))
		{
			if (size < offset + sizeof(DdsHeaderDx10)) return E_FAIL;
			DdsHeaderDx10 x;
			memcpy(&x, data + offset, sizeof(x));
			offset += sizeof(x);
			m.format = (DXGI_FORMAT)x.dxgiFormat;
			m.arraySize = (std::max)(1u, x.arraySize);
			switch (x.resourceDimension)
			{
			case 2: m.dimension = TEX_DIMENSION_TEXTURE1D; m.height = 1; break;
			case 4: m.dimension = TEX_DIMENSION_TEXTURE3D; m.depth = (std::max)(1u, h.depth); break;
			default: m.dimension = TEX_DIMENSION_TEXTURE2D; break;
			}
			if (x.miscFlag & 0x4) { m.miscFlags |= TEX_MISC_TEXTURECUBE; m.arraySize *= 6; }
		}
		else
		{
			m.format = LegacyFormat(h.ddspf);
			if (h.caps2 & 0x200) { m.miscFlags |= TEX_MISC_TEXTURECUBE; m.arraySize = 6; }   // 큐브 (여섯 면 모두라고 본다)
			if ((h.caps2 & 0x200000) && h.depth > 1) { m.dimension = TEX_DIMENSION_TEXTURE3D; m.depth = h.depth; }
		}
		if (m.format == DXGI_FORMAT_UNKNOWN || BitsPerPixel(m.format) == 0) return E_NOTIMPL;
		HRESULT hr = image.Initialize(m);
		if (FAILED(hr)) return hr;
		if (size - offset < image.GetPixelsSize()) { image.Release(); return E_FAIL; }
		memcpy(image.GetPixels(), data + offset, image.GetPixelsSize());
		if (metaOut) *metaOut = image.GetMetadata();
		return S_OK;
	}

	HRESULT LoadFromDDSFile(const wchar_t* file, DDS_FLAGS flags, TexMetadata* meta, ScratchImage& image)
	{
		std::ifstream in(std::filesystem::path(file), std::ios::binary);
		if (!in) return E_FAIL;
		std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		return LoadFromDDSMemory(bytes.data(), bytes.size(), flags, meta, image);
	}

	HRESULT LoadFromDDSFile(const char* file, DDS_FLAGS flags, TexMetadata* meta, ScratchImage& image)
	{
		std::ifstream in(file, std::ios::binary);
		if (!in) return E_FAIL;
		std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		return LoadFromDDSMemory(bytes.data(), bytes.size(), flags, meta, image);
	}

	HRESULT ScratchImage::OverrideFormat(DXGI_FORMAT f)
	{
		if (!_images || BitsPerPixel(f) != BitsPerPixel(_meta.format)) return E_INVALIDARG;
		_meta.format = f;
		for (size_t i = 0; i < _count; ++i) _images[i].format = f;
		return S_OK;
	}

	bool ScratchImage::IsAlphaAllOpaque() const
	{
		if (_meta.format != DXGI_FORMAT_R8G8B8A8_UNORM && _meta.format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
			_meta.format != DXGI_FORMAT_B8G8R8A8_UNORM && _meta.format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
			return false;
		for (size_t i = 0; i < _count; ++i)
			for (size_t p = 3; p < _images[i].slicePitch; p += 4)
				if (_images[i].pixels[p] != 255) return false;
		return true;
	}

	DXGI_FORMAT MakeTypeless(DXGI_FORMAT f)
	{
		switch (f)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_UINT: case DXGI_FORMAT_R8G8B8A8_SNORM:
		case DXGI_FORMAT_R8G8B8A8_SINT: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
		case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
		case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_TYPELESS;
		case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: return DXGI_FORMAT_BC1_TYPELESS;
		case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: return DXGI_FORMAT_BC2_TYPELESS;
		case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB: return DXGI_FORMAT_BC3_TYPELESS;
		case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM: return DXGI_FORMAT_BC4_TYPELESS;
		case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM: return DXGI_FORMAT_BC5_TYPELESS;
		case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB: return DXGI_FORMAT_BC7_TYPELESS;
		case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_UINT: case DXGI_FORMAT_R16G16B16A16_SNORM:
		case DXGI_FORMAT_R16G16B16A16_SINT: return DXGI_FORMAT_R16G16B16A16_TYPELESS;
		default: return f;
		}
	}

	HRESULT LoadFromWICFile(const wchar_t*, WIC_FLAGS, TexMetadata*, ScratchImage& image) { image.Release(); return E_NOTIMPL; }
	HRESULT LoadFromWICFile(const char*, WIC_FLAGS, TexMetadata*, ScratchImage& image) { image.Release(); return E_NOTIMPL; }
	HRESULT GetMetadataFromWICFile(const wchar_t*, WIC_FLAGS, TexMetadata&) { return E_NOTIMPL; }
	HRESULT GetMetadataFromWICFile(const char*, WIC_FLAGS, TexMetadata&) { return E_NOTIMPL; }
	HRESULT LoadFromTGAFile(const wchar_t*, TexMetadata*, ScratchImage& image) { image.Release(); return E_NOTIMPL; }
	HRESULT LoadFromTGAFile(const char*, TexMetadata*, ScratchImage& image) { image.Release(); return E_NOTIMPL; }
	HRESULT SaveToDDSFile(const Image*, size_t, const TexMetadata&, DDS_FLAGS, const wchar_t*) { return E_NOTIMPL; }
	HRESULT SaveToDDSFile(const Image*, size_t, const TexMetadata&, DDS_FLAGS, const char*) { return E_NOTIMPL; }
	HRESULT Resize(const Image&, size_t, size_t, TEX_FILTER_FLAGS, ScratchImage& out) { out.Release(); return E_NOTIMPL; }
	HRESULT GenerateMipMaps(const Image*, size_t, const TexMetadata&, TEX_FILTER_FLAGS, size_t, ScratchImage& out) { out.Release(); return E_NOTIMPL; }
	HRESULT Compress(const Image*, size_t, const TexMetadata&, DXGI_FORMAT, TEX_COMPRESS_FLAGS, float, ScratchImage& out) { out.Release(); return E_NOTIMPL; }
	HRESULT Decompress(const Image*, size_t, const TexMetadata&, DXGI_FORMAT, ScratchImage& out) { out.Release(); return E_NOTIMPL; }

	HRESULT ScratchImage::InitializeFromImage(const Image& src, bool, CP_FLAGS flags)
	{
		HRESULT hr = Initialize2D(src.format, src.width, src.height, 1, 1, flags);
		if (FAILED(hr)) return hr;
		const Image& dst = _images[0];
		const size_t rows = (std::min)(src.slicePitch / (std::max)(src.rowPitch, size_t(1)), dst.slicePitch / (std::max)(dst.rowPitch, size_t(1)));
		for (size_t r = 0; r < rows; ++r)
			memcpy(dst.pixels + r * dst.rowPitch, src.pixels + r * src.rowPitch, (std::min)(src.rowPitch, dst.rowPitch));
		return S_OK;
	}
	HRESULT Convert(const Image&, DXGI_FORMAT, TEX_FILTER_FLAGS, float, ScratchImage& out) { out.Release(); return E_NOTIMPL; }
}
