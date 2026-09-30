#include "pch.h"
#include <windows.h>
#include "GameTimer.h"

GameTimer::GameTimer()
{
	int64 countsPerSec;
	::QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
	_secondsPerCount = 1.0 / (double)countsPerSec;
}

// Returns the total time elapsed since Reset() was called, NOT counting any
// time when the clock is stopped.
float GameTimer::TotalTime()const
{
	// If we are stopped, do not count the time that has passed since we stopped.
	// Moreover, if we previously already had a pause, the distance 
	// mStopTime - mBaseTime includes paused time, which we do not want to count.
	// To correct this, we can subtract the paused time from mStopTime:  
	//
	//                     |<--paused time-->|
	// ----*---------------*-----------------*------------*------------*------> time
	//  mBaseTime       mStopTime        startTime     mStopTime    mCurrTime

	if (_stopped)
	{
		return (float)(((_stopTime - _pausedTime) - _baseTime) * _secondsPerCount);
	}

	// The distance mCurrTime - mBaseTime includes paused time,
	// which we do not want to count.  To correct this, we can subtract 
	// the paused time from mCurrTime:  
	//
	//  (mCurrTime - mPausedTime) - mBaseTime 
	//
	//                     |<--paused time-->|
	// ----*---------------*-----------------*------------*------> time
	//  mBaseTime       mStopTime        startTime     mCurrTime
	
	else
	{
		return (float)(((_currTime-_pausedTime)-_baseTime)*_secondsPerCount);
	}
}

float GameTimer::DeltaTime()const
{
	return (float)_deltaTime;
}

void GameTimer::Reset()
{
	int64 currTime;
	::QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

	_baseTime = currTime;
	_prevTime = currTime;
	_stopTime = 0;
	_stopped  = false;
}

void GameTimer::Start()
{
	int64 startTime;
	::QueryPerformanceCounter((LARGE_INTEGER*)&startTime);


	// Accumulate the time elapsed between stop and start pairs.
	//
	//                     |<-------d------->|
	// ----*---------------*-----------------*------------> time
	//  mBaseTime       mStopTime        startTime     

	if (_stopped)
	{
		_pausedTime += (startTime - _stopTime);	

		_prevTime = startTime;
		_stopTime = 0;
		_stopped  = false;
	}
}

void GameTimer::Stop()
{
	if (!_stopped)
	{
		int64 currTime;
		::QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

		_stopTime = currTime;
		_stopped  = true;
	}
}

void GameTimer::Tick()
{
	if( _stopped )
	{
		_deltaTime = 0.0;
		return;
	}

	int64 currTime;
	::QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
	_currTime = currTime;

	// Time difference between this frame and the previous.
	_deltaTime = (_currTime - _prevTime)*_secondsPerCount;

	// Prepare for next frame.
	_prevTime = _currTime;

	// Force nonnegative.  The DXSDK's CDXUTTimer mentions that if the 
	// processor goes into a power save mode or we get shuffled to another
	// processor, then mDeltaTime can be negative.
	if (_deltaTime < 0.0)
	{
		_deltaTime = 0.0;
	}
}

