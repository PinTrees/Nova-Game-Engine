#include "pch.h"
#include "Profiler.h"
#include "LockFree.h"
#include <atomic>
#include <chrono>
#include <unordered_set>
#include <thread>

namespace
{
	using Clock = std::chrono::steady_clock;
	constexpr size_t kMaxFrames = 300;

	std::deque<Profiler::Frame> s_History;
	Profiler::Frame s_Current;
	Clock::time_point s_FrameStart;
	bool s_InFrame = false;
	std::thread::id s_MainThread = std::this_thread::get_id();

	struct Open { size_t Index; Clock::time_point Start; };
	std::vector<Open> s_Stack;
	// 지금 구간 이름 (깊이마다) — s_Stack 은 Begin 안에서 늘어나며 할당하므로 할당 훅이 읽으면 안 된다. 이 고정 배열만 읽는다
	constexpr int kScopeNames = 64;
	const char* s_ScopeNames[kScopeNames] = {};
	int s_ScopeDepth = 0;

	float MsSince(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<float, std::milli>(b - a).count(); }
	int64_t ToNs(Clock::time_point t) { return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count(); }

	// ================================================================ 다른 스레드의 구간 (스레드마다 SPSC 링 — 쓰는 쪽 = 그 스레드, 읽는 쪽 = 메인)
	struct RawSample
	{
		const char* Name = nullptr;
		int64_t StartNs = 0, EndNs = 0;
		uint16_t Depth = 0;
	};
	struct alignas(64) ThreadBuffer   // 스레드마다 하나 — 이웃 스레드의 버퍼와 캐시 라인을 나누지 않게
	{
		LockFree::SpscRing<RawSample> Ring{ 16384 };
		std::atomic<const char*> Name{ nullptr };
		uint16_t Index = 0;
		std::atomic<uint64_t> Dropped{ 0 };
	};
	constexpr int kMaxThreads = 128;
	std::atomic<ThreadBuffer*> s_Threads[kMaxThreads];
	std::atomic<int> s_ThreadCount{ 0 };
	thread_local ThreadBuffer* t_Buffer = nullptr;
	thread_local uint16_t t_Depth = 0;

	ThreadBuffer* MyBuffer()
	{
		if (t_Buffer)
			return t_Buffer;
		const int i = s_ThreadCount.fetch_add(1, std::memory_order_relaxed);
		if (i >= kMaxThreads)
			return nullptr;
		auto* b = new ThreadBuffer();   // 프로그램이 끝날 때까지 (스레드가 끝나도 메인이 남은 구간을 읽는다)
		b->Index = (uint16_t)i;
		s_Threads[i].store(b, std::memory_order_release);
		t_Buffer = b;
		return b;
	}

	// ================================================================ GPU (타임스탬프 쿼리)
	constexpr int kGpuSlots = 6;   // 결과가 몇 프레임 늦게 오므로 고리로 돌린다
	struct GpuQuery
	{
		const char* Name = nullptr;
		uint16_t Depth = 0;
		ComPtr<GfxQuery> Begin, End;
		ComPtr<GfxQuery> Stats;   // PIPELINE_STATISTICS (구간 안의 픽셀 셰이더 실행 수 등)
	};
	struct GpuSlot
	{
		ComPtr<GfxQuery> Disjoint;
		std::vector<GpuQuery> Queries;
		int Used = 0;
		uint64_t FrameIndex = 0;
		bool Pending = false;
	};
	GpuSlot s_Gpu[kGpuSlots];
	GpuSlot* s_GpuCurrent = nullptr;
	std::vector<int> s_GpuStack;   // 열린 쿼리 번호

	GfxQuery* MakeQuery(D3D11_QUERY type)
	{
		D3D11_QUERY_DESC d = { type, 0 };
		GfxQuery* q = nullptr;
		Application::GetI()->GetDevice()->CreateQuery(&d, &q);
		return q;
	}

	Profiler::Frame* FindFrame(uint64_t index)
	{
		for (auto it = s_History.rbegin(); it != s_History.rend(); ++it)
			if (it->Index == index)
				return &*it;
		return nullptr;
	}

