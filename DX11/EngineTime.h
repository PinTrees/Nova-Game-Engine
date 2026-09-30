#pragma once
#include "TimeManager.h"

class Time
{
public:
	static float GetDeltaTime()
	{
		return TimeManager::GetI()->GetfDT();
	}

	static float DeltaTime()
	{
		return TimeManager::GetI()->GetfDT();
	}

	static float GetFixedDeltaTime()
	{
		return 0.02f;
	}

	static UINT GetFPS()
	{
		return TimeManager::GetI()->GetFrame();
	}
};
