#pragma once
#include "Component.h"

class Mesh;

// Unity 의 Cloth: 같은 오브젝트의 메시 (Mesh Filter — Plane 등) 를 천으로 (Jolt Soft Body, Play 중).
//  - 메시 정점을 위치로 묶어 (이음매의 같은 자리 정점은 하나) 늘어남 · 비틀림 · 접힘 구속 + 고정점에서 멀어지지 않게 (LRA)
//  - 고정 (Pin): 위쪽 가장자리 · 위 두 모서리 · 고른 정점 — 오브젝트를 옮기면 고정점이 따라오고 나머지가 끌려온다
//  - 장면의 콜라이더 (상자 · 구 · 캡슐 · 메시 · 지형 · Rigidbody) 와 부딪힌다 (자기 콜라이더 · 트리거는 빼고)
//  - 바람 = External Acceleration + Random Acceleration (출렁임)
//  그리기: 메시의 사본을 만들어 프레임마다 정점 · 법선을 고친다 (원래 메시 파일은 그대로)
class Cloth : public Component
{
public:
	enum PinMode { PinNone = 0, PinTopEdge = 1, PinTopCorners = 2 };
	float StretchingStiffness = 1.0f;   // 0 ~ 1 (1 = 늘지 않음)
	float BendingStiffness = 0.0f;      // 0 ~ 1 (0 = 잘 접힘)
	bool UseGravity = true;
	float Damping = 0.0f;               // 0 ~ 1
	float Friction = 0.5f;
	float Thickness = 0.02f;            // 콜라이더에서 떨어지는 거리 (m)
	Vec3 ExternalAcceleration = Vec3::Zero;
	Vec3 RandomAcceleration = Vec3::Zero;
	int Pin = PinNone;
	std::vector<uint32> PinnedVertices;  // 고른 정점 (천 정점 번호) — Pin 과 함께
	float SolverFrequency = 120.0f;     // Unity 의 Cloth Solver Frequency (반복 수 = / 20)

	Cloth();
	~Cloth() override;
	void FixedUpdate() override;
	void LateUpdate() override;
	void OnDestroy() override;
	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "mesh_renderer"; }

	bool IsSimulating() const { return m_Handle != 0; }
	// Unity: 이번 스텝의 오브젝트 움직임을 천 전체에 그대로 (순간 이동 — 휘날리지 않게). 1 m 넘게 한 번에 옮기면 저절로
	void ClearTransformMotion() { m_ClearMotion = true; }
	const std::vector<Vec3>& WorldVertices() const { return m_World; }   // 천 정점 (월드, Play 중)
	int SimVertexCount() const { return (int)m_RestLocal.size(); }
	const std::vector<uint32>& Pinned() const { return m_Pinned; }

	GENERATE_COMPONENT_BODY(Cloth)

private:
	uint32 m_Handle = 0, m_Serial = 0;
	std::shared_ptr<Mesh> m_Original, m_Copy;
	std::vector<int> m_RenderToSim;      // 그리기 정점 → 천 정점
	std::vector<Vec3> m_RestLocal;       // 천 정점 (오브젝트 로컬, 시작 자세)
	std::vector<uint32> m_Triangles;     // 천 삼각형
	std::vector<uint32> m_Pinned;
	std::vector<float> m_NormalSign;     // 그리기 정점마다 원래 법선 쪽 (+1 / -1)
	std::vector<Vec3> m_World;
	double m_Time = 0.0;
	Vec3 m_LastPosition = Vec3::Zero;
	bool m_ClearMotion = false;
	bool Create();
	void Release();
};

REGISTER_COMPONENT(Cloth)
