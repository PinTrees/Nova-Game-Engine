#include "pch.h"
#include "AllocTracker.h"
#include "Profiler.h"
#include <atomic>
#include <algorithm>
#include <vector>

#if defined(_DEBUG) && defined(_MSC_VER) && !defined(NOVA_ANDROID) && !defined(NOVA_WEB)
#include <crtdbg.h>
#include <DbgHelp.h>
#include <cstring>
#include <string>
#define NOVA_ALLOC_TRACK 1
#endif

// 힙 할당 세기 — AllocTracker.h 의 설명
namespace Memory::AllocTracker
{
#if NOVA_ALLOC_TRACK
	namespace
	{
		// 훅 안에서는 할당하면 안 된다 (다시 들어온다) — 미리 잡은 고정 표 · 원자 값만
		struct Slot
		{
			std::atomic<const char*> Name{ nullptr };
			std::atomic<uint64_t> Count{ 0 }, Bytes{ 0 };
		};
		constexpr size_t kSlots = 2048;
		constexpr int kFrames = 240;
		Slot s_Table[kSlots];
		std::atomic<uint64_t> s_Count{ 0 }, s_Bytes{ 0 }, s_Other{ 0 }, s_OtherBytes{ 0 };
		std::atomic<bool> s_Running{ false };
		DWORD s_MainThread = 0;
		_CRT_ALLOC_HOOK s_Previous = nullptr;
		bool s_Installed = false;
		// 호출 스택 표본 (고정 표 — 훅 안에서 할당 없음): 스택 앞 몇 칸의 해시로 묶는다
		constexpr int kDepth = 14, kSkip = 3;   // 훅 · CRT 안쪽 몇 칸은 건너뛴다
		struct StackSlot
		{
			std::atomic<uint64_t> Hash{ 0 };
			std::atomic<uint64_t> Count{ 0 };
			void* Frames[kDepth] = {};
			std::atomic<int> Depth{ 0 };
		};
		constexpr size_t kStackSlots = 1024;
		StackSlot s_Stacks[kStackSlots];
		std::atomic<bool> s_SampleStacks{ false };
		char s_FocusScope[128] = {};
		std::atomic<uint64_t> s_SampleTick{ 0 };

		void SampleStack()
		{
			if ((s_SampleTick.fetch_add(1, std::memory_order_relaxed) & 15) != 0)
				return;
			void* frames[kDepth + kSkip] = {};
			const USHORT n = RtlCaptureStackBackTrace(kSkip, kDepth, frames, nullptr);
			uint64_t h = 1469598103934665603ull;
			for (USHORT i = 0; i < n; ++i) { h ^= (uint64_t)frames[i]; h *= 1099511628211ull; }
			if (h == 0) h = 1;
			size_t i = h % kStackSlots;
			for (size_t probe = 0; probe < kStackSlots; ++probe, i = (i + 1) % kStackSlots)
			{
				uint64_t cur = s_Stacks[i].Hash.load(std::memory_order_acquire);
				if (cur == 0)
				{
					uint64_t expected = 0;
					if (s_Stacks[i].Hash.compare_exchange_strong(expected, h))
					{
						for (USHORT k = 0; k < n; ++k) s_Stacks[i].Frames[k] = frames[k];
						s_Stacks[i].Depth.store(n, std::memory_order_release);
						s_Stacks[i].Count.fetch_add(1, std::memory_order_relaxed);
						return;
					}
					cur = expected;
				}
				if (cur == h)
				{
					s_Stacks[i].Count.fetch_add(1, std::memory_order_relaxed);
					return;
				}
			}
		}

		// 프레임마다 (메인 스레드만 쓴다)
		uint64_t s_LastCount = 0, s_LastBytes = 0, s_Frames = 0;
		uint64_t s_FrameCounts[kFrames] = {}, s_FrameBytes[kFrames] = {};
		uint64_t s_MaxPerFrame = 0;

