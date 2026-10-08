#include "pch.h"
#include "FrameArena.h"
#include "Allocators.h"
#include <atomic>

// 프레임 아레나 — FrameArena.h 의 설명
namespace FrameArena
{
	namespace
	{
		constexpr size_t kSize = size_t(16) << 20;
		struct Slot
		{
			Memory::LinearAllocator Arena;
			std::unique_ptr<Memory::LinearResource> Resource;
			bool Ready = false;
		};
		Slot s_Slots[2];
		std::atomic<int> s_Current{ 0 };
		uint64_t s_Frames = 0;
		size_t s_LastUsed = 0;

		Slot& Get(int i)
		{
			Slot& s = s_Slots[i];
			if (!s.Ready)
			{
				s.Ready = s.Arena.Init(kSize);
				s.Resource = std::make_unique<Memory::LinearResource>(s.Arena);
			}
			return s;
		}
	}

	void BeginFrame()
	{
		const int cur = s_Current.load(std::memory_order_relaxed);
		s_LastUsed = Get(cur).Arena.Used();
		const int next = cur ^ 1;
		Get(next).Arena.Reset();   // 두 프레임 전 칸 (그 프레임의 임시 값은 다 끝났다)
		s_Current.store(next, std::memory_order_release);
		++s_Frames;
	}

	std::pmr::memory_resource* Resource()
	{
		return Get(s_Current.load(std::memory_order_acquire)).Resource.get();
	}

	nlohmann::json Info()
	{
		const Slot& s = Get(s_Current.load());
		return { { "capacityMB", kSize >> 20 }, { "usedKB", s.Arena.Used() / 1024 }, { "lastFrameKB", s_LastUsed / 1024 },
			{ "peakKB", (std::max)(s_Slots[0].Arena.Peak(), s_Slots[1].Arena.Peak()) / 1024 },
			{ "fallbacks", s_Slots[0].Arena.Failures() + s_Slots[1].Arena.Failures() }, { "frames", s_Frames } };
	}
}
