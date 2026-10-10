#pragma once
#include "PCGGraph.h"
#include "WorldGen.h"
#include <vector>

// PCG 그래프 실행: 스포너 노드 하나 × 셀 (사각형) 하나 → 메시마다 월드 행렬.
//  작업 스레드에서 돈다 (그래프는 복사본, 월드 함수는 순수 함수). 같은 셀 = 같은 결과 (점은 월드 격자 · 위치 해시로 정한다)
namespace PCG
{
	struct Point
	{
		float X = 0, Y = 0, Z = 0;
		float Nx = 0, Ny = 1, Nz = 0;
		float Density = 1.0f;
		float Height01 = 0.0f;
		float SlopeDeg = 0.0f;
		WorldGen::Biome Biome;
		float Extent = 1.0f;
		float Yaw = 0.0f, Scale = 1.0f, Align = 0.0f, TiltX = 0.0f, TiltZ = 0.0f, OffsetY = 0.0f;
		uint32_t Seed = 0;
	};

	struct Rect { float X0 = 0, Z0 = 0, X1 = 0, Z1 = 0; };

	// 지형 격자: 점 높이는 가까운 World Terrain 타일 (513) 과 같은 격자 · 같은 삼각형으로 (그려진 땅 위에 정확히)
	struct TerrainGrid
	{
		float OriginX = -16384.0f, OriginZ = -16384.0f;   // 격자 0 의 월드 위치
		float Spacing = 2.0f;
	};

	struct CellOutput
	{
		std::vector<std::vector<XMFLOAT4X4>> PerMesh;   // 스포너의 Meshes 차례
		float MinY = 0.0f, MaxY = 0.0f;
		int Points = 0;
		double Ms = 0.0;
	};

	void ExecuteSpawner(const Graph& graph, int spawnerId, const WorldGen::Settings& world, const TerrainGrid& grid, int volumeSeed, const Rect& rect, CellOutput& out);

	// 월드 높이 (격자 삼각형 보간 — 그려진 가까운 지형과 같은 면)
	float GroundHeight(const WorldGen::Settings& world, const TerrainGrid& grid, float x, float z);
}
