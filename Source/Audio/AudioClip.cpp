#include "pch.h"
#include "AudioClip.h"
#include "AudioDecoder.h"
#include "AssetImportSettings.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
	std::string NormalizePath(std::string path)
	{
		std::replace(path.begin(), path.end(), '/', '\\');
		if (!fs::path(path).is_absolute())
		{
			const size_t first = path.find_first_not_of('\\');
			path.erase(0, first == std::string::npos ? path.size() : first);
		}
		return path;
	}

	std::wstring FilePath(const std::string& path)
	{
		fs::path p(string_to_wstring(path));
		return p.is_absolute() ? p.wstring() : PathManager::GetI()->GetMovePathW(string_to_wstring(path));
	}

	std::map<std::string, std::shared_ptr<AudioClip>>& Cache()
	{
		static std::map<std::string, std::shared_ptr<AudioClip>> cache;
		return cache;
	}

	uint32_t U32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
	uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
}

std::string AudioClip::Name() const
{
	return fs::path(Path).stem().string();
}

bool AudioClip::IsAudioPath(const std::string& path)
{
	std::string ext = fs::path(path).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	return ext == ".wav" || ext == ".ogg" || ext == ".mp3";
}

std::unique_ptr<AudioDecoder> AudioDecoder::Open(int codec, const std::shared_ptr<const std::vector<uint8_t>>& data)
{
	if (codec == Vorbis) return OpenVorbisDecoder(data);
	if (codec == Mp3) return OpenMp3Decoder(data);
	return nullptr;
}

std::unique_ptr<AudioDecoder> AudioClip::OpenDecoder() const
{
	return Codec != 0 && Encoded ? AudioDecoder::Open(Codec, Encoded) : nullptr;
}

namespace
{
	// OGG / MP3: Load Type 이 Decompress On Load 면 다 풀어 WAV 처럼, 아니면 재생할 때 푼다
	std::shared_ptr<AudioClip> LoadCompressed(const std::string& path, std::vector<uint8_t>&& file, int codec, const AssetImport::AudioSettings& settings)
	{
		auto encoded = std::make_shared<const std::vector<uint8_t>>(std::move(file));
		std::unique_ptr<AudioDecoder> dec = AudioDecoder::Open(codec, encoded);
		if (dec == nullptr || dec->Channels <= 0 || dec->Channels > 8 || dec->Frequency <= 0)
		{
			EditorLog::Write("Audio", "cannot decode %s", path.c_str());
			return nullptr;
		}
		auto clip = std::make_shared<AudioClip>();
		clip->Path = path;
		clip->Codec = codec;
		clip->SourceChannels = dec->Channels;
		clip->Channels = settings.ForceToMono ? 1 : dec->Channels;
		clip->Frequency = dec->Frequency;
		clip->LoadType = settings.LoadType;
		WAVEFORMATEX& f = clip->Format;
		f.wFormatTag = WAVE_FORMAT_PCM;
		f.nChannels = (WORD)clip->Channels;
		f.nSamplesPerSec = (DWORD)dec->Frequency;
		f.wBitsPerSample = 16;
		f.nBlockAlign = (WORD)(2 * clip->Channels);
		f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
		f.cbSize = 0;
		if (settings.LoadType != AssetImport::AudioSettings::DecompressOnLoad)
		{
			clip->Streaming = true;
			clip->Encoded = encoded;
			clip->Samples = (uint32_t)dec->Frames;
		}
		else
		{
			// 다 푼다 (길이를 모르면 끝까지 읽으며 늘린다)
			std::vector<int16_t> pcm;
			std::vector<int16_t> chunk((size_t)4096 * dec->Channels);
			for (;;)
			{
				const uint64_t got = dec->Read(chunk.data(), 4096);
				if (got == 0)
					break;
				if (clip->Channels == dec->Channels)
					pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + (size_t)got * dec->Channels);
				else
					for (uint64_t i = 0; i < got; ++i)   // Force To Mono: 채널 평균
					{
						int sum = 0;
						for (int c = 0; c < dec->Channels; ++c)
							sum += chunk[(size_t)i * dec->Channels + c];
						pcm.push_back((int16_t)(sum / dec->Channels));
					}
			}
			clip->Samples = (uint32_t)(pcm.size() / clip->Channels);
			clip->Data.resize(pcm.size() * sizeof(int16_t));
			memcpy(clip->Data.data(), pcm.data(), clip->Data.size());
		}
		clip->Length = (float)clip->Samples / (float)clip->Frequency;
		EditorLog::Write("Audio", "clip loaded %s (%s, %d Hz, %d ch, %.2fs%s)", path.c_str(), codec == AudioDecoder::Vorbis ? "ogg" : "mp3",
			clip->Frequency, clip->Channels, clip->Length, clip->Streaming ? (settings.LoadType == AssetImport::AudioSettings::Streaming ? ", streaming" : ", compressed in memory") : "");
		return clip;
	}
}

