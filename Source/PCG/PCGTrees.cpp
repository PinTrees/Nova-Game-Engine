#include "pch.h"
#include "PCGTrees.h"
#include "TreeDesc.h"
#include <map>
#include <mutex>

namespace PCG
{
	bool IsTreePath(const std::string& path) { return path.rfind("tree:", 0) == 0; }

	int TreePreset(const std::string& path)
	{
		if (!IsTreePath(path))
			return -1;
		std::string name = path.substr(5);
		const size_t hash = name.find('#');
		if (hash != std::string::npos)
			name = name.substr(0, hash);
		int count = 0;
		const char* const* names = TreeParams::PresetNames(count);
		for (int i = 0; i < count; ++i)
			if (_stricmp(names[i], name.c_str()) == 0)
				return i;
		return -1;
	}

	const TreeDesc* TreeSpecies(const std::string& path, int variant, float cullDistance)
	{
		static std::mutex lock;
		static std::map<std::string, std::unique_ptr<TreeDesc>> species;
		const int preset = TreePreset(path);
		if (preset < 0)
			return nullptr;
		variant = std::clamp(variant, 0, kTreeVariants - 1);
		const std::string key = path + "|" + std::to_string(variant) + "|" + std::to_string((int)cullDistance);
		std::lock_guard<std::mutex> l(lock);
		auto& slot = species[key];
		if (!slot)
		{
			auto d = std::make_unique<TreeDesc>();
			d->Params.Seed = 1009 + variant * 7919 + preset * 131;
			d->ApplyPreset(preset);
			// 변형: 키 · 기울기를 조금씩 (같은 종이라도 줄지어 보이지 않게)
			d->Params.Height *= 0.92f + 0.16f * variant / (std::max)(1, kTreeVariants - 1);
			d->Params.Lean += variant == 1 ? 3.0f : 0.0f;
			// 그리는 거리는 스포너가 정한다 (멀리는 임포스터)
			d->CullDistance = (std::max)(d->CullDistance, cullDistance);
			d->Invalidate();
			slot = std::move(d);
		}
		return slot.get();
	}

	int TreeVariantAt(float x, float z)
	{
		uint32_t h = (uint32_t)(int32_t)floorf(x * 3.1f) * 0x27d4eb2dU ^ (uint32_t)(int32_t)floorf(z * 2.7f) * 0x165667b1U;
		h ^= h >> 15; h *= 0x85ebca6bU; h ^= h >> 13;
		return (int)(h % (uint32_t)kTreeVariants);
	}
}
