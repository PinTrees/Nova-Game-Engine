#pragma once
#include "MemoryStats.h"

// Profiler 의 Memory 모듈 데이터. 0.5 초마다 (또는 Refresh 를 누르면) 범주별 사용량을 다시 센다.
//  GPU: Textures, Meshes, Shadow Maps, Trees, Terrain (+ 이 프로세스의 GPU 사용량 − 센 것 = Other GPU: 렌더 타깃·드라이버 등)
//  CPU: Undo History, Scene JSON Cache, Profiler History, Terrain Data, Tree Pick Data
//  프레임마다 Profiler 통계("Memory/…")로도 남겨 그래프를 그린다
namespace ProfilerMemory
{
	enum Group { kTextures, kMeshes, kShadowMaps, kTrees, kTerrain, kEditor, kGroupCount };   // 그래프 묶음
	extern const char* const kGroupNames[kGroupCount];
	extern const char* const kGroupStats[kGroupCount];   // Profiler 통계 이름

	void Update();          // Profiler 창이 모으는 동안 매 프레임
	void RequestRefresh();  // 다음 Update 에 바로 다시 센다
	const MemoryStats::Report& Latest();
	int GroupOf(const MemoryStats::Category& c);
}
