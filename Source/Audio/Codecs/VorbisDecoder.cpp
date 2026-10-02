// OGG Vorbis 디코더 (stb_vorbis v1.22, 공개 도메인). 엔진 PCH 없이·경고 없이 컴파일 (CMakeLists.txt)
#include "../AudioDecoder.h"
#include <algorithm>
#include "../../ThirdParty/AudioCodecs/stb_vorbis.c"

namespace
{
	class VorbisDecoder : public AudioDecoder
	{
	public:
		VorbisDecoder(std::shared_ptr<const std::vector<uint8_t>> data, stb_vorbis* v) : m_Data(std::move(data)), m_Vorbis(v)
		{
			const stb_vorbis_info info = stb_vorbis_get_info(v);
			Channels = info.channels;
			Frequency = (int)info.sample_rate;
			Frames = stb_vorbis_stream_length_in_samples(v);
		}
		~VorbisDecoder() override { stb_vorbis_close(m_Vorbis); }

		uint64_t Read(int16_t* out, uint64_t frames) override
		{
			uint64_t done = 0;
			while (done < frames)
			{
				const int want = (int)(std::min<uint64_t>)(frames - done, 4096);
				const int got = stb_vorbis_get_samples_short_interleaved(m_Vorbis, Channels, out + done * Channels, want * Channels);
				if (got <= 0)
					break;
				done += (uint64_t)got;
			}
			return done;
		}

		bool Rewind() override { return stb_vorbis_seek_start(m_Vorbis) != 0; }

	private:
		std::shared_ptr<const std::vector<uint8_t>> m_Data;   // stb_vorbis 가 이 메모리를 읽는다
		stb_vorbis* m_Vorbis;
	};
}

std::unique_ptr<AudioDecoder> OpenVorbisDecoder(const std::shared_ptr<const std::vector<uint8_t>>& data)
{
	if (!data || data->empty())
		return nullptr;
	int error = 0;
	stb_vorbis* v = stb_vorbis_open_memory(data->data(), (int)data->size(), &error, nullptr);
	if (v == nullptr)
		return nullptr;
	return std::make_unique<VorbisDecoder>(data, v);
}
