#pragma once
#include "LockFree.h"
#include "Profiler.h"
#include <atomic>
#include <chrono>
#include <thread>

// 렌더 스레드 (DirectX 12 · Vulkan — docs/RENDER_THREAD.md): 메인이 기록을 마친 큐 작업 (제출 · 신호 · 기다림 · Present) 을
//  넣은 순서대로 실행하는 스레드 하나. 메인 → 무잠금 SPSC 링 → 렌더 스레드
//  - 작업마다 번호 (넣은 순서). 메인은 그 번호까지 실행됐는지만 기다린다 (WaitFor · Sync) — 잠금 없음
//  - Task 는 복사할 수 있는 작은 값 (포인터 · 핸들 · 숫자) 이고 Kind (0 ~ 7) 를 갖는다. 실행 함수는 백엔드가 준다
//  - 통계: 종류마다 실행 수 · 시간, 메인이 기다린 시간
template <typename Task>
class SubmitThread
{
public:
	using RunFn = void (*)(void* owner, Task& task);   // 렌더 스레드에서
	static constexpr int kKinds = 8;

	SubmitThread() = default;
	SubmitThread(const SubmitThread&) = delete;
	SubmitThread& operator=(const SubmitThread&) = delete;
	~SubmitThread() { Stop(); }

	bool Running() const { return m_Thread.joinable(); }

	void Start(const char* profilerName, RunFn run, void* owner)
	{
		if (Running())
			return;
		m_Run = run;
		m_Owner = owner;
		m_Name = profilerName;
		m_Quit.store(false, std::memory_order_relaxed);
		m_Thread = std::thread([this] { ThreadMain(); });
	}

	// 남은 작업을 다 실행하고 멈춘다
	void Stop()
	{
		if (!Running())
			return;
		Sync();
		m_Quit.store(true, std::memory_order_release);
		Wake();
		m_Thread.join();
	}

	// 메인: 작업을 넣고 그 번호를 돌려준다
	uint64_t Push(const Task& task)
	{
		Task t = task;
		const uint64_t serial = m_Pushed.load(std::memory_order_relaxed) + 1;
		t.Serial = serial;
		while (!m_Ring.Push(t))
			std::this_thread::yield();   // 링이 가득 (드묾) — 렌더 스레드를 기다린다
		m_Pushed.store(serial, std::memory_order_release);
		Wake();
		return serial;
	}

	// 메인: serial 번 작업까지 실행될 때까지 (오래 걸리면 렌더 스레드가 무엇을 하던 중인지 Editor.log 에)
	void WaitFor(uint64_t serial)
	{
		if (serial == 0 || m_Done.load(std::memory_order_acquire) >= serial)
			return;
		const auto t0 = std::chrono::steady_clock::now();
		bool logged = false;
		for (;;)
		{
			const uint64_t done = m_Done.load(std::memory_order_acquire);
			if (done >= serial)
				break;
			if (!logged && MsSince(t0) > 2000.0)
			{
				logged = true;
				EditorLog::Write("RenderThread", "main waited 2 s for task #%llu (done %llu, queued %zu, render thread running kind %d)",
					(unsigned long long)serial, (unsigned long long)done, m_Ring.SizeApprox(), m_Stage.load());
			}
			std::this_thread::yield();
		}
		Add(m_WaitMs, MsSince(t0));
		m_Waits.fetch_add(1, std::memory_order_relaxed);
	}
	void Sync() { WaitFor(m_Pushed.load(std::memory_order_relaxed)); }

	uint64_t Pushed() const { return m_Pushed.load(std::memory_order_acquire); }
	uint64_t Done() const { return m_Done.load(std::memory_order_acquire); }
	uint64_t Count(int kind) const { return m_Count[kind].load(std::memory_order_relaxed); }
	double Ms(int kind) const { return m_Ms[kind].load(std::memory_order_relaxed); }
	double MainWaitMs() const { return m_WaitMs.load(std::memory_order_relaxed); }
	uint64_t MainWaits() const { return m_Waits.load(std::memory_order_relaxed); }
	void ResetStats()
	{
		for (int i = 0; i < kKinds; ++i) { m_Count[i] = 0; m_Ms[i] = 0.0; }
		m_WaitMs = 0.0;
		m_Waits = 0;
	}

private:
	static double MsSince(std::chrono::steady_clock::time_point t) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); }
	static void Add(std::atomic<double>& a, double v) { double cur = a.load(std::memory_order_relaxed); while (!a.compare_exchange_weak(cur, cur + v, std::memory_order_relaxed)) {} }

	void Wake()
	{
		m_Wake.fetch_add(1, std::memory_order_release);
		m_Wake.notify_one();
	}

	void ThreadMain()
	{
#ifdef _WIN32
		SetThreadDescription(GetCurrentThread(), L"Nova Render Thread");
#endif
		Profiler::SetThreadName("Render Thread");
		for (;;)
		{
			Task t;
			if (!m_Ring.Pop(t))
			{
				if (m_Quit.load(std::memory_order_acquire))
					return;
				const uint32_t e = m_Wake.load(std::memory_order_acquire);
				if (m_Ring.SizeApprox() == 0 && !m_Quit.load(std::memory_order_acquire))
					m_Wake.wait(e, std::memory_order_acquire);
				continue;
			}
			const int kind = (t.Kind >= 0 && t.Kind < kKinds) ? t.Kind : 0;
			{
				Profiler::Scope scope(m_Name);
				const auto t0 = std::chrono::steady_clock::now();
				m_Stage.store(kind, std::memory_order_relaxed);
				m_Run(m_Owner, t);
				m_Stage.store(-1, std::memory_order_relaxed);
				Add(m_Ms[kind], MsSince(t0));
				m_Count[kind].fetch_add(1, std::memory_order_relaxed);
			}
			m_Done.store(t.Serial, std::memory_order_release);
		}
	}

	RunFn m_Run = nullptr;
	void* m_Owner = nullptr;
	const char* m_Name = "Render Thread";
	std::thread m_Thread;
	LockFree::SpscRing<Task> m_Ring{ 1024 };
	// 메인이 쓰는 값 · 렌더 스레드가 쓰는 값은 캐시 라인을 따로 (거짓 공유)
	alignas(64) std::atomic<uint64_t> m_Pushed{ 0 };   // 메인
	alignas(64) std::atomic<uint32_t> m_Wake{ 0 };     // 메인이 올린다 — 렌더 스레드가 잠드는 값
	std::atomic<bool> m_Quit{ false };
	alignas(64) std::atomic<uint64_t> m_Done{ 0 };     // 렌더 스레드
	std::atomic<int> m_Stage{ -1 };
	std::atomic<uint64_t> m_Count[kKinds] = {};
	std::atomic<double> m_Ms[kKinds] = {};
	alignas(64) std::atomic<double> m_WaitMs{ 0.0 };   // 메인
	std::atomic<uint64_t> m_Waits{ 0 };
};
