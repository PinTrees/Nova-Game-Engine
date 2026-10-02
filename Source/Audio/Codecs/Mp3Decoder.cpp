// MP3 디코더 (dr_mp3, 공개 도메인 / MIT-0). 엔진 PCH 없이·경고 없이 컴파일 (CMakeLists.txt)
#include "../AudioDecoder.h"
#define DR_MP3_IMPLEMENTATION
#include "../../ThirdParty/AudioCodecs/dr_mp3.h"

namespace
{
	class Mp3Decoder : public AudioDecoder
	{
	public:
		explicit Mp3Decoder(std::shared_ptr<const std::vector<uint8_t>> data) : m_Data(std::move(data)) {}
		~Mp3Decoder() override
		{
			if (m_Open)
				drmp3_uninit(&m_Mp3);
		}

		bool Init()
		{
			m_Open = drmp3_init_memory(&m_Mp3, m_Data->data(), m_Data->size(), nullptr) != 0;
			if (!m_Open)
				return false;
			Channels = (int)m_Mp3.channels;
			Frequency = (int)m_Mp3.sampleRate;
			Frames = drmp3_get_pcm_frame_count(&m_Mp3);   // 전체를 한 번 훑는다 (길이)
			drmp3_seek_to_pcm_frame(&m_Mp3, 0);
			return true;
		}

		uint64_t Read(int16_t* out, uint64_t frames) override { return drmp3_read_pcm_frames_s16(&m_Mp3, frames, out); }
		bool Rewind() override { return drmp3_seek_to_pcm_frame(&m_Mp3, 0) != 0; }

	private:
		std::shared_ptr<const std::vector<uint8_t>> m_Data;
		drmp3 m_Mp3 = {};
		bool m_Open = false;
	};
}

std::unique_ptr<AudioDecoder> OpenMp3Decoder(const std::shared_ptr<const std::vector<uint8_t>>& data)
{
	if (!data || data->empty())
		return nullptr;
	auto d = std::make_unique<Mp3Decoder>(data);
	if (!d->Init())
		return nullptr;
	return d;
}
