#include "pch.h"
#include "JobSystem.h"
#include "LockFree.h"
#include "Profiler.h"
#include <thread>
#include <vector>
#include <memory>
#include <cmath>

#if defined(_MSC_VER) && !defined(NOVA_ANDROID) && !defined(NOVA_WEB)
#define NOVA_JOBS_FIBERS 1
#endif
#if defined(NOVA_WEB) && !defined(__EMSCRIPTEN_PTHREADS__)
#define NOVA_JOBS_NO_THREADS 1
#endif

#if defined(_MSC_VER)
#define NOVA_NOINLINE __declspec(noinline)
#include <intrin.h>
#else
#define NOVA_NOINLINE __attribute__((noinline))
#endif

// Job System — JobSystem.h 의 설명
namespace Jobs
{
	namespace
	{
		constexpr int kMaxWorkers = 63;
		constexpr size_t kDequeSize = 4096;
		constexpr size_t kGlobalSize = 65536;
		constexpr size_t kJobPool = 16384;
		constexpr int kFiberPool = 128;
		constexpr SIZE_T kFiberCommit = 64 * 1024;
		constexpr SIZE_T kFiberReserve = 1024 * 1024;   // 주소 공간만 (쓰는 만큼 커진다)

		struct FiberCtx;
		struct WaitNode
		{
			FiberCtx* Fiber = nullptr;
			WaitNode* Next = nullptr;
		};
		struct FiberCtx
		{
			void* Handle = nullptr;
			Job* Work = nullptr;
			WaitNode Node;
		};

		enum class PostKind : uint8_t { None, Finished, Wait };
		struct Post
		{
			PostKind Kind = PostKind::None;
			FiberCtx* Fiber = nullptr;
			Counter* Cnt = nullptr;
		};

		struct alignas(LockFree::kCacheLine) Worker
		{
			int Index = 0;
			LockFree::WorkStealingDeque<Job*> Deque{ kDequeSize };
			void* SchedulerFiber = nullptr;   // 이 스레드 자신의 파이버 (일꾼만)
			FiberCtx* Current = nullptr;      // 지금 이 스레드에서 도는 잡 파이버
			Post Pending;                     // 잡 파이버가 스케줄러로 돌아오며 남긴 일 (끝남 · 기다림)
			uint32_t Rng = 0x9E3779B9u;
			const char* ProfilerName = nullptr;   // Profiler Timeline 의 줄 이름 (메인 스레드에서 Intern)
			std::thread Thread;
			std::atomic<uint64_t> Executed{ 0 }, Stolen{ 0 }, Sleeps{ 0 }, Switches{ 0 }, Waits{ 0 }, Helped{ 0 };
		};

		std::vector<std::unique_ptr<Worker>> s_Workers;   // [0] = 메인 스레드
		int s_WorkerCount = 0;
		bool s_Initialized = false;
		alignas(LockFree::kCacheLine) std::atomic<bool> s_Quit{ false };   // 쉬는 일꾼이 자주 읽는다 — 자주 쓰는 값과 떨어뜨린다
		std::atomic<bool> s_Inline{ false };
		bool s_UseFibers = false;

		std::unique_ptr<LockFree::MpmcRing<Job*>> s_Global[3];
		std::unique_ptr<Job[]> s_JobStore;
		std::unique_ptr<LockFree::MpmcRing<Job*>> s_FreeJobs;
		std::atomic<uint64_t> s_HeapJobs{ 0 };

		std::vector<FiberCtx> s_Fibers;
		std::unique_ptr<LockFree::MpmcRing<FiberCtx*>> s_FreeFibers, s_Ready;
		alignas(LockFree::kCacheLine) std::atomic<uint64_t> s_Migrations{ 0 };
		int s_SyntheticLoad = 0;
		alignas(LockFree::kCacheLine) std::atomic<int> s_BackgroundRunning{ 0 };
		int s_BackgroundCap = 1;   // 일꾼의 절반 (적어도 1)
		uint64_t s_LastExecuted = 0, s_LastStolen = 0;

		// 잠들기 · 깨우기: 원자 값 위에서 기다린다 (WaitOnAddress / futex — 잠금 없음).
		//  여러 스레드가 자주 쓰는 값은 캐시 라인을 따로 (거짓 공유 — 한 값을 쓸 때마다 이웃 값을 읽는 스레드의 줄이 무효가 된다)
		alignas(LockFree::kCacheLine) std::atomic<uint32_t> s_Epoch{ 0 };
		alignas(LockFree::kCacheLine) std::atomic<int> s_Sleeping{ 0 };