std::shared_ptr<AudioClip> AudioClip::Load(const std::string& rawPath)
{
	const std::string path = NormalizePath(rawPath);
	if (path.empty())
		return nullptr;
	if (auto it = Cache().find(path); it != Cache().end())
		return it->second;

	std::ifstream in(FilePath(path), std::ios::binary);
	if (!in)
	{
		EditorLog::Write("Audio", "clip not found %s", path.c_str());
		return nullptr;
	}
	std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	std::string ext = fs::path(path).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	const AssetImport::AudioSettings settings = AssetImport::LoadAudio(FilePath(path));
	if (ext == ".ogg" || ext == ".mp3")
	{
		auto clip = LoadCompressed(path, std::move(file), ext == ".ogg" ? AudioDecoder::Vorbis : AudioDecoder::Mp3, settings);
		if (clip)
			Cache()[path] = clip;
		return clip;
	}
	if (file.size() < 12 || memcmp(file.data(), "RIFF", 4) != 0 || memcmp(file.data() + 8, "WAVE", 4) != 0)
	{
		EditorLog::Write("Audio", "not a RIFF/WAVE file %s", path.c_str());
		return nullptr;
	}

	auto clip = std::make_shared<AudioClip>();
	clip->Path = path;
	bool haveFmt = false, haveData = false;
	size_t pos = 12;
	while (pos + 8 <= file.size())
	{
		const uint8_t* ck = file.data() + pos;
		const uint32_t size = U32(ck + 4);
		const size_t body = pos + 8;
		if (body + size > file.size())
			break;
		if (memcmp(ck, "fmt ", 4) == 0 && size >= 16)
		{
			uint16_t tag = U16(file.data() + body);
			if (tag == 0xFFFE && size >= 40)   // WAVE_FORMAT_EXTENSIBLE: 하위 형식 GUID 의 앞 2바이트가 실제 형식
				tag = U16(file.data() + body + 24);
			WAVEFORMATEX& f = clip->Format;
			f.wFormatTag = tag;
			f.nChannels = U16(file.data() + body + 2);
			f.nSamplesPerSec = U32(file.data() + body + 4);
			f.nAvgBytesPerSec = U32(file.data() + body + 8);
			f.nBlockAlign = U16(file.data() + body + 12);
			f.wBitsPerSample = U16(file.data() + body + 14);
			f.cbSize = 0;
			haveFmt = true;
		}
		else if (memcmp(ck, "data", 4) == 0)
		{
			clip->Data.assign(file.begin() + body, file.begin() + body + size);
			haveData = true;
		}
		pos = body + size + (size & 1);   // 청크는 짝수 경계
	}

	const WAVEFORMATEX& f = clip->Format;
	const bool supported = (f.wFormatTag == WAVE_FORMAT_PCM && (f.wBitsPerSample == 8 || f.wBitsPerSample == 16 || f.wBitsPerSample == 24 || f.wBitsPerSample == 32))
		|| (f.wFormatTag == WAVE_FORMAT_IEEE_FLOAT && f.wBitsPerSample == 32);
	if (!haveFmt || !haveData || !supported || f.nChannels == 0 || f.nBlockAlign == 0)
	{
		EditorLog::Write("Audio", "unsupported wav %s (tag %u, %u bit, %u ch)", path.c_str(), f.wFormatTag, f.wBitsPerSample, f.nChannels);
		return nullptr;
	}
	clip->SourceChannels = f.nChannels;
	if (settings.ForceToMono && f.nChannels > 1)
	{
		// Force To Mono: 어떤 형식이든 채널 평균 → 16 비트 PCM
		const int ch = f.nChannels, bytes = f.wBitsPerSample / 8;
		const bool isFloat = f.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
		const size_t frames = clip->Data.size() / f.nBlockAlign;
		std::vector<int16_t> mono(frames);
		for (size_t i = 0; i < frames; ++i)
		{
			float sum = 0.0f;
			for (int c = 0; c < ch; ++c)
			{
				const uint8_t* p = clip->Data.data() + i * f.nBlockAlign + (size_t)c * bytes;
				float v = 0.0f;
				if (isFloat) memcpy(&v, p, 4);
				else if (bytes == 1) v = (p[0] - 128) / 128.0f;
				else if (bytes == 2) v = (int16_t)U16(p) / 32768.0f;
				else if (bytes == 3) v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) / 2147483648.0f;
				else v = (int32_t)U32(p) / 2147483648.0f;
				sum += v;
			}
			mono[i] = (int16_t)std::clamp((int)std::lround(sum / ch * 32767.0f), -32768, 32767);
		}
		clip->Data.resize(frames * sizeof(int16_t));
		memcpy(clip->Data.data(), mono.data(), clip->Data.size());
		WAVEFORMATEX& w = clip->Format;
		w.wFormatTag = WAVE_FORMAT_PCM;
		w.nChannels = 1;
		w.wBitsPerSample = 16;
		w.nBlockAlign = 2;
		w.nAvgBytesPerSec = w.nSamplesPerSec * 2;
	}
	clip->Channels = clip->Format.nChannels;
	clip->Frequency = (int)clip->Format.nSamplesPerSec;
	clip->Samples = (uint32_t)(clip->Data.size() / clip->Format.nBlockAlign);
	clip->Length = clip->Frequency > 0 ? (float)clip->Samples / (float)clip->Frequency : 0.0f;
	Cache()[path] = clip;
	EditorLog::Write("Audio", "clip loaded %s (%d Hz, %d ch, %u bit, %.2fs)", path.c_str(), clip->Frequency, clip->Channels, f.wBitsPerSample, clip->Length);
	return clip;
}

void AudioClip::Forget(const std::string& rawPath)
{
	Cache().erase(NormalizePath(rawPath));
}

std::vector<std::string> AudioClip::FindAll()
{
	std::vector<std::string> out;
	auto scan = [&](const std::wstring& root) {
		std::error_code ec;
		if (!fs::exists(root, ec))
			return;
		for (const auto& e : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec))
			if (e.is_regular_file(ec) && IsAudioPath(e.path().string()))
				out.push_back(wstring_to_string(PathManager::GetI()->GetCutSolutionPath(e.path().wstring())));
	};
	scan(PathManager::GetI()->GetMovePathW(L"Assets\\"));
	scan(PathManager::GetI()->GetMovePathW(L"Resources\\Packages\\"));
	std::sort(out.begin(), out.end());
	return out;
}
