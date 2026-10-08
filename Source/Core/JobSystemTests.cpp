#include "pch.h"
#include "JobSystem.h"
#include "LockFree.h"
#include "CliServer.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

// nova jobs — Job System 확인 · 무잠금 자료 구조 스트레스 검사 · 측정 (Tools/tests/run_tests.ps1 -Only jobs)
namespace Jobs
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		double MsSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

		struct Result
		{
			std::string Name;
			bool Ok = false;
			double Ms = 0.0;
			std::string Detail;
		};

		// Chase-Lev: 주인이 넣고 꺼내는 동안 도둑 3 이 훔친다 — 모든 항목이 정확히 한 번
		Result TestDeque()
		{
			Result r{ "deque" };
			constexpr int N = 1000000, Thieves = 3;
			std::vector<int> items(N);
			std::vector<std::atomic<uint8_t>> seen(N);
			for (auto& s : seen) s.store(0, std::memory_order_relaxed);
			LockFree::WorkStealingDeque<int*> dq(1024);
			std::atomic<bool> done{ false };
			std::atomic<int64_t> stolen{ 0 }, popped{ 0 };
			auto take = [&](int* p) { seen[p - items.data()].fetch_add(1, std::memory_order_relaxed); };
			const auto t0 = Clock::now();
			std::vector<std::thread> thieves;
			for (int t = 0; t < Thieves; ++t)
				thieves.emplace_back([&]() {
					while (!done.load(std::memory_order_acquire))
						if (int* p = dq.Steal()) { take(p); stolen.fetch_add(1, std::memory_order_relaxed); }
					while (int* p = dq.Steal()) { take(p); stolen.fetch_add(1, std::memory_order_relaxed); }
				});
			for (int i = 0; i < N; ++i)
			{
				while (!dq.Push(&items[i]))
					if (int* p = dq.Pop()) { take(p); popped.fetch_add(1, std::memory_order_relaxed); }
				if ((i & 3) == 3)   // 넷 넣고 하나 꺼낸다 (마지막 하나를 두고 훔치기와 겨룬다)
					if (int* p = dq.Pop()) { take(p); popped.fetch_add(1, std::memory_order_relaxed); }
			}
			while (int* p = dq.Pop()) { take(p); popped.fetch_add(1, std::memory_order_relaxed); }
			done.store(true, std::memory_order_release);
			for (auto& t : thieves) t.join();
			r.Ms = MsSince(t0);
			int missing = 0, dup = 0;
			for (auto& s : seen) { const int v = s.load(); if (v == 0) ++missing; else if (v > 1) ++dup; }
			r.Ok = missing == 0 && dup == 0 && stolen.load() > 0;
			r.Detail = "items " + std::to_string(N) + ", popped " + std::to_string(popped.load()) + ", stolen " + std::to_string(stolen.load()) +
				", missing " + std::to_string(missing) + ", duplicated " + std::to_string(dup);
			return r;
		}

		// Vyukov MPMC: 넣는 4 · 꺼내는 4, 1M 값 — 정확히 한 번
		Result TestMpmc()
		{
			Result r{ "mpmc" };
			constexpr int P = 4, C = 4, PerP = 250000, N = P * PerP;
			LockFree::MpmcRing<uint32_t> q(1024);
			std::vector<std::atomic<uint8_t>> seen(N);
			for (auto& s : seen) s.store(0, std::memory_order_relaxed);
			std::atomic<int> consumed{ 0 };
			const auto t0 = Clock::now();
			std::vector<std::thread> ts;
			for (int p = 0; p < P; ++p)
				ts.emplace_back([&, p]() { for (int i = 0; i < PerP; ++i) { const uint32_t v = (uint32_t)(p * PerP + i); while (!q.Push(v)) std::this_thread::yield(); } });
			for (int c = 0; c < C; ++c)
				ts.emplace_back([&]() {
					uint32_t v;
					while (consumed.load(std::memory_order_relaxed) < N)
						if (q.Pop(v)) { seen[v].fetch_add(1, std::memory_order_relaxed); consumed.fetch_add(1, std::memory_order_relaxed); }
						else std::this_thread::yield();
				});
			for (auto& t : ts) t.join();
			r.Ms = MsSince(t0);
			int missing = 0, dup = 0;
			for (auto& s : seen) { const int v = s.load(); if (v == 0) ++missing; else if (v > 1) ++dup; }
			r.Ok = missing == 0 && dup == 0;
			r.Detail = "4 producers x 4 consumers, " + std::to_string(N) + " values, missing " + std::to_string(missing) + ", duplicated " + std::to_string(dup);
			return r;
		}

		// SPSC: 순서 그대로
		Result TestSpsc()
		{
			Result r{ "spsc" };
			constexpr uint64_t N = 2000000;
			LockFree::SpscRing<uint64_t> q(1024);
			uint64_t bad = 0, got = 0;
			const auto t0 = Clock::now();
			std::thread prod([&]() { for (uint64_t i = 0; i < N; ++i) while (!q.Push(i)) std::this_thread::yield(); });
			uint64_t v;
			while (got < N)
				if (q.Pop(v)) { if (v != got) ++bad; ++got; }
			prod.join();
			r.Ms = MsSince(t0);
			r.Ok = bad == 0 && got == N;
			r.Detail = std::to_string(got) + " values in order, out of order " + std::to_string(bad);
			return r;
		}

		// 삼중 버퍼: 읽는 쪽이 찢긴 값 (반만 쓴 판) 을 보지 않고, 판 번호는 늘기만 한다
		Result TestTriple()
		{
			Result r{ "triple" };
			struct Payload { uint64_t Seq; uint64_t Data[15]; };
			LockFree::TripleBuffer<Payload> tb;
			constexpr uint64_t N = 500000;
			std::atomic<bool> done{ false };
			uint64_t torn = 0, backwards = 0, reads = 0, last = 0;
			const auto t0 = Clock::now();
			std::thread writer([&]() {
				for (uint64_t i = 1; i <= N; ++i)
				{
					Payload& p = tb.Write();
					p.Seq = i;
					for (int k = 0; k < 15; ++k) p.Data[k] = i * 31 + k;
					tb.Publish();
				}
				done.store(true, std::memory_order_release);
			});
			for (;;)
			{
				const bool finished = done.load(std::memory_order_acquire);   // Acquire 보다 먼저 — 끝났는데 새 판이 없으면 정말 끝
				if (!tb.Acquire())
				{
					if (finished) break;
					continue;
				}
				const Payload& p = tb.Read();
				++reads;
				for (int k = 0; k < 15; ++k) if (p.Data[k] != p.Seq * 31 + k) { ++torn; break; }
				if (p.Seq < last) ++backwards;
				last = p.Seq;
			}
			writer.join();
			r.Ms = MsSince(t0);
			r.Ok = torn == 0 && backwards == 0 && reads > 0 && last == N;
			r.Detail = std::to_string(N) + " publishes, " + std::to_string(reads) + " reads, torn " + std::to_string(torn) + ", backwards " + std::to_string(backwards) + ", last " + std::to_string(last);
			return r;
		}

		// 작은 잡 20 만 개 + Counter
		Result TestJobs()
		{
			Result r{ "jobs" };
			constexpr int N = 200000;
			std::atomic<int64_t> sum{ 0 };
			std::vector<std::atomic<int>> perThread(64);
			for (auto& a : perThread) a = 0;
			Counter c;
			const auto t0 = Clock::now();
			for (int i = 0; i < N; ++i)
				Run([&sum, &perThread, i]() {
					sum.fetch_add(i, std::memory_order_relaxed);
					const int t = CurrentThread();
					perThread[(t >= 0 && t < 64) ? t : 63].fetch_add(1, std::memory_order_relaxed);
				}, &c, Priority::Normal, "Test Job");
			Wait(c);
			r.Ms = MsSince(t0);
			int threadsUsed = 0;
			for (auto& a : perThread) if (a.load() > 0) ++threadsUsed;
			const int64_t expect = (int64_t)N * (N - 1) / 2;
			r.Ok = sum.load() == expect && c.Done() && (Inline() || threadsUsed > 1);
			char buf[160];
			snprintf(buf, sizeof(buf), "%d jobs, sum ok %s, %d threads ran jobs, %.0f ns per job", N, sum.load() == expect ? "yes" : "no", threadsUsed, r.Ms * 1e6 / N);
			r.Detail = buf;
			return r;
		}

		double Heavy(int i)
		{
			double x = (double)i;
			for (int k = 0; k < 48; ++k) x = std::sin(x) * 0.5 + std::sqrt(std::fabs(x) + k);
			return x;
		}

		// ParallelFor = 차례로 돌린 값과 같다
		Result TestParallelFor()
		{
			Result r{ "parallelfor" };
			constexpr int N = 400000;
			std::vector<double> a(N), b(N);
			for (int i = 0; i < N; ++i) a[i] = Heavy(i);
			const auto t0 = Clock::now();
			ParallelFor(N, 256, [&](int begin, int end) { for (int i = begin; i < end; ++i) b[i] = Heavy(i); }, "Test ParallelFor");
			r.Ms = MsSince(t0);
			int diff = 0;
			for (int i = 0; i < N; ++i) if (a[i] != b[i]) ++diff;
			r.Ok = diff == 0;
			r.Detail = std::to_string(N) + " items, different " + std::to_string(diff);
			return r;
		}

		// 잡 안에서 잡을 넣고 기다린다 (파이버: 기다리는 잡은 내려놓고 일꾼은 다른 일)
		Result TestNested()
		{
			Result r{ "nested" };
			constexpr int Parents = 64, Children = 32;
			std::atomic<int> leaves{ 0 }, parentsDone{ 0 };
			const nlohmann::json before = Info();
			Counter c;
			const auto t0 = Clock::now();
			for (int p = 0; p < Parents; ++p)
				Run([&]() {
					Counter mine;
					for (int k = 0; k < Children; ++k)
						Run([&leaves, k]() { volatile double x = Heavy(k); (void)x; leaves.fetch_add(1, std::memory_order_relaxed); }, &mine, Priority::Normal, "Test Child");
					Wait(mine);
					if (leaves.load(std::memory_order_relaxed) >= 0)
						parentsDone.fetch_add(1, std::memory_order_relaxed);
				}, &c, Priority::Normal, "Test Parent");
			Wait(c);
			r.Ms = MsSince(t0);
			const nlohmann::json after = Info();
			uint64_t waits = 0;
			for (const auto& t : after["threads"]) waits += t.value("fiberWaits", (uint64_t)0);
			for (const auto& t : before["threads"]) waits -= t.value("fiberWaits", (uint64_t)0);
			r.Ok = leaves.load() == Parents * Children && parentsDone.load() == Parents && (!FibersEnabled() || waits > 0);
			r.Detail = std::to_string(Parents) + " parents x " + std::to_string(Children) + " children, leaves " + std::to_string(leaves.load()) +
				", fiber waits " + std::to_string(waits) + ", migrations " + std::to_string(after.value("fiberMigrations", (uint64_t)0) - before.value("fiberMigrations", (uint64_t)0));
			return r;
		}

		// 깊은 나무: 잡마다 넷을 넣고 기다린다 (깊이 6 → 잎 4096)
		void Tree(int depth, std::atomic<int>& leaves)
		{
			if (depth == 0)
			{
				leaves.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			Counter c;
			for (int i = 0; i < 4; ++i)
				Run([depth, &leaves]() { Tree(depth - 1, leaves); }, &c, Priority::Normal, "Test Tree");
			Wait(c);
		}
		Result TestTree()
		{
			Result r{ "tree" };
			std::atomic<int> leaves{ 0 };
			const auto t0 = Clock::now();
			Counter c;
			Run([&leaves]() { Tree(6, leaves); }, &c, Priority::Normal, "Test Tree");
			Wait(c);
			r.Ms = MsSince(t0);
			r.Ok = leaves.load() == 4096;
			r.Detail = "depth 6 x fan-out 4, leaves " + std::to_string(leaves.load()) + " (5461 jobs, each waits on its 4 children)";
			return r;
		}

		// Background 잡은 일꾼만 돌린다 (메인이 기다리며 돕다가 긴 백그라운드 일을 잡지 않는다)
		Result TestBackground()
		{
			Result r{ "background" };
			std::atomic<int> where{ -2 };
			Counter bg, fg;
			const auto t0 = Clock::now();
			Run([&where]() { where = CurrentThread(); std::this_thread::sleep_for(std::chrono::milliseconds(30)); }, &bg, Priority::Background, "Test Background");
			Run([]() {}, &fg, Priority::High, "Test High");
			Wait(fg);
			const double fgMs = MsSince(t0);
			Wait(bg);
			r.Ms = MsSince(t0);
			r.Ok = Inline() ? where.load() == 0 : (where.load() > 0 && fgMs < 25.0);
			char buf[128];
			snprintf(buf, sizeof(buf), "background ran on thread %d, high-priority wait %.2f ms", where.load(), fgMs);
			r.Detail = buf;
			return r;
		}

		nlohmann::json ToJson(const Result& r) { return { { "name", r.Name }, { "ok", r.Ok }, { "ms", r.Ms }, { "detail", r.Detail } }; }
	}

	void RegisterEditor()
	{
		CliServer::Register("jobs", "Job System: {op: info|test|bench|set (inline, load = items of synthetic ParallelFor per frame)|reset, kind?: all|deque|mpmc|spsc|triple|jobs|parallelfor|nested|tree|background, inline?: bool}",
			[](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
				const std::string op = args.value("op", std::string("info"));
				if (op == "info")
				{
					result = Info();
					return true;
				}
				if (op == "reset")
				{
					ResetStats();
					result = Info();
					return true;
				}
				if (op == "set")
				{
					if (args.contains("inline"))
						SetInline(args["inline"].is_boolean() ? args["inline"].get<bool>() : args["inline"].dump() != "false");
					if (args.contains("load"))
						SetSyntheticLoad(args["load"].is_number() ? args["load"].get<int>() : atoi(args["load"].get<std::string>().c_str()));
					result = Info();
					return true;
				}
				if (op == "test")
				{
					const std::string kind = args.value("kind", std::string("all"));
					std::vector<Result> rs;
					auto want = [&](const char* k) { return kind == "all" || kind == k; };
					if (want("deque")) rs.push_back(TestDeque());
					if (want("mpmc")) rs.push_back(TestMpmc());
					if (want("spsc")) rs.push_back(TestSpsc());
					if (want("triple")) rs.push_back(TestTriple());
					if (want("jobs")) rs.push_back(TestJobs());
					if (want("parallelfor")) rs.push_back(TestParallelFor());
					if (want("nested")) rs.push_back(TestNested());
					if (want("tree")) rs.push_back(TestTree());
					if (want("background")) rs.push_back(TestBackground());
					if (rs.empty())
					{
						error = "unknown test '" + kind + "'";
						return false;
					}
					nlohmann::json arr = nlohmann::json::array();
					bool ok = true;
					for (const Result& r : rs) { arr.push_back(ToJson(r)); ok = ok && r.Ok; }
					result = { { "ok", ok }, { "tests", arr }, { "workers", WorkerCount() }, { "fibers", FibersEnabled() }, { "inline", Inline() } };
					return true;
				}
				if (op == "bench")
				{
					// 무거운 반복: 차례로 vs ParallelFor, 빈 잡의 처리량
					const int n = args.value("n", 400000);
					std::vector<double> out(n);
					auto t0 = Clock::now();
					for (int i = 0; i < n; ++i) out[i] = Heavy(i);
					const double serial = MsSince(t0);
					t0 = Clock::now();
					ParallelFor(n, 256, [&](int b, int e) { for (int i = b; i < e; ++i) out[i] = Heavy(i); }, "Bench ParallelFor");
					const double parallel = MsSince(t0);
					constexpr int kEmpty = 100000;
					Counter c;
					t0 = Clock::now();
					for (int i = 0; i < kEmpty; ++i)
						Run([]() {}, &c, Priority::Normal, "Bench Empty");
					Wait(c);
					const double emptyMs = MsSince(t0);
					result = { { "items", n }, { "serialMs", serial }, { "parallelMs", parallel }, { "speedup", parallel > 0 ? serial / parallel : 0.0 },
						{ "threads", Inline() ? 1 : WorkerCount() + 1 }, { "emptyJobs", kEmpty }, { "emptyJobNs", emptyMs * 1e6 / kEmpty }, { "jobsPerSecond", kEmpty / (emptyMs / 1000.0) } };
					return true;
				}
				error = "unknown op '" + op + "' (info, test, bench, set, reset)";
				return false;
			});
	}
}