		thread_local Worker* t_Worker = nullptr;
		// 파이버가 다른 스레드에서 이어질 수 있다 → 스레드 지역 값은 늘 새로 읽는다 (컴파일러가 주소를 붙잡아 두지 않게)
		NOVA_NOINLINE Worker* CurrentWorker() { return t_Worker; }

		inline void Pause()
		{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
			_mm_pause();
#elif defined(__x86_64__) || defined(__i386__)
			__builtin_ia32_pause();
#else
			std::this_thread::yield();
#endif
		}

		uint32_t NextRandom(Worker& w)
		{
			uint32_t x = w.Rng;
			x ^= x << 13; x ^= x >> 17; x ^= x << 5;
			return w.Rng = x;
		}

		void FreeJob(Job* j)
		{
			if (j->Pooled)
			{
				if (!s_FreeJobs->Push(j))
					EditorLog::Write("Jobs", "job pool overflow");   // 일어나지 않는다 (풀 크기 = 링 크기)
			}
			else
				delete j;
		}

		bool HasWork()
		{
			if (s_Ready && s_Ready->SizeApprox() > 0) return true;
			if (s_Global[0]->SizeApprox() > 0 || s_Global[1]->SizeApprox() > 0) return true;
			// Background 는 동시 실행 상한 아래일 때만 일 (상한이면 끝날 때 깨운다)
			if (s_Global[2]->SizeApprox() > 0 && s_BackgroundRunning.load(std::memory_order_relaxed) < s_BackgroundCap) return true;
			for (auto& w : s_Workers) if (!w->Deque.EmptyApprox()) return true;
			return false;
		}

		void Wake(int count)
		{
			std::atomic_thread_fence(std::memory_order_seq_cst);
			if (s_Sleeping.load(std::memory_order_relaxed) <= 0)
				return;
			s_Epoch.fetch_add(1, std::memory_order_release);
			if (count > 1)
				s_Epoch.notify_all();
			else
				s_Epoch.notify_one();
		}

		// 기다리던 파이버를 준비 큐로 (Counter 가 0 이 됐다)
		void ReleaseWaiters(Counter* c)
		{
			WaitNode* n = static_cast<WaitNode*>(c->Waiters.exchange(nullptr, std::memory_order_acq_rel));
			int count = 0;
			while (n)
			{
				WaitNode* next = n->Next;   // 넣는 순간 다른 일꾼이 이어 돌려 노드를 다시 쓸 수 있다 — 먼저 읽는다
				FiberCtx* f = n->Fiber;
				while (!s_Ready->Push(f)) Pause();   // 준비 큐 = 파이버 수만큼 — 늘 들어간다
				++count;
				n = next;
			}
			if (count)
				Wake(count);
		}

		void Execute(Job* j)
		{
			{
				Profiler::Scope scope(j->Name);
				j->Invoke(j);
			}
			j->Destroy(j);
			Counter* c = j->Cnt;
			const bool slot = j->BackgroundSlot;
			FreeJob(j);
			if (slot)
			{
				s_BackgroundRunning.fetch_sub(1, std::memory_order_relaxed);
				if (s_Global[2]->SizeApprox() > 0)
					Wake(1);   // 기다리던 백그라운드 잡
			}
			if (c)
			{
				c->Busy.fetch_add(1, std::memory_order_relaxed);
				if (c->Value.fetch_sub(1, std::memory_order_acq_rel) == 1)
					ReleaseWaiters(c);
				c->Busy.fetch_sub(1, std::memory_order_release);
			}
			if (Worker* w = CurrentWorker())   // 파이버가 옮겨 왔을 수 있다 — 지금 스레드
				w->Executed.fetch_add(1, std::memory_order_relaxed);
		}

