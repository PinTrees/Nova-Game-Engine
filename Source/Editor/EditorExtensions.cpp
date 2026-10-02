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
}
