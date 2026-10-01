#pragma once
#include "MemoryStats.h"
#include "RockDesc.h"

// 바위 그리기 (Rock 컴포넌트 전부).
//  - 같은 설정(RockDesc::Hash)의 바위를 묶어 LOD 단계마다 인스턴싱 한 번 (본 패스·그림자·깊이 패스)
//  - 절두체 밖은 뺀다, LOD 는 화면 크기(바위 크기 / 거리 × LOD Bias)로 LOD0 → LOD1 → LOD2 → 안 그림
//  - 메시는 RockGenerator 로 처음 쓸 때 만들고 모양 키로 캐시
namespace RockRenderer
{
	enum class Pass { Main, Shadow, NormalDepth };

	void DrawAll(Pass pass, bool editor);

	struct MeshInfo
	{
		Vec3 BoundsMin, BoundsMax;
		int Vertices = 0, Triangles = 0;
		double Ms = 0.0;
		const std::vector<XMFLOAT3>* Positions = nullptr;   // 선택용 (LOD0)
		const std::vector<uint32_t>* Indices = nullptr;
	};
	bool GetMeshInfo(const RockDesc& desc, MeshInfo& out, int lod = 0);

	struct Stats
	{
		int Rocks = 0;                 // 절두체 안
		int Lod0 = 0, Lod1 = 0, Lod2 = 0;
		int DrawCalls = 0;
	};
	const Stats& LastStats(bool editor);

	void CollectMemory(std::vector<MemoryStats::Item>& gpu, std::vector<MemoryStats::Item>& cpu);
}
