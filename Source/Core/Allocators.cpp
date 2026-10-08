#include "pch.h"
#include "Allocators.h"
#include <cstring>
#include <cstdlib>
#include <algorithm>

#if defined(_WIN32) && !defined(NOVA_ANDROID) && !defined(NOVA_WEB)
#define NOVA_VIRTUAL_MEMORY 1
#endif

// 알로케이터 — Allocators.h 의 설명
namespace Memory
{
	namespace
	{
		void* AlignedAlloc(size_t bytes, size_t align)
		{
#if defined(_MSC_VER)
			return _aligned_malloc(bytes, align);
#else
			void* p = nullptr;
			return posix_memalign(&p, align, bytes) == 0 ? p : nullptr;
#endif
		}
		void AlignedFree(void* p)
		{
#if defined(_MSC_VER)
			_aligned_free(p);
#else
			free(p);
#endif
		}
		int Log2(size_t v) { int n = 0; while ((size_t(1) << n) < v) ++n; return n; }
	}

	// ================================================================ Region
	bool Region::Reserve(size_t bytes)
	{
		Free();
		if (bytes == 0)
			return false;
#if NOVA_VIRTUAL_MEMORY
		const size_t size = AlignUp(bytes, kGranule);
		m_Base = static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE));
		if (!m_Base)
			return false;
		m_Size = size;
		m_Virtual = true;
		m_CommittedMap.assign(size / kGranule, 0);
		m_Committed = 0;
#else
		// 가상 메모리 예약이 없는 곳 (안드로이드 · 웹): 바로 잡는다 (전부 확정)
		m_Base = static_cast<uint8_t*>(AlignedAlloc(bytes, kCacheLine));
		if (!m_Base)
			return false;
		m_Size = bytes;
		m_Virtual = false;
		m_Committed = bytes;
