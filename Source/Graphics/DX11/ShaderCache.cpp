#include "pch.h"
#include "ShaderCache.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include "LoadingScreen.h"

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

	// 파일 하나의 내용 지문: 수정 시각 + 크기 + 경로
	uint64_t FileStamp(const fs::path& file)
	{
		std::error_code ec;
		const uint64_t t = static_cast<uint64_t>(fs::last_write_time(file, ec).time_since_epoch().count());
		const uint64_t n = static_cast<uint64_t>(fs::file_size(file, ec));
		uint64_t h = std::hash<std::wstring>{}(file.lexically_normal().wstring());
		h ^= t + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
		h ^= n + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
		return h;
	}

	// 셰이더 파일과 그 파일이 #include 하는 파일들(재귀)의 지문.
	// 폴더 전체가 아니라 실제 의존 파일만 보므로, .fx 하나를 고치면 그 파일과 그것을 include 하는 셰이더만 다시 컴파일된다.
	void CollectStamp(const fs::path& file, std::set<std::wstring>& visited, uint64_t& h, std::vector<std::wstring>& deps)
	{
		const std::wstring key = file.lexically_normal().wstring();
		if (!visited.insert(key).second)
			return;
		h ^= FileStamp(file) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
		deps.push_back(file.filename().wstring());
		std::ifstream in(file);
		std::string line;
		while (std::getline(in, line))
		{
			const size_t p = line.find("#include");
			if (p == std::string::npos || line.find_first_not_of(" \t") != p)
				continue;
			const size_t a = line.find('"', p), b = a == std::string::npos ? a : line.find('"', a + 1);
			if (a == std::string::npos || b == std::string::npos)
				continue;
			CollectStamp(file.parent_path() / string_to_wstring(line.substr(a + 1, b - a - 1)), visited, h, deps);
		}
	}

	uint64_t ComputeStamp(const fs::path& file, std::vector<std::wstring>& deps)
	{
		std::set<std::wstring> visited;
		uint64_t h = 0xCBF29CE484222325ull;
		CollectStamp(file, visited, h, deps);
		return h;
	}
}

HRESULT ShaderCache::CompileEffect(const std::wstring& filename, UINT shaderFlags,
	Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, Microsoft::WRL::ComPtr<ID3DBlob>& outMsgs)
{
	fs::path src(filename);
	std::error_code ec;
	fs::path cacheDir = fs::path(L"ShaderCache");
	fs::path cacheFile = cacheDir / (src.stem().wstring() + L"_" + std::to_wstring(std::hash<std::wstring>{}(src.lexically_normal().wstring())) + L".fxo");

	std::vector<std::wstring> deps;
	uint64_t stamp = ComputeStamp(src, deps);

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
					LoadingScreen::OnShader(filename, true);
					EditorLog::Write("Shader", "cache hit %s", wstring_to_string(src.filename().wstring()).c_str());
					memcpy(blob->GetBufferPointer(), data.data(), data.size());
					outBlob = blob;
					return S_OK;
				}
			}
		}
	}

	LoadingScreen::OnShader(filename, false);   // 컴파일은 오래 걸린다 (로딩 창에 표시)
	{
		std::string depList;
		for (const auto& d : deps) depList += (depList.empty() ? "" : ", ") + wstring_to_string(d);
		EditorLog::Write("Shader", "cache miss %s (depends on: %s)", wstring_to_string(src.filename().wstring()).c_str(), depList.c_str());
	}
	const ULONGLONG compileStart = ::GetTickCount64();
	HRESULT hr = ::D3DCompileFromFile(filename.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, nullptr,
		"fx_5_0", shaderFlags, 0, outBlob.GetAddressOf(), outMsgs.GetAddressOf());
	EditorLog::Write("Shader", "compiled %s in %llu ms (hr=0x%08X)%s%s", wstring_to_string(src.filename().wstring()).c_str(), ::GetTickCount64() - compileStart, (unsigned)hr,
		outMsgs ? "\n" : "", outMsgs ? (const char*)outMsgs->GetBufferPointer() : "");

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

UINT ShaderCache::DefaultFlags()
{
	UINT flags = 0;
#if defined( DEBUG ) || defined( _DEBUG )
	flags |= D3D10_SHADER_DEBUG;
	flags |= D3D10_SHADER_SKIP_OPTIMIZATION;
#endif
	return flags;
}

void ShaderCache::PrecompileParallel(const std::vector<std::wstring>& files, UINT shaderFlags)
{
	const ULONGLONG start = ::GetTickCount64();
	std::atomic<size_t> next = 0;
	const unsigned workers = (std::max)(1u, (std::min)((unsigned)files.size(), std::thread::hardware_concurrency()));
	std::vector<std::thread> threads;
	for (unsigned w = 0; w < workers; ++w)
		threads.emplace_back([&]() {
			for (size_t i = next++; i < files.size(); i = next++)
			{
				Microsoft::WRL::ComPtr<ID3DBlob> blob, msgs;
				CompileEffect(files[i], shaderFlags, blob, msgs);   // 캐시가 맞으면 읽기만, 아니면 컴파일해서 캐시에 쓴다
			}
		});
	for (auto& t : threads)
		t.join();
	EditorLog::Write("Shader", "precompile %zu effects on %u threads: %llu ms", files.size(), workers, ::GetTickCount64() - start);
}