	// 다 끝난 슬롯의 결과를 읽어 그 프레임 기록에 채운다 (기다리지 않는다)
	void CollectGpu(bool waitAll)
	{
		GfxContext* dc = Application::GetI()->GetDeviceContext();
		for (GpuSlot& slot : s_Gpu)
		{
			if (!slot.Pending || &slot == s_GpuCurrent)
				continue;
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
			if (dc->GetData(slot.Disjoint.Get(), &dj, sizeof(dj), waitAll ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;
			slot.Pending = false;
			Profiler::Frame* frame = FindFrame(slot.FrameIndex);
			if (frame == nullptr || dj.Disjoint || dj.Frequency == 0)
				continue;
			std::vector<Profiler::GpuSample> out;
			std::vector<uint64_t> begins;
			uint64_t first = 0;
			bool ok = true;
			for (int i = 0; i < slot.Used && ok; ++i)
			{
				uint64_t b = 0, e = 0;
				ok = dc->GetData(slot.Queries[i].Begin.Get(), &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
					dc->GetData(slot.Queries[i].End.Get(), &e, sizeof(e), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
				D3D11_QUERY_DATA_PIPELINE_STATISTICS st = {};
				if (ok && slot.Queries[i].Stats)
					ok = dc->GetData(slot.Queries[i].Stats.Get(), &st, sizeof(st), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
				if (!ok)
					break;
				if (i == 0 || b < first) first = b;
				begins.push_back(b);
				Profiler::GpuSample sample{ slot.Queries[i].Name, slot.Queries[i].Depth, 0.0f, (e > b) ? (float)((double)(e - b) * 1000.0 / dj.Frequency) : 0.0f };
				sample.Pixels = st.PSInvocations;
				sample.Primitives = st.CPrimitives;
				out.push_back(sample);
			}
			if (!ok)
				continue;
			for (size_t i = 0; i < out.size(); ++i)
				out[i].StartMs = (float)((double)(begins[i] - first) * 1000.0 / dj.Frequency);
			// 맨 위 구간 합 = 프레임 GPU 시간 (겹치지 않는 구간들)
			float total = 0.0f;
			for (const auto& s : out)
				if (s.Depth == 0)
					total += s.Ms;
			frame->Gpu = std::move(out);
			frame->GpuMs = total;
		}
	}
}

namespace Profiler
{
	bool s_Want = false;
	bool s_Forced = false;
	void SetCollecting(bool on) { s_Want = on; }
	void ForceCollecting(bool on) { s_Forced = on; }

	const char* Intern(const std::string& name)
	{
		static std::unordered_set<std::string> s_Names;
		return s_Names.insert(name).first->c_str();
	}

	void BeginFrame()
	{
		g_Collecting = s_Want || s_Forced;
		s_InFrame = Collecting();
		if (!s_InFrame)
			return;
		// 프레임을 시작하는 스레드 = 구간을 재는 스레드 (안드로이드는 정적 초기화 스레드와 그리기 스레드가 다르다)
		s_MainThread = std::this_thread::get_id();
		static uint64_t s_Index = 0;
		s_Current = Frame();
		s_Current.Index = ++s_Index;
		s_Current.Cpu.reserve(256);
		s_Stack.clear();
		s_ScopeDepth = 0;
		s_FrameStart = Clock::now();
		s_Current.StartNs = ToNs(s_FrameStart);

		// GPU 슬롯
		s_GpuStack.clear();
		GpuSlot& slot = s_Gpu[s_Current.Index % kGpuSlots];
		if (slot.Pending)
		{
			s_GpuCurrent = nullptr;
			CollectGpu(true);   // 고리가 한 바퀴 돌았는데 아직 안 읽힌 슬롯 (드묾)
			slot.Pending = false;
		}
		if (!slot.Disjoint)
			slot.Disjoint.Attach(MakeQuery(D3D11_QUERY_TIMESTAMP_DISJOINT));
		slot.Used = 0;
		slot.FrameIndex = s_Current.Index;
		s_GpuCurrent = &slot;
		if (slot.Disjoint)
			Application::GetI()->GetDeviceContext()->Begin(slot.Disjoint.Get());
	}

	void EndFrame()
	{
		if (!s_InFrame)
			return;
		s_InFrame = false;
		// 닫히지 않은 구간은 여기서 닫는다
		const auto now = Clock::now();
		while (!s_Stack.empty())
		{
			s_Current.Cpu[s_Stack.back().Index].Ms = MsSince(s_Stack.back().Start, now);
			s_Stack.pop_back();
		}
		s_ScopeDepth = 0;
		s_Current.CpuMs = MsSince(s_FrameStart, now);

		// 다른 스레드의 구간: 시작 시각으로 제 프레임에 (지난 프레임에서 시작해 늦게 끝난 것은 그 프레임에)
		const int threads = (std::min)(s_ThreadCount.load(std::memory_order_acquire), kMaxThreads);
		for (int t = 0; t < threads; ++t)
		{
			ThreadBuffer* b = s_Threads[t].load(std::memory_order_acquire);
			if (!b)
				continue;
			RawSample r;
			while (b->Ring.Pop(r))
			{
				Profiler::Frame* target = &s_Current;
				if (r.StartNs < s_Current.StartNs)
				{
					target = nullptr;
					for (auto it = s_History.rbegin(); it != s_History.rend(); ++it)
						if (r.StartNs >= it->StartNs)
						{
							target = &*it;
							break;
						}
				}
				if (!target)
					continue;
				target->Threads.push_back({ r.Name, b->Index, r.Depth, (float)((r.StartNs - target->StartNs) / 1e6), (float)((r.EndNs - r.StartNs) / 1e6) });
			}
		}

		EndGpuFrame();

		s_History.push_back(std::move(s_Current));
		while (s_History.size() > kMaxFrames)
			s_History.pop_front();
		CollectGpu(false);
	}

	void EndGpuFrame()
	{
		if (!s_InFrame || s_GpuCurrent == nullptr)
			return;
		if (s_GpuCurrent->Disjoint)
		{
			Application::GetI()->GetDeviceContext()->End(s_GpuCurrent->Disjoint.Get());
			s_GpuCurrent->Pending = s_GpuCurrent->Used > 0;
		}
		s_GpuCurrent = nullptr;   // 뒤의 GPU 구간은 이 프레임에 넣지 않는다
	}

	void Begin(const char* name)
	{
		if (!s_InFrame || std::this_thread::get_id() != s_MainThread)
			return;
		s_Stack.push_back({ s_Current.Cpu.size(), Clock::now() });
		s_Current.Cpu.push_back({ name, (uint16_t)(s_Stack.size() - 1), MsSince(s_FrameStart, s_Stack.back().Start), 0.0f });
		if (s_ScopeDepth < kScopeNames)
			s_ScopeNames[s_ScopeDepth] = name;
		++s_ScopeDepth;
	}

	void End()
	{
		if (!s_InFrame || s_Stack.empty() || std::this_thread::get_id() != s_MainThread)
			return;
		s_Current.Cpu[s_Stack.back().Index].Ms = MsSince(s_Stack.back().Start, Clock::now());
		s_Stack.pop_back();
		if (s_ScopeDepth > 0)
			--s_ScopeDepth;
	}

	const char* CurrentScopeName()
	{
		const int d = s_ScopeDepth < kScopeNames ? s_ScopeDepth : kScopeNames;
		return d > 0 && s_ScopeNames[d - 1] ? s_ScopeNames[d - 1] : "(frame)";
	}

	bool IsMainThread() { return std::this_thread::get_id() == s_MainThread; }
	int64_t NowNs() { return ToNs(Clock::now()); }

	void SetThreadName(const char* name)
	{
		if (ThreadBuffer* b = MyBuffer())
			b->Name.store(name, std::memory_order_release);
	}

	const char* ThreadName(uint16_t thread)
	{
		ThreadBuffer* b = thread < kMaxThreads ? s_Threads[thread].load(std::memory_order_acquire) : nullptr;
		const char* n = b ? b->Name.load(std::memory_order_acquire) : nullptr;
		if (n)
			return n;
		static const char* s_Unnamed[kMaxThreads] = {};
		if (thread < kMaxThreads && !s_Unnamed[thread])
			s_Unnamed[thread] = Intern("Thread " + std::to_string(thread));
		return thread < kMaxThreads ? s_Unnamed[thread] : "Thread";
	}

	int ThreadCount() { return (std::min)(s_ThreadCount.load(std::memory_order_acquire), kMaxThreads); }

	void ThreadSampleBegin(int64_t& startNs, uint16_t& depth)
	{
		startNs = ToNs(Clock::now());
		depth = t_Depth++;
	}

	void ThreadSampleEnd(const char* name, int64_t startNs, uint16_t depth)
	{
		// 잡 파이버가 다른 스레드에서 끝나면 그 스레드의 깊이가 어긋날 수 있다 — 0 아래로는 내리지 않는다
		if (t_Depth > 0)
			--t_Depth;
		ThreadBuffer* b = MyBuffer();
		if (!b)
			return;
		RawSample r;
		r.Name = name;
		r.StartNs = startNs;
		r.EndNs = ToNs(Clock::now());
		r.Depth = depth;
		if (!b->Ring.Push(r))
			b->Dropped.fetch_add(1, std::memory_order_relaxed);   // 메인이 오래 안 읽었다 (Profiler 를 막 켠 때) — 버린다
	}

	void GpuBegin(const char* name)
	{
		if (!s_InFrame || s_GpuCurrent == nullptr || std::this_thread::get_id() != s_MainThread)
			return;
		GpuSlot& slot = *s_GpuCurrent;
		if (slot.Used >= (int)slot.Queries.size())
		{
			GpuQuery q;
			q.Begin.Attach(MakeQuery(D3D11_QUERY_TIMESTAMP));
			q.End.Attach(MakeQuery(D3D11_QUERY_TIMESTAMP));
			q.Stats.Attach(MakeQuery(D3D11_QUERY_PIPELINE_STATISTICS));
			slot.Queries.push_back(std::move(q));
		}
		GpuQuery& q = slot.Queries[slot.Used];
		q.Name = name;
		q.Depth = (uint16_t)s_GpuStack.size();
		if (q.Begin)
			Application::GetI()->GetDeviceContext()->End(q.Begin.Get());
		if (q.Stats)
			Application::GetI()->GetDeviceContext()->Begin(q.Stats.Get());
		s_GpuStack.push_back(slot.Used);
		++slot.Used;
	}

	void GpuEnd()
	{
		if (!s_InFrame || s_GpuCurrent == nullptr || s_GpuStack.empty() || std::this_thread::get_id() != s_MainThread)
			return;
		GpuQuery& q = s_GpuCurrent->Queries[s_GpuStack.back()];
		s_GpuStack.pop_back();
		if (q.Stats)
			Application::GetI()->GetDeviceContext()->End(q.Stats.Get());
		if (q.End)
			Application::GetI()->GetDeviceContext()->End(q.End.Get());
	}

	void SetStat(const char* name, double value)
	{
		if (!s_InFrame)
			return;
		for (Stat& s : s_Current.Stats)
			if (s.Name == name) { s.Value = value; return; }
		s_Current.Stats.push_back({ name, value });
	}

	bool CurrentFrameHas(const char* name)
	{
		if (!s_InFrame)
			return false;
		for (const CpuSample& s : s_Current.Cpu)
			if (s.Name == name || strcmp(s.Name, name) == 0)
				return true;
		return false;
	}

	const std::deque<Frame>& History() { return s_History; }

	size_t MemoryBytes()
	{
		size_t bytes = 0;
		for (const Frame& f : s_History)
			bytes += sizeof(Frame) + f.Cpu.capacity() * sizeof(CpuSample) + f.Gpu.capacity() * sizeof(GpuSample) + f.Stats.capacity() * sizeof(Stat) + f.Threads.capacity() * sizeof(ThreadSample);
		return bytes;
	}

	void Clear()
	{
		s_History.clear();
		for (GpuSlot& slot : s_Gpu)
			slot.Pending = false;
	}
}
