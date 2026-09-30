#pragma once

class GameTimer
{
public:
	GameTimer();

	float TotalTime() const;  // in seconds
	float DeltaTime() const; // in seconds

	void Reset(); // Call before message loop.
	void Start(); // Call when unpaused.
	void Stop();  // Call when paused.
	void Tick();  // Call every frame.

private:
	double _secondsPerCount = 0.0;
	double _deltaTime = -1.0;
	int64 _baseTime = 0;
	int64 _pausedTime = 0;
	int64 _stopTime = 0;
	int64 _prevTime = 0;
	int64 _currTime = 0;
	bool _stopped = false;
};