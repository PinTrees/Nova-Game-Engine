#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>

// 무잠금 (lock-free) 자료 구조 — Job System · 스레드 사이 데이터 핑퐁에 쓴다 (docs/CONCURRENCY_ROADMAP.md).
//  - SpscRing      : 한 스레드가 넣고 한 스레드가 꺼내는 고정 크기 링 (Profiler 일꾼 구간 · 명령 스트림)
//  - MpmcRing      : 여러 스레드가 넣고 꺼내는 고정 크기 링 (Dmitry Vyukov 의 bounded MPMC — 칸마다 순번)
//  - WorkStealingDeque : 주인은 아래에서 넣고 꺼내고 (LIFO), 다른 스레드는 위에서 훔친다 (FIFO) — Chase-Lev,
//                    약한 메모리 모델 판 (Lê · Pop · Cohen · Zappa Nardelli, PPoPP 2013)
//  - TripleBuffer  : 쓰는 쪽 하나 · 읽는 쪽 하나가 서로 기다리지 않고 최신 값을 주고받는다 (쓰기 · 가운데 · 읽기 칸을 원자 교환)
// 모두 잠금 · 시스템 호출 없이 원자 연산만 쓴다. 크기는 2 의 거듭제곱으로 올린다.
namespace LockFree
{
	// 거짓 공유 (false sharing) 를 막는 줄 크기
	inline constexpr size_t kCacheLine = 64;

	inline size_t RoundUpPow2(size_t v)
	{
		size_t p = 1;
		while (p < v) p <<= 1;
		return p;
	}

	// ------------------------------------------------------------------ SPSC
	template <class T>
	class SpscRing
	{
		static_assert(std::is_trivially_copyable_v<T> || std::is_move_assignable_v<T>, "T must be movable");
	public:
		explicit SpscRing(size_t capacity = 1024) : m_Mask(RoundUpPow2(capacity < 2 ? 2 : capacity) - 1), m_Buf(new T[m_Mask + 1]) {}
		SpscRing(const SpscRing&) = delete;
		SpscRing& operator=(const SpscRing&) = delete;

		size_t Capacity() const { return m_Mask + 1; }

		// 넣는 스레드만
		bool Push(const T& v)
		{
			const size_t t = m_Tail.load(std::memory_order_relaxed);
			if (t - m_HeadCache == Capacity())
			{
				m_HeadCache = m_Head.load(std::memory_order_acquire);
				if (t - m_HeadCache == Capacity())
					return false;
			}
			m_Buf[t & m_Mask] = v;
			m_Tail.store(t + 1, std::memory_order_release);
			return true;
		}
		// 꺼내는 스레드만
		bool Pop(T& out)
		{
			const size_t h = m_Head.load(std::memory_order_relaxed);
			if (h == m_TailCache)
			{
				m_TailCache = m_Tail.load(std::memory_order_acquire);
				if (h == m_TailCache)
					return false;
			}
			out = std::move(m_Buf[h & m_Mask]);
			m_Head.store(h + 1, std::memory_order_release);
			return true;
		}
		size_t SizeApprox() const { return m_Tail.load(std::memory_order_acquire) - m_Head.load(std::memory_order_acquire); }

	private:
		const size_t m_Mask;
		std::unique_ptr<T[]> m_Buf;
		alignas(kCacheLine) std::atomic<size_t> m_Head{ 0 };
		size_t m_TailCache = 0;   // 꺼내는 스레드의 사본 (넣는 쪽 줄을 덜 읽는다)
		alignas(kCacheLine) std::atomic<size_t> m_Tail{ 0 };
		size_t m_HeadCache = 0;   // 넣는 스레드의 사본
	};

