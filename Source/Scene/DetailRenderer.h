#pragma once
#include "MemoryStats.h"
#include "DetailPrototype.h"

class TerrainData;

// 지형 디테일 그리기 (풀·꽃·작은 돌, Unity 의 Terrain Details).
//  - 지형을 16 m 조각으로 나누고, 카메라 근처(Detail Distance) 조각만 밀도 맵에서 덩어리를 흩뿌려 캐시한다.
//    조각의 밀도·높이·레이어·설정 내용이 바뀐 조각만 다시 만든다 (여러 스레드, 프레임마다 개수 제한)
//  - 덩어리 = 절차 메시 (풀: 휘어진 잎 여러 장 / 꽃: 잎 + 줄기 + 꽃송이 / 돌: 찌그러진 구 몇 개). 종류·LOD 마다 인스턴싱 한 번
//  - 멀수록 순위가 높은 덩어리부터 빠지고(셰이더가 땅속으로 줄임) 남은 덩어리를 넓힌다. 가까이 LOD0, 멀리 LOD1 (잎 절반)
//  - 깊이 사전 패스 + 본 패스(깊이 EQUAL) + 그림자 (Shadow Distance 안, 그림자 맵 텍셀이 덩어리보다 충분히 작을 때만)
namespace DetailRenderer
{
	enum class Pass { Main, Shadow, NormalDepth };

	void DrawAll(Pass pass, bool editor);

	struct Stats
	{
		int Instances = 0;      // 본 패스에 그린 덩어리
		int Chunks = 0;         // 보이는 조각
		int Cached = 0;         // 캐시에 있는 조각
		int Built = 0;          // 이번 프레임에 만든 조각
		float BuildMs = 0.0f;
		int DrawCalls = 0;
	};
	const Stats& LastStats(bool editor);

	// 덩어리 메시 정보 (편집기 표시용)
	bool GetMeshInfo(const DetailPrototype& proto, int& vertices, int& triangles, int lod = 0);

	void CollectMemory(std::vector<MemoryStats::Item>& gpu, std::vector<MemoryStats::Item>& cpu);
}
