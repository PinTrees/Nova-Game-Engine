#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <deque>

// NOVA Profiler (Unity 의 Profiler 창 데이터).
//  - CPU: 메인 스레드의 중첩 구간(PROFILE_SCOPE)을 프레임마다 시작 순서대로 기록 (깊이 + 시작/길이)
//  - 다른 스레드 (Job Worker · 물리 · 렌더 스레드): 같은 PROFILE_SCOPE 를 스레드마다의 무잠금 링 (SPSC) 에 쌓고,
//    메인 스레드가 EndFrame 에 모아 그 프레임의 Threads 에 넣는다 (Timeline 의 스레드 줄)
//  - GPU: D3D11 타임스탬프 쿼리(PROFILE_GPU). 결과는 몇 프레임 뒤에 도착하므로 그 프레임 기록에 나중에 채운다
//  - 통계: 시스템이 프레임마다 SetStat("이름", 값) 으로 알린다 (드로 콜, 묶음, 컬링 …)
//  - Profiler 창이 열려 있고 Record 가 켜져 있을 때만 모은다 (꺼져 있으면 구간마다 bool 검사 하나)
namespace Profiler
{
	struct CpuSample
	{
		const char* Name;     // Intern 된 이름 (프로그램이 끝날 때까지 유효)
		uint16_t Depth;
		float StartMs;        // 프레임 시작 기준
		float Ms;
	};
	struct GpuSample
	{
		const char* Name;
		uint16_t Depth;
		float StartMs;        // 프레임의 첫 GPU 구간 기준
		float Ms;             // 타임스탬프 사이 시간. CPU 가 느려 GPU 가 기다리면 그 빈 시간도 들어간다
		uint64_t Pixels = 0;      // 픽셀 셰이더 실행 수 (PIPELINE_STATISTICS: 겹쳐 칠한 것 포함 = 실제 GPU 일)
		uint64_t Primitives = 0;  // 래스터라이저로 간 삼각형 수
	};
	// 메인 밖 스레드의 구간 (Thread = RegisterThread 순서, ThreadName 으로 이름)
	struct ThreadSample
	{
		const char* Name;
		uint16_t Thread;
		uint16_t Depth;
		float StartMs;        // 프레임 시작 기준 (음수 = 앞 프레임에서 시작)
		float Ms;
	};
	struct Stat
	{
		const char* Name;
		double Value;
	};
	struct Frame
	{
		uint64_t Index = 0;
		float CpuMs = 0.0f;
		float GpuMs = -1.0f;  // < 0 = 아직 없음
		std::vector<CpuSample> Cpu;
		std::vector<GpuSample> Gpu;
		std::vector<Stat> Stats;
		std::vector<ThreadSample> Threads;
		int64_t StartNs = 0;  // 프레임 시작 (steady_clock) — 늦게 끝난 스레드 구간을 제 프레임에 넣는다
	};

	// 모으기 켜기/끄기 (Profiler 창: 열림 && Record). 다음 BeginFrame 부터 적용 (프레임 중간에 바뀌어 구간이 어긋나지 않게)
	void SetCollecting(bool on);
	void ForceCollecting(bool on);   // Profiler 창과 상관없이 모으기 (CLI nova perf)
	inline bool g_Collecting = false;
	inline bool Collecting() { return g_Collecting; }

	void BeginFrame();
	void EndFrame();
	// GPU 프레임 (TIMESTAMP_DISJOINT) 을 Present 직전에 닫는다 — 렌더 스레드 (DX11 명령 목록) 는 쿼리가 목록 사이에 걸치면 결과가 오지 않는다.
	//  EndFrame 은 아직 안 닫혔으면 닫는다
	void EndGpuFrame();

	NOVA_API void Begin(const char* internedName);
	NOVA_API void End();

	// 스레드: 메인 (BeginFrame 을 부르는 스레드) 인가. 다른 스레드는 이름을 붙여 둘 수 있다 (없으면 "Thread N")
	NOVA_API bool IsMainThread();
	// 메인 스레드의 지금 구간 이름 (할당 훅이 읽는다 — 할당 없이, 메인 스레드에서만 뜻이 있다). 구간이 없으면 "(frame)"
	const char* CurrentScopeName();
	void SetThreadName(const char* internedName);
	const char* ThreadName(uint16_t thread);
	int ThreadCount();
	int64_t NowNs();
	NOVA_API void ThreadSampleBegin(int64_t& startNs, uint16_t& depth);
	NOVA_API void ThreadSampleEnd(const char* internedName, int64_t startNs, uint16_t depth);
	const char* Intern(const std::string& name);

	void GpuBegin(const char* internedName);
	void GpuEnd();

	void SetStat(const char* internedName, double value);
	bool CurrentFrameHas(const char* name);   // 이번 프레임에 그 이름의 CPU 구간이 있었나 (그 화면을 그렸나)

	const std::deque<Frame>& History();   // 오래된 것 → 최근 (최대 300)
	void Clear();
	size_t MemoryBytes();   // 기록한 프레임들 (Profiler 메모리)

	struct Scope
	{
		// 메인 스레드 = 중첩 스택, 그 밖 = 시작 시각을 들고 있다가 끝날 때 한 번에 (잡 파이버가 다른 스레드에서 끝나도 된다)
		const char* Name;
		int64_t StartNs = 0;
		uint16_t Depth = 0;
		uint8_t Mode = 0;   // 0 꺼짐, 1 메인, 2 다른 스레드
		explicit Scope(const char* name) : Name(name)
		{
			if (!Collecting())
				return;
			if (IsMainThread()) { Mode = 1; Begin(name); }
			else { Mode = 2; ThreadSampleBegin(StartNs, Depth); }
		}
		~Scope()
		{
			if (Mode == 1) End();
			else if (Mode == 2) ThreadSampleEnd(Name, StartNs, Depth);
		}
	};
	struct GpuScope
	{
		bool Active;
		explicit GpuScope(const char* name) : Active(Collecting()) { if (Active) GpuBegin(name); }
		~GpuScope() { if (Active) GpuEnd(); }
	};

	// 연달아 이어지는 단계 (그림자 → 깊이 → 본 패스 …): Next 가 앞 단계를 닫고 새 단계를 연다. CPU + GPU 같이
	class Phases
	{
	public:
		explicit Phases(bool gpu = true) : m_UseGpu(gpu) {}
		~Phases() { Close(); }
		void Next(const char* name)
		{
			Close();
			if (!Collecting())
				return;
			Begin(name);
			m_Cpu = true;
			if (m_UseGpu)
			{
				GpuBegin(name);
				m_Gpu = true;
			}
		}
		void Close()
		{
			if (m_Gpu) GpuEnd();
			if (m_Cpu) End();
			m_Cpu = m_Gpu = false;
		}
	private:
		bool m_UseGpu, m_Cpu = false, m_Gpu = false;
	};
}

#define PROFILE_CAT2(a, b) a##b
#define PROFILE_CAT(a, b) PROFILE_CAT2(a, b)
// 이름은 문자열 상수 (그대로 Intern 없이 쓴다)
#define PROFILE_SCOPE(name) Profiler::Scope PROFILE_CAT(_profScope, __LINE__)(name)
#define PROFILE_GPU(name) Profiler::GpuScope PROFILE_CAT(_profGpu, __LINE__)(name)
