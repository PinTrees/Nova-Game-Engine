#pragma once
#include <atomic>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <exception>
#include <future>
#include <memory>
#include <new>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <nlohmann/json.hpp>

// NOVA Job System — 작업 훔치기 (work-stealing) + 파이버 (docs/JOB_SYSTEM.md, 동시성 로드맵 1 단계).
//  - 일꾼 스레드 = 코어 − 1 (메인 스레드는 기다리는 동안 돕는다). 스레드마다 Chase-Lev 덱 (자기 일은 LIFO, 남의 일은 위에서 훔친다)
//    + 우선순위마다 공용 MPMC 링 (High · Normal · Background). 모두 무잠금 (LockFree.h), 일이 없으면 원자 값 위에서 잠든다
//  - Counter: 잡 수를 센다. Wait(counter) 는
//      · 일꾼의 잡 안 (Windows): 파이버를 내려놓고 (대기 목록에 올리고) 일꾼은 다른 잡을 돌린다 — 0 이 되면 아무 일꾼이 이어서 돌린다
//      · 메인 스레드 · 파이버 없는 플랫폼: 기다리는 동안 다른 잡 (High · Normal) 을 직접 돌린다 (돕기)
//  - 잡 안에서 지켜야 할 것: 파이버는 기다린 뒤 다른 스레드에서 이어질 수 있다 → Wait 를 사이에 둔 thread_local 값을 믿지 않는다.
//    엔진 객체 (GameObject · 컴포넌트) 를 만들거나 지우지 않는다 (Unity 의 Job 과 같은 규칙)
//  - 웹 (스레드 없는 wasm) · SetInline(true): 잡을 부른 자리에서 바로 돌린다
namespace Jobs
{
	enum class Priority : uint8_t { High = 0, Normal = 1, Background = 2 };

	// 남은 잡 수. 0 이 되면 기다리던 파이버를 깨운다. 같은 Counter 를 여러 번 (0 → n → 0) 써도 된다
	struct Counter
	{
		std::atomic<int32_t> Value{ 0 };
		std::atomic<int32_t> Busy{ 0 };      // 이 Counter 를 아직 만지는 스레드 수 (끝난 뒤 Counter 가 사라져도 안전하게)
		std::atomic<void*> Waiters{ nullptr };
		bool Done() const { return Value.load(std::memory_order_acquire) == 0 && Busy.load(std::memory_order_acquire) == 0; }
	};

	struct Job;
	using JobFn = void (*)(Job*);

	// 잡 하나: 함수 + 64 바이트 안의 람다 (더 크면 힙). 풀에서 꺼내 쓰고 돌려준다
	struct Job
	{
		JobFn Invoke = nullptr;
		JobFn Destroy = nullptr;
		Counter* Cnt = nullptr;
		const char* Name = "Job";
		Priority Prio = Priority::Normal;
		bool Pooled = true;
		bool BackgroundSlot = false;   // Background 동시 실행 수에 셌다 (끝날 때 돌려준다)
		alignas(16) unsigned char Storage[64];
	};

	// 시작 · 끝 (App 이 부른다). workers < 0 = 코어 − 1. 웹은 0
	void Init(int workers = -1);
	void Shutdown();
	bool Initialized();

	int WorkerCount();
	bool FibersEnabled();
	int CurrentThread();       // 0 = 메인, 1 .. N = 일꾼, -1 = 그 밖 스레드
	bool IsWorkerThread();
	void SetInline(bool on);   // 비교 · 문제 찾기: 모든 잡을 부른 스레드에서 바로 (일꾼을 쓰지 않는다)
	bool Inline();

	Job* AllocateJob();
	void Submit(Job* job);
	void Wait(Counter& counter);

	// 잡 하나 넣기. counter 가 있으면 넣기 전에 1 올리고 끝나면 1 내린다
	template <class F>
	void Run(F&& fn, Counter* counter = nullptr, Priority prio = Priority::Normal, const char* name = "Job")
	{
		using Fn = std::decay_t<F>;
		Job* j = AllocateJob();
		j->Cnt = counter;
		j->Name = name;
		j->Prio = prio;
		if constexpr (sizeof(Fn) <= sizeof(j->Storage) && alignof(Fn) <= 16)
		{
			new (j->Storage) Fn(std::forward<F>(fn));
			j->Invoke = [](Job* x) { (*std::launder(reinterpret_cast<Fn*>(x->Storage)))(); };
			j->Destroy = [](Job* x) { std::launder(reinterpret_cast<Fn*>(x->Storage))->~Fn(); };
		}
		else
		{
			Fn* heap = new Fn(std::forward<F>(fn));
			memcpy(j->Storage, &heap, sizeof(heap));
			j->Invoke = [](Job* x) { Fn* p; memcpy(&p, x->Storage, sizeof(p)); (*p)(); };
			j->Destroy = [](Job* x) { Fn* p; memcpy(&p, x->Storage, sizeof(p)); delete p; };
		}
		if (counter)
			counter->Value.fetch_add(1, std::memory_order_relaxed);
		Submit(j);
	}

