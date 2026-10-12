#pragma once
#include "BuildingDesc.h"

class UMaterial;

// 건축물 재질 텍스처를 실행 중에 수학식으로 굽는다 (파일 없음, 512² + 밉맵). 재질마다 세 장:
//  - Base Map  : 밝은 중성 무늬 (감마) — 재질의 Base Color (사용자가 고른 색) 를 곱한다
//  - Normal Map: 높이 무늬에서 (줄눈 · 돌 · 기와 단 · 나뭇결). 탄젠트 규약은 BuildingGenerator (T = 가로, B = 아래 · 비탈 아래)
//  - Mask      : r = Metallic, g = Occlusion (틈), a = Smoothness — 엔진 Metallic Map · Occlusion Map 칸에 같은 텍스처를 넣는다
// UV 는 미터라 재질 Tiling = 1 / 타일 크기 (m).
namespace BuildingTextures
{
	enum class Kind
	{
		// 벽 (BuildingDesc::WallMat 순서)
		Rubble, Ashlar, Brick, Plaster, Stucco, Planks, Logs, Marble,
		// 지붕 (BuildingDesc::RoofMat 순서)
		ClayTiles, Slate, Thatch, Shingles, Copper, Lead, Turf,
		// 그 밖
		Timber, Glass, LeadedGlass, DoorPlanks, Metal, Fabric,
		Ornament,      // 칠한 무늬 판 (북유럽 마름모 · 지그재그 · 꽃무늬 — 색이 들어 있다, 흰색을 곱하면 그대로)
		Banner,        // 깃발 천 (테두리 · 해 문장 — 깃발 판 하나에 한 장)
		Foliage,       // 꽃 상자의 잎 · 꽃
		Count
	};
	const char* KindName(Kind k);
	Kind WallKind(BuildingDesc::WallMat m);
	Kind RoofKind(BuildingDesc::RoofMat m);
	float TileMeters(Kind k);   // 텍스처 한 장이 덮는 크기 (m)

	// 재질 (Kind + 감마 색) — 같은 값이면 같은 재질을 돌려준다 (텍스처는 Kind 마다 한 번 굽는다)
	//  grimeRows > 0: 1 층 벽 (때 · 물 얼룩 · 이끼가 아래로 진해진다). 텍스처 한 장이 세로로 무늬 grimeRows 번 = 1 층 높이 — v = 땅에서 위로
	std::shared_ptr<UMaterial> Material(Kind k, const float color[3], int grimeRows = 0, float grime = 0.0f);
	// 생성기 재질 칸 (BuildingGenerator::Slot) 의 Kind · 색
	Kind SlotKind(const BuildingDesc& d, int slot);
	void SlotColor(const BuildingDesc& d, int slot, float out[3]);

	// 미리 보기 · 검사: 이 Kind 의 Base Map (없으면 굽는다)
	GfxShaderResourceView* BaseMap(Kind k);
	int BakedCount();       // 지금까지 구운 Kind 수
	void ClearCache();      // 장치를 다시 만들 때 등
}
