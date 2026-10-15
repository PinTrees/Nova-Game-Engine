#include "pch.h"
#include "AnimationEventStore.h"

namespace
{
	struct FileEntry
	{
		long long Stamp = 0;
		unsigned long long CheckedTick = 0;
		std::map<int, std::shared_ptr<const AnimationEventStore::EventList>> Clips;
	};

	std::mutex s_Lock;
	std::map<std::string, FileEntry>& Files()
	{
		static std::map<std::string, FileEntry> files;
		return files;
	}

	std::string Normalize(std::string path)
	{
		std::replace(path.begin(), path.end(), '/', '\\');
		const size_t first = path.find_first_not_of('\\');
		path.erase(0, first == std::string::npos ? path.size() : first);
		return path;
	}

	std::wstring FullPath(const std::string& path)
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

	void Read(const std::wstring& file, FileEntry& e)
	{
		e.Clips.clear();
		std::ifstream in(file, std::ios::binary);
		if (!in)
			return;
		json j = json::parse(in, nullptr, false);
		if (j.is_discarded() || !j.contains("clips") || !j["clips"].is_object())
			return;
		for (auto it = j["clips"].begin(); it != j["clips"].end(); ++it)
		{
			if (!it.value().is_array())
				continue;
			auto list = std::make_shared<AnimationEventStore::EventList>();
			for (const auto& ej : it.value())
			{
				AnimationEventStore::Event ev;
				ev.Time = ej.value("time", 0.0f);
				ev.Function = ej.value("functionName", std::string());
				ev.String = ej.value("stringParameter", std::string());
				ev.Float = ej.value("floatParameter", 0.0f);
				ev.Int = ej.value("intParameter", 0);
				if (!ev.Function.empty())
					list->push_back(ev);
			}
			std::sort(list->begin(), list->end(), [](const auto& a, const auto& b) { return a.Time < b.Time; });
			e.Clips[atoi(it.key().c_str())] = list;
		}
	}
}

namespace AnimationEventStore
{
	std::string FilePath(const std::string& modelPath)
	{
		return Normalize(modelPath) + ".animevents";
	}

	std::shared_ptr<const EventList> Get(const std::string& modelPath, int clipIndex)
	{
		if (modelPath.empty())
			return nullptr;
		const std::string key = Normalize(modelPath);
		std::lock_guard<std::mutex> lock(s_Lock);
		FileEntry& e = Files()[key];
		const unsigned long long now = ::GetTickCount64();
		if (e.CheckedTick == 0 || now - e.CheckedTick >= 500)
		{
			e.CheckedTick = now;
			const std::wstring file = FullPath(key + ".animevents");
			const long long stamp = Stamp(file);
			if (stamp != e.Stamp)
			{
				e.Stamp = stamp;
				Read(file, e);
			}
		}
		auto it = e.Clips.find(clipIndex);
		return it == e.Clips.end() || it->second->empty() ? nullptr : it->second;
	}

	bool Set(const std::string& modelPath, int clipIndex, const EventList& events)
	{
		const std::string key = Normalize(modelPath);
		const std::wstring file = FullPath(key + ".animevents");
		json j = json::object();
		{
			std::ifstream in(file, std::ios::binary);
			if (in)
			{
				j = json::parse(in, nullptr, false);
				if (j.is_discarded() || !j.is_object())
					j = json::object();
			}
		}
		if (!j.contains("clips") || !j["clips"].is_object())
			j["clips"] = json::object();
		EventList sorted = events;
		std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.Time < b.Time; });
		json arr = json::array();
		for (const Event& ev : sorted)
			arr.push_back({ { "time", ev.Time }, { "functionName", ev.Function }, { "stringParameter", ev.String },
				{ "floatParameter", ev.Float }, { "intParameter", ev.Int } });
		if (arr.empty())
			j["clips"].erase(std::to_string(clipIndex));
		else
			j["clips"][std::to_string(clipIndex)] = arr;
		{
			std::ofstream os(file, std::ios::binary | std::ios::trunc);
			if (!os)
				return false;
			os << j.dump(4);
		}
		std::lock_guard<std::mutex> lock(s_Lock);
		FileEntry& e = Files()[key];
		e.CheckedTick = 0;   // 다음 Get 이 다시 읽는다
		e.Stamp = -1;
		return true;
	}
}
