#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <new>
#include <string>
#include <type_traits>
#include <vector>
#include "LockFree.h"

// NOVA 메모리 알로케이터 (docs/MEMORY_ALLOCATORS.md).
//  - LinearAllocator : 앞으로만 잘라 쓰고 한 번에 비운다 (프레임 임시 · 불러오기 임시). AllocateAtomic = 여러 스레드가 함께 (CAS 하나)
//  - StackAllocator  : 표시 (Marker) 까지 되돌린다 — 안쪽 범위부터 (LIFO)
//  - PoolAllocator   : 같은 크기 블록 (GameObject · Transform …). 빈 블록 목록 (침입형). ConcurrentPool = 무잠금 (MPMC 링)
//  - BuddyAllocator  : 2 의 거듭제곱 블록을 나누고 · 합친다. 다 놓으면 처음의 한 덩어리로 돌아온다 (단편화 0) — 씬 힙
//  - 모든 블록은 캐시 라인 (64 바이트) 에 맞춘다. CacheAligned<T> = 한 줄을 혼자 쓰는 값 (거짓 공유 막기)
//  - 메모리 영역은 Region (Windows: VirtualAlloc 예약 → 쓰는 만큼 확정, 다 놓으면 되돌림)
namespace Memory
{
	inline constexpr size_t kCacheLine = 64;

	inline size_t AlignUp(size_t v, size_t a) { return (v + (a - 1)) & ~(a - 1); }
	inline bool IsPow2(size_t v) { return v && !(v & (v - 1)); }

	// 한 캐시 라인을 혼자 쓴다 (스레드마다 바뀌는 값을 나란히 둘 때) — alignas(64) 라 sizeof 가 64 의 배수로 올라간다
	template <class T>
	struct alignas(kCacheLine) CacheAligned
	{
		T Value{};
		T& operator*() { return Value; }
		const T& operator*() const { return Value; }
		T* operator->() { return &Value; }
	};

	// ---------------------------------------------------------------- 가상 메모리 영역
	class Region
	{
	public:
		Region() = default;
		~Region() { Free(); }
		Region(const Region&) = delete;
		Region& operator=(const Region&) = delete;

		bool Reserve(size_t bytes);   // 주소만 잡는다 (Windows) — 다른 곳은 바로 할당
		void Free();
		// [offset, offset + bytes) 를 확정 (64 KB 단위, 이미 된 곳은 건너뛴다)
		bool Commit(size_t offset, size_t bytes);
		void DecommitAll();           // 실제 메모리를 되돌린다 (주소는 그대로)
		uint8_t* Base() const { return m_Base; }
		size_t Size() const { return m_Size; }
		size_t Committed() const { return m_Committed; }
		bool Contains(const void* p) const { return p >= m_Base && p < m_Base + m_Size; }

	private:
		static constexpr size_t kGranule = 64 * 1024;
		uint8_t* m_Base = nullptr;
		size_t m_Size = 0;
		size_t m_Committed = 0;
		std::vector<uint8_t> m_CommittedMap;   // 64 KB 마다 확정했나
		bool m_Virtual = false;
	};

	// ---------------------------------------------------------------- Linear
	class LinearAllocator
	{
	public:
		LinearAllocator() = default;
		explicit LinearAllocator(size_t capacity) { Init(capacity); }
		bool Init(size_t capacity);
		void* Allocate(size_t bytes, size_t align = 16);         // 한 스레드
		void* AllocateAtomic(size_t bytes, size_t align = 16);   // 여러 스레드 (무잠금)
		void Reset() { m_Offset.store(0, std::memory_order_relaxed); ++m_Resets; }
		size_t Marker() const { return m_Offset.load(std::memory_order_relaxed); }
		void Rewind(size_t marker) { m_Offset.store(marker, std::memory_order_relaxed); }
		size_t Used() const { return m_Offset.load(std::memory_order_relaxed); }
		size_t Capacity() const { return m_Region.Size(); }
		size_t Peak() const { return m_Peak.load(std::memory_order_relaxed); }
		uint64_t Failures() const { return m_Failures.load(std::memory_order_relaxed); }
		bool Owns(const void* p) const { return m_Region.Contains(p); }

	private:
		Region m_Region;
		alignas(kCacheLine) std::atomic<size_t> m_Offset{ 0 };
		std::atomic<size_t> m_Peak{ 0 };
		std::atomic<uint64_t> m_Failures{ 0 };
		uint64_t m_Resets = 0;
	};

	// ---------------------------------------------------------------- Stack (LIFO)
	class StackAllocator
	{
	public:
		using Marker = size_t;
		explicit StackAllocator(size_t capacity = 0) { if (capacity) Init(capacity); }
		bool Init(size_t capacity);
		void* Allocate(size_t bytes, size_t align = 16);
		Marker GetMarker() const { return m_Top; }
		void FreeToMarker(Marker m) { if (m <= m_Top) m_Top = m; }
		// 맨 위 블록만 놓는다 (블록 머리에 앞 위치 · 끝을 적어 둔다) — 다른 블록이면 false
		bool FreeTop(void* p);
		void Clear() { m_Top = 0; }
		size_t Used() const { return m_Top; }
		size_t Capacity() const { return m_Region.Size(); }
		size_t Peak() const { return m_Peak; }

	private:
		Region m_Region;
		size_t m_Top = 0, m_Peak = 0;
	};

