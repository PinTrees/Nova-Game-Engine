#include "pch.h"
#include "AnimationOverrides.h"

namespace
{
	std::mutex s_Lock;
	std::unordered_map<const GameObject*, std::vector<AnimationOverrides::Clip>>& Table()
	{
		static std::unordered_map<const GameObject*, std::vector<AnimationOverrides::Clip>> table;
		return table;
	}
	std::unordered_map<const GameObject*, unsigned>& Revisions()
	{
		static std::unordered_map<const GameObject*, unsigned> revisions;
		return revisions;
	}
}

namespace AnimationOverrides
{
	void Set(const GameObject* go, const std::vector<Clip>& clips)
	{
		if (go == nullptr)
			return;
		std::lock_guard<std::mutex> lock(s_Lock);
		if (clips.empty())
			Table().erase(go);
		else
			Table()[go] = clips;
		++Revisions()[go];
	}

	void Clear(const GameObject* go)
	{
		std::lock_guard<std::mutex> lock(s_Lock);
		Table().erase(go);
		++Revisions()[go];
	}

	unsigned Revision(const GameObject* go)
	{
		std::lock_guard<std::mutex> lock(s_Lock);
		auto it = Revisions().find(go);
		return it == Revisions().end() ? 0 : it->second;
	}

	bool Get(const GameObject* go, std::vector<Clip>& out)
	{
		std::lock_guard<std::mutex> lock(s_Lock);
		auto it = Table().find(go);
		if (it == Table().end())
			return false;
		out = it->second;
		return !out.empty();
	}

	bool Has(const GameObject* go)
	{
		std::lock_guard<std::mutex> lock(s_Lock);
		return Table().count(go) > 0;
	}
}
