#include "pch.h"
#include "Profiler.h"
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

	float MsSince(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<float, std::milli>(b - a).count(); }

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
	void SetCollecting(bool on) { s_Want = on; }

	const char* Intern(const std::string& name)
	{
		static std::unordered_set<std::string> s_Names;
		return s_Names.insert(name).first->c_str();
	}

	void BeginFrame()
	{
		g_Collecting = s_Want;
		s_InFrame = Collecting();
		if (!s_InFrame)
			return;
		static uint64_t s_Index = 0;
		s_Current = Frame();
		s_Current.Index = ++s_Index;
		s_Current.Cpu.reserve(256);
		s_Stack.clear();
		s_FrameStart = Clock::now();

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
		s_Current.CpuMs = MsSince(s_FrameStart, now);

		if (s_GpuCurrent && s_GpuCurrent->Disjoint)
		{
			Application::GetI()->GetDeviceContext()->End(s_GpuCurrent->Disjoint.Get());
			s_GpuCurrent->Pending = s_GpuCurrent->Used > 0;
		}
		s_GpuCurrent = nullptr;

		s_History.push_back(std::move(s_Current));
		while (s_History.size() > kMaxFrames)
			s_History.pop_front();
		CollectGpu(false);
	}

	void Begin(const char* name)
	{
		if (!s_InFrame || std::this_thread::get_id() != s_MainThread)
			return;
		s_Stack.push_back({ s_Current.Cpu.size(), Clock::now() });
		s_Current.Cpu.push_back({ name, (uint16_t)(s_Stack.size() - 1), MsSince(s_FrameStart, s_Stack.back().Start), 0.0f });
	}

	void End()
	{
		if (!s_InFrame || s_Stack.empty() || std::this_thread::get_id() != s_MainThread)
			return;
		s_Current.Cpu[s_Stack.back().Index].Ms = MsSince(s_Stack.back().Start, Clock::now());
		s_Stack.pop_back();
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
			bytes += sizeof(Frame) + f.Cpu.capacity() * sizeof(CpuSample) + f.Gpu.capacity() * sizeof(GpuSample) + f.Stats.capacity() * sizeof(Stat);
		return bytes;
	}

	void Clear()
	{
		s_History.clear();
		for (GpuSlot& slot : s_Gpu)
			slot.Pending = false;
	}
}
