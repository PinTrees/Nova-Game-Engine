#pragma once
#include <chrono>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include "Profiler.h"

// 간단한 구간 시간 재기 (개발용). NOVA_DEV_PROFILE=1 이면 120 프레임(또는 3 초)마다 구간별 프레임당 평균 ms 를 Editor.log 에 남긴다.
//  사용: { FRAME_PROFILE("Hierarchy"); ... }  /  프레임 끝에서 FrameProfiler::EndFrame()
namespace FrameProfiler
{
	inline bool Enabled()
	{
		static const bool on = [] { char v[8] = {}; return ::GetEnvironmentVariableA("NOVA_DEV_PROFILE", v, sizeof(v)) > 0 && v[0] == '1'; }();
		return on;
	}

	inline std::map<std::string, double>& Totals()
	{
		static std::map<std::string, double> totals;
		return totals;
	}

	// 로그(NOVA_DEV_PROFILE) + Profiler 창 둘 다에 기록
	struct Scope
	{
		std::string Name;
		std::chrono::steady_clock::time_point Start;
		bool Recorded = false;
		explicit Scope(const std::string& name) : Name(Enabled() ? name : std::string()), Start(Enabled() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point())
		{
			if (Profiler::Collecting())
			{
				Profiler::Begin(Profiler::Intern(name));
				Recorded = true;
			}
		}
		~Scope()
		{
			if (Recorded)
				Profiler::End();
			if (Enabled())
				Totals()[Name] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
		}
	};

	inline void EndFrame()
	{
		if (!Enabled())
			return;
		static int frames = 0;
		static auto since = std::chrono::steady_clock::now();
		++frames;
		// 120 프레임 또는 3 초마다 (느릴 때도 기록이 남게)
		if (frames < 120 && std::chrono::steady_clock::now() - since < std::chrono::seconds(3))
			return;
		since = std::chrono::steady_clock::now();
		std::vector<std::pair<double, std::string>> list;
		for (const auto& [name, ms] : Totals())
			list.push_back({ ms / frames, name });
		std::sort(list.rbegin(), list.rend());
		std::string line;
		for (const auto& [ms, name] : list)
		{
			char buf[128];
			snprintf(buf, sizeof(buf), "%s %.2f  ", name.c_str(), ms);
			line += buf;
		}
		EditorLog::Write("Profile", "ms/frame: %s", line.c_str());
		Totals().clear();
		frames = 0;
	}
}

#define FRAME_PROFILE_CAT2(a, b) a##b
#define FRAME_PROFILE_CAT(a, b) FRAME_PROFILE_CAT2(a, b)
#define FRAME_PROFILE(name) FrameProfiler::Scope FRAME_PROFILE_CAT(_frameProfile, __LINE__)(name)
