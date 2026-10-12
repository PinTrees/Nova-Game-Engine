#include "pch.h"
#include "TextureLibrary.h"
#include "PackageManager.h"
#include "EditorLog.h"
#include <fstream>
#include <filesystem>
#include <sstream>

namespace
{
	std::vector<TextureLibrary::Material> s_All;
	bool s_Loaded = false;

	std::string Lower(std::string s)
	{
		for (char& c : s)
			c = (char)tolower((unsigned char)c);
		return s;
	}

	void Load()
	{
		s_Loaded = true;
		s_All.clear();
		for (const PackageInfo* p : PackageManager::InProject())
		{
			const std::filesystem::path catalog = std::filesystem::path(p->Folder) / "catalog.json";
			std::error_code ec;
			if (!std::filesystem::exists(catalog, ec))
				continue;
			std::ifstream is(catalog);
			const nlohmann::json j = nlohmann::json::parse(is, nullptr, false);
			if (!j.is_object() || !j.contains("materials") || !j["materials"].is_array())
			{
				EditorLog::Write("Textures", "%s: catalog.json is not readable", p->Name.c_str());
				continue;
			}
			for (const nlohmann::json& e : j["materials"])
			{
				TextureLibrary::Material m;
				m.Id = e.value("id", std::string());
				if (m.Id.empty())
					continue;
				m.Name = e.value("name", m.Id);
				m.Category = e.value("category", m.Id.substr(0, m.Id.find('/')));
				m.Description = e.value("description", std::string());
				m.Package = p->Name;
				m.TileMeters = e.value("tileMeters", 2.0f);
				if (e.contains("tags") && e["tags"].is_array())
					for (const auto& t : e["tags"])
						if (t.is_string())
							m.Tags.push_back(t.get<std::string>());
				if (e.contains("maps") && e["maps"].is_object())
					for (auto it = e["maps"].begin(); it != e["maps"].end(); ++it)
						if (it.value().is_string())
							m.Maps[it.key()] = (std::filesystem::path(p->Folder) / std::filesystem::path(it.value().get<std::string>())).lexically_normal().wstring();
				if (e.contains("source"))
					m.Source = e["source"];
				s_All.push_back(std::move(m));
			}
		}
		EditorLog::Write("Textures", "texture library: %d materials", (int)s_All.size());
	}
}

namespace TextureLibrary
{
	const std::vector<Material>& All()
	{
		if (!s_Loaded)
			Load();
		return s_All;
	}

	void Reload()
	{
		s_Loaded = false;
	}

	const Material* Find(const std::string& idOrName)
	{
		const auto& all = All();
		if (idOrName.empty())
			return nullptr;
		for (const Material& m : all)
			if (m.Id == idOrName)
				return &m;
		const std::string q = Lower(idOrName);
		for (const Material& m : all)
			if (Lower(m.Id) == q || Lower(m.Name) == q)
				return &m;
		for (const Material& m : all)
		{
			const std::string id = Lower(m.Id);
			const size_t slash = id.rfind('/');
			if (slash != std::string::npos && id.substr(slash + 1) == q)
				return &m;
		}
		return nullptr;
	}

	std::vector<const Material*> Search(const std::string& query, const std::string& category)
	{
		std::vector<std::string> words;
		std::istringstream ss(Lower(query));
		for (std::string w; ss >> w;)
			words.push_back(w);
		std::vector<std::pair<int, const Material*>> scored;
		for (const Material& m : All())
		{
			if (!category.empty() && Lower(m.Category) != Lower(category))
				continue;
			int score = words.empty() ? 1 : 0;
			const std::string id = Lower(m.Id), name = Lower(m.Name), desc = Lower(m.Description);
			for (const std::string& w : words)
			{
				int s = 0;
				if (id.find(w) != std::string::npos) s += 4;
				if (name.find(w) != std::string::npos) s += 3;
				for (const std::string& t : m.Tags)
					if (Lower(t) == w) s += 3;
					else if (Lower(t).find(w) != std::string::npos) s += 1;
				if (desc.find(w) != std::string::npos) s += 1;
				score += s;
			}
			if (score > 0)
				scored.push_back({ score, &m });
		}
		std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
		std::vector<const Material*> out;
		for (const auto& s : scored)
			out.push_back(s.second);
		return out;
	}

	std::vector<std::string> Categories()
	{
		std::vector<std::string> out;
		for (const Material& m : All())
			if (std::find(out.begin(), out.end(), m.Category) == out.end())
				out.push_back(m.Category);
		return out;
	}

	nlohmann::json ToJson(const Material& m, bool full)
	{
		nlohmann::json j = { { "id", m.Id }, { "name", m.Name }, { "category", m.Category }, { "tags", m.Tags }, { "description", m.Description } };
		if (full)
		{
			j["package"] = m.Package;
			j["tileMeters"] = m.TileMeters;
			nlohmann::json maps = nlohmann::json::object();
			for (const auto& kv : m.Maps)
				maps[kv.first] = std::filesystem::path(kv.second).string();
			j["maps"] = maps;
			j["source"] = m.Source;
		}
		return j;
	}
}