		Job* FindWork(Worker* w, bool background)
		{
			Job* j = nullptr;
			if (w && (j = w->Deque.Pop()) != nullptr)
				return j;
			if (s_Global[0]->Pop(j) || s_Global[1]->Pop(j))
				return j;
			const int n = (int)s_Workers.size();
			if (n > 1)
			{
				const int start = w ? (int)(NextRandom(*w) % (uint32_t)n) : 0;
				for (int i = 0; i < n; ++i)
				{
					Worker* v = s_Workers[(start + i) % n].get();
					if (v == w)
						continue;
					if ((j = v->Deque.Steal()) != nullptr)
					{
						if (w) w->Stolen.fetch_add(1, std::memory_order_relaxed);
						return j;
					}
				}
			}
			if (background && s_BackgroundRunning.load(std::memory_order_relaxed) < s_BackgroundCap)
			{
				if (s_BackgroundRunning.fetch_add(1, std::memory_order_relaxed) < s_BackgroundCap && s_Global[2]->Pop(j))
				{
					j->BackgroundSlot = true;
					return j;
				}
				s_BackgroundRunning.fetch_sub(1, std::memory_order_relaxed);
			}
			return nullptr;
		}

#if NOVA_JOBS_FIBERS
		void HandlePending(Worker* w)
		{
			const Post p = w->Pending;
			w->Pending = Post();
			if (p.Kind == PostKind::Finished)
			{
				while (!s_FreeFibers->Push(p.Fiber)) Pause();
			}
			else if (p.Kind == PostKind::Wait)
			{
				// 파이버가 완전히 빠져나온 뒤에 대기 목록에 올린다 (그 전에 깨우면 같은 스택을 두 스레드가 돈다)
				Counter* c = p.Cnt;
				c->Busy.fetch_add(1, std::memory_order_acq_rel);
				WaitNode* node = &p.Fiber->Node;
				node->Fiber = p.Fiber;
				void* head = c->Waiters.load(std::memory_order_relaxed);
				do { node->Next = static_cast<WaitNode*>(head); } while (!c->Waiters.compare_exchange_weak(head, node, std::memory_order_release, std::memory_order_relaxed));
				if (c->Value.load(std::memory_order_acquire) == 0)
					ReleaseWaiters(c);   // 사이에 0 이 됐다 — 우리가 깨운다
				c->Busy.fetch_sub(1, std::memory_order_release);
				w->Waits.fetch_add(1, std::memory_order_relaxed);
			}
		}

		void RunFiber(Worker* w, FiberCtx* f)
		{
			FiberCtx* outer = w->Current;
			w->Current = f;
			w->Switches.fetch_add(1, std::memory_order_relaxed);
			SwitchToFiber(f->Handle);
			// 스케줄러로 돌아왔다 (이 스레드 그대로 — 스케줄러 파이버는 옮겨 다니지 않는다)
			w->Current = outer;
			HandlePending(w);
		}

		void WINAPI FiberMain(void* param)
		{
			FiberCtx* f = static_cast<FiberCtx*>(param);
			for (;;)
			{
				Execute(f->Work);
				f->Work = nullptr;
				// 다음 잡도 이 파이버에서 (스케줄러를 오가는 두 번의 전환을 아낀다). 깨어난 파이버가 있으면 스케줄러가 먼저 이어 돌리게 돌아간다
				Worker* w = CurrentWorker();
				while (s_Ready->SizeApprox() == 0 && !s_Quit.load(std::memory_order_relaxed))
				{
					Job* next = FindWork(w, true);
					if (!next)
						break;
					Execute(next);
					w = CurrentWorker();   // 그 잡이 기다렸다면 다른 스레드일 수 있다
				}
				w->Pending = { PostKind::Finished, f, nullptr };
				SwitchToFiber(w->SchedulerFiber);
			}
		}

		bool OnSchedulerFiber(Worker* w)
		{
			return w && w->SchedulerFiber && GetCurrentFiber() == w->SchedulerFiber;
		}
#endif

		// 잠깐 돌다가 잠든다
		void Idle(Worker* w)
		{
			for (int i = 0; i < 256; ++i)
			{
				if (HasWork() || s_Quit.load(std::memory_order_relaxed))
					return;
				Pause();
			}
			s_Sleeping.fetch_add(1, std::memory_order_seq_cst);
			const uint32_t e = s_Epoch.load(std::memory_order_acquire);
			if (!HasWork() && !s_Quit.load(std::memory_order_acquire))
			{
				s_Epoch.wait(e, std::memory_order_acquire);
				w->Sleeps.fetch_add(1, std::memory_order_relaxed);
			}
			s_Sleeping.fetch_sub(1, std::memory_order_relaxed);
		}

