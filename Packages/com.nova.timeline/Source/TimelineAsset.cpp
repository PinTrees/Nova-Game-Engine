#include "pch.h"
#include "TimelineAsset.h"

namespace
{
	std::string NormalizePath(std::string path)
	{
		std::replace(path.begin(), path.end(), '/', '\\');
		if (!std::filesystem::path(path).is_absolute())
		{
			const size_t first = path.find_first_not_of('\\');
			path.erase(0, first == std::string::npos ? path.size() : first);
		}
		return path;
	}

	std::wstring FilePath(const std::string& path)
	{
		std::filesystem::path p(string_to_wstring(path));
		return p.is_absolute() ? p.wstring() : PathManager::GetI()->GetMovePathW(string_to_wstring(path));
	}

	long long Stamp(const std::wstring& file)
	{
		std::error_code ec;
		const auto t = std::filesystem::last_write_time(file, ec);
		return ec ? 0 : (long long)t.time_since_epoch().count();
	}

	struct CacheEntry { std::shared_ptr<TimelineAsset> Asset; long long Stamp = 0; };
	std::map<std::string, CacheEntry>& Cache()
	{
		static std::map<std::string, CacheEntry> cache;
		return cache;
	}

	const char* kTypes[] = { "Animation", "Activation", "Audio", "Signal", "Cinemachine" };
}

const char* const* TimelineAsset::TrackTypes(int& count)
{
	count = (int)(sizeof(kTypes) / sizeof(kTypes[0]));
	return kTypes;
}

double TimelineAsset::Duration() const
{
	if (FixedDuration > 0.0)
		return FixedDuration;
	double d = 0.0;
	for (const TimelineTrack& t : Tracks)
	{
		for (const TimelineClip& c : t.Clips)
			d = (std::max)(d, c.End());
		for (const TimelineMarker& m : t.Markers)
			d = (std::max)(d, m.Time);
	}
	return d;
}

TimelineTrack* TimelineAsset::FindTrack(const std::string& idOrName)
{
	for (TimelineTrack& t : Tracks)
		if (t.Id == idOrName)
			return &t;
	for (TimelineTrack& t : Tracks)
		if (t.Name == idOrName)
			return &t;
	// 번호
	if (!idOrName.empty() && std::all_of(idOrName.begin(), idOrName.end(), ::isdigit))
	{
		const size_t i = (size_t)atoi(idOrName.c_str());
		if (i < Tracks.size())
			return &Tracks[i];
	}
	return nullptr;
}

TimelineTrack& TimelineAsset::AddTrack(const std::string& type, const std::string& name)
{
	TimelineTrack t;
	t.Type = type;
	// id: 종류 + 겹치지 않는 번호 (지워도 다른 트랙의 바인딩이 그대로)
	int n = 1;
	for (;; ++n)
	{
		const std::string id = type + std::to_string(n);
		bool used = false;
		for (const TimelineTrack& o : Tracks)
			used |= o.Id == id;
		if (!used)
		{
			t.Id = id;
			break;
		}
	}
	t.Name = name.empty() ? type + " Track" + (n > 1 ? " " + std::to_string(n) : std::string()) : name;
	Tracks.push_back(t);
	return Tracks.back();
}

std::string TimelineAsset::ToJsonString() const
{
	json tracks = json::array();
	for (const TimelineTrack& t : Tracks)
	{
		json clips = json::array();
		for (const TimelineClip& c : t.Clips)
		{
			json cj = { { "name", c.Name }, { "start", c.Start }, { "duration", c.Duration }, { "clipIn", c.ClipIn }, { "speed", c.Speed },
				{ "easeIn", c.EaseIn }, { "easeOut", c.EaseOut } };
			if (!c.Asset.empty()) { cj["asset"] = c.Asset; cj["clipIndex"] = c.ClipIndex; }
			if (c.Loop) cj["loop"] = true;
			if (c.Object) cj["object"] = c.Object;
			clips.push_back(cj);
		}
		json markers = json::array();
		for (const TimelineMarker& m : t.Markers)
			markers.push_back({ { "time", m.Time }, { "method", m.Method }, { "string", m.String }, { "float", m.Float }, { "int", m.Int }, { "retroactive", m.Retroactive } });
		json tj = { { "id", t.Id }, { "type", t.Type }, { "name", t.Name }, { "clips", clips } };
		if (t.Muted) tj["muted"] = true;
		if (!markers.empty()) tj["markers"] = markers;
		tracks.push_back(tj);
	}
	json j = { { "nova_timeline", 1 }, { "frameRate", FrameRate }, { "tracks", tracks } };
	if (FixedDuration > 0.0)
		j["duration"] = FixedDuration;
	return j.dump(2);
}

