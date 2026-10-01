#pragma once
#include "Component.h"

// Unity 의 NavMeshAgent: SetDestination 하면 구운 내비 데이터로 길을 찾아 꺾이는 점을 따라 걷는다 (Play 중).
// 가속·자동 감속(Auto Braking)·멈출 거리·회전 속도. 높이는 바닥(내비 데이터)에 붙인다.
class NavMeshAgent : public Component
{
public:
	float Speed = 3.5f;
	float AngularSpeed = 120.0f;   // 도/초
	float Acceleration = 8.0f;
	float StoppingDistance = 0.0f;
	bool AutoBraking = true;
	float Radius = 0.5f;
	float Height = 2.0f;
	float BaseOffset = 0.0f;

	// 실행 상태
	std::vector<Vec3> Corners;
	size_t Next = 0;
	Vec3 Destination = Vec3::Zero;
	Vec3 Velocity = Vec3::Zero;
	bool HasPath = false;
	bool IsStopped = false;

	NavMeshAgent();

	bool SetDestination(const Vec3& target);
	void ResetPath();
	bool Warp(const Vec3& position);
	float RemainingDistance() const;
	Vec3 SteeringTarget() const { return HasPath && Next < Corners.size() ? Corners[Next] : Vec3::Zero; }
	bool IsOnNavMesh() const;

	void Update() override;
	void OnInspectorGUI() override;
	void OnDrawGizmos() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "capsule_collider"; }

	GENERATE_COMPONENT_BODY(NavMeshAgent)
};

REGISTER_PACKAGE_COMPONENT(NavMeshAgent)
