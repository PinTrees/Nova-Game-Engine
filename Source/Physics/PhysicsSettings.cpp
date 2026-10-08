#include "pch.h"
#include "PhysicsSettings.h"
#include "TagsAndLayers.h"
#include <atomic>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
	constexpr int N = TagsAndLayers::kLayerCount;
	bool s_Loaded = false;
	Vec3 s_Gravity(0.0f, -9.81f, 0.0f);
	bool s_Async = false;
	uint32 s_Saved[N];                  // 설정 (행 a 의 비트 b)
	std::atomic<uint32> s_Runtime[N];   // 실행 중 (충돌 필터가 작업 스레드에서 읽는다)

	std::wstring File() { return PathManager::GetI()->GetMovePathW(L"ProjectSettings\\PhysicsSettings.json"); }

	void CopyToRuntime()
	{
		for (int i = 0; i < N; ++i)
			s_Runtime[i].store(s_Saved[i], std::memory_order_relaxed);
	}

	void Ensure()
	{
		if (s_Loaded)
			return;
		s_Loaded = true;
		for (int i = 0; i < N; ++i)
			s_Saved[i] = 0xFFFFFFFFu;
		s_Gravity = Vec3(0.0f, -9.81f, 0.0f);
		s_Async = false;
		std::ifstream in(File());
		if (in)
		{
			const json j = json::parse(in, nullptr, false);
			if (j.is_object())
			{
				if (j.contains("gravity") && j["gravity"].is_array() && j["gravity"].size() == 3)
					s_Gravity = Vec3(j["gravity"][0].get<float>(), j["gravity"][1].get<float>(), j["gravity"][2].get<float>());
				s_Async = j.value("asyncSimulation", false);
				// 끈 쌍만 적는다: [[a, b], ...] (레이어 번호)
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
		CopyToRuntime();
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
			os << json{ { "gravity", { s_Gravity.x, s_Gravity.y, s_Gravity.z } }, { "ignoredLayerPairs", pairs }, { "asyncSimulation", s_Async } }.dump(4);
	}
}

namespace PhysicsSettings
{
	Vec3 Gravity()
	{
		Ensure();
		return s_Gravity;
	}

	void SetGravity(const Vec3& g)
	{
		Ensure();
		s_Gravity = g;
		Save();
	}

	bool AsyncSimulation()
	{
		Ensure();
		// 검사: NOVA_PHYSICS_ASYNC=1 이면 설정과 상관없이 켠다 (기존 물리 스위트를 겹치기 모드로 돌린다)
		static const bool s_Forced = [] { char v[8] = {}; return ::GetEnvironmentVariableA("NOVA_PHYSICS_ASYNC", v, sizeof(v)) > 0 && v[0] == '1'; }();
		return s_Async || s_Forced;
	}

	void SetAsyncSimulation(bool on)
	{
		Ensure();
		s_Async = on;
		Save();
	}

	bool LayersCollide(int a, int b)
	{
		Ensure();
		if (a < 0 || a >= N || b < 0 || b >= N)
			return true;
		return (s_Runtime[a].load(std::memory_order_relaxed) >> b) & 1u;
	}

	uint32 CollisionMask(int layer)
	{
		Ensure();
		return layer >= 0 && layer < N ? s_Runtime[layer].load(std::memory_order_relaxed) : 0xFFFFFFFFu;
	}

	void SetLayersCollide(int a, int b, bool collide, bool save)
	{
		Ensure();
		if (a < 0 || a >= N || b < 0 || b >= N)
			return;
		if (collide)
		{
			s_Saved[a] |= 1u << b;
			s_Saved[b] |= 1u << a;
		}
		else
		{
			s_Saved[a] &= ~(1u << b);
			s_Saved[b] &= ~(1u << a);
		}
		s_Runtime[a].store(s_Saved[a], std::memory_order_relaxed);
		s_Runtime[b].store(s_Saved[b], std::memory_order_relaxed);
		if (save)
			Save();
	}

	void SetAllCollide(bool collide)
	{
		Ensure();
		for (int i = 0; i < N; ++i)
			s_Saved[i] = collide ? 0xFFFFFFFFu : 0u;
		CopyToRuntime();
		Save();
	}

	void IgnoreLayerCollisionRuntime(int a, int b, bool ignore)
	{
		Ensure();
		if (a < 0 || a >= N || b < 0 || b >= N)
			return;
		if (ignore)
		{
			s_Runtime[a].fetch_and(~(1u << b), std::memory_order_relaxed);
			s_Runtime[b].fetch_and(~(1u << a), std::memory_order_relaxed);
		}
		else
		{
			s_Runtime[a].fetch_or(1u << b, std::memory_order_relaxed);
			s_Runtime[b].fetch_or(1u << a, std::memory_order_relaxed);
		}
	}

	void ResetRuntime()
	{
		Ensure();
		CopyToRuntime();
	}

	void Reload()
	{
		s_Loaded = false;
	}
}