void TimelineAsset::ApplyJson(const std::string& text)
{
	json j = json::parse(text, nullptr, false);
	if (j.is_discarded() || !j.is_object())
		return;
	FrameRate = j.value("frameRate", 30.0);
	FixedDuration = j.value("duration", 0.0);
	Tracks.clear();
	if (j.contains("tracks") && j["tracks"].is_array())
		for (const auto& tj : j["tracks"])
		{
			TimelineTrack t;
			t.Id = tj.value("id", std::string());
			t.Type = tj.value("type", std::string("Animation"));
			t.Name = tj.value("name", t.Type + " Track");
			t.Muted = tj.value("muted", false);
			if (t.Id.empty())
				t.Id = t.Type + std::to_string(Tracks.size() + 1);
			if (tj.contains("clips") && tj["clips"].is_array())
				for (const auto& cj : tj["clips"])
				{
					TimelineClip c;
					c.Name = cj.value("name", std::string());
					c.Start = cj.value("start", 0.0);
					c.Duration = (std::max)(1e-3, cj.value("duration", 1.0));
					c.ClipIn = cj.value("clipIn", 0.0);
					c.Speed = cj.value("speed", 1.0);
					c.EaseIn = (std::max)(0.0, cj.value("easeIn", 0.0));
					c.EaseOut = (std::max)(0.0, cj.value("easeOut", 0.0));
					c.Asset = cj.value("asset", std::string());
					c.ClipIndex = cj.value("clipIndex", 0);
					c.Loop = cj.value("loop", false);
					c.Object = cj.value("object", (uint64_t)0);
					t.Clips.push_back(c);
				}
			if (tj.contains("markers") && tj["markers"].is_array())
				for (const auto& mj : tj["markers"])
				{
					TimelineMarker m;
					m.Time = mj.value("time", 0.0);
					m.Method = mj.value("method", std::string());
					m.String = mj.value("string", std::string());
					m.Float = mj.value("float", 0.0f);
					m.Int = mj.value("int", 0);
					m.Retroactive = mj.value("retroactive", false);
					t.Markers.push_back(m);
				}
			std::sort(t.Clips.begin(), t.Clips.end(), [](const TimelineClip& a, const TimelineClip& b) { return a.Start < b.Start; });
			std::sort(t.Markers.begin(), t.Markers.end(), [](const TimelineMarker& a, const TimelineMarker& b) { return a.Time < b.Time; });
			Tracks.push_back(t);
		}
	++Revision;
}

bool TimelineAsset::Save() const
{
	const std::wstring file = FilePath(Path);
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
	{
		std::ofstream os(file, std::ios::binary | std::ios::trunc);
		if (!os)
			return false;
		os << ToJsonString();
	}
	auto it = Cache().find(Path);
	if (it != Cache().end())
		it->second.Stamp = Stamp(file);   // 우리가 쓴 것은 다시 읽지 않는다
	return true;
}

std::shared_ptr<TimelineAsset> TimelineAsset::Load(const std::string& rawPath)
{
	const std::string path = NormalizePath(rawPath);
	if (path.empty())
		return nullptr;
	const std::wstring file = FilePath(path);
	const long long stamp = Stamp(file);
	auto it = Cache().find(path);
	if (it != Cache().end() && (stamp == 0 || stamp == it->second.Stamp))
		return it->second.Asset;
	std::ifstream in(file, std::ios::binary);
	if (!in)
		return it != Cache().end() ? it->second.Asset : nullptr;
	std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	std::shared_ptr<TimelineAsset> a = it != Cache().end() ? it->second.Asset : std::make_shared<TimelineAsset>();
	a->Path = path;
	a->ApplyJson(text);   // 같은 객체를 고친다 (창 · 디렉터가 같은 것을 본다)
	Cache()[path] = { a, stamp };
	return a;
}

std::shared_ptr<TimelineAsset> TimelineAsset::Create(const std::string& rawPath)
{
	auto a = std::make_shared<TimelineAsset>();
	a->Path = NormalizePath(rawPath);
	a->Save();
	Cache()[a->Path] = { a, Stamp(FilePath(a->Path)) };
	return a;
}
