#include "pch.h"
#include "AssetImportSettings.h"
#include <filesystem>
#include <fstream>
#include <mutex>

namespace fs = std::filesystem;
using nlohmann::json;

namespace AssetImport
{
	namespace
	{
		const char* kCompression[] = { "None", "NormalQuality", "HighQuality" };
		const char* kTextureType[] = { "Default", "NormalMap", "Sprite" };
		const char* kAnimationType[] = { "Generic", "Humanoid" };
		const char* kLoadType[] = { "DecompressOnLoad", "CompressedInMemory", "Streaming" };

		template <size_t N>
		int IndexOf(const json& j, const char* key, const char* (&names)[N], int fallback)
		{
			if (!j.contains(key))
				return fallback;
			const json& v = j[key];
			if (v.is_number_integer())
			{
				const int i = v.get<int>();
				return i >= 0 && i < (int)N ? i : fallback;
			}
			if (v.is_string())
			{
				const std::string s = v.get<std::string>();
				for (size_t i = 0; i < N; ++i)
					if (_stricmp(s.c_str(), names[i]) == 0)
						return (int)i;
			}
			return fallback;
		}

		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return s;
		}

		long long Stamp(const std::wstring& path)
		{
			std::error_code ec;
			const auto t = fs::last_write_time(path, ec);
			return ec ? 0 : (long long)t.time_since_epoch().count();
		}

		// .meta 내용 캐시 (경로 → 수정 시각, JSON). 텍스처·모델을 읽을 때마다 파일을 열지 않게
		struct Entry { long long Stamp = 0; json Data; };
		std::mutex s_Lock;
		std::map<std::wstring, Entry>& Cache()
		{
			static std::map<std::wstring, Entry> cache;
			return cache;
		}

