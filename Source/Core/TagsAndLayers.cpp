#include "pch.h"
#include "TagsAndLayers.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
	const char* kBuiltinTags[] = { "Untagged", "Respawn", "Finish", "EditorOnly", "MainCamera", "Player", "GameController" };

	bool s_Loaded = false;
	std::string s_Layers[TagsAndLayers::kLayerCount];
	std::vector<std::string> s_Tags;   // Builtin + 프로젝트

	std::wstring File() { return PathManager::GetI()->GetMovePathW(L"ProjectSettings\\TagManager.json"); }

	void Defaults()
	{
		for (std::string& l : s_Layers)
			l.clear();
		s_Layers[0] = "Default";
		s_Layers[1] = "TransparentFX";
		s_Layers[2] = "Ignore Raycast";
		s_Layers[4] = "Water";
		s_Layers[5] = "UI";
		s_Tags.assign(std::begin(kBuiltinTags), std::end(kBuiltinTags));
	}

	void Ensure()
	{
		if (s_Loaded)
			return;
		s_Loaded = true;
		Defaults();
		std::ifstream in(File());
		if (!in)
			return;
		const json j = json::parse(in, nullptr, false);
		if (!j.is_object())
			return;
		if (j.contains("layers") && j["layers"].is_array())
			for (int i = 0; i < TagsAndLayers::kLayerCount && i < (int)j["layers"].size(); ++i)
				if (!TagsAndLayers::IsBuiltinLayer(i) && j["layers"][i].is_string())
					s_Layers[i] = j["layers"][i].get<std::string>();
		if (j.contains("tags") && j["tags"].is_array())
			for (const json& t : j["tags"])
				if (t.is_string() && !t.get<std::string>().empty() && std::find(s_Tags.begin(), s_Tags.end(), t.get<std::string>()) == s_Tags.end())
					s_Tags.push_back(t.get<std::string>());
	}

	void Save()
	{
		json layers = json::array(), tags = json::array();
		for (const std::string& l : s_Layers)
			layers.push_back(l);
		for (size_t i = std::size(kBuiltinTags); i < s_Tags.size(); ++i)
			tags.push_back(s_Tags[i]);
		const std::wstring path = File();
		std::error_code ec;
		fs::create_directories(fs::path(path).parent_path(), ec);
		std::ofstream os(path, std::ios::trunc);
		if (os)
			os << json{ { "tags", tags }, { "layers", layers } }.dump(4);
	}
}

namespace TagsAndLayers
{
	bool IsBuiltinLayer(int layer)
	{
		return layer == 0 || layer == 1 || layer == 2 || layer == 4 || layer == 5;
	}

	const std::string& LayerName(int layer)
	{
		static const std::string empty;
		Ensure();
		return layer >= 0 && layer < kLayerCount ? s_Layers[layer] : empty;
	}

	bool SetLayerName(int layer, const std::string& name)
	{
		Ensure();
		if (layer < 0 || layer >= kLayerCount || IsBuiltinLayer(layer))
			return false;
		// 같은 이름은 하나만 (Unity 와 같음)
		if (!name.empty())
			for (int i = 0; i < kLayerCount; ++i)
				if (i != layer && s_Layers[i] == name)
					return false;
		s_Layers[layer] = name;
		Save();
		return true;
	}

	int NameToLayer(const std::string& name)
	{
		Ensure();
		if (name.empty())
			return -1;
		for (int i = 0; i < kLayerCount; ++i)
			if (s_Layers[i] == name)
				return i;
		return -1;
	}

	std::vector<int> NamedLayers()
	{
		Ensure();
		std::vector<int> out;
		for (int i = 0; i < kLayerCount; ++i)
			if (!s_Layers[i].empty())
				out.push_back(i);
		return out;
	}

	const std::vector<std::string>& Tags()
	{
		Ensure();
		return s_Tags;
	}

	bool IsBuiltinTag(const std::string& tag)
	{
		for (const char* t : kBuiltinTags)
			if (tag == t)
				return true;
		return false;
	}

	bool AddTag(const std::string& tag)
	{
		Ensure();
		if (tag.empty() || std::find(s_Tags.begin(), s_Tags.end(), tag) != s_Tags.end())
			return false;
		s_Tags.push_back(tag);
		Save();
		return true;
	}

	bool RemoveTag(const std::string& tag)
	{
		Ensure();
		if (IsBuiltinTag(tag))
			return false;
		auto it = std::find(s_Tags.begin(), s_Tags.end(), tag);
		if (it == s_Tags.end())
			return false;
		s_Tags.erase(it);
		Save();
		return true;
	}

	void Reload()
	{
		s_Loaded = false;
	}

	int MigrateLegacyLayer(int layer)
	{
		if (layer == 3) return Water;
		if (layer == 4) return UI;
		return layer;
	}

	uint32 MigrateLegacyMask(uint32 mask)
	{
		const uint32 keep = mask & ~((1u << 3) | (1u << 4) | (1u << 5));
		uint32 out = keep;
		if (mask & (1u << 3)) out |= 1u << Water;
		if (mask & (1u << 4)) out |= 1u << UI;
		return out;
	}

	void MigrateLegacyObjectJson(json& go)
	{
		if (!go.is_object())
			return;
		if (go.contains("layer") && go["layer"].is_number_integer())
			go["layer"] = MigrateLegacyLayer(go["layer"].get<int>());
		if (go.contains("components") && go["components"].is_array())
			for (json& c : go["components"])
				for (const char* key : { "includeLayers", "excludeLayers" })
					if (c.is_object() && c.contains(key) && c[key].is_number_integer())
						c[key] = MigrateLegacyMask(c[key].get<uint32>());
		if (go.contains("children") && go["children"].is_array())
			for (json& child : go["children"])
				MigrateLegacyObjectJson(child);
	}
}