		int __cdecl Hook(int type, void* user, size_t size, int block, long request, const unsigned char* file, int line)
		{
			if (s_Running.load(std::memory_order_relaxed) && (type == _HOOK_ALLOC || type == _HOOK_REALLOC) && block != _CRT_BLOCK)
			{
				s_Count.fetch_add(1, std::memory_order_relaxed);
				s_Bytes.fetch_add(size, std::memory_order_relaxed);
				if (GetCurrentThreadId() == s_MainThread)
				{
					const char* name = Profiler::CurrentScopeName();
					if (s_SampleStacks.load(std::memory_order_relaxed) && (s_FocusScope[0] == 0 || strcmp(name, s_FocusScope) == 0))
						SampleStack();
					size_t i = ((uintptr_t)name >> 4) % kSlots;
					for (size_t probe = 0; probe < kSlots; ++probe, i = (i + 1) % kSlots)
					{
						const char* cur = s_Table[i].Name.load(std::memory_order_acquire);
						if (cur == nullptr)
						{
							const char* expected = nullptr;
							if (!s_Table[i].Name.compare_exchange_strong(expected, name) && expected != name)
								continue;
							cur = name;
						}
						if (cur == name)
						{
							s_Table[i].Count.fetch_add(1, std::memory_order_relaxed);
							s_Table[i].Bytes.fetch_add(size, std::memory_order_relaxed);
							break;
						}
					}
				}
				else
				{
					s_Other.fetch_add(1, std::memory_order_relaxed);
					s_OtherBytes.fetch_add(size, std::memory_order_relaxed);
				}
			}
			return s_Previous ? s_Previous(type, user, size, block, request, file, line) : TRUE;
		}

		void ResetCounts()
		{
			for (Slot& s : s_Table) { s.Name = nullptr; s.Count = 0; s.Bytes = 0; }
			for (StackSlot& s : s_Stacks) { s.Hash = 0; s.Count = 0; s.Depth = 0; }
			s_SampleTick = 0;
			s_Count = 0; s_Bytes = 0; s_Other = 0; s_OtherBytes = 0;
			s_LastCount = s_LastBytes = s_Frames = s_MaxPerFrame = 0;
		}
	}

	bool Supported() { return true; }
	bool Running() { return s_Running.load(); }

	void Start(bool stacks, const char* scope)
	{
		s_MainThread = GetCurrentThreadId();
		ResetCounts();
		strncpy_s(s_FocusScope, scope ? scope : "", _TRUNCATE);
		s_SampleStacks = stacks;
		if (!s_Installed)
		{
			s_Previous = _CrtSetAllocHook(Hook);
			s_Installed = true;
		}
		Profiler::ForceCollecting(true);   // 구간 이름이 있어야 나눈다
		s_Running = true;
	}

	void Stop()
	{
		s_Running = false;
		Profiler::ForceCollecting(false);
	}

	void OnFrame()
	{
		if (!s_Running.load(std::memory_order_relaxed))
			return;
		const uint64_t c = s_Count.load(std::memory_order_relaxed), b = s_Bytes.load(std::memory_order_relaxed);
		if (s_Frames > 0)   // 첫 프레임은 켠 자리부터라 빼고
		{
			const uint64_t n = c - s_LastCount;
			s_FrameCounts[s_Frames % kFrames] = n;
			s_FrameBytes[s_Frames % kFrames] = b - s_LastBytes;
			s_MaxPerFrame = (std::max)(s_MaxPerFrame, n);
		}
		++s_Frames;
		s_LastCount = c;
		s_LastBytes = b;
	}

