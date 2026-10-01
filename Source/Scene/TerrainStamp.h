#pragma once
#include "Component.h"

// 지형 스탬프 (Atlas 의 Stamp / World Creator 의 Shape Layer 처럼 지형 요소를 오브젝트로).
//  - Transform: 위치(x, z) = 가운데, 크기 x·z = 덮는 넓이(m), 크기 y = 높이 배율, 회전 y = 방향. 위치 y = Blend/Max/Min 의 기준 높이
//  - 모양은 식으로 만든다 (텍스처 없음): 산, 언덕, 분화구, 화산, 메사, 능선, 협곡, 사구, 섬 / 또는 높이맵 이미지
//  - 지형 생성기(TerrainGenerator)가 켜진 지형에 Order 순으로 합친다. 옮기거나 값을 바꾸면 지형이 바로 다시 생성된다
class TerrainStamp : public Component
{
public:
	enum class Shape { Mountain, Hill, Crater, Volcano, Mesa, Ridge, Canyon, Dunes, Island, Heightmap, Count };
	enum class Operation { Add, Subtract, Max, Min, Blend, Count };

	Shape StampShape = Shape::Mountain;
	Operation Op = Operation::Add;
	float Height = 120.0f;        // m (× Transform 크기 y)
	float Opacity = 1.0f;
	float BlendSize = 0.35f;      // 가장자리 부드럽게 (반지름에 대한 비율)
	float Roundness = 1.0f;       // 0 = 사각 영역, 1 = 원
	float Power = 1.0f;           // > 1 = 더 가파르게
	float Detail = 0.35f;         // 표면 노이즈
	float DetailScale = 0.25f;    // 노이즈 크기 (스탬프 크기에 대한 비율)
	int Seed = 1;
	int Order = 0;                // 작은 것부터 합친다 (같으면 Hierarchy 순)
	std::string HeightmapPath;    // Shape::Heightmap (프로젝트 기준 경로, 흑백)

	TerrainStamp();
	virtual ~TerrainStamp();

	static const std::vector<TerrainStamp*>& All();
	bool IsActiveStamp() const;
	static const char* ShapeName(Shape s);
	static const char* OperationName(Operation o);

	virtual void OnInspectorGUI() override;
	virtual void OnDrawGizmos() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "terrain_paint"; }

	GENERATE_COMPONENT_BODY(TerrainStamp)
};

REGISTER_COMPONENT(TerrainStamp)
