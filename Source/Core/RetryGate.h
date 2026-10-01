#pragma once
#include <windows.h>

// GPU 자원 만들기가 실패했을 때 매 프레임 다시 시도하지 않게 하는 간격 (예: VRAM 예산 초과 → 1 초 뒤 다시)
struct RetryGate
{
	ULONGLONG NextTick = 0;
	bool Ready() const { return ::GetTickCount64() >= NextTick; }
	void Failed(ULONGLONG ms = 1000) { NextTick = ::GetTickCount64() + ms; }
	void Succeeded() { NextTick = 0; }
};
