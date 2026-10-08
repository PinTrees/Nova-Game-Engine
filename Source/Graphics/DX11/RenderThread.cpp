#include "pch.h"
#include "RenderThread.h"

#if defined(NOVA_ANDROID) || defined(NOVA_WEB)
// 안드로이드 · 웹: DirectX 11 이 없다 — 렌더 스레드 없음
namespace RenderThread
{
	bool Supported() { return false; }
	uint64_t RecordingSerial() { return 0; }
	uint64_t CompletedSerial() { return 0; }
	bool Enabled() { return false; }
	bool SetEnabled(bool) { return false; }
	void InitFromSettings() {}
	void Shutdown() {}
	void AfterPresent() {}
	void SubmitFrame(IDXGISwapChain*, unsigned, unsigned) {}
	void QueuePresent(IDXGISwapChain*, unsigned, unsigned) {}
	void Flush() {}
	void Sync() {}
	nlohmann::json Info() { return { { "supported", false } }; }
	void RegisterEditor() {}
}
#else
#include "LockFree.h"
#include "Profiler.h"
#include "CliServer.h"
#include "RenderPipelineSettings.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <dxgi.h>

// GfxDx11.cpp 의 DirectX 11 컨텍스트 감싸개 (DxContext) 를 deferred 로 바꾸는 도우미
namespace GfxDx11
{
	bool SetDeferred(GfxContext* ctx, bool on, std::string& error);
	ID3D11CommandList* FinishSegment(GfxContext* ctx);
	ID3D11DeviceContext* Immediate(GfxContext* ctx);
	ID3D11DeviceContext* Recording(GfxContext* ctx);
}
void ImGui_ImplDX11_SetDeviceContext(ID3D11DeviceContext* ctx);
void ImGui_ImplDX11_SetPresentHook(void (*present)(IDXGISwapChain*), void (*sync)());

namespace RenderThread
{
	namespace
	{
		struct PresentItem
		{
			IDXGISwapChain* Swap = nullptr;   // AddRef 해 둔다 (렌더 스레드가 Present 뒤 Release)
			unsigned Sync = 0, Flags = 0;
		};
		struct Item
		{
			ID3D11CommandList* List = nullptr;
			std::vector<PresentItem> Presents;
			uint64_t Serial = 0;
			bool Frame = false;
		};

		std::atomic<bool> s_Enabled{ false };
		std::thread s_Thread;
		std::atomic<bool> s_Quit{ false };
		LockFree::SpscRing<Item*> s_Queue{ 256 };     // 메인 → 렌더 스레드
		// 메인이 쓰는 값 · 렌더 스레드가 쓰는 값은 캐시 라인을 따로 (거짓 공유)
		alignas(64) std::atomic<uint32_t> s_Wake{ 0 };            // 렌더 스레드가 잠드는 원자 값 (메인이 올린다)
		alignas(64) std::atomic<uint64_t> s_Submitted{ 0 };       // 메인
		alignas(64) std::atomic<uint64_t> s_Completed{ 0 };       // 렌더 스레드
		uint64_t s_LastFrame = 0;                     // 메인만: 마지막으로 넘긴 프레임의 번호
		std::vector<PresentItem> s_Pending;           // 메인만: 이번 프레임에 함께 Present 할 창 (ImGui 뷰포트)
		ComPtr<ID3D11DeviceContext> s_Immediate;
		// 통계
		std::atomic<uint64_t> s_Frames{ 0 }, s_Syncs{ 0 }, s_Flushes{ 0 }, s_Lists{ 0 };
		std::atomic<double> s_ExecuteMs{ 0 }, s_PresentMs{ 0 }, s_MainWaitMs{ 0 };
		std::atomic<bool> s_PresentFailLogged{ false };
		alignas(64) std::atomic<int> s_Stage{ 0 };   // 렌더 스레드가 지금 하는 일 (0 쉼, 1 실행, 2 Present) — 오래 기다리면 Editor.log 에

