#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <chrono>
#include <thread>
#include "JobSystem.h"

// Jolt 의 JobSystem 을 Nova Job System 위로 (동시성 로드맵 2 단계).
//  예전에는 Jolt 가 자기 스레드 풀 (코어 − 1 개) 을 따로 띄워 엔진 일꾼과 코어를 다퉜다 — 이제 같은 일꾼이 물리 잡도 돌린다.
//  잡 객체 · 의존성 · 장벽 (Barrier) 은 Jolt 의 것 그대로 (JobSystemWithBarrier), 실행만 Jobs::Run 으로.
//  PhysicsSystem::Update 를 부른 스레드는 장벽에서 기다리며 준비된 물리 잡을 직접 돌린다 (Jolt 기본 동작)
class JoltNovaJobSystem final : public JPH::JobSystemWithBarrier
{
public:
	JoltNovaJobSystem(JPH::uint maxJobs, JPH::uint maxBarriers)
	{
		JobSystemWithBarrier::Init(maxBarriers);
		m_Jobs.Init(maxJobs, maxJobs);
	}

	int GetMaxConcurrency() const override { return Jobs::Inline() ? 1 : Jobs::WorkerCount() + 1; }

	JPH::JobHandle CreateJob(const char* name, JPH::ColorArg color, const JobFunction& fn, JPH::uint32 dependencies) override
	{
		JPH::uint32 index;
		for (;;)
		{
			index = m_Jobs.ConstructObject(name, color, this, fn, dependencies);
			if (index != Available::cInvalidObjectIndex)
				break;
			std::this_thread::sleep_for(std::chrono::microseconds(100));   // 풀이 다 찼다 (cMaxPhysicsJobs) — 드묾
		}
		Job* job = &m_Jobs.Get(index);
		JPH::JobHandle handle(job);   // 아래에서 넣자마자 끝날 수 있다 — 먼저 잡아 둔다
		if (dependencies == 0)
			QueueJob(job);
		return handle;
	}

protected:
	void QueueJob(Job* job) override
	{
		job->AddRef();
		// 잡 이름 = Jolt 의 이름 (Profiler Timeline 의 일꾼 줄에 그대로 — Jolt 프로파일이 꺼져 있으면 이름이 없다)
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		const char* name = job->GetName();
#else
		const char* name = "Physics Job";
#endif
		Jobs::Run([job]() { job->Execute(); job->Release(); }, nullptr, Jobs::Priority::High, name);
	}
	void QueueJobs(Job** jobs, JPH::uint count) override
	{
		for (JPH::uint i = 0; i < count; ++i)
			QueueJob(jobs[i]);
	}
	void FreeJob(Job* job) override { m_Jobs.DestructObject(job); }

private:
	using Available = JPH::FixedSizeFreeList<Job>;
	Available m_Jobs;
};
