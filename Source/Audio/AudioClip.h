#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <mmreg.h>

class AudioDecoder;

// Unity 의 AudioClip.
//  - WAV(PCM 8/16/24/32비트, 32비트 float): 통째로 메모리에
//  - OGG Vorbis · MP3: Import Settings(.meta) 의 Load Type 대로
//    Decompress On Load(기본) = 읽을 때 16 비트 PCM 으로 다 푼다
//    Compressed In Memory · Streaming = 압축된 채로 두고 재생할 때 조금씩 푼다 (긴 배경음악)
//  - Force To Mono: 채널을 평균해 모노로 (WAV 는 16 비트로 바꾼다)
class AudioClip
{
public:
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
	int LoadType = 0;              // AssetImport::AudioSettings::LoadType (WAV 는 늘 0)
	int SourceChannels = 0;        // 파일의 채널 수 (Force To Mono 면 Channels = 1)

	std::string Name() const;
	// 스트리밍 재생마다 새 디코더 (처음부터)
	std::unique_ptr<AudioDecoder> OpenDecoder() const;

	// 경로로 읽기 (같은 경로는 같은 객체, 실패하면 nullptr)
	static std::shared_ptr<AudioClip> Load(const std::string& path);
	// 캐시에서 뺀다 (Import Settings 를 바꾼 뒤 다시 읽게). 재생 중인 보이스는 들고 있던 것을 계속 쓴다
	static void Forget(const std::string& path);
	// 프로젝트 Assets 와 엔진 패키지의 모든 오디오 파일 (.wav .ogg .mp3, 상대 경로)
	static std::vector<std::string> FindAll();
	static bool IsAudioPath(const std::string& path);
};
