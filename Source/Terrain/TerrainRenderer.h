#pragma once

class TerrainData;

// 지형 LOD 렌더러 (쿼드트리).
//  - 루트 노드 = 지형 전체. 노드 오차를 화면에 투영한 픽셀 수가 Pixel Error 를 넘으면 4등분한다 (Unity 의 Pixel Error 와 같은 뜻).
//    → 카메라 가까운 곳은 작은 노드로 계속 쪼개져 촘촘하고, 먼 곳은 큰 노드 하나로 단순하게 그려진다.
//  - 모든 노드는 같은 격자(32 x 32 칸)이고 노드가 클수록 높이맵을 성기게 샘플링한다.
//  - 이웃 노드 크기 차이는 최대 2배로 맞추고, 큰 이웃과 닿는 변은 격자점을 붙여 틈을 없앤다 (인덱스 버퍼 16가지).
//  - 정점 버퍼 없이 인덱스 버퍼만 쓰고 높이는 셰이더가 높이맵에서 읽는다 (편집 즉시 반영).
//  - 선택은 편집 시간/런타임 구분 없이 매 프레임, 화면(Scene 뷰 / Game 뷰)마다 그 카메라로 한다.
//    같은 화면의 그림자·노멀깊이·본 패스는 같은 카메라로 고르므로 모양이 일치한다. 절두체 밖 노드는 그리지 않는다.
namespace TerrainRenderer
{
	enum class Pass { Main, Shadow, NormalDepth };

	struct Stats
	{
		int Nodes = 0;          // 고른 잎 노드 수 (지형 전체를 덮는다)
		int DrawnNodes = 0;     // 절두체 안이라 그린 노드 수
		int Triangles = 0;
		int DepthHistogram[16] = {};   // 깊이별 그린 노드 수 (0 = 지형 전체 크기)
		std::vector<XMINT3> DrawnLeaves;   // (깊이, x, z) - Scene 뷰 LOD 노드 표시용
	};

	// origin = 지형 월드 위치 (Unity 지형처럼 회전/크기는 쓰지 않는다)
	// heightTransition: 높이 기반 레이어 섞기의 전환 폭 (0 = 끔 — Terrain 의 Height-Based Blend)
	// skirt > 0: 지형 가장자리 잎에 그 깊이 (m) 의 스커트 — World Terrain 타일 (해상도가 다른 이웃과의 틈을 가린다)
	void Draw(TerrainData& data, const Vec3& origin, Pass pass, float pixelError, Stats* stats = nullptr, float heightTransition = 0.0f, float skirt = 0.0f);
}