	// ------------------------------------------------------------------ MPMC (Vyukov)
	template <class T>
	class MpmcRing
	{
		struct Cell
		{
			std::atomic<size_t> Seq;
			T Value;
		};
	public:
		explicit MpmcRing(size_t capacity = 1024) : m_Mask(RoundUpPow2(capacity < 2 ? 2 : capacity) - 1), m_Cells(new Cell[m_Mask + 1])
		{
			for (size_t i = 0; i <= m_Mask; ++i)
				m_Cells[i].Seq.store(i, std::memory_order_relaxed);
		}
		MpmcRing(const MpmcRing&) = delete;
		MpmcRing& operator=(const MpmcRing&) = delete;

		size_t Capacity() const { return m_Mask + 1; }

		bool Push(const T& v)
		{
			size_t pos = m_Enqueue.load(std::memory_order_relaxed);
			for (;;)
			{
				Cell& c = m_Cells[pos & m_Mask];
				const size_t seq = c.Seq.load(std::memory_order_acquire);
				const intptr_t diff = (intptr_t)seq - (intptr_t)pos;
				if (diff == 0)
				{
					if (m_Enqueue.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
					{
						c.Value = v;
						c.Seq.store(pos + 1, std::memory_order_release);
						return true;
					}
				}
				else if (diff < 0)
					return false;   // 가득 참
				else
					pos = m_Enqueue.load(std::memory_order_relaxed);
			}
		}
		bool Pop(T& out)
		{
			size_t pos = m_Dequeue.load(std::memory_order_relaxed);
			for (;;)
			{
				Cell& c = m_Cells[pos & m_Mask];
				const size_t seq = c.Seq.load(std::memory_order_acquire);
				const intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);
				if (diff == 0)
				{
					if (m_Dequeue.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
					{
						out = std::move(c.Value);
						c.Seq.store(pos + m_Mask + 1, std::memory_order_release);
						return true;
					}
				}
				else if (diff < 0)
					return false;   // 비었음
				else
					pos = m_Dequeue.load(std::memory_order_relaxed);
			}
		}
		// 대략의 개수 (다른 스레드가 바꾸는 중일 수 있다 — 깨울지 정할 때만)
		size_t SizeApprox() const
		{
			const size_t e = m_Enqueue.load(std::memory_order_acquire), d = m_Dequeue.load(std::memory_order_acquire);
			return e > d ? e - d : 0;
		}

	private:
		const size_t m_Mask;
		std::unique_ptr<Cell[]> m_Cells;
		alignas(kCacheLine) std::atomic<size_t> m_Enqueue{ 0 };
		alignas(kCacheLine) std::atomic<size_t> m_Dequeue{ 0 };
	};

	// ------------------------------------------------------------------ Chase-Lev 작업 훔치기 덱 (포인터만)
	template <class T>
	class WorkStealingDeque
	{
		static_assert(std::is_pointer_v<T>, "WorkStealingDeque holds pointers");
	public:
		explicit WorkStealingDeque(size_t capacity = 4096) : m_Mask(RoundUpPow2(capacity < 2 ? 2 : capacity) - 1), m_Buf(new std::atomic<T>[m_Mask + 1])
		{
			for (size_t i = 0; i <= m_Mask; ++i)
				m_Buf[i].store(nullptr, std::memory_order_relaxed);
		}
		WorkStealingDeque(const WorkStealingDeque&) = delete;
		WorkStealingDeque& operator=(const WorkStealingDeque&) = delete;

		size_t Capacity() const { return m_Mask + 1; }

