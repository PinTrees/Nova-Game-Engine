#include "pch.h"
#include "GfxD3D12Internal.h"

// DirectX 12 창 표시: 창마다 플립 스왑체인 (본 창 + ImGui 가 만든 OS 창).
//  엔진은 늘 텍스처 (RGBA8) 에 그리고 Present 가 스왑체인 버퍼로 복사한다 (Vulkan · GL 과 같은 방식 — 창 크기만큼, 형식이 같아 복사로 충분).
//  수직 동기 0 = 찢어짐 허용 (되면), 1 이상 = 동기. 본 창 기준으로 CPU 가 GPU 보다 2 프레임 넘게 앞서 가지 않는다
namespace GfxD3D12Impl
{
	Dev::Swap* Dev::SwapFor(HWND wnd, UINT width, UINT height)
	{
		auto it = Swaps.find(wnd);
		if (it != Swaps.end()) return it->second.get();
		auto s = std::make_unique<Swap>();
		s->Wnd = wnd;
		DXGI_SWAP_CHAIN_DESC1 sd = {};
		sd.Width = (std::max)(1u, width);
		sd.Height = (std::max)(1u, height);
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.SampleDesc.Count = 1;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = 3;
		sd.Scaling = DXGI_SCALING_STRETCH;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		sd.Flags = AllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
		ComPtr<IDXGISwapChain1> sc1;
		const HRESULT hr = Factory->CreateSwapChainForHwnd(Queue.Get(), wnd, &sd, nullptr, nullptr, &sc1);
		if (FAILED(hr) || FAILED(sc1.As(&s->Chain)))
		{
			char buf[64];
			snprintf(buf, sizeof(buf), "hr=0x%08X", (unsigned)hr);
			Once(std::string("swap-fail:") + buf, "CreateSwapChainForHwnd failed: %s", buf);
			s->Failed = true;
		}
		else
		{
			Factory->MakeWindowAssociation(wnd, DXGI_MWA_NO_ALT_ENTER);
			s->Width = sd.Width;
			s->Height = sd.Height;
			for (UINT i = 0; i < sd.BufferCount; ++i)
			{
				ComPtr<ID3D12Resource> b;
				s->Chain->GetBuffer(i, IID_PPV_ARGS(&b));
				s->Buffers.push_back(b);
			}
			EditorLog::Write("DX12", "swapchain %u x %u, %u buffers, flip discard%s%s", sd.Width, sd.Height, sd.BufferCount, AllowTearing ? ", tearing" : "",
				wnd == Window ? "" : " (window)");
		}
		Swap* p = s.get();
		Swaps[wnd] = std::move(s);
		return p;
	}

	void Dev::ReleaseWindow(HWND wnd)
	{
		auto it = Swaps.find(wnd);
		if (it == Swaps.end()) return;
		// 그 창의 버퍼를 쓰는 명령이 다 끝난 뒤 (창 닫기는 드물다)
		Submit(false);
		WaitSerial(Submitted);
		Swaps.erase(it);
	}

	void Dev::DestroyAllSwapchains()
	{
		Swaps.clear();
	}

