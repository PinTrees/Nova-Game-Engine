#pragma once

// 월드 풀밭 (World Terrain 의 바이옴 지형 위 — 49. GrassField.fx): 카메라 둘레 수백 m 를 빽빽한 풀로.
//  인스턴스 버퍼 없이 정점 셰이더가 덩어리를 만든다 (월드 고정 격자 · 거리 단계 · 지형 높이 · 풀 레이어 스플랫 · 바이옴 밀도 · 사진 색).
//  CPU 는 지형 타일마다 쿼드트리로 패치를 고르고 (절두체 · 풀이 없는 곳 건너뛰기) 패치마다 한 번 그린다.
//  설정은 World Terrain 의 Grass (켜기 · 밀도 · 거리 · 키). 길 · 강 · 바위 · 눈 · 사막은 스플랫 · 바이옴이 비운다
namespace GrassField
{
	void DrawMain(bool editor);

	struct Stats
	{
		int Patches = 0;        // 그린 패치
		int Points = 0;         // 패치의 격자 점 (덩어리 후보)
		int Vertices = 0;       // 정점 셰이더 수 (점 × 잎 × 마디 × 6)
		int Culled = 0;         // 절두체 · 풀 없음으로 건너뛴 패치
		float CpuMs = 0.0f;
	};
	const Stats& LastStats(bool editor);
}