		void WorkerLoop(Worker* w)
		{
			while (!s_Quit.load(std::memory_order_acquire))
			{
#if NOVA_JOBS_FIBERS
				if (s_UseFibers)
				{
					FiberCtx* f = nullptr;
					if (s_Ready->Pop(f))
					{
						RunFiber(w, f);   // 기다리다 깬 잡을 이어서
						continue;
					}
				}
#endif
				Job* j = FindWork(w, true);
				if (!j)
				{
					Idle(w);
					continue;
				}
#if NOVA_JOBS_FIBERS
				if (s_UseFibers)
				{
					FiberCtx* f = nullptr;
					if (s_FreeFibers->Pop(f))
					{
						f->Work = j;
						RunFiber(w, f);
						continue;
					}
				}
#endif
				Execute(j);   // 파이버가 모자라면 스케줄러 위에서 (그 안의 Wait 는 돕기)
			}
		}

		void ThreadMain(Worker* w)
		{
			t_Worker = w;
#if defined(_MSC_VER) && !defined(NOVA_ANDROID) && !defined(NOVA_WEB)
			wchar_t name[64];
			swprintf_s(name, L"Nova Job Worker %d", w->Index);
			SetThreadDescription(GetCurrentThread(), name);
#endif
			Profiler::SetThreadName(w->ProfilerName);
#if NOVA_JOBS_FIBERS
			if (s_UseFibers)
				w->SchedulerFiber = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
#endif
			WorkerLoop(w);
#if NOVA_JOBS_FIBERS
			if (w->SchedulerFiber)
				ConvertFiberToThread();
#endif
			t_Worker = nullptr;
		}
	}

	void Init(int workers)
	{
		if (s_Initialized)
			return;
#if NOVA_JOBS_NO_THREADS
		workers = 0;
#else
		if (workers < 0)
			workers = (int)std::thread::hardware_concurrency() - 1;
		// NOVA_JOB_WORKERS=<수> (검사 · 측정)
		if (const char* env = getenv("NOVA_JOB_WORKERS"))
			workers = atoi(env);
		workers = workers < 0 ? 0 : (workers > kMaxWorkers ? kMaxWorkers : workers);
#endif
		s_WorkerCount = workers;
		s_BackgroundCap = workers / 2 > 1 ? workers / 2 : 1;
		for (auto& g : s_Global)
			g = std::make_unique<LockFree::MpmcRing<Job*>>(kGlobalSize);
		s_JobStore = std::make_unique<Job[]>(kJobPool);
		s_FreeJobs = std::make_unique<LockFree::MpmcRing<Job*>>(kJobPool);
		for (size_t i = 0; i < kJobPool; ++i)
			s_FreeJobs->Push(&s_JobStore[i]);

		s_UseFibers = false;
#if NOVA_JOBS_FIBERS
		const char* fiberEnv = getenv("NOVA_JOB_FIBERS");
		s_UseFibers = workers > 0 && !(fiberEnv && fiberEnv[0] == '0');
		if (s_UseFibers)
		{
			s_Fibers.resize(kFiberPool);
			s_FreeFibers = std::make_unique<LockFree::MpmcRing<FiberCtx*>>(kFiberPool);
			s_Ready = std::make_unique<LockFree::MpmcRing<FiberCtx*>>(kFiberPool);
			for (FiberCtx& f : s_Fibers)
			{
				f.Handle = CreateFiberEx(kFiberCommit, kFiberReserve, FIBER_FLAG_FLOAT_SWITCH, FiberMain, &f);
				if (f.Handle)
					s_FreeFibers->Push(&f);
			}
		}
#endif
		if (!s_Ready)
			s_Ready = std::make_unique<LockFree::MpmcRing<FiberCtx*>>(2);

		s_Workers.clear();
		for (int i = 0; i <= workers; ++i)
		{
			auto w = std::make_unique<Worker>();
			w->Index = i;
			w->Rng = 0x9E3779B9u * (uint32_t)(i + 1);
			w->ProfilerName = i == 0 ? "Main Thread" : Profiler::Intern("Job Worker " + std::to_string(i));
			s_Workers.push_back(std::move(w));
		}
		t_Worker = s_Workers[0].get();   // 부른 스레드 = 메인
		s_Quit = false;
		s_Initialized = true;
		for (int i = 1; i <= workers; ++i)
		{
			Worker* w = s_Workers[i].get();
			w->Thread = std::thread([w]() { ThreadMain(w); });
		}
		EditorLog::Write("Jobs", "job system: %d workers + main, fibers %s (%d x %u KB stacks), deque %zu, global %zu x 3",
			workers, s_UseFibers ? "on" : "off", s_UseFibers ? kFiberPool : 0, (unsigned)(kFiberReserve / 1024), kDequeSize, kGlobalSize);
	}