		json ReadMeta(const std::wstring& assetPath)
		{
			const std::wstring meta = MetaPath(assetPath);
			const long long stamp = Stamp(meta);
			std::lock_guard<std::mutex> lock(s_Lock);
			Entry& e = Cache()[meta];
			if (stamp == 0)
			{
				e = Entry();
				return json::object();
			}
			if (e.Stamp != stamp)
			{
				std::ifstream in(meta, std::ios::binary);
				json j = json::parse(in, nullptr, false);
				e.Data = j.is_object() ? j : json::object();
				e.Stamp = stamp;
			}
			return e.Data;
		}
	}

	// ------------------------------------------------------------------ 텍스처
	json TextureSettings::ToJson() const
	{
		return json{ { "textureType", kTextureType[std::clamp(TextureType, 0, 2)] }, { "sRGB", SRGB }, { "maxSize", MaxSize },
			{ "compression", kCompression[std::clamp(Compression, 0, 2)] }, { "mipmaps", MipMaps } };
	}

	TextureSettings TextureSettings::Raw()
	{
		TextureSettings s;
		s.MaxSize = 16384;
		s.Compression = None;
		return s;
	}

	void TextureSettings::FromJson(const json& j)
	{
		const int m = j.value("maxSize", 2048);
		// Unity 와 같이 2 의 거듭제곱 32 ~ 16384
		int size = 32;
		while (size < m && size < 16384)
			size *= 2;
		MaxSize = size;
		TextureType = IndexOf(j, "textureType", kTextureType, Default);
		SRGB = j.value("sRGB", true);
		Compression = IndexOf(j, "compression", kCompression, NormalQuality);
		// Sprite (2D and UI) 는 Unity 처럼 밉 없이가 기본
		MipMaps = j.value("mipmaps", TextureType != Sprite);
	}

	bool TextureSettings::IsDefault() const
	{
		const TextureSettings d;
		return TextureType == d.TextureType && SRGB == d.SRGB && MaxSize == d.MaxSize && Compression == d.Compression && MipMaps == d.MipMaps;
	}

	std::string TextureSettings::CacheTag() const
	{
		return "t" + std::to_string(TextureType) + (SRGB ? "" : "l") + "m" + std::to_string(MaxSize) + "c" + std::to_string(Compression) + (MipMaps ? "" : "n");
	}

	// ------------------------------------------------------------------ 모델
	json ModelSettings::ToJson() const
	{
		json j{ { "scaleFactor", ScaleFactor }, { "importAnimation", ImportAnimation } };
		if (AnimationType != Auto)
			j["animationType"] = kAnimationType[AnimationType == Humanoid ? 1 : 0];
		if (!HumanBones.empty())
			j["humanBones"] = HumanBones;
		return j;
	}

	void ModelSettings::FromJson(const json& j)
	{
		ScaleFactor = j.value("scaleFactor", 1.0f);
		if (!(ScaleFactor > 0.0f) || !std::isfinite(ScaleFactor))
			ScaleFactor = 1.0f;
		ImportAnimation = j.value("importAnimation", true);
		AnimationType = IndexOf(j, "animationType", kAnimationType, Auto);
		HumanBones.clear();
		if (j.contains("humanBones") && j["humanBones"].is_object())
			for (auto it = j["humanBones"].begin(); it != j["humanBones"].end(); ++it)
				if (it.value().is_string() && !it.value().get<std::string>().empty())
					HumanBones[it.key()] = it.value().get<std::string>();
	}

	// ------------------------------------------------------------------ 오디오
	json AudioSettings::ToJson() const
	{
		return json{ { "loadType", kLoadType[std::clamp(LoadType, 0, 2)] }, { "forceToMono", ForceToMono } };
	}

	void AudioSettings::FromJson(const json& j)
	{
		LoadType = IndexOf(j, "loadType", kLoadType, DecompressOnLoad);
		ForceToMono = j.value("forceToMono", false);
	}

	// ------------------------------------------------------------------
	Kind KindOf(const std::wstring& path)
	{
		const std::string ext = Lower(fs::path(path).extension().string());
		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".dds")
			return Kind::Texture;
		if (ext == ".fbx" || ext == ".vrm" || ext == ".glb" || ext == ".gltf")
			return Kind::Model;
		if (ext == ".wav" || ext == ".ogg" || ext == ".mp3")
			return Kind::Audio;
		return Kind::None;
	}

	bool AppliesTo(const std::wstring& fullPath)
	{
		auto norm = [](std::wstring p) {
			std::replace(p.begin(), p.end(), L'/', L'\\');
			std::transform(p.begin(), p.end(), p.begin(), ::towlower);
			return fs::path(p).lexically_normal().wstring();
		};
		const std::wstring p = norm(fullPath);
		for (const wchar_t* root : { L"Assets\\", L"Resources\\Packages\\" })
		{
			const std::wstring r = norm(PathManager::GetI()->GetMovePathW(root));
			if (!r.empty() && p.compare(0, r.size(), r) == 0)
				return true;
		}
		return false;
	}

	std::wstring MetaPath(const std::wstring& assetPath)
	{
		return assetPath + L".meta";
	}

	TextureSettings LoadTexture(const std::wstring& assetPath)
	{
		TextureSettings s;
		s.FromJson(ReadMeta(assetPath));
		return s;
	}

	ModelSettings LoadModel(const std::wstring& assetPath)
	{
		ModelSettings s;
		s.FromJson(ReadMeta(assetPath));
		return s;
	}

	AudioSettings LoadAudio(const std::wstring& assetPath)
	{
		AudioSettings s;
		s.FromJson(ReadMeta(assetPath));
		return s;
	}

	json LoadJson(const std::wstring& assetPath)
	{
		switch (KindOf(assetPath))
		{
		case Kind::Texture: return LoadTexture(assetPath).ToJson();
		case Kind::Model: return LoadModel(assetPath).ToJson();
		case Kind::Audio: return LoadAudio(assetPath).ToJson();
		default: return json::object();
		}
	}

	bool Save(const std::wstring& assetPath, const json& settings)
	{
		const Kind kind = KindOf(assetPath);
		if (kind == Kind::None)
			return false;
		json out = json::object();
		out["importer"] = kind == Kind::Texture ? "TextureImporter" : (kind == Kind::Model ? "ModelImporter" : "AudioImporter");
		// 알려진 키만 (형식을 맞춰) 저장
		json clean;
		if (kind == Kind::Texture) { TextureSettings s; s.FromJson(settings); clean = s.ToJson(); }
		else if (kind == Kind::Model) { ModelSettings s; s.FromJson(settings); clean = s.ToJson(); }
		else { AudioSettings s; s.FromJson(settings); clean = s.ToJson(); }
		out.update(clean);
		const std::wstring meta = MetaPath(assetPath);
		{
			std::ofstream f(meta, std::ios::binary | std::ios::trunc);
			if (!f)
				return false;
			f << out.dump(2) << "\n";
		}
		// 같은 초 안에 두 번 써도 다시 읽게
		std::lock_guard<std::mutex> lock(s_Lock);
		Cache().erase(meta);
		return true;
	}

	long long MetaStamp(const std::wstring& assetPath)
	{
		return Stamp(MetaPath(assetPath));
	}

	namespace
	{
		std::map<std::wstring, TextureInfo>& Infos()
		{
			static std::map<std::wstring, TextureInfo> infos;
			return infos;
		}
		std::wstring InfoKey(std::wstring p)
		{
			std::replace(p.begin(), p.end(), L'/', L'\\');
			std::transform(p.begin(), p.end(), p.begin(), ::towlower);
			return fs::path(p).lexically_normal().wstring();
		}
	}

	void RecordTexture(const std::wstring& assetPath, const TextureInfo& info)
	{
		std::lock_guard<std::mutex> lock(s_Lock);
		Infos()[InfoKey(assetPath)] = info;
	}

	bool GetTextureInfo(const std::wstring& assetPath, TextureInfo& out)
	{
		std::lock_guard<std::mutex> lock(s_Lock);
		auto it = Infos().find(InfoKey(assetPath));
		if (it == Infos().end())
			return false;
		out = it->second;
		return true;
	}
}
