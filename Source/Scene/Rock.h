#pragma once
#include "Component.h"
#include "RockDesc.h"

// NOVA 바위·절벽 (나무처럼 우리 엔진 고유의 절차적 컴포넌트).
//  - RockDesc(모양 + 재질 + LOD)로 SDF 를 조형해 메시를 만든다 (RockGenerator, Seed 로 무작위 변형)
//  - 그리기는 RockRenderer: 같은 설정의 바위를 인스턴싱으로 한 번에, 거리에 따라 LOD0 → LOD1 → LOD2
class Rock : public Component
{
public:
	RockDesc Desc;

	Rock();
	virtual ~Rock();

	void ApplyPreset(int preset) { Desc.ApplyPreset(preset); }

	// 로컬 범위 / 광선 검사 (Scene 뷰 선택·F 포커스). LOD0 기준
	bool GetLocalBounds(Vec3& bmin, Vec3& bmax);
	bool RaycastLocal(const Vec3& origin, const Vec3& dir, float& t);

	static const std::vector<Rock*>& All();
	bool IsDrawable() const;

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "terrain_paint"; }

	GENERATE_COMPONENT_BODY(Rock)
};

REGISTER_COMPONENT(Rock)