	nlohmann::json Report(int top)
	{
		const uint64_t frames = s_Frames > 1 ? (std::min<uint64_t>)(s_Frames - 1, kFrames) : 0;
		uint64_t sum = 0, bytes = 0;
		for (uint64_t i = 0; i < frames; ++i) { sum += s_FrameCounts[(s_Frames - 1 - i) % kFrames]; bytes += s_FrameBytes[(s_Frames - 1 - i) % kFrames]; }
		std::vector<std::pair<uint64_t, const char*>> list;
		uint64_t mainTotal = 0;
		for (const Slot& s : s_Table)
			if (const char* n = s.Name.load())
			{
				list.push_back({ s.Count.load(), n });
				mainTotal += s.Count.load();
			}
		std::sort(list.rbegin(), list.rend());
		nlohmann::json scopes = nlohmann::json::array();
		const double totalFrames = (double)(std::max<uint64_t>)(1, s_Frames);
		for (size_t i = 0; i < list.size() && (int)i < top; ++i)
			scopes.push_back({ { "scope", list[i].second }, { "total", list[i].first }, { "perFrame", list[i].first / totalFrames } });
		// 호출 스택 표본: 많은 것부터 함수 이름으로 (dbghelp — 훅 밖이라 할당해도 된다). 할당기 · 표준 라이브러리 칸은 건너뛴다
		nlohmann::json stacks = nlohmann::json::array();
		{
			std::vector<std::pair<uint64_t, const StackSlot*>> st;
			for (const StackSlot& s : s_Stacks)
				if (s.Hash.load() && s.Count.load()) st.push_back({ s.Count.load(), &s });
			std::sort(st.begin(), st.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
			HANDLE process = GetCurrentProcess();
			static bool s_Sym = false;
			if (!s_Sym && !st.empty())
			{
				SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
				SymInitialize(process, nullptr, TRUE);   // 이미 됐으면 실패해도 된다 (EditorLog 의 충돌 기록이 먼저 했을 수 있다)
				s_Sym = true;
			}
			for (size_t k = 0; k < st.size() && k < 12; ++k)
			{
				nlohmann::json frames = nlohmann::json::array();
				const StackSlot& s = *st[k].second;
				for (int f = 0; f < s.Depth.load() && frames.size() < 6; ++f)
				{
					char buffer[sizeof(SYMBOL_INFO) + 256] = {};
					SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
					sym->SizeOfStruct = sizeof(SYMBOL_INFO);
					sym->MaxNameLen = 255;
					DWORD64 off = 0;
					std::string name = SymFromAddr(process, (DWORD64)s.Frames[f], &off, sym) ? sym->Name : "?";
					if (name.rfind("std::", 0) == 0 || name.find("operator new") != std::string::npos || name.find("malloc") != std::string::npos ||
						name.find("_Allocate") != std::string::npos || name.find("allocator") != std::string::npos || name == "?")
						continue;
					IMAGEHLP_LINE64 line = {};
					line.SizeOfStruct = sizeof(line);
					DWORD lineOff = 0;
					if (SymGetLineFromAddr64(process, (DWORD64)s.Frames[f], &lineOff, &line))
					{
						const char* file = strrchr(line.FileName, '\\');
						name += std::string("  (") + (file ? file + 1 : line.FileName) + ":" + std::to_string(line.LineNumber) + ")";
					}
					frames.push_back(name);
				}
				stacks.push_back({ { "samples", st[k].first }, { "perFrame", st[k].first * 16.0 / (double)(std::max<uint64_t>)(1, s_Frames) }, { "frames", frames } });
			}
		}
		return { { "supported", true }, { "running", Running() }, { "frames", frames }, { "stacks", stacks },
			{ "allocationsPerFrame", frames ? (double)sum / frames : 0.0 }, { "bytesPerFrame", frames ? (double)bytes / frames : 0.0 },
			{ "maxPerFrame", s_MaxPerFrame }, { "mainThreadTotal", mainTotal }, { "otherThreadsTotal", s_Other.load() },
			{ "otherThreadsPerFrame", s_Other.load() / totalFrames }, { "scopes", scopes } };
	}
#else
	bool Supported() { return false; }
	void Start() {}
	void Stop() {}
	bool Running() { return false; }
	void OnFrame() {}
	nlohmann::json Report(int) { return { { "supported", false } }; }
#endif
}
