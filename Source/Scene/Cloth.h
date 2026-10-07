#pragma once
#include "Component.h"
#include "Vertex.h"

class Mesh;
class SkinnedMeshRenderer;

// Unity 의 Cloth: 같은 오브젝트의 메시를 천으로 (Jolt Soft Body, Play 중).
//  - Mesh Filter (Plane 등): 메시 정점을 위치로 묶어 (이음매의 같은 자리 정점은 하나) 늘어남 · 비틀림 · 접힘 구속 + 고정점에서 멀어지지 않게 (LRA)
//  - Skinned Mesh Renderer (치마 · 망토): 천이 뼈대를 따라간다 — 정점마다 피부 (애니메이션 자세) 에서 벗어날 수 있는 거리
//    (coefficients 의 maxDistance, 0 = 피부에 붙음) 와 피부 안쪽으로 들어가지 않는 뒤 막이 (collisionSphereDistance)
//  - 고정 (Pin): 위쪽 가장자리 · 위 두 모서리 · 고른 정점 — 오브젝트 (스킨이면 뼈대) 를 옮기면 고정점이 따라오고 나머지가 끌려온다
//  - 장면의 콜라이더 (상자 · 구 · 캡슐 · 메시 · 지형 · Rigidbody) 와 부딪힌다 (자기 콜라이더 · 트리거는 빼고)
//  - 바람 = External Acceleration + Random Acceleration (출렁임)
//  그리기: Mesh Filter 는 메시의 사본을, 스킨은 렌더러의 동적 정점 버퍼를 프레임마다 고친다 (원래 메시 파일은 그대로)
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

	// Unity 의 ClothSkinningCoefficient (천 정점마다): maxDistance = 피부에서 벗어날 수 있는 거리 (0 = 고정),
	//  collisionSphereDistance = 피부 안쪽으로 들어갈 수 있는 거리 (뒤 막이). FLT_MAX = 제한 없음. 비어 있으면 모두 FLT_MAX
	struct Coefficient { float MaxDistance = FLT_MAX; float CollisionSphereDistance = FLT_MAX; };
	std::vector<Coefficient> Coefficients;

	Cloth();
	~Cloth() override;
	void FixedUpdate() override;
	void LateUpdate() override;
	void OnDestroy() override;
	void OnHierarchyActiveChanged(bool active) override;   // 꺼지면 천을 물리에서 내린다 (다시 켜면 시작 자세로 새로)
	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "mesh_renderer"; }

	bool IsSimulating() const { return m_Handle != 0; }
	bool IsSkinned() const { return m_Skinned; }
	// Unity: 이번 스텝의 오브젝트 움직임을 천 전체에 그대로 (순간 이동 — 휘날리지 않게). 1 m 넘게 한 번에 옮기면 저절로
	void ClearTransformMotion() { m_ClearMotion = true; }
	const std::vector<Vec3>& WorldVertices() const { return m_World; }   // 천 정점 (월드, Play 중)
	int SimVertexCount() const { return (int)m_RestLocal.size(); }
	const std::vector<uint32>& Pinned() const { return m_Pinned; }
	// 천 정점 수 (Play 전에도 — coefficients 의 길이)
	int ClothVertexCount();
	// 메시 정점마다 지금 위치 (오브젝트 로컬, Unity Cloth.vertices)
	void LocalVertices(std::vector<Vec3>& out);

	GENERATE_COMPONENT_BODY(Cloth)

private:
	uint32 m_Handle = 0, m_Serial = 0;
	bool m_Skinned = false;
	std::shared_ptr<Mesh> m_Original, m_Copy;
	std::vector<int> m_RenderToSim;      // 그리기 정점 → 천 정점
	std::vector<Vec3> m_RestLocal;       // 천 정점 (오브젝트 로컬, 시작 자세 — 스킨이면 바인드 자세)
	std::vector<uint32> m_Triangles;     // 천 삼각형
	std::vector<uint32> m_Pinned;
	std::vector<float> m_NormalSign;     // 그리기 정점마다 원래 법선 쪽 (+1 / -1)
	std::vector<Vec3> m_World;
	std::vector<Vertex::PosNormalTexTanSkinned> m_SkinVerts;   // 스킨: 렌더러에 넘길 정점 (단위 본 하나)
	double m_Time = 0.0;
	Vec3 m_LastPosition = Vec3::Zero;
	bool m_ClearMotion = false;
	bool Create();
	void Release();
	// 같은 자리 정점 묶기 → m_RenderToSim · m_RestLocal (positions 공간 그대로) · m_Triangles
	void Weld(const std::vector<Vec3>& positions, const std::vector<uint32>& indices);
	bool SourceGeometry(std::vector<Vec3>& positions, std::vector<uint32>& indices, SkinnedMeshRenderer** skinned);
	void SkinJoints(SkinnedMeshRenderer* r, std::vector<Matrix>& joints);
	void RecomputeNormals(const std::vector<Vec3>& local, std::vector<Vec3>& normals) const;
};

REGISTER_COMPONENT(Cloth)
