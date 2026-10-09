#include "pch.h"
#include "SceneStreaming.h"
#include "SceneManager.h"
#include "ResourceManager.h"
#include "CliServer.h"
#include "Utils.h"
#include "Profiler.h"
#include "JobSystem.h"
#include <future>

namespace SceneStreaming
{
	namespace
	{
		std::vector<Jobs::Future<void>> s_PrewarmJobs;
		nlohmann::json OpJson(int id, const SceneManager::SceneOp& s)
		{
			return { { "op", id }, { "progress", s.Progress }, { "done", s.Done }, { "failed", s.Failed }, { "handle", s.Handle }, { "allowActivation", s.AllowActivation },
				{ "activateMs", s.ActivateMs }, { "buildMs", s.BuildMs }, { "assetMs", s.AssetMs }, { "assetLoads", s.AssetLoads }, { "teardownMs", s.TeardownMs },
				{ "enterMs", s.EnterMs }, { "objects", s.Objects }, { "maxWaitFrameMs", s.MaxWaitFrameMs }, { "waitFrames", s.WaitFrames }, { "frameAfterMs", s.FrameAfterMs },
				{ "stageMs", s.StageMs }, { "stageMaxMs", s.StageMaxMs }, { "stageFrames", s.StageFrames }, { "stageAssetMs", s.StageAssetMs }, { "stageAssetLoads", s.StageAssetLoads },
				{ "roots", s.Roots } };
		}

		nlohmann::json StatsJson()
		{
			const ResourceManager::LoadStats& s = ResourceManager::GetI()->Stats();
			const Utils::PrefetchStats p = Utils::GetPrefetchStats();
			return { { "textures", s.Textures }, { "textureMs", s.TextureMs }, { "materials", s.Materials }, { "materialMs", s.MaterialMs },
				{ "meshFiles", s.MeshFiles }, { "meshFileMs", s.MeshFileMs }, { "assetMs", s.TotalMs },
				{ "prefetch", { { "texturesRequested", p.Requested }, { "texturesUsed", p.Used }, { "waited", p.Waited }, { "claimedByMain", p.Claimed },
					{ "decodeMs", p.DecodeMs }, { "files", p.Files } } } };
		}
	}

	void QueuePrewarmJob(std::function<void()> fn)
	{
		s_PrewarmJobs.push_back(Jobs::Async(std::move(fn), Jobs::Priority::Background, "Scene Load (Prewarm)"));
	}

	bool PrewarmJobsDone()
	{
		for (auto& f : s_PrewarmJobs)
			if (f.valid() && f.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
				return false;
		return true;
	}

	void ClearPrewarmJobs()
	{
		s_PrewarmJobs.clear();   // Future 는 지울 때 끝나기를 기다린다
	}

	void RegisterEditor()
	{
		CliServer::Register("scenestream", "Scene streaming (SceneManager.LoadSceneAsync, Play only): {op: load|status|allow|stats|reset, path?, additive?: bool, async?: bool (default true), allow?: bool, id?: int}",
			[](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
				const std::string op = args.value("op", std::string("stats"));
				SceneManager* sm = SceneManager::GetI();
				auto toBool = [&](const char* key, bool def) {
					if (!args.contains(key)) return def;
					const auto& v = args[key];
					return v.is_boolean() ? v.get<bool>() : v.dump() != "false" && v.dump() != "\"false\"";
				};
				if (op == "load")
				{
					const std::string path = args.value("path", std::string());
					if (path.empty()) { error = "path is required (Assets/Scenes/X.scene)"; return false; }
					if (!Application::IsPlaying()) { error = "Play first (runtime scene loading)"; return false; }
					const int id = sm->RequestSceneLoad(string_to_wstring(path), toBool("additive", false), toBool("async", true));
					if (id == 0) { error = "the load request was refused"; return false; }
					if (!toBool("allow", true))
						sm->SetSceneOpAllowActivation(id, false);
					result = { { "op", id } };
					return true;
				}
				if (op == "status" || op == "allow")
				{
					const int id = args.value("id", 0);
					if (op == "allow")
						sm->SetSceneOpAllowActivation(id, toBool("allow", true));
					const SceneManager::SceneOp* s = sm->GetSceneOp(id);
					if (!s) { error = "no such operation " + std::to_string(id); return false; }
					result = OpJson(id, *s);
					return true;
				}
				if (op == "priority")
				{
					// Application.backgroundLoadingPriority (0 Low 2 ms, 1 BelowNormal 4 ms, 2 Normal 10 ms, 4 High 50 ms)
					if (args.contains("value"))
						Application::backgroundLoadingPriority = args.value("value", 1);
					result = { { "priority", Application::backgroundLoadingPriority }, { "budgetMs", Application::BackgroundLoadingBudgetMs() } };
					return true;
				}
				if (op == "profile")
				{
					Profiler::ForceCollecting(toBool("on", true));
					result = json::object();
					return true;
				}
				if (op == "slowframes")
				{
					// Profiler 기록에서 긴 프레임의 구간 (깊이까지) — 바꿔 끼운 뒤 첫 프레임이 무엇에 걸리나
					const double minMs = args.value("minMs", 100.0);
					const int depth = args.value("depth", 3);
					json frames = json::array();
					for (const Profiler::Frame& f : Profiler::History())
					{
						if (f.CpuMs < minMs)
							continue;
						json scopes = json::array();
						for (const Profiler::CpuSample& c : f.Cpu)
							if (c.Depth <= depth && c.Ms >= 1.0f)
								scopes.push_back(std::string(c.Depth, '.') + c.Name + " " + std::to_string((int)c.Ms));
						frames.push_back({ { "index", f.Index }, { "cpuMs", f.CpuMs }, { "scopes", scopes } });
					}
					result = { { "frames", frames } };
					return true;
				}
				if (op == "reset")
				{
					ResourceManager::GetI()->ResetStats();
					result = StatsJson();
					return true;
				}
				if (op != "stats") { error = "unknown op '" + op + "' (load, status, allow, stats, reset)"; return false; }
				result = StatsJson();
				return true;
			});
	}
}
