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
	// 엔진 셰이더 폴더 (작업 폴더 = Binaries). 패키지 셰이더가 #include "32. InstancedBasic.fx" 처럼 엔진 셰이더를 포함할 때 찾는 곳
	fs::path EngineShaderDir()
	{
		std::error_code ec;
		return fs::absolute(L"../Shaders", ec);
	}

	// #include 찾기: 포함하는 파일의 폴더 → 맨 위 파일의 폴더 → 엔진 Shaders 폴더
	//  (D3D_COMPILE_STANDARD_FILE_INCLUDE 는 패키지 폴더에서 엔진 셰이더 안의 #include 를 찾지 못했다)
	class ShaderInclude : public ID3DInclude
	{
	public:
		explicit ShaderInclude(const fs::path& top) : m_Top(top.parent_path()), m_Engine(EngineShaderDir()) {}
		HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* bytes) override
		{
			fs::path base = m_Top;
			if (parent)
				if (auto it = m_DirOf.find(parent); it != m_DirOf.end())
					base = it->second;
			const fs::path rel = fs::path(string_to_wstring(name));
			for (const fs::path& dir : { base, m_Top, m_Engine })
			{
				const fs::path file = dir / rel;
				std::ifstream in(file, std::ios::binary);
				if (!in)
					continue;
				std::vector<char> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				char* mem = new char[buf.size() + 1];
				memcpy(mem, buf.data(), buf.size());
				mem[buf.size()] = 0;
				m_DirOf[mem] = file.parent_path();
				*data = mem;
				*bytes = (UINT)buf.size();
				return S_OK;
			}
			return E_FAIL;
		}
		HRESULT __stdcall Close(LPCVOID data) override
		{
			m_DirOf.erase(data);
			delete[] static_cast<const char*>(data);
			return S_OK;
		}
	private:
		fs::path m_Top, m_Engine;
		std::map<LPCVOID, fs::path> m_DirOf;
	};
}

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
			fs::path inc = file.parent_path() / string_to_wstring(line.substr(a + 1, b - a - 1));
			std::error_code ec;
			if (!fs::exists(inc, ec))
				inc = EngineShaderDir() / string_to_wstring(line.substr(a + 1, b - a - 1));   // 패키지 셰이더 → 엔진 셰이더
			CollectStamp(inc, visited, h, deps);
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

namespace
{
	std::mutex s_MsgLock;
	std::mutex s_DebugCompileLock;   // 디버그 정보 컴파일 · 내부 오류 다시 하기는 한 번에 하나
	std::map<std::wstring, std::string> s_LastMsgs;
}

std::string ShaderCache::LastMessages(const std::wstring& filename)
{
	std::lock_guard<std::mutex> lock(s_MsgLock);
	auto it = s_LastMsgs.find(fs::path(filename).lexically_normal().wstring());
	return it != s_LastMsgs.end() ? it->second : std::string();
}

HRESULT ShaderCache::CompileEffect(const std::wstring& filename, UINT shaderFlags,
	Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, Microsoft::WRL::ComPtr<ID3DBlob>& outMsgs)
{
	fs::path src(filename);
	std::error_code ec;
	fs::path cacheDir = fs::path(L"ShaderCache");
	// 컴파일 플래그도 이름에 넣는다: Debug(최적화 끔)와 Release(최적화) 캐시가 서로 덮어쓰지 않게
	fs::path cacheFile = cacheDir / (src.stem().wstring() + L"_" + std::to_wstring(std::hash<std::wstring>{}(src.lexically_normal().wstring())) +
		L"_f" + std::to_wstring(shaderFlags) + L".fxo");

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
	ShaderInclude include(src);
	HRESULT hr;
	{
		// 디버그 정보 (D3D10_SHADER_DEBUG) 를 넣는 컴파일은 한 번에 하나: 여러 스레드가 함께 하면 컴파일러의 PDB 쓰기가
		//  깨져 ("failed to create inline type info in PDB") 프로세스가 abort() 로 죽는다
		std::unique_lock<std::mutex> serial(s_DebugCompileLock, std::defer_lock);
		if (shaderFlags & D3D10_SHADER_DEBUG)
			serial.lock();
		hr = ::D3DCompileFromFile(filename.c_str(), nullptr, &include, nullptr,
			"fx_5_0", shaderFlags, 0, outBlob.GetAddressOf(), outMsgs.GetAddressOf());
	}
	// 컴파일러 내부 오류 (메모리 · PDB) 는 다른 컴파일과 겹쳐서 날 수 있다 → 혼자서 한 번 더
	if (FAILED(hr) && outMsgs && (strstr((const char*)outMsgs->GetBufferPointer(), "internal error") != nullptr ||
		strstr((const char*)outMsgs->GetBufferPointer(), "out of memory") != nullptr))
	{
		std::lock_guard<std::mutex> serial(s_DebugCompileLock);
		EditorLog::Write("Shader", "retry %s alone (compiler internal error while compiling in parallel)", wstring_to_string(src.filename().wstring()).c_str());
		outBlob.Reset();
		outMsgs.Reset();
		ShaderInclude retryInclude(src);
		hr = ::D3DCompileFromFile(filename.c_str(), nullptr, &retryInclude, nullptr,
			"fx_5_0", shaderFlags, 0, outBlob.GetAddressOf(), outMsgs.GetAddressOf());
	}
	EditorLog::Write("Shader", "compiled %s in %llu ms (hr=0x%08X)%s%s", wstring_to_string(src.filename().wstring()).c_str(), ::GetTickCount64() - compileStart, (unsigned)hr,
		outMsgs ? "\n" : "", outMsgs ? (const char*)outMsgs->GetBufferPointer() : "");
	{
		std::lock_guard<std::mutex> lock(s_MsgLock);
		s_LastMsgs[src.lexically_normal().wstring()] = outMsgs ? std::string((const char*)outMsgs->GetBufferPointer(), outMsgs->GetBufferSize()) : (FAILED(hr) ? "compile failed" : "");
	}

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
	// Debug 빌드: 최적화만 끈다. 셰이더 디버그 정보 (PIX 에서 HLSL 줄 단위 디버깅) 는 NOVA_SHADER_DEBUG=1 일 때만 —
	//  큰 이펙트 (32 · 58) 를 디버그 정보와 함께 여러 스레드에서 컴파일하면 컴파일러가 abort() 했다 (켜면 한 번에 하나씩)
	flags |= D3D10_SHADER_SKIP_OPTIMIZATION;
	static const bool s_ShaderDebug = [] { char v[8] = {}; return ::GetEnvironmentVariableA("NOVA_SHADER_DEBUG", v, sizeof(v)) > 0 && v[0] == '1'; }();
	if (s_ShaderDebug)
		flags |= D3D10_SHADER_DEBUG;
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
