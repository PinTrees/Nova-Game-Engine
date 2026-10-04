#pragma once
#include "WinCompat.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// 안드로이드: 엔진이 쓰는 DirectXTex 의 일부 (같은 이름 · 같은 뜻). Windows 의 DirectXTex 는 미리 빌드된 Windows 라이브러리라 쓸 수 없다.
//  이미지 묶음 (ScratchImage) · 메타데이터 · 행 간격 계산 · DDS 읽기 (PC 에서 구운 텍스처 캐시 · 하늘 큐브맵). 구현 = Android/Source/DirectXTexLite.cpp
namespace DirectX
{
	enum TEX_DIMENSION { TEX_DIMENSION_TEXTURE1D = 2, TEX_DIMENSION_TEXTURE2D = 3, TEX_DIMENSION_TEXTURE3D = 4 };
	enum TEX_MISC_FLAG { TEX_MISC_TEXTURECUBE = 0x4 };
	enum CP_FLAGS { CP_FLAGS_NONE = 0 };
	enum DDS_FLAGS { DDS_FLAGS_NONE = 0 };
	enum TEX_FILTER_FLAGS : unsigned long { TEX_FILTER_DEFAULT = 0, TEX_FILTER_LINEAR = 0x200000, TEX_FILTER_CUBIC = 0x300000, TEX_FILTER_BOX = 0x400000, TEX_FILTER_SRGB = 0x3000000 };

	struct TexMetadata
	{
		size_t width = 0, height = 0, depth = 0, arraySize = 0, mipLevels = 0;
		uint32_t miscFlags = 0, miscFlags2 = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		TEX_DIMENSION dimension = TEX_DIMENSION_TEXTURE2D;
		bool IsCubemap() const { return (miscFlags & TEX_MISC_TEXTURECUBE) != 0; }
		bool IsVolumemap() const { return dimension == TEX_DIMENSION_TEXTURE3D; }
		size_t ComputeIndex(size_t mip, size_t item, size_t slice) const;   // 없으면 size_t(-1)
	};

	struct Image
	{
		size_t width = 0, height = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		size_t rowPitch = 0, slicePitch = 0;
		uint8_t* pixels = nullptr;
	};

	class ScratchImage
	{
	public:
		ScratchImage() = default;
		ScratchImage(ScratchImage&&) = default;
		ScratchImage& operator=(ScratchImage&&) = default;
		ScratchImage(const ScratchImage&) = delete;
		ScratchImage& operator=(const ScratchImage&) = delete;

		HRESULT Initialize(const TexMetadata& meta, CP_FLAGS flags = CP_FLAGS_NONE);
		HRESULT Initialize1D(DXGI_FORMAT fmt, size_t length, size_t arraySize, size_t mipLevels, CP_FLAGS flags = CP_FLAGS_NONE);
		HRESULT Initialize2D(DXGI_FORMAT fmt, size_t width, size_t height, size_t arraySize, size_t mipLevels, CP_FLAGS flags = CP_FLAGS_NONE);
		HRESULT Initialize3D(DXGI_FORMAT fmt, size_t width, size_t height, size_t depth, size_t mipLevels, CP_FLAGS flags = CP_FLAGS_NONE);
		HRESULT InitializeCube(DXGI_FORMAT fmt, size_t width, size_t height, size_t nCubes, size_t mipLevels, CP_FLAGS flags = CP_FLAGS_NONE);
		HRESULT InitializeFromImage(const Image& src, bool allow1D = false, CP_FLAGS flags = CP_FLAGS_NONE);   // 이미지 하나를 복사
		void Release();

		const TexMetadata& GetMetadata() const { return _meta; }
		const Image* GetImage(size_t mip, size_t item, size_t slice) const;
		const Image* GetImages() const { return _images.get(); }
		size_t GetImageCount() const { return _count; }
		uint8_t* GetPixels() const { return _pixels.get(); }
		size_t GetPixelsSize() const { return _size; }
		HRESULT OverrideFormat(DXGI_FORMAT f);   // 같은 크기의 형식으로 이름만 바꾼다 (sRGB ↔ 선형)
		bool IsAlphaAllOpaque() const;