	void Dev::PresentTo(Swap& s, Tex2D* source, int width, int height, int interval, bool pace)
	{
		if (Lost) return;
		if (Immediate) Immediate->FinishAsync();   // 프레임 끝: 컴퓨트 결과를 기다린 뒤 (다음 프레임이 그 자원을 다시 쓰기 전에)
		if (!s.Chain || s.Failed || !source || width <= 0 || height <= 0)
		{
			Submit(false);
			return;
		}
		if (s.Width != (UINT)width || s.Height != (UINT)height)
		{
			// 크기 바꾸기: 버퍼를 쓰는 명령이 다 끝난 뒤
			Submit(false);
			WaitSerial(Submitted);
			s.Buffers.clear();
			const HRESULT hr = s.Chain->ResizeBuffers(0, (UINT)width, (UINT)height, DXGI_FORMAT_UNKNOWN, AllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
			if (FAILED(hr))
			{
				EditorLog::Write("DX12", "ResizeBuffers %d x %d failed hr=0x%08X", width, height, (unsigned)hr);
				CheckRemoved(hr, "ResizeBuffers");
				s.Failed = true;
				return;
			}
			DXGI_SWAP_CHAIN_DESC1 sd;
			s.Chain->GetDesc1(&sd);
			for (UINT i = 0; i < sd.BufferCount; ++i)
			{
				ComPtr<ID3D12Resource> b;
				s.Chain->GetBuffer(i, IID_PPV_ARGS(&b));
				s.Buffers.push_back(b);
			}
			s.Width = (UINT)width;
			s.Height = (UINT)height;
		}
		const UINT index = s.Chain->GetCurrentBackBufferIndex();
		if (index >= s.Buffers.size()) { Submit(false); return; }
		ID3D12Resource* dst = s.Buffers[index].Get();

		// 텍스처 → 스왑체인 버퍼 (같은 RGBA8 — 복사)
		Image& src = source->I;
		if (Immediate)
		{
			Immediate->Transition(src.R, 0, 1, 0, 1, D3D12_RESOURCE_STATE_COPY_SOURCE);
			Immediate->FlushBarriers();
		}
		ID3D12GraphicsCommandList* cl = Cmd();
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = dst;
		b.Transition.Subresource = 0;
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		cl->ResourceBarrier(1, &b);
		D3D12_TEXTURE_COPY_LOCATION sl = {}, dl = {};
		sl.pResource = src.R.Res.Get();
		sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dl.pResource = dst;
		dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		const D3D12_BOX box = { 0, 0, 0, (std::min)(src.Width, s.Width), (std::min)(src.Height, s.Height), 1 };
		cl->CopyTextureRegion(&dl, 0, 0, 0, &sl, &box);
		std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
		cl->ResourceBarrier(1, &b);
		Submit(false);
		const uint64_t serial = Submitted;
		const UINT flags = interval <= 0 && AllowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0;
		const HRESULT hr = s.Chain->Present(interval > 0 ? 1 : 0, flags);
		if (FAILED(hr))
		{
			char buf[64];
			snprintf(buf, sizeof(buf), "hr=0x%08X", (unsigned)hr);
			Once(std::string("present-fail:") + buf, "Present failed: %s", buf);
			CheckRemoved(hr, "Present");
		}
		if (!pace) return;
		// 2 프레임 넘게 앞서 가지 않는다 (업로드 링 · 디스크립터 링이 끝없이 늘지 않게)
		Frames.push_back(serial);
		while (Frames.size() > 2)
		{
			WaitSerial(Frames.front());
			Frames.pop_front();
		}
	}
}

namespace GfxD3D12
{
	using namespace GfxD3D12Impl;

	void Present(GfxDevice* device, GfxTexture2D* backBuffer, int windowWidth, int windowHeight, int syncInterval)
	{
		auto* d = static_cast<Dev*>(device);
		Tex2D* t = backBuffer && backBuffer->Api() == GfxApi::DirectX12 ? static_cast<Tex2D*>(backBuffer) : nullptr;
		Dev::Swap* s = d->Window ? d->SwapFor(d->Window, (UINT)(std::max)(1, windowWidth), (UINT)(std::max)(1, windowHeight)) : nullptr;
		if (s) d->PresentTo(*s, t, windowWidth, windowHeight, syncInterval, true);
		else d->Submit(false);
	}

	bool PresentWindow(GfxDevice* device, HWND window, GfxTexture2D* texture, int width, int height, int syncInterval)
	{
		auto* d = static_cast<Dev*>(device);
		Dev::Swap* s = d->SwapFor(window, (UINT)(std::max)(1, width), (UINT)(std::max)(1, height));
		if (!s || s->Failed) return false;
		d->PresentTo(*s, texture && texture->Api() == GfxApi::DirectX12 ? static_cast<Tex2D*>(texture) : nullptr, width, height, syncInterval, false);
		return true;
	}

	void ReleaseWindow(GfxDevice* device, HWND window)
	{
		static_cast<Dev*>(device)->ReleaseWindow(window);
	}
}
