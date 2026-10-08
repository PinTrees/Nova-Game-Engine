#include "pch.h"
#include "MemoryHeaps.h"
#include "Allocators.h"
#include <atomic>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <map>
#if defined(_WIN32) && !defined(NOVA_ANDROID) && !defined(NOVA_WEB)
#include <psapi.h>
#define NOVA_PROCESS_MEMORY 1
#endif

// 씬 힙 — MemoryHeaps.h 의 설명
namespace Memory::Heaps
{
	namespace
	{
		constexpr size_t kReserve = size_t(256) << 20;   // 가상 주소 (쓰는 만큼만 확정)
		constexpr size_t kBuddyMin = 1024;                // Buddy 의 가장 작은 블록 (작은 것은 Pool 이 받는다)
		constexpr int kClasses = 16;                      // Pool 크기 64, 128, … 1024
		constexpr size_t kChunk = 64 * 1024;              // Pool 묶음 (Buddy 에서 받는다)
		constexpr size_t kFallbackAlign = kCacheLine;     // 시스템 힙으로 갈 때 (힙이 가득 · 활성 힙 없음)
		constexpr int kMaxHeaps = 64;

		// 짧은 잠금 (한 힙 안의 받기 · 놓기). 씬 할당은 거의 메인 스레드라 다툼이 드물다
		struct SpinLock
		{
			std::atomic_flag F = ATOMIC_FLAG_INIT;
			void lock() { while (F.test_and_set(std::memory_order_acquire)) while (F.test(std::memory_order_relaxed)) std::this_thread::yield(); }
			void unlock() { F.clear(std::memory_order_release); }
		};

		// 누수 보고용 꼬리표를 적나: Debug 빌드는 늘, Release 는 NOVA_HEAP_TRACK=1
		bool Track()
		{
#if defined(_DEBUG)
			static const bool s = true;
#else
			static const bool s = [] { const char* v = getenv("NOVA_HEAP_TRACK"); return v && v[0] == '1'; }();
#endif
			return s;
		}

		int ClassOf(size_t bytes) { return bytes <= size_t(kClasses) * kCacheLine ? (int)((bytes + kCacheLine - 1) / kCacheLine) - 1 : -1; }
	}

	struct SceneHeap
	{
		std::string Name;
		uint64_t Id = 0;
		BuddyAllocator Buddy;
		PoolAllocator Pools[kClasses];
		SpinLock Lock;
		size_t LiveAllocs = 0, LiveBytes = 0, PeakBytes = 0;
		std::unordered_map<void*, const char*> Tags;   // 누수 보고 (Track 일 때만)
		uint64_t TotalAllocs = 0;
		bool Destroyed = false;
	};

	namespace
	{
		std::atomic<SceneHeap*> s_Heaps[kMaxHeaps];
		std::atomic<SceneHeap*> s_Active{ nullptr };          // 정해 둔 힙 (ActiveScope)
		std::atomic<SceneHeap* (*)()> s_Resolver{ nullptr };  // 없으면 현재 씬의 힙
		std::atomic<uint64_t> s_NextId{ 1 }, s_Fallbacks{ 0 }, s_FallbackBytes{ 0 }, s_Released{ 0 };
		std::mutex s_InfoMutex;
		nlohmann::json s_LastReleased = nlohmann::json::object();

		void* ChunkFromBuddy(size_t bytes, void* user) { return static_cast<SceneHeap*>(user)->Buddy.Allocate(bytes); }   // 힙 잠금 안에서
		void ChunkToBuddy(void* p, void* user) { static_cast<SceneHeap*>(user)->Buddy.Free(p); }

		SceneHeap* Find(const void* p)
		{
			for (auto& slot : s_Heaps)
				if (SceneHeap* h = slot.load(std::memory_order_acquire))
					if (h->Buddy.Owns(p))
						return h;
			return nullptr;
		}

		void* FallbackAllocate(size_t bytes)
		{
			s_Fallbacks.fetch_add(1, std::memory_order_relaxed);
			s_FallbackBytes.fetch_add(bytes, std::memory_order_relaxed);
			return ::operator new(bytes, std::align_val_t{ kFallbackAlign });
		}

