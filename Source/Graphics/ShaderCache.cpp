#include "pch.h"
#include "ShaderCache.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <mutex>

namespace fs = std::filesystem;

namespace
{
	constexpr uint32_t kCacheMagic = 0x43584644; // 'DFXC'

	struct CacheHeader
	{
		uint32_t magic;
		uint32_t flags;
		uint64_t stamp;
		uint64_t size;
	};

	// Shaders 폴더 내 모든 .fx 중 가장 최근 수정 시각. (LightHelper.fx 같은 include 변경도 감지)
	uint64_t ComputeStamp(const fs::path& shaderDir)
	{
		static std::mutex mtx;
		static std::map<std::wstring, uint64_t> cache;
		std::lock_guard<std::mutex> lock(mtx);

		std::wstring key = shaderDir.wstring();
		auto it = cache.find(key);
		if (it != cache.end())
			return it->second;

		uint64_t latest = 0;
		std::error_code ec;
		for (const auto& e : fs::directory_iterator(shaderDir, ec))
		{
			if (e.is_regular_file(ec) && e.path().extension() == L".fx")
			{
				uint64_t t = static_cast<uint64_t>(e.last_write_time(ec).time_since_epoch().count());
				if (t > latest) latest = t;
			}
		}
		cache[key] = latest;
		return latest;
	}
}

HRESULT ShaderCache::CompileEffect(const std::wstring& filename, UINT shaderFlags,
	Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, Microsoft::WRL::ComPtr<ID3DBlob>& outMsgs)
{
	fs::path src(filename);
	std::error_code ec;
	fs::path cacheDir = fs::path(L"ShaderCache");
	fs::path cacheFile = cacheDir / (src.stem().wstring() + L"_" + std::to_wstring(std::hash<std::wstring>{}(src.lexically_normal().wstring())) + L".fxo");

	uint64_t stamp = ComputeStamp(src.parent_path());

	// 캐시 적중 시 컴파일 생략
	{
		std::ifstream in(cacheFile, std::ios::binary);
		CacheHeader h{};
		if (in.read(reinterpret_cast<char*>(&h), sizeof(h)) && h.magic == kCacheMagic &&
			h.flags == shaderFlags && h.stamp == stamp && h.size > 0)
		{
			std::vector<char> data(static_cast<size_t>(h.size));
			if (in.read(data.data(), data.size()))
			{
				Microsoft::WRL::ComPtr<ID3DBlob> blob;
				if (SUCCEEDED(D3DCreateBlob(data.size(), blob.GetAddressOf())))
				{
					memcpy(blob->GetBufferPointer(), data.data(), data.size());
					outBlob = blob;
					return S_OK;
				}
			}
		}
	}

	HRESULT hr = ::D3DCompileFromFile(filename.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, nullptr,
		"fx_5_0", shaderFlags, 0, outBlob.GetAddressOf(), outMsgs.GetAddressOf());

	if (SUCCEEDED(hr) && outBlob)
	{
		fs::create_directories(cacheDir, ec);
		std::ofstream out(cacheFile, std::ios::binary | std::ios::trunc);
		if (out)
		{
			CacheHeader h{ kCacheMagic, shaderFlags, stamp, outBlob->GetBufferSize() };
			out.write(reinterpret_cast<const char*>(&h), sizeof(h));
			out.write(static_cast<const char*>(outBlob->GetBufferPointer()), outBlob->GetBufferSize());
		}
	}
	return hr;
}