	private:
		TexMetadata _meta;
		std::unique_ptr<Image[]> _images;
		size_t _count = 0;
		std::unique_ptr<uint8_t[]> _pixels;
		size_t _size = 0;
	};

	size_t BitsPerPixel(DXGI_FORMAT fmt);
	bool IsCompressed(DXGI_FORMAT fmt);
	bool IsSRGB(DXGI_FORMAT fmt);
	DXGI_FORMAT MakeSRGB(DXGI_FORMAT fmt);
	HRESULT ComputePitch(DXGI_FORMAT fmt, size_t width, size_t height, size_t& rowPitch, size_t& slicePitch, CP_FLAGS flags = CP_FLAGS_NONE);

	enum WIC_FLAGS : unsigned long { WIC_FLAGS_NONE = 0, WIC_FLAGS_FORCE_RGB = 0x8, WIC_FLAGS_IGNORE_SRGB = 0x20, WIC_FLAGS_FORCE_SRGB = 0x40 };
	enum TGA_FLAGS : unsigned long { TGA_FLAGS_NONE = 0 };
	enum TEX_COMPRESS_FLAGS : unsigned long { TEX_COMPRESS_DEFAULT = 0, TEX_COMPRESS_PARALLEL = 0x10000000 };
	constexpr float TEX_THRESHOLD_DEFAULT = 0.5f;
	DXGI_FORMAT MakeTypeless(DXGI_FORMAT fmt);

	// 안드로이드에서 못 하는 것 (E_NOTIMPL): 이미지 디코딩 (PNG · JPG · TGA), 크기 바꾸기 · 밉 만들기 · 압축 · 저장.
	//  텍스처는 PC 의 에디터가 만든 DDS 캐시(TextureCache)를 쓴다
	HRESULT LoadFromWICFile(const wchar_t* file, WIC_FLAGS flags, TexMetadata* meta, ScratchImage& image);
	HRESULT LoadFromWICFile(const char* file, WIC_FLAGS flags, TexMetadata* meta, ScratchImage& image);
	HRESULT GetMetadataFromWICFile(const wchar_t* file, WIC_FLAGS flags, TexMetadata& meta);
	HRESULT GetMetadataFromWICFile(const char* file, WIC_FLAGS flags, TexMetadata& meta);
	HRESULT LoadFromTGAFile(const wchar_t* file, TexMetadata* meta, ScratchImage& image);
	HRESULT LoadFromTGAFile(const char* file, TexMetadata* meta, ScratchImage& image);
	HRESULT SaveToDDSFile(const Image* images, size_t count, const TexMetadata& meta, DDS_FLAGS flags, const wchar_t* file);
	HRESULT SaveToDDSFile(const Image* images, size_t count, const TexMetadata& meta, DDS_FLAGS flags, const char* file);
	HRESULT Resize(const Image& src, size_t width, size_t height, TEX_FILTER_FLAGS filter, ScratchImage& out);
	HRESULT GenerateMipMaps(const Image* images, size_t count, const TexMetadata& meta, TEX_FILTER_FLAGS filter, size_t levels, ScratchImage& out);
	HRESULT Compress(const Image* images, size_t count, const TexMetadata& meta, DXGI_FORMAT format, TEX_COMPRESS_FLAGS flags, float threshold, ScratchImage& out);
	HRESULT Decompress(const Image* images, size_t count, const TexMetadata& meta, DXGI_FORMAT format, ScratchImage& out);
	HRESULT Convert(const Image& src, DXGI_FORMAT format, TEX_FILTER_FLAGS filter, float threshold, ScratchImage& out);

	// DDS (DX10 머리 · 옛 FourCC DXT1/3/5 · 기본 RGBA8/BGRA8)
	HRESULT LoadFromDDSMemory(const uint8_t* data, size_t size, DDS_FLAGS flags, TexMetadata* meta, ScratchImage& image);
	HRESULT LoadFromDDSFile(const wchar_t* file, DDS_FLAGS flags, TexMetadata* meta, ScratchImage& image);
	HRESULT LoadFromDDSFile(const char* file, DDS_FLAGS flags, TexMetadata* meta, ScratchImage& image);
}