		// 지운 씬의 힙이 비었다: Pool 묶음을 Buddy 로 → 짝끼리 합쳐져 한 덩어리 (단편화 0) → 영역째 운영체제에
		void Release(SceneHeap* h)
		{
			for (PoolAllocator& p : h->Pools)
				p.Release();
			const bool whole = h->Buddy.Live() == 0 && h->Buddy.LargestFree() == h->Buddy.Capacity();
			std::string error;
			const bool valid = h->Buddy.Validate(&error);
			{
				std::lock_guard<std::mutex> lock(s_InfoMutex);
				s_LastReleased = { { "name", h->Name }, { "id", h->Id }, { "peakBytes", h->PeakBytes }, { "totalAllocations", h->TotalAllocs },
					{ "coalescedToOneBlock", whole }, { "fragmentation", h->Buddy.Fragmentation() }, { "valid", valid } };
			}
			EditorLog::Write("Memory", "scene heap '%s' released: %llu allocations served, peak %zu KB, buddy coalesced to one %zu MB block %s (fragmentation %.3f)%s%s",
				h->Name.c_str(), (unsigned long long)h->TotalAllocs, h->PeakBytes / 1024, h->Buddy.LargestFree() >> 20, whole ? "yes" : "NO",
				h->Buddy.Fragmentation(), valid ? "" : " — invalid: ", error.c_str());
			for (auto& slot : s_Heaps)
			{
				SceneHeap* expected = h;
				if (slot.compare_exchange_strong(expected, nullptr))
					break;
			}
			s_Released.fetch_add(1, std::memory_order_relaxed);
			delete h;   // Region 이 주소 공간째 돌려준다
		}
	}

	SceneHeap* Create(const char* name)
	{
		auto* h = new SceneHeap();
		h->Name = name ? name : "Scene";
		h->Id = s_NextId.fetch_add(1);
		if (!h->Buddy.Init(kReserve, kBuddyMin))
		{
			EditorLog::Write("Memory", "scene heap '%s': could not reserve %zu MB — using the system heap", h->Name.c_str(), kReserve >> 20);
			delete h;
			return nullptr;
		}
		for (int c = 0; c < kClasses; ++c)
		{
			const size_t block = size_t(c + 1) * kCacheLine;
			h->Pools[c].Init(block, kChunk / block, ChunkFromBuddy, ChunkToBuddy, h);
		}
		for (auto& slot : s_Heaps)
		{
			SceneHeap* expected = nullptr;
			if (slot.compare_exchange_strong(expected, h))
				return h;
		}
		EditorLog::Write("Memory", "too many scene heaps (%d) — '%s' uses the system heap", kMaxHeaps, h->Name.c_str());
		delete h;
		return nullptr;
	}

	void Destroy(SceneHeap* h)
	{
		if (!h)
			return;
		size_t live, bytes;
		{
			std::lock_guard<SpinLock> lock(h->Lock);
			h->Destroyed = true;
			live = h->LiveAllocs;
			bytes = h->LiveBytes;
		}
		SceneHeap* expected = h;
		s_Active.compare_exchange_strong(expected, nullptr);
		if (live == 0)
			Release(h);
		else
		{
			EditorLog::Write("Memory", "scene heap '%s' still holds %zu allocations (%zu KB) after its scene was deleted — kept until they are freed (leak, or objects moved out of the scene)",
				h->Name.c_str(), live, bytes / 1024);
			// 무엇이 남았나 (꼬리표별 개수 — 많은 것부터)
			std::map<std::string, int> byTag;
			{
				std::lock_guard<SpinLock> lock(h->Lock);
				for (const auto& [p, tag] : h->Tags)
					++byTag[tag ? tag : "(untagged)"];
			}
			std::vector<std::pair<int, std::string>> top;
			for (auto& [t, n] : byTag) top.push_back({ n, t });
			std::sort(top.rbegin(), top.rend());
			for (size_t i = 0; i < top.size() && i < 8; ++i)
				EditorLog::Write("Memory", "  left in '%s': %d x %s", h->Name.c_str(), top[i].first, top[i].second.c_str());
		}
	}

	void SetActive(SceneHeap* h) { s_Active.store(h && !h->Destroyed ? h : nullptr, std::memory_order_release); }
	void SetCurrentResolver(SceneHeap* (*resolver)()) { s_Resolver.store(resolver, std::memory_order_release); }
	SceneHeap* ActiveOverride() { return s_Active.load(std::memory_order_acquire); }
	SceneHeap* Active()
	{
		if (SceneHeap* h = s_Active.load(std::memory_order_acquire))
			return h;
		auto resolver = s_Resolver.load(std::memory_order_acquire);
		return resolver ? resolver() : nullptr;
	}
	void Rename(SceneHeap* h, const char* name) { if (h && name) { std::lock_guard<SpinLock> lock(h->Lock); h->Name = name; } }

