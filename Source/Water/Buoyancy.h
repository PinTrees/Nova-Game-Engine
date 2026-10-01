#pragma once
#include "Component.h"

// 부력 (Unreal BuoyancyComponent 의 폰툰 / Crest SimpleFloatingObject 방식). 같은 GameObject 의 Rigidbody 에 힘을 준다.
//  - 폰툰 = Size 상자 바닥의 네 모서리 + 가운데 (로컬). 물리 스텝마다 폰툰 위 수면(WaterBody::Query, 화면과 같은 Gerstner 식)을 구해
//    잠긴 깊이만큼 위로 미는 힘, 물속 속도에 맞서는 저항, 강 흐름에 떠밀리는 힘을 폰툰 위치에 준다 → 기울며 파도를 탄다
//  - Float Strength 1 = 상자 높이의 절반이 잠기면 무게와 같아진다
class Buoyancy : public Component
{
public:
	Vec3 Size = Vec3(2.0f, 1.0f, 4.0f);   // 떠 있는 부피 상자 (로컬 m, 가운데 기준)
	float FloatStrength = 1.0f;
	float WaterDrag = 1.0f;               // 물속 이동 저항 (1/s)
	float WaterAngularDrag = 0.8f;        // 물속 회전 저항 (1/s)
	float FlowForce = 1.0f;               // 강 흐름을 따라가는 정도

	Buoyancy();
	virtual void FixedUpdate() override;
	virtual void OnInspectorGUI() override;
	virtual void OnDrawGizmos() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "terrain_paint"; }

	float Submerged() const { return m_Submerged; }   // 지난 스텝의 잠긴 비율 (0~1, 스크립트용)

	GENERATE_COMPONENT_BODY(Buoyancy)

private:
	float m_Submerged = 0.0f;
	int m_LogStep = 0;
};

REGISTER_COMPONENT(Buoyancy)
