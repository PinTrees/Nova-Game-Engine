#include "pch.h"
#include "AndroidEngine.h"
#include "MemoryStats.h"
#include "ShaderCache.h"
#include <android/log.h>
#include <fstream>

// Windows 전용 엔진 파일의 안드로이드 판: EditorLog (DbgHelp) · MemoryStats (psapi · DXGI) · ShaderCache (D3DCompile)

// ---- EditorLog: logcat (태그 NOVA) + 앱 파일 폴더의 Logs/Editor.log
namespace
{
	std::mutex s_LogLock;
	std::chrono::steady_clock::time_point s_LogStart = std::chrono::steady_clock::now();
	std::string LogPath() { return NovaAndroid::FilesDir().empty() ? std::string() : NovaAndroid::FilesDir() + "/Logs/Editor.log"; }
}

namespace EditorLog
{
	void Init()
	{
		const std::string path = LogPath();
		if (path.empty()) return;
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
		std::filesystem::rename(path, std::filesystem::path(path).parent_path() / "Editor-prev.log", ec);
		s_LogStart = std::chrono::steady_clock::now();
	}
	void Shutdown() {}
	void Heartbeat() {}

	void Write(const char* category, const char* format, ...)
	{
		char msg[4096];
		va_list ap;
		va_start(ap, format);
		vsnprintf(msg, sizeof(msg), format, ap);
		va_end(ap);
		__android_log_print(ANDROID_LOG_INFO, "NOVA", "[%s] %s", category, msg);
		const std::string path = LogPath();
		if (path.empty()) return;
		const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_LogStart).count();
		std::lock_guard<std::mutex> lock(s_LogLock);
		std::ofstream out(path, std::ios::app);
		char head[64];
		snprintf(head, sizeof(head), "[%8.3f] [%s] ", t, category);
		out << head << msg << "\n";
	}

	std::wstring GetFilePath() { return string_to_wstring(LogPath()); }
	void OpenInEditor() {}
}

// ---- MemoryStats: 자원 크기는 설명에서, 프로세스는 /proc/self/status, GPU 사용량은 알 수 없음 (0)
namespace MemoryStats
{
	size_t ResourceBytes(GfxResource* resource)
	{
		if (!resource) return 0;
		D3D11_RESOURCE_DIMENSION dim;
		resource->GetType(&dim);
		auto texBytes = [](DXGI_FORMAT f, size_t w, size_t h, size_t d, size_t mips, size_t layers) {
			size_t total = 0;
			for (size_t m = 0; m < (std::max)(mips, size_t(1)); ++m)
			{
				size_t row = 0, slice = 0;
				DirectX::ComputePitch(f, (std::max)(size_t(1), w >> m), (std::max)(size_t(1), h >> m), row, slice);
				total += slice * (std::max)(size_t(1), d >> m);
			}
			return total * (std::max)(layers, size_t(1));
		};
		switch (dim)
		{
		case D3D11_RESOURCE_DIMENSION_BUFFER: { D3D11_BUFFER_DESC d; static_cast<GfxBuffer*>(resource)->GetDesc(&d); return d.ByteWidth; }
		case D3D11_RESOURCE_DIMENSION_TEXTURE1D: { D3D11_TEXTURE1D_DESC d; static_cast<GfxTexture1D*>(resource)->GetDesc(&d); return texBytes(d.Format, d.Width, 1, 1, d.MipLevels, d.ArraySize); }
		case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
		{
			D3D11_TEXTURE2D_DESC d;
			static_cast<GfxTexture2D*>(resource)->GetDesc(&d);
			return texBytes(d.Format, d.Width, d.Height, 1, d.MipLevels, d.ArraySize) * (std::max)(1u, d.SampleDesc.Count);
		}
		case D3D11_RESOURCE_DIMENSION_TEXTURE3D: { D3D11_TEXTURE3D_DESC d; static_cast<GfxTexture3D*>(resource)->GetDesc(&d); return texBytes(d.Format, d.Width, d.Height, d.Depth, d.MipLevels, 1); }
		default: return 0;
		}
	}

	size_t ViewBytes(GfxView* view)
	{
		if (!view) return 0;
		ComPtr<GfxResource> r;
		view->GetResource(r.GetAddressOf());
		return ResourceBytes(r.Get());
	}

	void QueryProcess(Report& report)
	{
		std::ifstream in("/proc/self/status");
		std::string line;
		while (std::getline(in, line))
		{
			unsigned long long kb = 0;
			if (sscanf(line.c_str(), "VmRSS: %llu kB", &kb) == 1) report.WorkingSet = (size_t)kb * 1024;
			else if (sscanf(line.c_str(), "RssAnon: %llu kB", &kb) == 1) report.ProcessPrivate = (size_t)kb * 1024;
		}
	}

	std::string FormatBytes(size_t bytes)
	{
		char buf[32];
		if (bytes >= (1ull << 30)) snprintf(buf, sizeof(buf), "%.2f GB", bytes / 1073741824.0);
		else if (bytes >= (1ull << 20)) snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1048576.0);
		else if (bytes >= 1024) snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
		else snprintf(buf, sizeof(buf), "%zu B", bytes);
		return buf;
	}
}

// ---- ShaderCache: 안드로이드에는 셰이더 컴파일러가 없다 (셰이더는 PC 에서 미리 GLSL ES 로 — nova android shaders)
namespace ShaderCache
{
	HRESULT CompileEffect(const std::wstring&, UINT, Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, Microsoft::WRL::ComPtr<ID3DBlob>& outMsgs)
	{
		outBlob.Reset();
		outMsgs.Reset();
		return E_NOTIMPL;
	}
	UINT DefaultFlags() { return 0; }
	void PrecompileParallel(const std::vector<std::wstring>&, UINT) {}
	std::string LastMessages(const std::wstring&) { return "shaders are not compiled on Android (export them on the PC)"; }
}