	void* Allocate(size_t bytes, size_t align, const char* tag)
	{
		SceneHeap* h = Active();
		if (!h || h->Destroyed || align > kBuddyMin)
			return FallbackAllocate(bytes);
		void* p = nullptr;
		{
			std::lock_guard<SpinLock> lock(h->Lock);
			if (!h->Destroyed)
			{
				const int c = ClassOf(bytes);
				p = c >= 0 && align <= kCacheLine ? h->Pools[c].Allocate() : h->Buddy.Allocate(bytes);
				if (p)
				{
					++h->LiveAllocs;
					++h->TotalAllocs;
					h->LiveBytes += bytes;
					if (h->LiveBytes > h->PeakBytes) h->PeakBytes = h->LiveBytes;
					if (Track()) h->Tags[p] = tag;
				}
			}
		}
		return p ? p : FallbackAllocate(bytes);   // 힙이 가득 — 시스템으로 (Info 의 fallbacks)
	}

	void Free(void* p, size_t bytes)
	{
		if (!p)
			return;
		SceneHeap* h = Find(p);
		if (!h)
		{
			::operator delete(p, std::align_val_t{ kFallbackAlign });
			return;
		}
		bool release = false;
		{
			std::lock_guard<SpinLock> lock(h->Lock);
			const int c = ClassOf(bytes);
			// 받을 때와 같은 판단: 작은 크기는 Pool (Pool 블록은 Buddy 블록의 시작이 아닐 수도 있다)
			if (c >= 0 && h->Pools[c].Owns(p))
				h->Pools[c].Free(p);
			else
				h->Buddy.Free(p);
			--h->LiveAllocs;
			h->LiveBytes -= bytes;
			if (Track()) h->Tags.erase(p);
			release = h->Destroyed && h->LiveAllocs == 0;
		}
		if (release)
			Release(h);   // 지운 씬에 남았던 마지막 블록
	}

	nlohmann::json Info()
	{
		nlohmann::json heaps = nlohmann::json::array();
		size_t committed = 0;
		for (auto& slot : s_Heaps)
		{
			SceneHeap* h = slot.load(std::memory_order_acquire);
			if (!h)
				continue;
			std::lock_guard<SpinLock> lock(h->Lock);
			nlohmann::json pools = nlohmann::json::array();
			for (const PoolAllocator& p : h->Pools)
				if (p.Chunks())
					pools.push_back({ { "block", p.BlockSize() }, { "live", p.Live() }, { "capacity", p.Capacity() } });
			committed += h->Buddy.Committed();
			heaps.push_back({ { "name", h->Name }, { "id", h->Id }, { "active", Active() == h }, { "lingering", h->Destroyed },
				{ "reservedMB", h->Buddy.Capacity() >> 20 }, { "committedKB", h->Buddy.Committed() / 1024 },
				{ "liveAllocations", h->LiveAllocs }, { "liveBytes", h->LiveBytes }, { "peakBytes", h->PeakBytes }, { "totalAllocations", h->TotalAllocs },
				{ "buddyUsedKB", h->Buddy.Used() / 1024 }, { "buddyBlocks", h->Buddy.Live() }, { "largestFreeKB", h->Buddy.LargestFree() / 1024 },
				{ "fragmentation", h->Buddy.Fragmentation() }, { "pools", pools } });
		}
		size_t privateBytes = 0;
#if NOVA_PROCESS_MEMORY
		PROCESS_MEMORY_COUNTERS_EX pmc = {};
		if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
			privateBytes = pmc.PrivateUsage;
#endif
		std::lock_guard<std::mutex> lock(s_InfoMutex);
		return { { "heaps", heaps }, { "committedKB", committed / 1024 }, { "processPrivateMB", privateBytes / 1048576.0 }, { "fallbacks", s_Fallbacks.load() }, { "fallbackBytes", s_FallbackBytes.load() },
			{ "released", s_Released.load() }, { "lastReleased", s_LastReleased } };
	}
}