	// ---------------------------------------------------------------- Pool (한 스레드 — 씬 힙 안에서)
	class PoolAllocator
	{
	public:
		// blockSize 는 캐시 라인에 맞춰 올린다. 블록을 다 쓰면 블록 묶음 (chunk) 을 더 받는다 (getChunk = 위 힙 · 없으면 시스템)
		using ChunkFn = void* (*)(size_t bytes, void* user);
		using FreeChunkFn = void (*)(void* p, void* user);
		void Init(size_t blockSize, size_t blocksPerChunk, ChunkFn getChunk = nullptr, FreeChunkFn freeChunk = nullptr, void* user = nullptr);
		~PoolAllocator() { Release(); }
		void* Allocate();
		void Free(void* p);
		bool Owns(const void* p) const;
		void Release();   // 묶음을 모두 돌려준다
		size_t BlockSize() const { return m_BlockSize; }
		size_t Live() const { return m_Live; }
		size_t Capacity() const { return m_Chunks.size() * m_PerChunk; }
		size_t Chunks() const { return m_Chunks.size(); }

	private:
		struct FreeNode { FreeNode* Next; };
		size_t m_BlockSize = 0, m_PerChunk = 0, m_Live = 0;
		FreeNode* m_Free = nullptr;
		std::vector<uint8_t*> m_Chunks;
		ChunkFn m_GetChunk = nullptr;
		FreeChunkFn m_FreeChunk = nullptr;
		void* m_User = nullptr;
	};

	// ---------------------------------------------------------------- 무잠금 Pool (여러 스레드) — 블록 번호를 MPMC 링으로
	class ConcurrentPool
	{
	public:
		bool Init(size_t blockSize, size_t blocks);
		void* Allocate();       // 없으면 nullptr
		void Free(void* p);
		bool Owns(const void* p) const { return m_Region.Contains(p); }
		size_t BlockSize() const { return m_BlockSize; }
		size_t Blocks() const { return m_Blocks; }
		size_t FreeApprox() const { return m_FreeList ? m_FreeList->SizeApprox() : 0; }

	private:
		Region m_Region;
		size_t m_BlockSize = 0, m_Blocks = 0;
		std::unique_ptr<LockFree::MpmcRing<uint32_t>> m_FreeList;
	};

	// ---------------------------------------------------------------- Buddy
	class BuddyAllocator
	{
	public:
		// capacity 는 2 의 거듭제곱으로 올린다. minBlock = 가장 작은 블록 (2 의 거듭제곱, 64 이상 — 모든 블록이 캐시 라인에 맞는다).
		//  칸 지도는 minBlock 마다 2 바이트 (예: 256 MB 힙 · 1 KB 블록 = 512 KB)
		bool Init(size_t capacity, size_t minBlock = kCacheLine);
		size_t MinBlock() const { return m_MinBlock; }
		~BuddyAllocator() = default;
		void* Allocate(size_t bytes);   // 블록 크기에 맞춰 정렬 (64 바이트 이상)
		void Free(void* p);
		bool Owns(const void* p) const { return m_Region.Contains(p); }
		size_t BlockSizeOf(const void* p) const;

		// 통계: 쓰는 바이트 (블록) · 개수 · 가장 큰 빈 블록 · 단편화 (1 − 가장 큰 빈 블록 / 빈 바이트)
		size_t Capacity() const { return m_Capacity; }
		size_t Used() const { return m_Used; }
		size_t Live() const { return m_Live; }
		size_t Peak() const { return m_Peak; }
		size_t LargestFree() const;
		double Fragmentation() const;
		size_t Committed() const { return m_Region.Committed(); }
		bool Empty() const { return m_Live == 0; }
		void DecommitIfEmpty();   // 다 놓였으면 실제 메모리를 되돌린다 (주소는 그대로)
		bool Validate(std::string* error = nullptr) const;   // 빈 목록 · 지도가 맞나 (검사)

	private:
		struct FreeBlock { FreeBlock* Next; FreeBlock* Prev; };
		int OrderFor(size_t bytes) const;
		void PushFree(size_t index, int order);
		void RemoveFree(size_t index, int order);
		uint8_t* Ptr(size_t index) const { return m_Region.Base() + index * m_MinBlock; }

		Region m_Region;
		size_t m_Capacity = 0, m_MinBlock = kCacheLine;
		int m_MaxOrder = 0;
		std::vector<FreeBlock*> m_FreeLists;
		std::vector<uint8_t> m_FreeOrder;    // 최소 블록 칸마다: 빈 블록의 시작이면 order + 1, 아니면 0
		std::vector<uint8_t> m_AllocOrder;   // 최소 블록 칸마다: 할당한 블록의 시작이면 order + 1
		size_t m_Used = 0, m_Live = 0, m_Peak = 0;
	};

	// ---------------------------------------------------------------- std::pmr 어댑터 (컨테이너가 알로케이터를 쓰게)
	class LinearResource final : public std::pmr::memory_resource
	{
	public:
		explicit LinearResource(LinearAllocator& a) : m_A(a) {}
	private:
		void* do_allocate(size_t bytes, size_t align) override
		{
			if (void* p = m_A.AllocateAtomic(bytes, align < 16 ? 16 : align))
				return p;
			return std::pmr::new_delete_resource()->allocate(bytes, align);   // 가득 — 시스템으로 (드묾, 통계에 남는다)
		}
		void do_deallocate(void* p, size_t bytes, size_t align) override
		{
			if (!m_A.Owns(p))
				std::pmr::new_delete_resource()->deallocate(p, bytes, align);   // 프레임 끝에 한꺼번에 — 따로 놓지 않는다
		}
		bool do_is_equal(const std::pmr::memory_resource& o) const noexcept override { return this == &o; }
		LinearAllocator& m_A;
	};
}
