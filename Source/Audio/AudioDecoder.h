#pragma once
#include <memory>
#include <vector>
#include <cstdint>

// 압축 오디오(OGG Vorbis · MP3)를 16 비트 PCM 으로 푼다. 원본 바이트는 여러 디코더가 함께 쓴다 (스트리밍 재생마다 하나).
//  - Vorbis: stb_vorbis (공개 도메인), MP3: dr_mp3 (공개 도메인 / MIT-0) — Source/ThirdParty/AudioCodecs
class AudioDecoder
{
public:
	enum Codec { Vorbis = 1, Mp3 = 2 };

	int Channels = 0;
	int Frequency = 0;
	uint64_t Frames = 0;            // 채널당 샘플 수 (모르면 0)

	virtual ~AudioDecoder() = default;
	// frames 만큼 interleaved int16 으로 (끝이면 덜 돌려준다)
	virtual uint64_t Read(int16_t* out, uint64_t frames) = 0;
	virtual bool Rewind() = 0;

	static std::unique_ptr<AudioDecoder> Open(int codec, const std::shared_ptr<const std::vector<uint8_t>>& data);
};

// Codecs/*.cpp (엔진 PCH 없이 컴파일)
std::unique_ptr<AudioDecoder> OpenVorbisDecoder(const std::shared_ptr<const std::vector<uint8_t>>& data);
std::unique_ptr<AudioDecoder> OpenMp3Decoder(const std::shared_ptr<const std::vector<uint8_t>>& data);
