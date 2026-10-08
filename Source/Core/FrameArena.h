#pragma once
#include <memory_resource>
#include <nlohmann/json.hpp>

// 프레임 아레나 (docs/MEMORY_ALLOCATORS.md): 한 프레임만 쓰는 임시 메모리.
//  LinearAllocator 두 개를 프레임마다 번갈아 (이번 프레임 것을 앞으로만 잘라 쓰고, 두 프레임 뒤에 통째로 비운다) — 놓기 = 아무것도 안 함.
//  여러 스레드 (잡) 가 함께 잘라 써도 된다 (무잠금 CAS). 가득 차면 시스템 힙으로 (통계 fallbacks).
//  쓰는 법: std::pmr::vector<T> v(FrameArena::Resource()); — 그 프레임 안에서만 (다음 프레임까지 들고 있으면 안 된다)
namespace FrameArena
{
	void BeginFrame();                         // App: 프레임 시작 (두 프레임 전 것을 비우고 이번 칸으로)
	std::pmr::memory_resource* Resource();     // 이번 프레임 칸
	nlohmann::json Info();                     // 크기 · 쓴 양 · 최고 · 가득 찬 수
}
