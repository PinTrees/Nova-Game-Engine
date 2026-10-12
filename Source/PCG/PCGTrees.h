#pragma once
#include <string>

class TreeDesc;

// PCG 의 절차적 나무 (트리 편집기의 TreeDesc — 바이옴 식생): Static Mesh Spawner 의 메시 칸에 "tree:Spruce" 처럼 쓰면
//  모델 파일 대신 그 종의 나무를 TreeRenderer (LOD · 임포스터 · 계층 바람 · 그림자) 로 그린다.
//  종마다 변형 몇 개 (씨앗 · 키) — 같은 나무가 줄지어 보이지 않게, 점 위치 해시로 고른다
namespace PCG
{
	constexpr int kTreeVariants = 2;   // 종마다 메시 · 임포스터 (임포스터 아틀라스 하나 5 MB 쯤이라 적게)
	bool IsTreePath(const std::string& path);               // "tree:" 로 시작
	int TreePreset(const std::string& path);                // "tree:Spruce" → 프리셋 번호 (없으면 -1)
	// 종 · 변형 · 그리는 거리 (스포너 Cull) 의 TreeDesc — 지우지 않는다 (TreeRenderer 가 프레임 동안 포인터를 쓴다)
	const TreeDesc* TreeSpecies(const std::string& path, int variant, float cullDistance);
	int TreeVariantAt(float x, float z);                    // 점 위치 → 변형 번호
}
