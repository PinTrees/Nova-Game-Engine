#pragma once
#include "Collider.h"

class TerrainData;

// Unity 의 Terrain Collider. TerrainData 의 높이맵을 Jolt HeightField 형상으로 만든다 (정적 전용).
// Terrain Data 가 비어 있으면 같은 GameObject 의 Terrain 이 쓰는 데이터를 사용한다.
class TerrainCollider
	: public Collider
{
private:
	std::string m_DataPath;
	std::shared_ptr<TerrainData> m_Data;
	bool m_EnableTreeColliders = true;   // 저장만 한다 (나무 없음)

public:
	TerrainCollider();
	~TerrainCollider();

	void SetTerrainData(const std::string& path);
	// 실제로 쓰는 데이터 (자기 것이 없으면 Terrain 의 것)
	std::shared_ptr<TerrainData> GetEffectiveData() const;

	virtual void OnInspectorGUI() override;
	virtual const char* InspectorIconName() const override { return "terrain_collider"; }

	GENERATE_COMPONENT_BODY(TerrainCollider)
};

REGISTER_COMPONENT(TerrainCollider)