	void Shutdown()
	{
		if (!s_Initialized)
			return;
		s_Quit = true;
		s_Epoch.fetch_add(1, std::memory_order_release);
		s_Epoch.notify_all();
		for (auto& w : s_Workers)
			if (w->Thread.joinable())
				w->Thread.join();
		// 남은 잡은 여기서 (끝날 때 잃지 않게)
		Job* j = nullptr;
		while ((j = FindWork(s_Workers[0].get(), true)) != nullptr)
			Execute(j);
#if NOVA_JOBS_FIBERS
		for (FiberCtx& f : s_Fibers)
			if (f.Handle)
				DeleteFiber(f.Handle);
#endif
		s_Fibers.clear();
		s_FreeFibers.reset();
		s_Ready.reset();
		s_Workers.clear();
		s_Initialized = false;
		t_Worker = nullptr;
	}

	bool Initialized() { return s_Initialized; }
	int WorkerCount() { return s_WorkerCount; }
	bool FibersEnabled() { return s_UseFibers; }
	int CurrentThread() { Worker* w = CurrentWorker(); return w ? w->Index : -1; }
	bool IsWorkerThread() { Worker* w = CurrentWorker(); return w && w->Index > 0; }
	void SetInline(bool on) { s_Inline = on; }
	bool Inline() { return s_Inline.load(std::memory_order_relaxed) || s_WorkerCount == 0 || !s_Initialized; }

	namespace { alignas(64) std::atomic<int> s_ParallelSections{ 0 }; }
	void EnterParallel() { s_ParallelSections.fetch_add(1, std::memory_order_acq_rel); }
	void LeaveParallel() { s_ParallelSections.fetch_sub(1, std::memory_order_acq_rel); }
	bool InParallel() { return s_ParallelSections.load(std::memory_order_acquire) > 0; }

	Job* AllocateJob()
	{
		Job* j = nullptr;
		if (s_FreeJobs && s_FreeJobs->Pop(j))
		{
			j->Pooled = true;
			j->BackgroundSlot = false;
			return j;
		}
		s_HeapJobs.fetch_add(1, std::memory_order_relaxed);
		j = new Job();
		j->Pooled = false;
		return j;
	}

	void Submit(Job* j)
	{
		if (Inline())
		{
			Execute(j);
			return;
		}
		Worker* w = CurrentWorker();
		const int prio = (int)j->Prio;
		if (j->Prio == Priority::Background || !w || !w->Deque.Push(j))
		{
			if (!s_Global[prio]->Push(j))
			{
				Execute(j);   // 공용 큐도 가득 — 여기서 바로
				return;
			}
		}
		Wake(1);
	}

	void Wait(Counter& c)
	{
		if (c.Done())
			return;
		Worker* w = CurrentWorker();
#if NOVA_JOBS_FIBERS
		if (s_UseFibers && w && w->Index > 0 && w->Current && !OnSchedulerFiber(w))
		{
			// 잡 파이버: 내려놓고 스케줄러로 — 0 이 되면 아무 일꾼이 여기서부터 이어 돌린다
			FiberCtx* self = w->Current;
			const int before = w->Index;
			w->Pending = { PostKind::Wait, self, &c };
			SwitchToFiber(w->SchedulerFiber);
			Worker* now = CurrentWorker();
			if (now && now->Index != before)
				s_Migrations.fetch_add(1, std::memory_order_relaxed);
			while (c.Busy.load(std::memory_order_acquire) != 0)
				Pause();
			return;
		}
#endif
		// 돕기: 기다리는 동안 High · Normal 잡을 직접 돌린다 (메인 스레드 · 파이버가 없을 때)
		int spins = 0;
		while (!c.Done())
		{
#if NOVA_JOBS_FIBERS
			// 스케줄러 파이버 위라면 깨어난 파이버도 이어 돌린다 (모두가 돕기만 하다 막히지 않게)
			if (s_UseFibers && OnSchedulerFiber(w))
			{
				FiberCtx* f = nullptr;
				if (s_Ready->Pop(f))
				{
					RunFiber(w, f);
					spins = 0;
					continue;
				}
			}
#endif
			if (Job* j = FindWork(w, false))
			{
				Execute(j);
				if (w) w->Helped.fetch_add(1, std::memory_order_relaxed);
				spins = 0;
				continue;
			}
			if (++spins < 64)
				Pause();
			else
				std::this_thread::yield();
		}
	}

