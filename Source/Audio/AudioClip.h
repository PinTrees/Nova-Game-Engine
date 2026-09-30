#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <mmreg.h>

// Unity 의 AudioClip: WAV(PCM 8/16/24/32비트, 32비트 float) 를 통째로 메모리에 읽는다.
class AudioClip
{
public:
	std::string Path;              // 상대 경로 (Assets\... 또는 Resources\Packages\...)
	WAVEFORMATEX Format = {};      // XAudio2 소스 보이스 형식
	std::vector<uint8_t> Data;     // 샘플 데이터
	int Channels = 0;
	int Frequency = 0;
	uint32_t Samples = 0;          // 채널당 샘플 수
	float Length = 0.0f;           // 초

	std::string Name() const;

	// 경로로 읽기 (같은 경로는 같은 객체, 실패하면 nullptr)
	static std::shared_ptr<AudioClip> Load(const std::string& path);
	// 프로젝트 Assets 와 엔진 패키지의 모든 .wav (상대 경로)
	static std::vector<std::string> FindAll();
	static bool IsAudioPath(const std::string& path);
};
