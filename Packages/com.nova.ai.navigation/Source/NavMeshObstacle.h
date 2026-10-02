#pragma once
#include "Component.h"
#include "NavData.h"

// Unity 의 NavMesh Obstacle: 움직이는 장애물 (상자, 문, 차).
//  - 에이전트는 늘 이것을 피해 밀려난다.
//  - Carve: 멈춰 있으면(Carve Only Stationary) 내비 메시에 구멍을 내서 길 찾기가 돌아가게 한다 (닿은 타일만 다시 만든다, Play 중).
class NavMeshObstacle : public Component
{
public:
	int Shape = 1;                      // Unity: 0 Capsule, 1 Box
	Vec3 Center = Vec3::Zero;           // 이 오브젝트 기준
	Vec3 Size = Vec3(1.0f, 1.0f, 1.0f); // Box
	float Radius = 0.5f;                // Capsule
	float Height = 2.0f;                // Capsule
	bool Carve = false;
	float MoveThreshold = 0.1f;
	float TimeToStationary = 0.5f;
	bool CarveOnlyStationary = true;

	NavMeshObstacle();
	~NavMeshObstacle();
	static const std::vector<NavMeshObstacle*>& All();

	// 월드 모양 (상자: 가운데·반 크기·Y 회전 / 캡슐: 아래 가운데·반지름·높이)
	NavObstacleShape WorldShape() const;
	bool IsCarving() const { return m_Ref != 0; }
	// 에이전트 밀어내기: XZ 에서 feet 를 이 모양 밖 radius 거리로 (겹치지 않으면 false)
	bool PushOut(const Vec3& feet, float radius, float height, Vec3& push) const;

	void LastUpdate() override;
	void OnDestroy() override { RemoveCarve(); }
	void OnInspectorGUI() override;
	void OnDrawGizmos() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "box_collider"; }

	GENERATE_COMPONENT_BODY(NavMeshObstacle)

private:
	std::weak_ptr<NavData> m_Data;
	uint32_t m_Ref = 0;
	Vec3 m_RestPos = Vec3::Zero;        // 마지막으로 멈춘(또는 깎은) 자리
	float m_RestYaw = 0.0f;
	float m_StillTime = 0.0f;
	bool m_HaveRest = false;
	void AddCarve(const std::shared_ptr<NavData>& data);
	void RemoveCarve();
};

REGISTER_PACKAGE_COMPONENT(NavMeshObstacle)