		// 주인만: 아래에 넣는다. 가득 차면 false (부른 쪽이 공용 큐로)
		bool Push(T v)
		{
			const int64_t b = m_Bottom.load(std::memory_order_relaxed);
			const int64_t t = m_Top.load(std::memory_order_acquire);
			if (b - t >= (int64_t)Capacity())
				return false;
			m_Buf[(size_t)b & m_Mask].store(v, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			m_Bottom.store(b + 1, std::memory_order_relaxed);
			return true;
		}
		// 주인만: 아래에서 꺼낸다 (마지막 하나는 훔치는 쪽과 CAS 로 겨룬다)
		T Pop()
		{
			const int64_t b = m_Bottom.load(std::memory_order_relaxed) - 1;
			m_Bottom.store(b, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_seq_cst);
			int64_t t = m_Top.load(std::memory_order_relaxed);
			if (t <= b)
			{
				T v = m_Buf[(size_t)b & m_Mask].load(std::memory_order_relaxed);
				if (t == b)
				{
					if (!m_Top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
						v = nullptr;   // 훔쳐 갔다
					m_Bottom.store(b + 1, std::memory_order_relaxed);
				}
				return v;
			}
			m_Bottom.store(b + 1, std::memory_order_relaxed);
			return nullptr;
		}
		// 아무 스레드: 위에서 훔친다 (겨루다 지면 nullptr — 다시 해도 된다)
		T Steal()
		{
			int64_t t = m_Top.load(std::memory_order_acquire);
			std::atomic_thread_fence(std::memory_order_seq_cst);
			const int64_t b = m_Bottom.load(std::memory_order_acquire);
			if (t < b)
			{
				T v = m_Buf[(size_t)t & m_Mask].load(std::memory_order_relaxed);
				if (!m_Top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
					return nullptr;
				return v;
			}
			return nullptr;
		}
		bool EmptyApprox() const { return m_Bottom.load(std::memory_order_acquire) <= m_Top.load(std::memory_order_acquire); }
		int64_t SizeApprox() const
		{
			const int64_t n = m_Bottom.load(std::memory_order_acquire) - m_Top.load(std::memory_order_acquire);
			return n > 0 ? n : 0;
		}

	private:
		const size_t m_Mask;
		std::unique_ptr<std::atomic<T>[]> m_Buf;
		alignas(kCacheLine) std::atomic<int64_t> m_Top{ 0 };
		alignas(kCacheLine) std::atomic<int64_t> m_Bottom{ 0 };
	};

	// ------------------------------------------------------------------ 삼중 버퍼 (쓰기 1 · 읽기 1)
	//  쓰는 쪽: Write() 칸에 다 쓰고 Publish(). 읽는 쪽: Acquire() 가 새 판이 있으면 바꿔 true, Read() 로 읽는다.
	//  어느 쪽도 기다리지 않는다 — 읽는 쪽은 늘 다 쓴 최신 판을 본다 (중간 판은 건너뛸 수 있다)
	template <class T>
	class TripleBuffer
	{
		// 가운데 칸 번호 (아래 2 비트) + 새 판 표시 (4)
		static constexpr uint32_t kDirty = 4;
	public:
		TripleBuffer() = default;
		explicit TripleBuffer(const T& init) { m_Slots[0] = m_Slots[1] = m_Slots[2] = init; }

		T& Write() { return m_Slots[m_Back]; }
		void Publish()
		{
			const uint32_t prev = m_Middle.exchange(m_Back | kDirty, std::memory_order_acq_rel);
			m_Back = prev & 3u;
			m_Published.fetch_add(1, std::memory_order_relaxed);
		}
		// 새 판이 있으면 읽기 칸과 바꾼다
		bool Acquire()
		{
			if ((m_Middle.load(std::memory_order_relaxed) & kDirty) == 0)
				return false;
			const uint32_t prev = m_Middle.exchange(m_Front, std::memory_order_acq_rel);
			m_Front = prev & 3u;
			return true;
		}
		const T& Read() const { return m_Slots[m_Front]; }
		T& ReadMutable() { return m_Slots[m_Front]; }
		uint64_t PublishedCount() const { return m_Published.load(std::memory_order_relaxed); }

	private:
		T m_Slots[3]{};
		alignas(kCacheLine) std::atomic<uint32_t> m_Middle{ 1 };
		alignas(kCacheLine) uint32_t m_Back = 0;    // 쓰는 스레드만
		alignas(kCacheLine) uint32_t m_Front = 2;   // 읽는 스레드만
		std::atomic<uint64_t> m_Published{ 0 };
	};
}
