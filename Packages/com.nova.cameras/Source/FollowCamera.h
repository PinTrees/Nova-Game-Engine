#pragma once
#include "Component.h"

// 3인칭 따라가기 카메라 (Unity Cinemachine 의 Third Person Follow 를 단순하게).
// 카메라 GameObject 에 붙인다: Play 중 LateUpdate 마다 대상 뒤·위(Distance, Height)로 부드럽게(Damping) 따라가며
// 대상의 LookAtHeight 지점을 본다. Avoid Obstacles 면 대상에서 카메라 쪽으로 레이를 쏘아 벽 앞으로 당긴다.
class FollowCamera : public Component
{
public:
	uint64 Target = 0;                  // 따라갈 GameObject (fileID)
	float Distance = 5.0f;              // 대상 뒤 거리 (m)
	float Height = 2.0f;                // 대상 위 높이 (m)
	float LookAtHeight = 1.2f;          // 대상의 이 높이를 본다
	float Damping = 8.0f;               // 따라가는 빠르기 (1/초, 0 = 바로)
	bool FollowTargetRotation = true;   // 대상이 도는 대로 뒤로 돈다 (끄면 Yaw 방향 고정)
	float Yaw = 0.0f;                   // 고정 방향 (도)
	bool AvoidObstacles = true;
	float ObstaclePadding = 0.2f;       // 벽 앞 여유 (m)

	FollowCamera();

	void Start() override;
	void LateUpdate() override;
	void OnInspectorGUI() override;
	void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "camera"; }

	GameObject* FindTarget() const;
	void SnapNow() { m_Snap = true; }

	GENERATE_COMPONENT_BODY(FollowCamera)

private:
	bool m_Snap = true;
	void Desired(GameObject* target, Vec3& pos, Vec3& look) const;
};

REGISTER_PACKAGE_COMPONENT(FollowCamera)
