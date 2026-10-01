#include "pch.h"
#include "MemoryStats.h"
#include <psapi.h>
#include <dxgi1_4.h>

namespace MemoryStats
{
	size_t ResourceBytes(GfxResource* resource)
	{
		if (resource == nullptr)
			return 0;
		D3D11_RESOURCE_DIMENSION dim;
		resource->GetType(&dim);
		size_t bytes = 0;
		auto mipBytes = [](DXGI_FORMAT format, UINT w, UINT h) {
			size_t row = 0, slice = 0;
			if (FAILED(DirectX::ComputePitch(format, w, h, row, slice)))
				return (size_t)w * h * 4;
			return slice;
		};
		switch (dim)
		{
		case D3D11_RESOURCE_DIMENSION_BUFFER:
		{
			ComPtr<GfxBuffer> b;
			if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(b.GetAddressOf()))))
			{
				D3D11_BUFFER_DESC d;
				b->GetDesc(&d);
				bytes = d.ByteWidth;
			}
			break;
		}
		case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
		{
			ComPtr<GfxTexture2D> t;
			if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(t.GetAddressOf()))))
			{
				D3D11_TEXTURE2D_DESC d;
				t->GetDesc(&d);
				// 깊이 형식(타입 없는 R24G8 등)도 ComputePitch 가 처리한다
				for (UINT m = 0; m < d.MipLevels; ++m)
					bytes += mipBytes(d.Format, (std::max)(1u, d.Width >> m), (std::max)(1u, d.Height >> m));
				bytes *= d.ArraySize * (std::max)(1u, d.SampleDesc.Count);
			}
			break;
		}
		case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
		{
			ComPtr<GfxTexture3D> t;
			if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(t.GetAddressOf()))))
			{
				D3D11_TEXTURE3D_DESC d;
				t->GetDesc(&d);
				for (UINT m = 0; m < d.MipLevels; ++m)
					bytes += mipBytes(d.Format, (std::max)(1u, d.Width >> m), (std::max)(1u, d.Height >> m)) * (std::max)(1u, d.Depth >> m);
			}
			break;
		}
		case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
		{
			ComPtr<GfxTexture1D> t;
			if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(t.GetAddressOf()))))
			{
				D3D11_TEXTURE1D_DESC d;
				t->GetDesc(&d);
				for (UINT m = 0; m < d.MipLevels; ++m)
					bytes += mipBytes(d.Format, (std::max)(1u, d.Width >> m), 1);
				bytes *= d.ArraySize;
			}
			break;
		}
		default:
			break;
		}
		return bytes;
	}

	size_t ViewBytes(GfxView* view)
	{
		if (view == nullptr)
			return 0;
		ComPtr<GfxResource> res;
		view->GetResource(res.GetAddressOf());
		return ResourceBytes(res.Get());
	}

	void QueryProcess(Report& report)
	{
		PROCESS_MEMORY_COUNTERS_EX pmc = {};
		pmc.cb = sizeof(pmc);
		if (::GetProcessMemoryInfo(::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
		{
			report.ProcessPrivate = pmc.PrivateUsage;
			report.WorkingSet = pmc.WorkingSetSize;
		}
		// GPU: 장치의 어댑터에서 이 프로세스의 전용(로컬) 메모리 사용량
		static ComPtr<IDXGIAdapter3> s_Adapter;
		static bool s_Tried = false;
		if (!s_Tried)
		{
			s_Tried = true;
			ComPtr<IDXGIDevice> dxgiDevice;
			ComPtr<IDXGIAdapter> adapter;
			if (SUCCEEDED(Application::GetI()->GetDevice()->Native() && static_cast<IUnknown*>(Application::GetI()->GetDevice()->Native())->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf()))) &&
				SUCCEEDED(dxgiDevice->GetAdapter(adapter.GetAddressOf())))
				adapter->QueryInterface(IID_PPV_ARGS(s_Adapter.GetAddressOf()));
		}
		if (s_Adapter)
		{
			DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
			if (SUCCEEDED(s_Adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
			{
				report.GpuUsage = (size_t)info.CurrentUsage;
				report.GpuBudget = (size_t)info.Budget;
			}
		}
	}

	std::string FormatBytes(size_t bytes)
	{
		char buf[32];
		if (bytes >= 1024ull * 1024 * 1024)
			snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
		else if (bytes >= 1024 * 1024)
			snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
		else if (bytes >= 1024)
			snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
		else
			snprintf(buf, sizeof(buf), "%zu B", bytes);
		return buf;
	}
}
