#pragma once
#include "Component.h"

// 바이옴 영역 (World Creator 의 Biome Layer 처럼 지형 일부에 다른 지형 특성·재질을).
//  - Transform: 위치(x, z) = 가운데, 크기 x·z = 덮는 넓이(m), 회전 y = 방향
//  - Preset = 바이옴 프리셋 이름 (Resources/Packages/Terrain/Biomes/*.biome)
//  - 지형 생성기가 켜진 지형에서: 영역 안은 프리셋의 Base 노이즈 → 프리셋 필터(침식 등) → 프리셋 재질 규칙으로 만들고,
//    가장자리(Blend Size)에서 지형 전체 설정과 부드럽게 섞는다. 가장자리는 노이즈로 흔들어 자연스럽게
class TerrainBiome : public Component
{
public:
	std::string Preset = "Grassland Hills";
	float Opacity = 1.0f;
	float BlendSize = 0.4f;       // 가장자리 섞는 폭 (반지름에 대한 비율)
	float Roundness = 1.0f;       // 0 = 사각 영역, 1 = 원
	float EdgeNoise = 0.5f;       // 경계를 노이즈로 흔드는 정도
	bool AffectHeights = true;    // 끄면 재질만 바꾼다
	bool AffectMaterials = true;  // 끄면 지형 모양만 바꾼다
	int Seed = 1;                 // 경계 노이즈
	int Order = 0;                // 작은 것부터 (뒤의 것이 위를 덮는다)

	TerrainBiome();
	virtual ~TerrainBiome();

	static const std::vector<TerrainBiome*>& All();
	bool IsActiveBiome() const;

	virtual void OnInspectorGUI() override;
	virtual void OnDrawGizmos() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "terrain_paint"; }

	GENERATE_COMPONENT_BODY(TerrainBiome)
};

REGISTER_COMPONENT(TerrainBiome)
