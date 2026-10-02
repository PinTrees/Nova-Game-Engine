#include "pch.h"
#include "Physics2DSettings.h"
#include "TagsAndLayers.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
	constexpr int N = TagsAndLayers::kLayerCount;
	bool s_Loaded = false;
	Vec2 s_Gravity(0.0f, -9.81f);
	bool s_QueriesHitTriggers = true;
	uint32 s_Saved[N];
	uint32 s_Runtime[N];
	uint32 s_Version = 1;

	std::wstring File() { return PathManager::GetI()->GetMovePathW(L"ProjectSettings\\Physics2DSettings.json"); }

	void Ensure()
	{
		if (s_Loaded)
			return;
		s_Loaded = true;
		for (int i = 0; i < N; ++i)
			s_Saved[i] = 0xFFFFFFFFu;
		s_Gravity = Vec2(0.0f, -9.81f);
		s_QueriesHitTriggers = true;
		std::ifstream in(File());
		if (in)
		{
			const json j = json::parse(in, nullptr, false);
			if (j.is_object())
			{
				if (j.contains("gravity") && j["gravity"].is_array() && j["gravity"].size() == 2)
					s_Gravity = Vec2(j["gravity"][0].get<float>(), j["gravity"][1].get<float>());
				s_QueriesHitTriggers = j.value("queriesHitTriggers", true);
				if (j.contains("ignoredLayerPairs") && j["ignoredLayerPairs"].is_array())
					for (const json& p : j["ignoredLayerPairs"])
						if (p.is_array() && p.size() == 2)
						{
							const int a = p[0].get<int>(), b = p[1].get<int>();
							if (a >= 0 && a < N && b >= 0 && b < N)
							{
								s_Saved[a] &= ~(1u << b);
								s_Saved[b] &= ~(1u << a);
							}
						}
			}
		}
		memcpy(s_Runtime, s_Saved, sizeof(s_Saved));
	}

	void Save()
	{
		json pairs = json::array();
		for (int a = 0; a < N; ++a)
			for (int b = a; b < N; ++b)
				if (!(s_Saved[a] & (1u << b)))
					pairs.push_back({ a, b });
		const std::wstring path = File();
		std::error_code ec;
		fs::create_directories(fs::path(path).parent_path(), ec);
		std::ofstream os(path, std::ios::trunc);
		if (os)
			os << json{ { "gravity", { s_Gravity.x, s_Gravity.y } }, { "queriesHitTriggers", s_QueriesHitTriggers }, { "ignoredLayerPairs", pairs } }.dump(4);
	}

	void Set(uint32* rows, int a, int b, bool collide)
	{
		if (collide) { rows[a] |= 1u << b; rows[b] |= 1u << a; }
		else { rows[a] &= ~(1u << b); rows[b] &= ~(1u << a); }
	}
}

namespace Physics2DSettings
{
	Vec2 Gravity() { Ensure(); return s_Gravity; }
	void SetGravity(const Vec2& g) { Ensure(); s_Gravity = g; Save(); }
	bool QueriesHitTriggers() { Ensure(); return s_QueriesHitTriggers; }
	void SetQueriesHitTriggers(bool v) { Ensure(); s_QueriesHitTriggers = v; Save(); }

	bool LayersCollide(int a, int b)
	{
		Ensure();
		if (a < 0 || a >= N || b < 0 || b >= N)
			return true;
		return (s_Runtime[a] >> b) & 1u;
	}

	uint32 CollisionMask(int layer)
	{
		Ensure();
		return layer >= 0 && layer < N ? s_Runtime[layer] : 0xFFFFFFFFu;
	}

	void SetLayersCollide(int a, int b, bool collide, bool save)
	{
		Ensure();
		if (a < 0 || a >= N || b < 0 || b >= N)
			return;
		Set(s_Saved, a, b, collide);
		Set(s_Runtime, a, b, collide);
		++s_Version;
		if (save)
			Save();
	}

	void SetAllCollide(bool collide)
	{
		Ensure();
		for (int i = 0; i < N; ++i)
			s_Saved[i] = collide ? 0xFFFFFFFFu : 0u;
		memcpy(s_Runtime, s_Saved, sizeof(s_Saved));
		++s_Version;
		Save();
	}

	void IgnoreLayerCollisionRuntime(int a, int b, bool ignore)
	{
		Ensure();
		if (a < 0 || a >= N || b < 0 || b >= N)
			return;
		Set(s_Runtime, a, b, !ignore);
		++s_Version;
	}

	void ResetRuntime()
	{
		Ensure();
		memcpy(s_Runtime, s_Saved, sizeof(s_Saved));
		++s_Version;
	}

	uint32 Version() { return s_Version; }
	void Reload() { s_Loaded = false; }
}