	// [0, count) 를 묶음으로 나눠 fn(begin, end). 부른 스레드도 한 몫을 돌리고 다 끝날 때까지 기다린다.
	//  minBatch: 묶음의 최소 크기 (작은 일을 너무 잘게 나누지 않게)
	template <class F>
	void ParallelFor(int count, int minBatch, F&& fn, const char* name = "ParallelFor", Priority prio = Priority::High)
	{
		if (count <= 0)
			return;
		const int threads = Inline() ? 1 : WorkerCount() + 1;
		int batch = (count + threads * 4 - 1) / (threads * 4);   // 스레드마다 4 묶음쯤 (훔치기로 고르게)
		if (batch < minBatch) batch = minBatch < 1 ? 1 : minBatch;
		if (threads <= 1 || batch >= count)
		{
			fn(0, count);
			return;
		}
		Counter counter;
		int begin = batch;
		for (; begin < count; begin += batch)
		{
			const int b = begin, e = (begin + batch < count) ? begin + batch : count;
			Run([&fn, b, e]() { fn(b, e); }, &counter, prio, name);
		}
		fn(0, batch);   // 첫 묶음은 여기서
		Wait(counter);
	}

	// 결과를 돌려주는 잡 (std::async 를 바꾼다 — 백그라운드 일: 씬 읽기 · 지형 생성 · 셰이더 컴파일).
	//  std::async 의 future 와 같이 지울 때 끝나기를 기다린다 (작업이 가리키는 것이 먼저 사라지지 않게)
	template <class R>
	class Future
	{
	public:
		struct State
		{
			Counter Done;
			std::conditional_t<std::is_void_v<R>, bool, std::optional<R>> Value{};
			std::exception_ptr Error;
		};
		Future() = default;
		explicit Future(std::shared_ptr<State> s) : m_S(std::move(s)) {}
		Future(Future&&) noexcept = default;
		Future& operator=(Future&& o) noexcept { if (this != &o) { wait(); m_S = std::move(o.m_S); } return *this; }
		~Future() { wait(); }

		bool valid() const { return m_S != nullptr; }
		bool Ready() const { return !m_S || m_S->Done.Done(); }
		void wait() const
		{
			if (!m_S || m_S->Done.Done())
				return;
			// 백그라운드 잡은 일꾼만 돌린다 — 메인은 돕지 않고 잠깐씩 쉬며 기다린다 (잡이 High 면 Wait 가 돕는다)
			while (!m_S->Done.Done())
				Wait(m_S->Done);
		}
		template <class Rep, class Per>
		std::future_status wait_for(const std::chrono::duration<Rep, Per>& d) const
		{
			const auto until = std::chrono::steady_clock::now() + d;
			while (!Ready())
			{
				if (std::chrono::steady_clock::now() >= until)
					return std::future_status::timeout;
				std::this_thread::sleep_for(std::chrono::microseconds(200));
			}
			return std::future_status::ready;
		}
		R get()
		{
			wait();
			std::shared_ptr<State> s = std::move(m_S);
			if (s->Error)
				std::rethrow_exception(s->Error);
			if constexpr (!std::is_void_v<R>)
				return std::move(*s->Value);
		}

	private:
		std::shared_ptr<State> m_S;
	};

	template <class F>
	auto Async(F&& fn, Priority prio = Priority::Background, const char* name = "Async") -> Future<std::invoke_result_t<std::decay_t<F>>>
	{
		using R = std::invoke_result_t<std::decay_t<F>>;
		auto s = std::make_shared<typename Future<R>::State>();
		Run([s, f = std::forward<F>(fn)]() mutable {
			try
			{
				if constexpr (std::is_void_v<R>) f();
				else s->Value.emplace(f());
			}
			catch (...) { s->Error = std::current_exception(); }
		}, &s->Done, prio, name);
		return Future<R>(std::move(s));
	}

	// 프레임마다 한 번 (App): 이 프레임에 돈 잡 수 → Profiler 통계 "Jobs/…", 검사용 합성 부하 (jobs set --load N)
	void OnFrame();
	void SetSyntheticLoad(int items);

	// 통계 (CLI jobs info · Profiler)
	nlohmann::json Info();
	void ResetStats();
	void RegisterEditor();   // CLI: nova jobs info|test|bench|set
}
