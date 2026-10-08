#include "pch.h"
#include "Allocators.h"
#include "MemoryHeaps.h"
#include "AllocTracker.h"
#include "FrameArena.h"
#include "CliServer.h"
#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

// nova memory — 알로케이터 스트레스 검사 · 거짓 공유 측정 · 힙 통계 (Tools/tests/run_tests.ps1 -Only memory)
namespace Memory
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		double MsSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }
		struct Result { std::string Name; bool Ok = false; double Ms = 0; std::string Detail; };
		bool Aligned(const void* p, size_t a) { return ((uintptr_t)p & (a - 1)) == 0; }

		// Linear: 한 스레드 + 8 스레드 (무잠금 CAS) — 받은 범위가 겹치지 않는다 (칸마다 주인 번호를 쓰고 다시 읽는다)
		Result TestLinear()
		{
			Result r{ "linear" };
			LinearAllocator a;
			if (!a.Init(64 << 20)) { r.Detail = "init failed"; return r; }
			const auto t0 = Clock::now();
			constexpr int Threads = 8, Per = 20000;
			std::atomic<int> bad{ 0 };
			std::vector<std::thread> ts;
			for (int t = 0; t < Threads; ++t)
				ts.emplace_back([&, t]() {
					std::vector<std::pair<uint32_t*, uint32_t>> mine;
					std::mt19937 rng(t + 1);
					for (int i = 0; i < Per; ++i)
					{
						const uint32_t words = 1 + rng() % 64;
						auto* p = static_cast<uint32_t*>(a.AllocateAtomic(words * 4, (rng() & 1) ? 64 : 16));
						if (!p) { bad++; continue; }
						for (uint32_t k = 0; k < words; ++k) p[k] = (uint32_t)(t << 24 | i);
						mine.push_back({ p, words });
					}
					for (size_t i = 0; i < mine.size(); ++i)
						for (uint32_t k = 0; k < mine[i].second; ++k)
							if (mine[i].first[k] != (uint32_t)(t << 24 | (int)i)) { bad++; break; }
				});
			for (auto& t : ts) t.join();
			r.Ms = MsSince(t0);
			const size_t used = a.Used();
			a.Reset();
			void* p = a.Allocate(100, 64);
			r.Ok = bad.load() == 0 && p && Aligned(p, 64) && a.Used() == 100;
			r.Detail = "8 threads x 20000 atomic allocations, overlapping/failed " + std::to_string(bad.load()) + ", used " + std::to_string(used / 1024) + " KB, reset → reused from offset 0";
			return r;
		}

		// Stack: 표시까지 되돌리기 · 맨 위만 놓기 (아래 블록 놓기는 거절)
		Result TestStack()
		{
			Result r{ "stack" };
			StackAllocator s(1 << 20);
			const auto t0 = Clock::now();
			void* a = s.Allocate(100);
			const auto mark = s.GetMarker();
			void* b = s.Allocate(200, 64);
			void* c = s.Allocate(300);
			const bool refuseMiddle = !s.FreeTop(b);
			const bool topOk = s.FreeTop(c);
			const bool nowB = s.FreeTop(b);
			void* d = s.Allocate(50);
			s.FreeToMarker(mark);
			const size_t afterMarker = s.Used();
			s.FreeToMarker(0);
			r.Ms = MsSince(t0);
			r.Ok = a && b && c && d && Aligned(b, 64) && refuseMiddle && topOk && nowB && afterMarker == mark && s.Used() == 0;
			r.Detail = std::string("LIFO: middle free refused ") + (refuseMiddle ? "yes" : "no") + ", top free " + (topOk ? "ok" : "no") + ", marker rewind " + (afterMarker == mark ? "ok" : "no");
			return r;
		}

		// Pool: 100 만 번 무작위 받기 · 놓기 — 같은 블록을 두 번 주지 않는다, 블록은 64 에 맞는다
		Result TestPool()
		{
			Result r{ "pool" };
			PoolAllocator pool;
			pool.Init(200, 256);
			std::vector<void*> live;
			std::mt19937 rng(7);
			int bad = 0;
			const auto t0 = Clock::now();
			for (int i = 0; i < 1000000; ++i)
			{
				if (live.empty() || (rng() % 3) != 0)
				{
					void* p = pool.Allocate();
					if (!p || !Aligned(p, 64)) { ++bad; continue; }
					*static_cast<uint32_t*>(p) = 0xC0FFEEu;
					live.push_back(p);
					if (live.size() > 5000) { pool.Free(live.front()); live.front() = live.back(); live.pop_back(); }
				}
				else
				{
					const size_t k = rng() % live.size();
					pool.Free(live[k]);
					live[k] = live.back();
					live.pop_back();
				}
			}
			std::sort(live.begin(), live.end());
			const bool unique = std::adjacent_find(live.begin(), live.end()) == live.end();
			for (void* p : live) pool.Free(p);
			r.Ms = MsSince(t0);
			r.Ok = bad == 0 && unique && pool.Live() == 0;
			r.Detail = "1M ops, block " + std::to_string(pool.BlockSize()) + " B (200 → cache line), chunks " + std::to_string(pool.Chunks()) + ", live after free " + std::to_string(pool.Live());
			return r;
		}

		// ConcurrentPool: 8 스레드가 받고 놓는다 — 동시에 같은 블록을 가진 스레드가 없다 (블록에 주인을 쓰고 확인)
		Result TestConcurrentPool()
		{
			Result r{ "concurrentpool" };
			ConcurrentPool pool;
			if (!pool.Init(48, 4096)) { r.Detail = "init failed"; return r; }
			std::atomic<int> bad{ 0 };
			const auto t0 = Clock::now();
			std::vector<std::thread> ts;
			for (int t = 0; t < 8; ++t)
				ts.emplace_back([&, t]() {
					for (int i = 0; i < 100000; ++i)
					{
						auto* p = static_cast<std::atomic<int>*>(pool.Allocate());
						if (!p) continue;
						int expected = 0;
						if (!p->compare_exchange_strong(expected, t + 1)) bad++;   // 다른 스레드가 쓰는 중이었다
						p->store(0);
						pool.Free(p);
					}
				});
			for (auto& t : ts) t.join();
			r.Ms = MsSince(t0);
			r.Ok = bad.load() == 0 && pool.FreeApprox() == pool.Blocks();
			r.Detail = "8 threads x 100000 alloc/free, double-owned " + std::to_string(bad.load()) + ", free " + std::to_string(pool.FreeApprox()) + "/" + std::to_string(pool.Blocks());
			return r;
		}

		// Buddy: 무작위 크기 (64 B ~ 256 KB) 20 만 번, 중간중간 구조 검사, 다 놓으면 한 덩어리 (단편화 0) · 확정 메모리 되돌림
		Result TestBuddy()
		{
			Result r{ "buddy" };
			BuddyAllocator b;
			if (!b.Init(64 << 20)) { r.Detail = "init failed"; return r; }
			std::vector<void*> live;
			std::mt19937 rng(11);
			int misaligned = 0, failed = 0, checks = 0;
			std::string error;
			bool valid = true;
			double maxFrag = 0;
			const auto t0 = Clock::now();
			for (int i = 0; i < 200000; ++i)
			{
				if (live.size() < 2000 && (live.empty() || (rng() % 5) < 3))
				{
					const size_t bytes = (rng() % 10 == 0) ? 4096 + rng() % (256 * 1024) : 16 + rng() % 2048;
					void* p = b.Allocate(bytes);
					if (!p) { ++failed; continue; }
					if (!Aligned(p, 64) || b.BlockSizeOf(p) < bytes) ++misaligned;
					memset(p, 0xAB, (std::min)(bytes, (size_t)256));
					live.push_back(p);
				}
				else
				{
					const size_t k = rng() % live.size();
					b.Free(live[k]);
					live[k] = live.back();
					live.pop_back();
				}
				maxFrag = (std::max)(maxFrag, b.Fragmentation());
				if (i % 20000 == 0) { ++checks; valid = valid && b.Validate(&error); }
			}
			const size_t peakCommitted = b.Committed();
			for (void* p : live) b.Free(p);
			valid = valid && b.Validate(&error);
			const bool oneBlock = b.LargestFree() == b.Capacity() && b.Fragmentation() == 0.0 && b.Live() == 0;
			b.DecommitIfEmpty();
			r.Ms = MsSince(t0);
			r.Ok = valid && misaligned == 0 && failed == 0 && oneBlock && b.Committed() <= 64 * 1024;
			char buf[320];
			snprintf(buf, sizeof(buf), "200k ops, %d structure checks %s, peak committed %zu KB, max fragmentation while busy %.2f, after freeing all: one %zu MB block (fragmentation 0) %s, committed after release %zu KB%s%s",
				checks, valid ? "ok" : "FAILED", peakCommitted / 1024, maxFrag, b.LargestFree() >> 20, oneBlock ? "yes" : "NO", b.Committed() / 1024, error.empty() ? "" : " — ", error.c_str());
			r.Detail = buf;
			return r;
		}

		// 거짓 공유: 8 스레드가 각자 카운터를 올린다 — 나란히 (한 캐시 라인) vs CacheAligned (줄마다 하나)
		Result TestFalseSharing()
		{
			Result r{ "falsesharing" };
			constexpr int Threads = 8, N = 5000000;
			auto run = [&](auto& counters) {
				const auto t0 = Clock::now();
				std::vector<std::thread> ts;
				for (int t = 0; t < Threads; ++t)
					ts.emplace_back([&, t]() { for (int i = 0; i < N; ++i) (*counters[t]).fetch_add(1, std::memory_order_relaxed); });
				for (auto& t : ts) t.join();
				return MsSince(t0);
			};
			struct Packed { std::atomic<uint64_t> V{ 0 }; std::atomic<uint64_t>& operator*() { return V; } };
			std::vector<Packed> packed(Threads);
			std::vector<CacheAligned<std::atomic<uint64_t>>> padded(Threads);
			const double packedMs = run(packed);
			const double paddedMs = run(padded);
			r.Ms = packedMs + paddedMs;
			uint64_t sumA = 0, sumB = 0;
			for (auto& c : packed) sumA += c.V.load();
			for (auto& c : padded) sumB += c.Value.load();
			r.Ok = sumA == (uint64_t)Threads * N && sumB == sumA && sizeof(CacheAligned<std::atomic<uint64_t>>) == kCacheLine && paddedMs < packedMs;
			char buf[200];
			snprintf(buf, sizeof(buf), "8 threads x 5M increments: packed (one cache line) %.1f ms, CacheAligned %.1f ms — %.1fx faster", packedMs, paddedMs, packedMs / (std::max)(paddedMs, 0.001));
			r.Detail = buf;
			return r;
		}

		// 속도: 같은 패턴 (64 ~ 512 B 50 만 번 받기 · 놓기) — new/delete vs Pool vs Buddy
		Result TestSpeed()
		{
			Result r{ "speed" };
			constexpr int N = 500000;
			std::vector<void*> slots(4096, nullptr);
			std::mt19937 rng(3);
			std::vector<uint32_t> sizes(N);
			for (auto& s : sizes) s = 64 + rng() % 448;
			auto bench = [&](auto alloc, auto free) {
				std::fill(slots.begin(), slots.end(), nullptr);
				const auto t0 = Clock::now();
				for (int i = 0; i < N; ++i)
				{
					void*& s = slots[(size_t)i & 4095];
					if (s) free(s);
					s = alloc(sizes[(size_t)i]);
				}
				for (void*& s : slots) if (s) { free(s); s = nullptr; }
				return MsSince(t0);
			};
			const double sysMs = bench([](size_t n) { return ::operator new(n); }, [](void* p) { ::operator delete(p); });
			PoolAllocator pool;
			pool.Init(512, 1024);
			const double poolMs = bench([&](size_t) { return pool.Allocate(); }, [&](void* p) { pool.Free(p); });
			BuddyAllocator buddy;
			buddy.Init(16 << 20);
			const double buddyMs = bench([&](size_t n) { return buddy.Allocate(n); }, [&](void* p) { buddy.Free(p); });
			r.Ms = sysMs + poolMs + buddyMs;
			r.Ok = poolMs < sysMs;   // 풀은 시스템 힙보다 빨라야 한다 (버디는 참고 — 칸 지도를 고친다)
			char buf[200];
			snprintf(buf, sizeof(buf), "500k alloc/free 64-512 B: new/delete %.1f ms, Pool %.1f ms (%.1fx), Buddy %.1f ms (%.1fx)", sysMs, poolMs, sysMs / (std::max)(poolMs, 0.001), buddyMs, sysMs / (std::max)(buddyMs, 0.001));
			r.Detail = buf;
			return r;
		}

		// 씬 힙: 씬 하나처럼 섞인 크기 2 만 개 → 다 놓고 힙을 지우면 한 덩어리로 합쳐져 영역째 돌려준다.
		//  놓지 않은 블록이 있으면 힙이 남았다가 (남은 힙) 마지막 블록이 놓일 때 돌려준다
		Result TestSceneHeap()
		{
			Result r{ "sceneheap" };
			const auto t0 = Clock::now();
			const uint64_t releasedBefore = Heaps::Info().value("released", (uint64_t)0);
			Heaps::SceneHeap* h = Heaps::Create("Test Scene");
			if (!h) { r.Detail = "create failed"; return r; }
			std::vector<std::pair<void*, size_t>> live;
			std::mt19937 rng(5);
			{
				Heaps::ActiveScope scope(h);
				for (int i = 0; i < 20000; ++i)
				{
					const size_t bytes = (rng() % 8 == 0) ? 2000 + rng() % 60000 : 24 + rng() % 1000;   // 대부분 Pool, 가끔 Buddy
					void* p = Heaps::Allocate(bytes);
					memset(p, 0x5A, (std::min)(bytes, (size_t)64));
					live.push_back({ p, bytes });
					if (rng() % 4 == 0)   // 중간중간 놓는다 (씬을 쓰는 동안처럼)
					{
						const size_t k = rng() % live.size();
						Heaps::Free(live[k].first, live[k].second);
						live[k] = live.back();
						live.pop_back();
					}
				}
			}
			const nlohmann::json busy = Heaps::Info();
			size_t liveBytes = 0, committedKB = 0;
			double frag = 0;
			for (const auto& hp : busy["heaps"])
				if (hp.value("name", std::string()) == "Test Scene") { liveBytes = hp.value("liveBytes", (size_t)0); committedKB = hp.value("committedKB", (size_t)0); frag = hp.value("fragmentation", 0.0); }
			// 하나만 남기고 놓은 뒤 씬을 지운다 → 남은 힙 → 마지막을 놓으면 돌려준다
			auto keep = live.back();
			live.pop_back();
			for (auto& [p, n] : live) Heaps::Free(p, n);
			Heaps::Destroy(h);
			bool lingering = false;
			const nlohmann::json mid = Heaps::Info();   // 임시 값의 안쪽을 돌면 안 된다 (임시 값이 먼저 사라진다)
			for (const auto& hp : mid["heaps"])
				if (hp.value("name", std::string()) == "Test Scene" && hp.value("lingering", false)) lingering = true;
			Heaps::Free(keep.first, keep.second);
			const nlohmann::json after = Heaps::Info();
			const nlohmann::json last = after["lastReleased"];
			r.Ms = MsSince(t0);
			const bool released = after.value("released", (uint64_t)0) == releasedBefore + 1 && last.value("name", std::string()) == "Test Scene";
			r.Ok = lingering && released && last.value("coalescedToOneBlock", false) && last.value("fragmentation", 1.0) == 0.0 && last.value("valid", false);
			char buf[320];
			snprintf(buf, sizeof(buf), "20000 mixed allocations (pool + buddy), live %zu KB, committed %zu KB, fragmentation while live %.2f; deleted with 1 block left → lingering %s; last freed → released %s, one block %s, fragmentation %.3f",
				liveBytes / 1024, committedKB, frag, lingering ? "yes" : "NO", released ? "yes" : "NO", last.value("coalescedToOneBlock", false) ? "yes" : "NO", last.value("fragmentation", 1.0));
			r.Detail = buf;
			return r;
		}

		nlohmann::json ToJson(const Result& r) { return { { "name", r.Name }, { "ok", r.Ok }, { "ms", r.Ms }, { "detail", r.Detail } }; }
	}

	void Heaps::RegisterEditor()
	{
		CliServer::Register("memory", "Memory allocators: {op: info|test|allocs (start?, stop?, top?), kind?: all|linear|stack|pool|concurrentpool|buddy|sceneheap|falsesharing|speed}",
			[](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
				const std::string op = args.value("op", std::string("info"));
				if (op == "info")
				{
					result = Heaps::Info();
					result["frameArena"] = FrameArena::Info();
					return true;
				}
				if (op == "allocs")
				{
					// 힙 할당 세기 (Debug): --start true 로 켜고, 그냥 부르면 읽고, --stop true 로 끈다
					if (!AllocTracker::Supported()) { error = "allocation tracking needs a Debug build (CRT allocation hook)"; return false; }
					if (args.value("start", false)) AllocTracker::Start(args.value("stacks", false), args.value("scope", std::string()).c_str());
					result = AllocTracker::Report(args.value("top", 16));
					if (args.value("stop", false)) AllocTracker::Stop();
					return true;
				}
				if (op == "test")
				{
					const std::string kind = args.value("kind", std::string("all"));
					auto want = [&](const char* k) { return kind == "all" || kind == k; };
					std::vector<Result> rs;
					if (want("linear")) rs.push_back(TestLinear());
					if (want("stack")) rs.push_back(TestStack());
					if (want("pool")) rs.push_back(TestPool());
					if (want("concurrentpool")) rs.push_back(TestConcurrentPool());
					if (want("buddy")) rs.push_back(TestBuddy());
					if (want("sceneheap")) rs.push_back(TestSceneHeap());
					if (want("falsesharing")) rs.push_back(TestFalseSharing());
					if (want("speed")) rs.push_back(TestSpeed());
					if (rs.empty()) { error = "unknown test '" + kind + "'"; return false; }
					nlohmann::json arr = nlohmann::json::array();
					bool ok = true;
					for (const Result& r : rs) { arr.push_back(ToJson(r)); ok = ok && r.Ok; }
					result = { { "ok", ok }, { "tests", arr } };
					return true;
				}
				error = "unknown op '" + op + "' (info, test)";
				return false;
			});
	}
}
