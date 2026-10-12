#include "pch.h"
#include "ImageIO.h"
#include <webp/decode.h>
#include <fstream>
#include <filesystem>

namespace
{
	bool ReadAll(const wchar_t* path, std::vector<uint8_t>& out)
	{
		std::ifstream is(std::filesystem::path(path), std::ios::binary);
		if (!is)
			return false;
		is.seekg(0, std::ios::end);
		const std::streamoff n = is.tellg();
		if (n <= 0)
			return false;
		out.resize((size_t)n);
		is.seekg(0, std::ios::beg);
		is.read(reinterpret_cast<char*>(out.data()), n);
		return (bool)is;
	}
}

namespace ImageIO
{
	bool IsWebP(const std::wstring& path)
	{
		std::wstring ext = std::filesystem::path(path).extension().wstring();
		for (wchar_t& ch : ext)
			ch = (wchar_t)towlower(ch);
		return ext == L".webp";
	}

	bool DecodeWebP(const void* data, size_t size, std::vector<uint8_t>& rgba, int& w, int& h)
	{
		w = h = 0;
		uint8_t* px = WebPDecodeRGBA(static_cast<const uint8_t*>(data), size, &w, &h);
		if (px == nullptr || w <= 0 || h <= 0)
			return false;
		rgba.assign(px, px + (size_t)w * h * 4);
		WebPFree(px);
		return true;
	}

	HRESULT Load(const wchar_t* path, DirectX::WIC_FLAGS flags, DirectX::TexMetadata* metadata, DirectX::ScratchImage& image)
	{
		if (!IsWebP(path))
			return DirectX::LoadFromWICFile(path, flags, metadata, image);
		std::vector<uint8_t> file, rgba;
		int w = 0, h = 0;
		if (!ReadAll(path, file) || !DecodeWebP(file.data(), file.size(), rgba, w, h))
			return E_FAIL;
		HRESULT hr = image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, (size_t)w, (size_t)h, 1, 1);
		if (FAILED(hr))
			return hr;
		const DirectX::Image* img = image.GetImage(0, 0, 0);
		for (int y = 0; y < h; ++y)
			memcpy(img->pixels + (size_t)y * img->rowPitch, rgba.data() + (size_t)y * w * 4, (size_t)w * 4);
		if (metadata)
			*metadata = image.GetMetadata();
		return S_OK;
	}

	HRESULT GetMetadata(const wchar_t* path, DirectX::WIC_FLAGS flags, DirectX::TexMetadata& metadata)
	{
		if (!IsWebP(path))
			return DirectX::GetMetadataFromWICFile(path, flags, metadata);
		std::vector<uint8_t> file;
		int w = 0, h = 0;
		if (!ReadAll(path, file) || !WebPGetInfo(file.data(), file.size(), &w, &h))
			return E_FAIL;
		metadata = DirectX::TexMetadata();
		metadata.width = (size_t)w;
		metadata.height = (size_t)h;
		metadata.depth = metadata.arraySize = metadata.mipLevels = 1;
		metadata.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
		return S_OK;
	}
}
