#pragma once
#include <cstddef>
#include <memory>
#include <typeinfo>
#include <nlohmann/json.hpp>
#include "NovaApi.h"

// 씬 힙 (docs/MEMORY_ALLOCATORS.md — 씬 전환 단편화 0).
//  씬마다 힙 하나: 가상 주소 256 MB 예약 (쓰는 만큼 확정) 위에 Buddy (1 KB 블록) + 크기별 Pool (64 ~ 1024 B, 64 단위 16 종 — 묶음은 Buddy 에서).
//  GameObject (operator new) 와 컴포넌트 (allocate_shared + SceneAllocator) 가 '활성 힙' 에서 받는다.
//  씬을 지우면 그 씬의 오브젝트가 모두 놓이고 → Pool 묶음이 Buddy 로 돌아가 짝끼리 합쳐져 한 덩어리 → 영역째 운영체제에 돌려준다.
//  힙이 다른 씬의 메모리와 섞이지 않으므로 씬을 몇 번 오가도 단편화가 남지 않는다. 남은 블록이 있으면 (누수 · 씬 밖으로 옮긴 오브젝트)
//  힙은 '남은 힙' 으로 남았다가 마지막 블록이 놓일 때 돌려준다 — Editor.log 에 몇 개 · 몇 바이트를 적는다
namespace Memory::Heaps
{
	struct SceneHeap;

	NOVA_API SceneHeap* Create(const char* name);
	NOVA_API void Destroy(SceneHeap* heap);        // 씬이 사라질 때 (비었으면 바로 돌려준다)
	// 활성 힙 = SetActive 로 정한 힙 (불러오는 씬 등 — ActiveScope) 이 있으면 그것, 없으면 현재 씬의 힙 (SetCurrentResolver)
	NOVA_API void SetActive(SceneHeap* heap);      // nullptr = 정하지 않음 (현재 씬으로)
	NOVA_API SceneHeap* Active();
	NOVA_API void SetCurrentResolver(SceneHeap* (*resolver)());
	NOVA_API SceneHeap* ActiveOverride();          // SetActive 로 정한 것만 (없으면 nullptr)
	NOVA_API void Rename(SceneHeap* heap, const char* name);

	// 활성 힙에서 (없거나 가득하면 시스템 — 통계에 센다). Free 는 주소로 힙을 찾는다 (bytes = 받은 크기)
	//  tag = 무엇인지 (누수 보고에 — Debug 빌드 또는 NOVA_HEAP_TRACK=1 일 때만 적는다)
	NOVA_API void* Allocate(size_t bytes, size_t align = 16, const char* tag = nullptr);
	NOVA_API void Free(void* p, size_t bytes);

	// 그 동안 이 힙에서 받는다 (씬을 불러오는 동안 등)
	struct ActiveScope
	{
		SceneHeap* Previous;   // 정해 둔 힙만 (현재 씬으로 풀린 힙을 되돌려 넣으면 씬이 바뀐 뒤에도 그 힙에 묶인다)
		explicit ActiveScope(SceneHeap* heap) : Previous(ActiveOverride()) { SetActive(heap); }
		~ActiveScope() { SetActive(Previous); }
	};

	// std::allocate_shared 용 (컴포넌트 + shared_ptr 제어 블록이 한 덩어리로 씬 힙에)
	template <class T>
	struct SceneAllocator
	{
		using value_type = T;
		SceneAllocator() noexcept = default;
		template <class U> SceneAllocator(const SceneAllocator<U>&) noexcept {}
		T* allocate(size_t n) { return static_cast<T*>(Allocate(n * sizeof(T), alignof(T) < 16 ? 16 : alignof(T), typeid(T).name())); }
		void deallocate(T* p, size_t n) noexcept { Free(p, n * sizeof(T)); }
		template <class U> bool operator==(const SceneAllocator<U>&) const noexcept { return true; }
		template <class U> bool operator!=(const SceneAllocator<U>&) const noexcept { return false; }
	};

	template <class T, class... Args>
	std::shared_ptr<T> MakeShared(Args&&... args)
	{
		return std::allocate_shared<T>(SceneAllocator<T>(), std::forward<Args>(args)...);
	}

	NOVA_API nlohmann::json Info();
	NOVA_API void RegisterEditor();   // CLI: nova memory (Allocators · 힙 통계 · 검사)
}
