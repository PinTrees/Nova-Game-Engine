#pragma once
#include "Component.h"
#include "NavData.h"

// Unity AI Navigation 의 NavMesh Link: 걸어서는 이어지지 않는 두 곳(낭떠러지 아래, 건너편 발판, 사다리 위)을 잇는다.
// 굽기와 따로 — 옮기거나 켜고 끄면 바로 내비 메시에 반영된다. 에이전트는 링크를 지나갈 때 시작 → 끝으로 옮겨진다
// (Auto Traverse Off Mesh Link, 꺼 두면 스크립트가 움직이고 CompleteOffMeshLink).
class NavMeshLink : public Component
{
public:
	Vec3 StartPoint = Vec3(0.0f, 0.0f, -2.5f);   // 이 오브젝트 기준 (크기는 무시 — Unity 와 같음)
	Vec3 EndPoint = Vec3(0.0f, 0.0f, 2.5f);
	uint64 StartTransform = 0;                    // 있으면 그 오브젝트 위치를 끝점으로
	uint64 EndTransform = 0;
	float Width = 0.0f;
	bool Bidirectional = true;

	NavMeshLink();
	~NavMeshLink();
	static const std::vector<NavMeshLink*>& All();
	// 켜져 있고 활성인 링크 전부 (월드 좌표)
	static void Collect(std::vector<NavLink>& out);

	Vec3 WorldStart() const;
	Vec3 WorldEnd() const;

	void OnInspectorGUI() override;
	void OnDrawGizmos() override;
	void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "terrain_paint"; }

	GENERATE_COMPONENT_BODY(NavMeshLink)

private:
	Vec3 ToWorld(const Vec3& local, uint64 target) const;
};

REGISTER_PACKAGE_COMPONENT(NavMeshLink)