		double MsSince(std::chrono::steady_clock::time_point t) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); }
		void Add(std::atomic<double>& a, double v) { double cur = a.load(); while (!a.compare_exchange_weak(cur, cur + v)) {} }

		void ThreadMain()
		{
			SetThreadDescription(GetCurrentThread(), L"Nova Render Thread");
			Profiler::SetThreadName("Render Thread");
			for (;;)
			{
				Item* item = nullptr;
				if (!s_Queue.Pop(item))
				{
					if (s_Quit.load(std::memory_order_acquire))
						return;
					const uint32_t e = s_Wake.load(std::memory_order_acquire);
					if (s_Queue.SizeApprox() == 0 && !s_Quit.load(std::memory_order_acquire))
						s_Wake.wait(e, std::memory_order_acquire);
					continue;
				}
				if (item->List)
				{
					Profiler::Scope scope(item->Frame ? "Render Thread: Execute Frame" : "Render Thread: Execute");
					const auto t0 = std::chrono::steady_clock::now();
					s_Stage = 1;
					s_Immediate->ExecuteCommandList(item->List, FALSE);
					item->List->Release();
					Add(s_ExecuteMs, MsSince(t0));
				}
				for (const PresentItem& p : item->Presents)
				{
					Profiler::Scope scope("Render Thread: Present");
					const auto t0 = std::chrono::steady_clock::now();
					s_Stage = 2;
					const HRESULT hr = p.Swap->Present(p.Sync, p.Flags);
					Add(s_PresentMs, MsSince(t0));
					if (FAILED(hr) && !s_PresentFailLogged.exchange(true))
						EditorLog::Write("RenderThread", "Present failed hr=0x%08X", (unsigned)hr);
					p.Swap->Release();
				}
				s_Stage = 0;
				const uint64_t serial = item->Serial;
				delete item;
				s_Completed.store(serial, std::memory_order_release);
				s_Completed.notify_all();
			}
		}

		// 메인: 지금까지 기록한 것 (+ Present) 을 넘긴다
		uint64_t Push(bool frame, std::vector<PresentItem>&& presents)
		{
			Item* item = new Item();
			item->List = GfxDx11::FinishSegment(Gfx::Context());
			item->Presents = std::move(presents);
			item->Frame = frame;
			// 번호는 넣기 전에 따로 둔다 — 넣는 순간 렌더 스레드가 실행하고 지울 수 있다 (item 을 다시 읽으면 안 된다)
			const uint64_t serial = s_Submitted.load(std::memory_order_relaxed) + 1;
			item->Serial = serial;
			s_Submitted.store(serial, std::memory_order_release);
			while (!s_Queue.Push(item))
				std::this_thread::yield();   // 256 개가 밀려 있다 (드묾) — 렌더 스레드를 기다린다
			++s_Lists;
			s_Wake.fetch_add(1, std::memory_order_release);
			s_Wake.notify_one();
			return serial;
		}

		void WaitFor(uint64_t serial)
		{
			if (serial == 0 || s_Completed.load(std::memory_order_acquire) >= serial)
				return;
			const auto t0 = std::chrono::steady_clock::now();
			bool logged = false;
			for (int spin = 0;; ++spin)
			{
				const uint64_t done = s_Completed.load(std::memory_order_acquire);
				if (done >= serial)
					break;
				if (!logged && MsSince(t0) > 2000.0)
				{
					logged = true;
					EditorLog::Write("RenderThread", "main waited 2 s for #%llu (completed %llu, queued %zu, render thread stage %d = %s)",
						(unsigned long long)serial, (unsigned long long)done, s_Queue.SizeApprox(), s_Stage.load(),
						s_Stage.load() == 1 ? "ExecuteCommandList" : s_Stage.load() == 2 ? "Present" : "idle");
				}
				std::this_thread::yield();
			}
			Add(s_MainWaitMs, MsSince(t0));
		}

		// D3D11 디버그 층 메시지 (오류 · 경고) 를 Editor.log 로 — 렌더 스레드가 켜진 동안 (deferred 의 잘못된 사용을 찾는다)
		ComPtr<ID3D11InfoQueue> s_Info;
		int s_InfoLogged = 0;
		void DrainDebugMessages()
		{
			if (!s_Info || s_InfoLogged >= 40)
				return;
			const UINT64 n = s_Info->GetNumStoredMessagesAllowedByRetrievalFilter();
			for (UINT64 i = 0; i < n && s_InfoLogged < 40; ++i)
			{
				SIZE_T size = 0;
				if (FAILED(s_Info->GetMessage(i, nullptr, &size)) || size == 0)
					continue;
				std::vector<char> buf(size);
				auto* m = reinterpret_cast<D3D11_MESSAGE*>(buf.data());
				// 알려진 것은 뺀다: #3146064 (deferred UpdateSubresource box — 우회함)
				if (SUCCEEDED(s_Info->GetMessage(i, m, &size)) && m->Severity <= D3D11_MESSAGE_SEVERITY_WARNING && (UINT)m->ID != 3146064u)
				{
					EditorLog::Write("RenderThread", "D3D11 %s #%d: %.*s", m->Severity <= D3D11_MESSAGE_SEVERITY_ERROR ? "error" : "warning", (int)m->ID, (int)m->DescriptionByteLength, m->pDescription);
					++s_InfoLogged;
				}
			}
			s_Info->ClearStoredMessages();
		}

		void PresentHook(IDXGISwapChain* swap) { QueuePresent(swap, 0, 0); }
		void SyncHook() { Sync(); }
	}

	bool Supported()
	{
		GfxDevice* dev = Gfx::Device();
		return dev && dev->Api() == GfxApi::DirectX11 && dev->Native() != nullptr;
	}

	bool Enabled() { return s_Enabled.load(std::memory_order_acquire); }
	uint64_t RecordingSerial() { return s_Submitted.load(std::memory_order_acquire) + 1; }
	uint64_t CompletedSerial() { return s_Completed.load(std::memory_order_acquire); }

	bool SetEnabled(bool on)
	{
		if (on == Enabled())
			return true;
		GfxContext* ctx = Gfx::Context();
		std::string error;
		if (on)
		{
			if (!Supported())
			{
				EditorLog::Write("RenderThread", "not supported on this graphics API (DirectX 11 only)");
				return false;
			}
			s_Immediate = GfxDx11::Immediate(ctx);
			if (!s_Immediate || !GfxDx11::SetDeferred(ctx, true, error))
			{
				EditorLog::Write("RenderThread", "could not start: %s", error.c_str());
				s_Immediate.Reset();
				return false;
			}
			ImGui_ImplDX11_SetDeviceContext(GfxDx11::Recording(ctx));
			{
				ComPtr<ID3D11Device> dev;
				s_Immediate->GetDevice(dev.GetAddressOf());
				s_Info.Reset();
				if (dev)
					dev.As(&s_Info);   // 디버그 층이 있을 때만 (Debug 빌드)
				if (s_Info)
					s_Info->ClearStoredMessages();
			}
			ImGui_ImplDX11_SetPresentHook(PresentHook, SyncHook);
			s_Quit = false;
			s_Enabled = true;
			s_Thread = std::thread(ThreadMain);
			EditorLog::Write("RenderThread", "on: commands recorded on a deferred context, executed + presented on the render thread");
			return true;
		}
		// 끄기: 남은 것을 다 실행하고 immediate 로 되돌린다
		Sync();
		s_Quit = true;
		s_Wake.fetch_add(1, std::memory_order_release);
		s_Wake.notify_all();
		if (s_Thread.joinable())
			s_Thread.join();
		s_Enabled = false;
		ImGui_ImplDX11_SetPresentHook(nullptr, nullptr);
		ImGui_ImplDX11_SetDeviceContext(s_Immediate.Get());
		GfxDx11::SetDeferred(ctx, false, error);
		s_Immediate.Reset();
		s_LastFrame = 0;
		EditorLog::Write("RenderThread", "off");
		return true;
	}

	void InitFromSettings()
	{
		char v[8] = {};
		const bool forced = ::GetEnvironmentVariableA("NOVA_RENDER_THREAD", v, sizeof(v)) > 0;
		const bool want = forced ? v[0] == '1' : RenderPipelineSettings::MultithreadedRendering();
		if (want && Supported())
			SetEnabled(true);
	}

	void Shutdown() { SetEnabled(false); }

	void AfterPresent()
	{
		static const bool s_Env = [] { char v[8] = {}; return ::GetEnvironmentVariableA("NOVA_D3D11_DEBUGLOG", v, sizeof(v)) > 0 && v[0] == '1'; }();
		if (!s_Env || Enabled())
			return;   // 렌더 스레드면 SubmitFrame 이 한다
		if (!s_Info && Supported())
			if (auto* dev = static_cast<ID3D11Device*>(Gfx::Device()->Native()))
				dev->QueryInterface(IID_PPV_ARGS(s_Info.GetAddressOf()));
		DrainDebugMessages();
	}

	void SubmitFrame(IDXGISwapChain* swap, unsigned syncInterval, unsigned flags)
	{
		if (!Enabled())
			return;
		std::vector<PresentItem> presents;
		presents.swap(s_Pending);
		if (swap)
		{
			swap->AddRef();
			presents.push_back({ swap, syncInterval, flags });
		}
		// 핑퐁: 앞 프레임 (렌더 스레드가 실행 중) 이 끝날 때까지만 기다린다 — 메인은 한 프레임까지만 앞선다
		{
			PROFILE_SCOPE("RenderThread.WaitPreviousFrame");
			WaitFor(s_LastFrame);
		}
		s_LastFrame = Push(true, std::move(presents));
		++s_Frames;
		DrainDebugMessages();
	}

	void QueuePresent(IDXGISwapChain* swap, unsigned syncInterval, unsigned flags)
	{
		if (!swap)
			return;
		if (!Enabled())
		{
			swap->Present(syncInterval, flags);
			return;
		}
		swap->AddRef();
		s_Pending.push_back({ swap, syncInterval, flags });
	}

	void Flush()
	{
		if (!Enabled())
			return;
		++s_Flushes;
		Push(false, {});
	}

	void Sync()
	{
		if (!Enabled())
			return;
		PROFILE_SCOPE("RenderThread.Sync");
		++s_Syncs;
		std::vector<PresentItem> presents;
		presents.swap(s_Pending);   // 보통 비어 있다
		WaitFor(Push(false, std::move(presents)));
	}

	nlohmann::json Info()
	{
		const uint64_t frames = s_Frames.load();
		return { { "supported", Supported() }, { "enabled", Enabled() }, { "frames", frames }, { "commandLists", s_Lists.load() },
			{ "syncs", s_Syncs.load() }, { "flushes", s_Flushes.load() }, { "submitted", s_Submitted.load() }, { "completed", s_Completed.load() },
			{ "executeMsPerFrame", frames ? s_ExecuteMs.load() / frames : 0.0 }, { "presentMsPerFrame", frames ? s_PresentMs.load() / frames : 0.0 },
			{ "mainWaitMsPerFrame", frames ? s_MainWaitMs.load() / frames : 0.0 } };
	}

	void RegisterEditor()
	{
		CliServer::Register("renderthread", "Render thread (Multithreaded Rendering, DirectX 11): {op: info|set|reset, enabled?: bool (saved in Project Settings)}",
			[](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
				const std::string op = args.value("op", std::string("info"));
				if (op == "set")
				{
					if (args.contains("enabled"))
					{
						const bool on = args["enabled"].is_boolean() ? args["enabled"].get<bool>() : args["enabled"].dump() != "false";
						RenderPipelineSettings::SetMultithreadedRendering(on);
						if (!SetEnabled(on) && on)
						{
							error = "render thread could not start (DirectX 11 only — see Editor.log)";
							return false;
						}
					}
				}
				else if (op == "reset")
				{
					s_Frames = 0; s_Syncs = 0; s_Flushes = 0; s_Lists = 0;
					s_ExecuteMs = 0; s_PresentMs = 0; s_MainWaitMs = 0;
				}
				else if (op != "info")
				{
					error = "unknown op '" + op + "' (info, set, reset)";
					return false;
				}
				result = Info();
				result["setting"] = RenderPipelineSettings::MultithreadedRendering();
				return true;
			});
	}
}
#endif
