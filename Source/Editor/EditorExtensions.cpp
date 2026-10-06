#include "pch.h"
#include "EditorExtensions.h"

namespace EditorExtensions
{
	namespace
	{
		std::vector<AssetType>& Types()
		{
			static std::vector<AssetType> types;
			return types;
		}

		struct SceneToolEntry { std::string Owner; SceneTool Tool; };
		std::vector<SceneToolEntry>& SceneTools()
		{
			static std::vector<SceneToolEntry> tools;
			return tools;
		}

		std::vector<CreateMenuItem>& MenuItems()
		{
			static std::vector<CreateMenuItem> items;
			return items;
		}

		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return s;
		}
	}

	void RegisterAssetType(const AssetType& type)
	{
		AssetType t = type;
		t.Extension = Lower(t.Extension);
		auto& list = Types();
		list.erase(std::remove_if(list.begin(), list.end(), [&](const AssetType& e) { return e.Extension == t.Extension; }), list.end());
		list.push_back(std::move(t));
	}

	void UnregisterOwner(const std::string& owner)
	{
		auto& list = Types();
		list.erase(std::remove_if(list.begin(), list.end(), [&](const AssetType& e) { return e.Owner == owner; }), list.end());
		auto& tools = SceneTools();
		tools.erase(std::remove_if(tools.begin(), tools.end(), [&](const SceneToolEntry& e) { return e.Owner == owner; }), tools.end());
		auto& items = MenuItems();
		items.erase(std::remove_if(items.begin(), items.end(), [&](const CreateMenuItem& e) { return e.Owner == owner; }), items.end());
	}

	const AssetType* FindAssetType(const std::string& extension)
	{
		const std::string ext = Lower(extension);
		for (const AssetType& t : Types())
			if (t.Extension == ext)
				return &t;
		return nullptr;
	}

	std::vector<const AssetType*> AssetTypes()
	{
		std::vector<const AssetType*> out;
		for (const AssetType& t : Types())
			out.push_back(&t);
		return out;
	}

	void RegisterSceneTool(const std::string& owner, SceneTool tool)
	{
		SceneTools().push_back({ owner, std::move(tool) });
	}

	bool RunSceneTools(const SceneViewContext& context)
	{
		bool used = false;
		for (const SceneToolEntry& e : SceneTools())
			if (e.Tool && e.Tool(context))
				used = true;
		return used;
	}

	void RegisterCreateMenu(const CreateMenuItem& item)
	{
		auto& items = MenuItems();
		items.erase(std::remove_if(items.begin(), items.end(), [&](const CreateMenuItem& e) { return e.Path == item.Path; }), items.end());
		items.push_back(item);
	}

	std::vector<const CreateMenuItem*> CreateMenuItems(const std::string& folder)
	{
		std::vector<const CreateMenuItem*> out;
		const std::string prefix = folder + "/";
		for (const CreateMenuItem& e : MenuItems())
			if (e.Path.rfind(prefix, 0) == 0 && e.Path.find('/', prefix.size()) == std::string::npos)
				out.push_back(&e);
		return out;
	}
}