	nlohmann::json Info()
	{
		nlohmann::json threads = nlohmann::json::array();
		for (auto& w : s_Workers)
			threads.push_back({ { "index", w->Index }, { "name", w->Index == 0 ? "Main Thread" : "Job Worker " + std::to_string(w->Index) },
				{ "executed", w->Executed.load() }, { "stolen", w->Stolen.load() }, { "helped", w->Helped.load() }, { "sleeps", w->Sleeps.load() },
				{ "fiberSwitches", w->Switches.load() }, { "fiberWaits", w->Waits.load() }, { "queued", w->Deque.SizeApprox() } });
		return {
			{ "initialized", s_Initialized }, { "workers", s_WorkerCount }, { "hardwareThreads", (int)std::thread::hardware_concurrency() },
			{ "inline", s_Inline.load() }, { "fibers", s_UseFibers },
			{ "fiberPool", s_UseFibers ? kFiberPool : 0 }, { "fibersFree", s_FreeFibers ? s_FreeFibers->SizeApprox() : 0 }, { "fibersReady", s_Ready ? s_Ready->SizeApprox() : 0 },
			{ "fiberMigrations", s_Migrations.load() },
			{ "queued", { { "high", s_Global[0] ? s_Global[0]->SizeApprox() : 0 }, { "normal", s_Global[1] ? s_Global[1]->SizeApprox() : 0 }, { "background", s_Global[2] ? s_Global[2]->SizeApprox() : 0 } } },
			{ "syntheticLoad", s_SyntheticLoad }, { "backgroundRunning", s_BackgroundRunning.load() }, { "backgroundCap", s_BackgroundCap }, { "jobPool", kJobPool }, { "jobsFree", s_FreeJobs ? s_FreeJobs->SizeApprox() : 0 }, { "heapJobs", s_HeapJobs.load() },
			{ "threads", threads } };
	}

	void SetSyntheticLoad(int items) { s_SyntheticLoad = items < 0 ? 0 : items; }

	void OnFrame()
	{
		if (s_SyntheticLoad > 0)
		{
			// 검사 · 보여 주기용: 무거운 수학을 ParallelFor 로 (Profiler Timeline 에 일꾼 줄이 생기는지)
			static std::vector<double> s_Out;
			s_Out.resize((size_t)s_SyntheticLoad);
			ParallelFor(s_SyntheticLoad, 64, [](int b, int e) {
				for (int i = b; i < e; ++i)
				{
					double x = (double)i;
					for (int k = 0; k < 32; ++k) x = std::sin(x) * 0.5 + std::sqrt(std::fabs(x) + k);
					s_Out[(size_t)i] = x;
				}
			}, "Jobs Synthetic Load");
		}
		uint64_t executed = 0, stolen = 0;
		for (auto& w : s_Workers)
		{
			executed += w->Executed.load(std::memory_order_relaxed);
			stolen += w->Stolen.load(std::memory_order_relaxed);
		}
		if (Profiler::Collecting())
		{
			static const char* kExec = Profiler::Intern("Jobs/Executed");
			static const char* kStolen = Profiler::Intern("Jobs/Stolen");
			Profiler::SetStat(kExec, (double)(executed - s_LastExecuted));
			Profiler::SetStat(kStolen, (double)(stolen - s_LastStolen));
		}
		s_LastExecuted = executed;
		s_LastStolen = stolen;
	}

	void ResetStats()
	{
		for (auto& w : s_Workers)
		{
			w->Executed = 0; w->Stolen = 0; w->Helped = 0; w->Sleeps = 0; w->Switches = 0; w->Waits = 0;
		}
		s_Migrations = 0;
		s_HeapJobs = 0;
		s_LastExecuted = s_LastStolen = 0;
	}
}
