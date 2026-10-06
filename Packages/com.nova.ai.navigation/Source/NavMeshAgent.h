#pragma once
#include "Component.h"

class NavData;

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

	bool AutoTraverseOffMeshLink = true;   // 끄면 링크 시작에서 멈추고 스크립트가 옮긴 뒤 CompleteOffMeshLink

	// 실행 상태 (Corners · Velocity · Link* 는 내비 공간 — 2D 표면이면 (x, 0, y). Destination 은 월드)
	std::vector<Vec3> Corners;
	std::vector<unsigned char> CornerFlags;   // NavData::CornerLinkStart
	size_t Next = 0;
	Vec3 Destination = Vec3::Zero;
	Vec3 Velocity = Vec3::Zero;
	bool HasPath = false;
	bool IsStopped = false;
	// Off-Mesh Link 를 지나는 중
	bool OnLink = false;
	Vec3 LinkStart = Vec3::Zero, LinkEnd = Vec3::Zero;
	float LinkProgress = 0.0f;   // 0..1

	NavMeshAgent();
	~NavMeshAgent();
	static const std::vector<NavMeshAgent*>& All();
	int AvoidancePriority = 50;    // Unity: 낮을수록 중요 (덜 밀린다), 0~99

	bool SetDestination(const Vec3& target);
	void ResetPath();
	bool Warp(const Vec3& position);
	float RemainingDistance() const;
	Vec3 SteeringTarget() const { return HasPath && Next < Corners.size() ? Corners[Next] : Vec3::Zero; }
	bool IsOnNavMesh() const;
	// Unity: 링크 이동을 끝낸다 (끝점으로 옮기고 남은 길을 이어 간다)
	void CompleteOffMeshLink();
	// 내비 공간 → 월드 (2D 표면 위면 (x, z) → (x, y), 아니면 그대로)
	bool Is2D() const { return m_2D; }
	Vec3 NavToWorld(const Vec3& nav) const { return m_2D ? Vec3(nav.x, nav.z, m_WorldZ) : nav; }
	Vec3 NavDirToWorld(const Vec3& v) const { return m_2D ? Vec3(v.x, v.z, 0.0f) : v; }

	void Update() override;
	void OnInspectorGUI() override;
	void OnDrawGizmos() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "capsule_collider"; }

	GENERATE_COMPONENT_BODY(NavMeshAgent)

private:
	const NavData* m_PathData = nullptr;   // 길을 찾은 내비 데이터 + 그때의 Revision (바뀌면 다시 찾는다)
	unsigned m_PathRevision = 0;
	mutable bool m_2D = false;       // 마지막으로 찾은 표면이 2D (XY)
	mutable float m_WorldZ = 0.0f;   // 2D 일 때 지킬 월드 z
	// 서 있는 표면과 내비 공간의 발 위치 (3D = 위치 - Base Offset, 2D = (x, 0, y))
	const NavData* Locate(Vec3& feet) const;
	void Place(const Vec3& feet);    // 내비 공간의 발 → Transform
	Vec3 Feet() const { Vec3 f; Locate(f); return f; }
};

REGISTER_PACKAGE_COMPONENT(NavMeshAgent)
