#include "pch.h"
#include "DayNightState.h"

namespace
{
	DayNightState s_State;
}

DayNightState& DayNightState::Get() { return s_State; }

void DayNightState::Reset() { s_State = DayNightState(); }
