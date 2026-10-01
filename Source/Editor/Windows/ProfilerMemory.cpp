#include "pch.h"
#include "ProfilerMemory.h"
#include "Profiler.h"
#include "FrameProfiler.h"
#include "ResourceManager.h"
#include "ShadowMap.h"
#include "TreeRenderer.h"
#include "RockRenderer.h"
#include "Terrain.h"
#include "TerrainData.h"
#include "UndoSystem.h"
#include <chrono>
#include <cstring>

namespace ProfilerMemory
{
	const char* const kGroupNames[kGroupCount] = { "Textures", "Meshes", "Shadow Maps", "Trees", "Terrain", "Editor Data" };
	const char* const kGroupStats[kGroupCount] = { "Memory/Textures", "Memory/Meshes", "Memory/Shadow Maps", "Memory/Trees", "Memory/Terrain", "Memory/Editor Data" };

	namespace
	{
		MemoryStats::Report s_Report;
		bool s_Refresh = true;
		std::chrono::steady_clock::time_point s_Last;

		MemoryStats::Category& Add(MemoryStats::Report& r, const char* name, bool gpu, std::vector<MemoryStats::Item> items)
		{
			MemoryStats::Category c;
			c.Name = name;
			c.Gpu = gpu;
			std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.Bytes > b.Bytes; });
			for (const auto& i : items)
				c.Bytes += i.Bytes;
			c.Items = std::move(items);
			r.Categories.push_back(std::move(c));
			return r.Categories.back();
		}

		void Collect()
		{
			const auto t0 = std::chrono::steady_clock::now();
			MemoryStats::Report r;
			r.TimeSec = ::GetTickCount64() / 1000.0;
			MemoryStats::QueryProcess(r);

			// ---- GPU
			std::vector<MemoryStats::Item> textures, meshes;
			ResourceManager::GetI()->CollectMemory(textures, meshes);
			Add(r, "Textures", true, std::move(textures));
			Add(r, "Meshes", true, std::move(meshes));
			{
				std::vector<MemoryStats::Item> maps;
				if (auto m = RenderManager::GetI()->BaseShadowMap)
					maps.push_back({ "Game View shadow maps", m->MemoryBytes() });
				if (auto m = RenderManager::GetI()->EditorShadowMap)
					maps.push_back({ "Scene View shadow maps", m->MemoryBytes() });
				Add(r, "Shadow Maps", true, std::move(maps));
			}
			std::vector<MemoryStats::Item> treeGpu, treeCpu;
			TreeRenderer::CollectMemory(treeGpu, treeCpu);
			Add(r, "Trees", true, std::move(treeGpu));
			std::vector<MemoryStats::Item> rockGpu, rockCpu;
			RockRenderer::CollectMemory(rockGpu, rockCpu);
			Add(r, "Rocks", true, std::move(rockGpu));
			std::vector<MemoryStats::Item> terrainGpu, terrainCpu;
			for (Terrain* t : Terrain::GetActiveTerrains())
				if (auto data = t->GetTerrainData())
				{
					const std::string name = data->Path.empty() ? std::string("Terrain") : data->Path;
					terrainGpu.push_back({ name, data->GpuBytes() });
					terrainCpu.push_back({ name, data->CpuBytes() });
				}
			Add(r, "Terrain", true, std::move(terrainGpu));

			// ---- CPU (에디터가 잡고 있는 큰 데이터)
			size_t history = 0, sceneCache = 0;
			Undo::MemoryUsage(history, sceneCache);
			Add(r, "Undo History", false, { { "Undo / Redo records (changed roots)", history } });
			Add(r, "Scene JSON Cache", false, { { "Per-root JSON (Undo)", sceneCache } });
			Add(r, "Profiler History", false, { { "Last 300 frames", Profiler::MemoryBytes() } });
			Add(r, "Terrain Data", false, std::move(terrainCpu));
			Add(r, "Tree Pick Data", false, std::move(treeCpu));

			// GPU 사용량 중 센 것을 뺀 나머지 (렌더 타깃, 후처리 버퍼, 셰이더, 드라이버 …)
			size_t tracked = 0;
			for (const auto& c : r.Categories)
				if (c.Gpu)
					tracked += c.Bytes;
			if (r.GpuUsage > tracked)
				Add(r, "Other GPU", true, { { "Render targets, post-processing, shaders, driver", r.GpuUsage - tracked } });

			s_Report = std::move(r);
			s_Last = std::chrono::steady_clock::now();
			if (FrameProfiler::Enabled())
				EditorLog::Write("Profiler", "memory snapshot %.2f ms (process %s, gpu %s)",
					std::chrono::duration<double, std::milli>(s_Last - t0).count(),
					MemoryStats::FormatBytes(s_Report.ProcessPrivate).c_str(), MemoryStats::FormatBytes(s_Report.GpuUsage).c_str());
		}
	}

	int GroupOf(const MemoryStats::Category& c)
	{
		if (!strcmp(c.Name, "Textures")) return kTextures;
		if (!strcmp(c.Name, "Meshes")) return kMeshes;
		if (!strcmp(c.Name, "Shadow Maps")) return kShadowMaps;
		if (!strcmp(c.Name, "Trees") || !strcmp(c.Name, "Tree Pick Data")) return kTrees;
		if (!strcmp(c.Name, "Terrain") || !strcmp(c.Name, "Terrain Data")) return kTerrain;
		if (!strcmp(c.Name, "Undo History") || !strcmp(c.Name, "Scene JSON Cache") || !strcmp(c.Name, "Profiler History")) return kEditor;
		return -1;   // Other GPU 는 그래프에 넣지 않는다 (전체 GPU 사용량에 포함)
	}

	void RequestRefresh() { s_Refresh = true; }

	const MemoryStats::Report& Latest() { return s_Report; }

	void Update()
	{
		if (!Profiler::Collecting())
			return;
		if (s_Refresh || std::chrono::steady_clock::now() - s_Last > std::chrono::milliseconds(500))
		{
			s_Refresh = false;
			Collect();
		}
		// 프레임마다 통계로 (그래프)
		double groups[kGroupCount] = {};
		for (const auto& c : s_Report.Categories)
			if (int g = GroupOf(c); g >= 0)
				groups[g] += (double)c.Bytes;
		for (int g = 0; g < kGroupCount; ++g)
			Profiler::SetStat(kGroupStats[g], groups[g]);
		Profiler::SetStat("Memory/Process Private", (double)s_Report.ProcessPrivate);
		Profiler::SetStat("Memory/GPU Usage", (double)s_Report.GpuUsage);
	}
}
