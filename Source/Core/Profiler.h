#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <deque>

// NOVA Profiler (Unity 의 Profiler 창 데이터).
//  - CPU: 메인 스레드의 중첩 구간(PROFILE_SCOPE)을 프레임마다 시작 순서대로 기록 (깊이 + 시작/길이)
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
	};

	// 모으기 켜기/끄기 (Profiler 창: 열림 && Record). 다음 BeginFrame 부터 적용 (프레임 중간에 바뀌어 구간이 어긋나지 않게)
	void SetCollecting(bool on);
	inline bool g_Collecting = false;
	inline bool Collecting() { return g_Collecting; }

	void BeginFrame();
	void EndFrame();

	void Begin(const char* internedName);
	void End();
	const char* Intern(const std::string& name);

	void GpuBegin(const char* internedName);
	void GpuEnd();

	void SetStat(const char* internedName, double value);
	bool CurrentFrameHas(const char* name);   // 이번 프레임에 그 이름의 CPU 구간이 있었나 (그 화면을 그렸나)

	const std::deque<Frame>& History();   // 오래된 것 → 최근 (최대 300)
	void Clear();

	struct Scope
	{
		bool Active;
		explicit Scope(const char* name) : Active(Collecting()) { if (Active) Begin(name); }
		~Scope() { if (Active) End(); }
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
