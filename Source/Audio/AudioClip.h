#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <mmreg.h>

class AudioDecoder;

// Unity 의 AudioClip.
//  - WAV(PCM 8/16/24/32비트, 32비트 float): 통째로 메모리에
//  - OGG Vorbis · MP3: 짧으면 읽을 때 16 비트 PCM 으로 다 풀고 (Decompress On Load),
//    kStreamSeconds 보다 길면 압축된 채로 두고 재생할 때 조금씩 푼다 (Streaming — 배경음악)
class AudioClip
{
public:
	static constexpr float kStreamSeconds = 20.0f;

	std::string Path;              // 상대 경로 (Assets\... 또는 Resources\Packages\...)
	WAVEFORMATEX Format = {};      // XAudio2 소스 보이스 형식
	std::vector<uint8_t> Data;     // 샘플 데이터 (스트리밍이면 비어 있음)
	int Channels = 0;
	int Frequency = 0;
	uint32_t Samples = 0;          // 채널당 샘플 수
	float Length = 0.0f;           // 초
	bool Streaming = false;
	int Codec = 0;                 // 0 PCM(WAV), AudioDecoder::Vorbis, AudioDecoder::Mp3
	std::shared_ptr<const std::vector<uint8_t>> Encoded;   // 압축 원본 (스트리밍)

	std::string Name() const;
	// 스트리밍 재생마다 새 디코더 (처음부터)
	std::unique_ptr<AudioDecoder> OpenDecoder() const;

	// 경로로 읽기 (같은 경로는 같은 객체, 실패하면 nullptr)
	static std::shared_ptr<AudioClip> Load(const std::string& path);
	// 프로젝트 Assets 와 엔진 패키지의 모든 오디오 파일 (.wav .ogg .mp3, 상대 경로)
	static std::vector<std::string> FindAll();
	static bool IsAudioPath(const std::string& path);
};