#endif
		return true;
	}

	void Region::Free()
	{
		if (!m_Base)
			return;
#if NOVA_VIRTUAL_MEMORY
		if (m_Virtual)
			VirtualFree(m_Base, 0, MEM_RELEASE);
		else
			AlignedFree(m_Base);
#else
		AlignedFree(m_Base);
#endif
		m_Base = nullptr;
		m_Size = m_Committed = 0;
		m_CommittedMap.clear();
	}

	bool Region::Commit(size_t offset, size_t bytes)
	{
		if (!m_Virtual || bytes == 0)
			return m_Base != nullptr;
#if NOVA_VIRTUAL_MEMORY
		const size_t g0 = offset / kGranule, g1 = (std::min)((offset + bytes - 1) / kGranule, m_CommittedMap.size() - 1);
		size_t g = g0;
		while (g <= g1)
		{
			if (m_CommittedMap[g]) { ++g; continue; }
			size_t run = g;
			while (run <= g1 && !m_CommittedMap[run]) ++run;   // 이어진 미확정 칸을 한 번에
			const size_t start = g * kGranule, len = (std::min)((run - g) * kGranule, m_Size - start);
			if (!VirtualAlloc(m_Base + start, len, MEM_COMMIT, PAGE_READWRITE))
				return false;
			for (size_t k = g; k < run; ++k) m_CommittedMap[k] = 1;
			m_Committed += len;
			g = run;
		}
#endif
		return true;
	}

	void Region::DecommitAll()
	{
#if NOVA_VIRTUAL_MEMORY
		if (m_Virtual && m_Committed)
		{
			VirtualFree(m_Base, m_Size, MEM_DECOMMIT);
			std::fill(m_CommittedMap.begin(), m_CommittedMap.end(), uint8_t(0));
			m_Committed = 0;
		}
#endif
	}

	// ================================================================ Linear
	bool LinearAllocator::Init(size_t capacity)
	{
		m_Offset = 0;
		m_Peak = 0;
		// 여러 스레드가 함께 자르므로 처음에 다 확정한다 (자를 때 시스템 호출 없음)
		return m_Region.Reserve(capacity) && m_Region.Commit(0, m_Region.Size());
	}

	void* LinearAllocator::Allocate(size_t bytes, size_t align)
	{
		const size_t start = AlignUp(m_Offset.load(std::memory_order_relaxed), align);
		const size_t end = start + bytes;
		if (end > m_Region.Size())
		{
			m_Failures.fetch_add(1, std::memory_order_relaxed);
			return nullptr;
		}
		m_Offset.store(end, std::memory_order_relaxed);
		if (end > m_Peak.load(std::memory_order_relaxed)) m_Peak.store(end, std::memory_order_relaxed);
		return m_Region.Base() + start;
	}

	void* LinearAllocator::AllocateAtomic(size_t bytes, size_t align)
	{
		size_t cur = m_Offset.load(std::memory_order_relaxed);
		for (;;)
		{
			const size_t start = AlignUp(cur, align);
			const size_t end = start + bytes;
			if (end > m_Region.Size())
			{
				m_Failures.fetch_add(1, std::memory_order_relaxed);
				return nullptr;
			}
			if (m_Offset.compare_exchange_weak(cur, end, std::memory_order_relaxed))
			{
				size_t peak = m_Peak.load(std::memory_order_relaxed);
				while (end > peak && !m_Peak.compare_exchange_weak(peak, end, std::memory_order_relaxed)) {}
				return m_Region.Base() + start;
			}
		}
	}

	// ================================================================ Stack
	bool StackAllocator::Init(size_t capacity)
	{
		m_Top = m_Peak = 0;
		return m_Region.Reserve(capacity) && m_Region.Commit(0, m_Region.Size());
	}

	void* StackAllocator::Allocate(size_t bytes, size_t align)
	{
		struct Header { size_t Prev, End; };
		const size_t start = AlignUp(m_Top + sizeof(Header), align < alignof(Header) ? alignof(Header) : align);
		const size_t end = start + bytes;
		if (end > m_Region.Size())
			return nullptr;
		Header* h = reinterpret_cast<Header*>(m_Region.Base() + start - sizeof(Header));
		h->Prev = m_Top;
		h->End = end;
		m_Top = end;
		m_Peak = (std::max)(m_Peak, m_Top);
		return m_Region.Base() + start;
	}

	bool StackAllocator::FreeTop(void* p)
	{
		struct Header { size_t Prev, End; };
		if (!m_Region.Contains(p))
			return false;
		const Header* h = reinterpret_cast<const Header*>(static_cast<uint8_t*>(p) - sizeof(Header));
		if (h->End != m_Top)
			return false;   // 맨 위가 아니다 (안쪽부터 놓아야 한다)
		m_Top = h->Prev;
		return true;
	}

	// ================================================================ Pool
	void PoolAllocator::Init(size_t blockSize, size_t blocksPerChunk, ChunkFn getChunk, FreeChunkFn freeChunk, void* user)
	{
		Release();
		m_BlockSize = AlignUp((std::max)(blockSize, sizeof(FreeNode)), kCacheLine);
		m_PerChunk = (std::max<size_t>)(1, blocksPerChunk);
		m_GetChunk = getChunk;
		m_FreeChunk = freeChunk;
		m_User = user;
	}

	void* PoolAllocator::Allocate()
	{
		if (!m_Free)
		{
			const size_t bytes = m_BlockSize * m_PerChunk;
			uint8_t* chunk = static_cast<uint8_t*>(m_GetChunk ? m_GetChunk(bytes, m_User) : AlignedAlloc(bytes, kCacheLine));
			if (!chunk)
				return nullptr;
			m_Chunks.push_back(chunk);
			for (size_t i = m_PerChunk; i-- > 0;)   // 앞 블록부터 나가게 (주소가 이어진다)
			{
				FreeNode* n = reinterpret_cast<FreeNode*>(chunk + i * m_BlockSize);
				n->Next = m_Free;
				m_Free = n;
			}
		}
		FreeNode* n = m_Free;
		m_Free = n->Next;
		++m_Live;
		return n;
	}

	void PoolAllocator::Free(void* p)
	{
		if (!p)
			return;
		FreeNode* n = static_cast<FreeNode*>(p);
		n->Next = m_Free;
		m_Free = n;
		--m_Live;
	}

	bool PoolAllocator::Owns(const void* p) const
	{
		const uint8_t* b = static_cast<const uint8_t*>(p);
		for (uint8_t* c : m_Chunks)
			if (b >= c && b < c + m_BlockSize * m_PerChunk)
				return true;
		return false;
	}

	void PoolAllocator::Release()
	{
		for (uint8_t* c : m_Chunks)
		{
			if (m_FreeChunk) m_FreeChunk(c, m_User);
			else AlignedFree(c);
		}
		m_Chunks.clear();
		m_Free = nullptr;
		m_Live = 0;
	}

	// ================================================================ ConcurrentPool
	bool ConcurrentPool::Init(size_t blockSize, size_t blocks)
	{
		m_BlockSize = AlignUp((std::max)(blockSize, sizeof(uint32_t)), kCacheLine);   // 블록마다 줄을 따로 (거짓 공유 없음)
		m_Blocks = blocks;
		if (!m_Region.Reserve(m_BlockSize * blocks) || !m_Region.Commit(0, m_BlockSize * blocks))
			return false;
		m_FreeList = std::make_unique<LockFree::MpmcRing<uint32_t>>(blocks);
		for (size_t i = 0; i < blocks; ++i)
			m_FreeList->Push((uint32_t)i);
		return true;
	}

	void* ConcurrentPool::Allocate()
	{
		uint32_t i;
		return m_FreeList && m_FreeList->Pop(i) ? m_Region.Base() + (size_t)i * m_BlockSize : nullptr;
	}

	void ConcurrentPool::Free(void* p)
	{
		if (!p || !Owns(p))
			return;
		const uint32_t i = (uint32_t)((static_cast<uint8_t*>(p) - m_Region.Base()) / m_BlockSize);
		while (!m_FreeList->Push(i)) {}   // 링 = 블록 수 — 늘 들어간다
	}

	// ================================================================ Buddy
	bool BuddyAllocator::Init(size_t capacity, size_t minBlock)
	{
		m_MinBlock = kCacheLine;
		while (m_MinBlock < minBlock) m_MinBlock <<= 1;
		size_t cap = m_MinBlock;
		while (cap < capacity) cap <<= 1;
		if (!m_Region.Reserve(cap))
			return false;
		m_Capacity = cap;
		m_MaxOrder = Log2(cap / m_MinBlock);
		m_FreeLists.assign((size_t)m_MaxOrder + 1, nullptr);
		m_FreeOrder.assign(cap / m_MinBlock, 0);
		m_AllocOrder.assign(cap / m_MinBlock, 0);
		m_Used = m_Live = m_Peak = 0;
		PushFree(0, m_MaxOrder);   // 처음엔 통째로 한 블록
		return true;
	}

	int BuddyAllocator::OrderFor(size_t bytes) const
	{
		const size_t blocks = (std::max<size_t>)(1, (bytes + m_MinBlock - 1) / m_MinBlock);
		return Log2(blocks);
	}

	void BuddyAllocator::PushFree(size_t index, int order)
	{
		m_Region.Commit(index * m_MinBlock, sizeof(FreeBlock));   // 빈 블록의 머리 (목록 고리) 를 쓸 곳만
		FreeBlock* b = reinterpret_cast<FreeBlock*>(Ptr(index));
		b->Prev = nullptr;
		b->Next = m_FreeLists[(size_t)order];
		if (b->Next)
			b->Next->Prev = b;
		m_FreeLists[(size_t)order] = b;
		m_FreeOrder[index] = (uint8_t)(order + 1);
	}

	void BuddyAllocator::RemoveFree(size_t index, int order)
	{
		FreeBlock* b = reinterpret_cast<FreeBlock*>(Ptr(index));
		if (b->Prev) b->Prev->Next = b->Next;
		else m_FreeLists[(size_t)order] = b->Next;
		if (b->Next) b->Next->Prev = b->Prev;
		m_FreeOrder[index] = 0;
	}

	void* BuddyAllocator::Allocate(size_t bytes)
	{
		if (!m_Capacity)
			return nullptr;
		const int order = OrderFor(bytes);
		if (order > m_MaxOrder)
			return nullptr;
		int k = order;
		while (k <= m_MaxOrder && !m_FreeLists[(size_t)k])
			++k;
		if (k > m_MaxOrder)
			return nullptr;   // 가득 (또는 그만큼 이어진 빈 곳이 없다)
		const size_t index = (size_t)(reinterpret_cast<uint8_t*>(m_FreeLists[(size_t)k]) - m_Region.Base()) / m_MinBlock;
		RemoveFree(index, k);
		while (k > order)   // 둘로 나눠 뒤 반쪽 (짝) 은 빈 목록으로
		{
			--k;
			PushFree(index + (size_t(1) << k), k);
		}
		const size_t blockBytes = m_MinBlock << order;
		if (!m_Region.Commit(index * m_MinBlock, blockBytes))
		{
			PushFree(index, order);   // 확정 실패 (메모리 부족) — 되돌린다 (짝과는 다음 Free 에서 합쳐진다)
			return nullptr;
		}
		m_AllocOrder[index] = (uint8_t)(order + 1);
		m_Used += blockBytes;
		++m_Live;
		m_Peak = (std::max)(m_Peak, m_Used);
		return Ptr(index);
	}

	void BuddyAllocator::Free(void* p)
	{
		if (!p || !Owns(p))
			return;
		size_t index = (size_t)(static_cast<uint8_t*>(p) - m_Region.Base()) / m_MinBlock;
		if (index >= m_AllocOrder.size() || m_AllocOrder[index] == 0)
			return;   // 이 힙의 블록 시작이 아니다 (두 번 놓기 등) — 무시
		int k = m_AllocOrder[index] - 1;
		m_AllocOrder[index] = 0;
		m_Used -= m_MinBlock << k;
		--m_Live;
		while (k < m_MaxOrder)   // 짝도 같은 크기로 비어 있으면 합친다
		{
			const size_t buddy = index ^ (size_t(1) << k);
			if (m_FreeOrder[buddy] != (uint8_t)(k + 1))
				break;
			RemoveFree(buddy, k);
			index = (std::min)(index, buddy);
			++k;
		}
		PushFree(index, k);
	}

	size_t BuddyAllocator::BlockSizeOf(const void* p) const
	{
		if (!Owns(p))
			return 0;
		const size_t index = (size_t)(static_cast<const uint8_t*>(p) - m_Region.Base()) / m_MinBlock;
		return m_AllocOrder[index] ? m_MinBlock << (m_AllocOrder[index] - 1) : 0;
	}

	size_t BuddyAllocator::LargestFree() const
	{
		for (int k = m_MaxOrder; k >= 0; --k)
			if (m_FreeLists[(size_t)k])
				return m_MinBlock << k;
		return 0;
	}

	double BuddyAllocator::Fragmentation() const
	{
		const size_t freeBytes = m_Capacity - m_Used;
		return freeBytes ? 1.0 - (double)LargestFree() / (double)freeBytes : 0.0;
	}

	void BuddyAllocator::DecommitIfEmpty()
	{
		if (m_Live != 0 || !m_Capacity)
			return;
		// 다 놓였다 = 통째로 한 블록 (짝이 모두 합쳐졌다). 실제 메모리를 되돌리고 머리만 다시
		m_Region.DecommitAll();
		std::fill(m_FreeLists.begin(), m_FreeLists.end(), nullptr);
		std::fill(m_FreeOrder.begin(), m_FreeOrder.end(), uint8_t(0));
		PushFree(0, m_MaxOrder);
	}

	bool BuddyAllocator::Validate(std::string* error) const
	{
		// 빈 블록 + 할당한 블록이 겹치지 않고 전체를 정확히 덮는가
		std::vector<uint8_t> covered(m_AllocOrder.size(), 0);
		auto mark = [&](size_t index, int k, const char* what) {
			const size_t n = size_t(1) << k;
			for (size_t i = index; i < index + n; ++i)
			{
				if (i >= covered.size() || covered[i])
				{
					if (error) *error = std::string("overlap or out of range at block ") + std::to_string(i) + " (" + what + ")";
					return false;
				}
				covered[i] = 1;
			}
			return true;
		};
		for (int k = 0; k <= m_MaxOrder; ++k)
			for (FreeBlock* b = m_FreeLists[(size_t)k]; b; b = b->Next)
			{
				const size_t index = (size_t)(reinterpret_cast<uint8_t*>(b) - m_Region.Base()) / m_MinBlock;
				if (m_FreeOrder[index] != (uint8_t)(k + 1)) { if (error) *error = "free map mismatch at " + std::to_string(index); return false; }
				if (!mark(index, k, "free")) return false;
			}
		for (size_t i = 0; i < m_AllocOrder.size(); ++i)
			if (m_AllocOrder[i] && !mark(i, m_AllocOrder[i] - 1, "allocated"))
				return false;
		for (size_t i = 0; i < covered.size(); ++i)
			if (!covered[i]) { if (error) *error = "block " + std::to_string(i) + " neither free nor allocated"; return false; }
		return true;
	}
}
