#pragma once
#include <nlohmann/json.hpp>

// 힙 할당 세기 (Debug CRT 의 할당 훅 — 모든 모듈 · 모든 스레드). 메인 스레드는 그 순간의 Profiler 구간 이름으로 나눈다.
//  프레임마다 몇 번 · 몇 바이트를 힙에서 받는지, 어느 구간이 많이 받는지 → 프레임 아레나로 옮길 곳을 고른다 (docs/MEMORY_ALLOCATORS.md).
//  Release 빌드에서는 없다 (Supported = false). CLI: nova memory allocs --start true | (읽기) | --stop true
namespace Memory::AllocTracker
{
	bool Supported();
	// stacks = 메인 스레드 할당의 호출 스택을 표본으로 (16 번에 한 번), scope = 그 Profiler 구간 안의 것만 (빈 값 = 모두)
	void Start(bool stacks = false, const char* scope = nullptr);
	void Stop();
	bool Running();
	void OnFrame();   // App: 프레임마다 (프레임당 개수를 고리에)
	nlohmann::json Report(int top = 16);
}
