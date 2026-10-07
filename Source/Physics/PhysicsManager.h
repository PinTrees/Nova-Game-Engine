#pragma once

class GameObject;
class Collider;
class RigidBody;
class CharacterController;
class Joint;
class HingeJoint;

// Unity 의 ForceMode
enum class ForceMode
{
	Force,           // 질량을 고려한 연속적인 힘 (N)
	Acceleration,    // 질량을 무시한 연속적인 가속도 (m/s²)
	Impulse,         // 질량을 고려한 순간 충격량 (N·s)
	VelocityChange,  // 질량을 무시한 순간 속도 변화 (m/s)
};

// Unity 의 RaycastHit
struct RaycastHit
{
	Vec3 point;
	Vec3 normal;
	float distance = 0.0f;
	Collider* collider = nullptr;
	GameObject* gameObject = nullptr;
};

// Jolt Physics 기반 물리 시스템 (Unity 의 PhysX 역할).
//  - Play 모드에 들어가면 Collider / RigidBody 가 있는 GameObject 마다 Jolt 바디를 만든다.
//    RigidBody 없음 = Static, Is Kinematic = Kinematic, 그 외 = Dynamic.
//    자식 오브젝트의 Collider 는 가장 가까운 부모 RigidBody 의 복합 형상에 포함된다 (Unity 와 동일).
//  - 고정 시간 간격(Fixed Timestep, 기본 0.02초)마다 FixedUpdate → 시뮬레이션 → 충돌/트리거 이벤트 순서로 진행한다.
//  - Jolt 헤더는 PhysicsManager.cpp 안에서만 사용한다 (JoltWorld 는 내부 구현).
class NOVA_API PhysicsManager
{
	SINGLE_HEADER(PhysicsManager)

public:
	struct JoltWorld;

private:
	std::unique_ptr<JoltWorld> m_World;
	bool m_JoltInitialized = false;

	Vec3 m_Gravity = Vec3(0.0f, -9.81f, 0.0f);
	float m_FixedTimestep = 0.02f;
	float m_MaxAllowedTimestep = 0.3333f;
	float m_Accumulator = 0.0f;
	uint32 m_WorldSerial = 0;
	float m_HeartbeatTime = 0.0f;   // Play 처음 3 초: 1 초마다 Editor.log 에 스텝 수 (물리가 멈춘 세션을 진단)
	int m_HeartbeatSteps = 0;
	int m_StepCount = 0;
	bool m_EditQueryWorld = false;   // BeginEditQueries 가 만든 임시 월드

public:
	void Init();
	void Start();                  // Play 시작 (Scene::Enter)
	void Update(float deltaTime);  // 매 프레임: 고정 간격으로 나누어 시뮬레이션
	void Exit();                   // Play 종료 (Scene::Exit)

	bool IsSimulating() const { return m_World != nullptr; }

	// ---- 설정 (Unity: Project Settings > Physics / Time) ----
	void SetGravity(const Vec3& gravity);
	Vec3 GetGravity() const { return m_Gravity; }
	void SetFixedTimestep(float seconds) { m_FixedTimestep = (std::max)(0.001f, seconds); }
	float GetFixedTimestep() const { return m_FixedTimestep; }

	// ---- Unity 의 Physics.Raycast ----
	// layerMask: 비트 = 레이어 (Unity 의 Physics.DefaultRaycastLayers = Ignore Raycast 를 뺀 모두)
	static constexpr uint32 kDefaultRaycastLayers = ~(1u << 2);
	bool Raycast(const Vec3& origin, const Vec3& direction, RaycastHit& hit, float maxDistance = 1000.0f, bool hitTriggers = false, uint32 layerMask = kDefaultRaycastLayers);
	// 맞은 것을 가까운 순으로 여러 개 (트리거 제외, staticOnly 면 Rigidbody 가 없는 정적 바디만) — NavMesh 굽기 등. 여러 스레드에서 불러도 된다
	int RaycastAll(const Vec3& origin, const Vec3& direction, float maxDistance, RaycastHit* out, int maxHits, bool staticOnly);
	// 편집 중에도 Raycast 를 쓰게 지금 씬으로 물리 월드를 맞춘다 (없으면 임시로 만든다 / End 에서 내림). Play 중이면 그대로
	bool BeginEditQueries();
	void EndEditQueries();
	// 모든 바디를 감싸는 상자 (월드가 있을 때)
	bool GetWorldBounds(Vec3& outMin, Vec3& outMax);
	// 상자 안의 정적 콜라이더(트리거 제외) 삼각형 — NavMesh 굽기 (Unity 의 Use Geometry = Physics Colliders).
	// verts = x,y,z 반복, tris = 정점 번호 3 개씩 (바깥에서 보아 반시계). 돌려주는 값 = 삼각형 수
	int CollectStaticTriangles(const Vec3& boundsMin, const Vec3& boundsMax, std::vector<float>& verts, std::vector<int>& tris);

	// ---- RigidBody 가 호출하는 바디 조작 (바디가 없으면 false / 기본값) ----
	bool GetLinearVelocity(RigidBody* rb, Vec3& out);
	bool SetLinearVelocity(RigidBody* rb, const Vec3& v);
	bool GetAngularVelocity(RigidBody* rb, Vec3& out);
	bool SetAngularVelocity(RigidBody* rb, const Vec3& w);
	bool AddForce(RigidBody* rb, const Vec3& force, ForceMode mode);
	bool AddTorque(RigidBody* rb, const Vec3& torque, ForceMode mode);
	bool AddForceAtPosition(RigidBody* rb, const Vec3& force, const Vec3& position, ForceMode mode);
	bool MovePosition(RigidBody* rb, const Vec3& position);
	bool MoveRotation(RigidBody* rb, const Quaternion& rotation);
	bool IsSleeping(RigidBody* rb);
	void Sleep(RigidBody* rb);
	void WakeUp(RigidBody* rb);
	Vec3 GetWorldCenterOfMass(RigidBody* rb);

	// ---- CharacterController (Jolt CharacterVirtual) ----
	// motion 만큼 쓸고 지나가며 이동, CollisionFlags 비트 합을 돌려준다. Play 가 아니면 Transform 만 옮긴다
	int MoveCharacter(CharacterController* cc, const Vec3& motion, float deltaTime);
	void RemoveCharacter(CharacterController* cc);

	// ---- Joint (Fixed / Hinge / Spring → Jolt Constraint) ----
	void RemoveJoint(Joint* joint);
	// ---- Unity 의 Physics.IgnoreCollision: 두 오브젝트 (의 Rigidbody 바디) 끼리 접촉하지 않는다. Play 중에만 (저장하지 않는다)
	void IgnoreCollision(GameObject* a, GameObject* b, bool ignore = true);
	bool GetIgnoreCollision(GameObject* a, GameObject* b);
	// ---- 천 (Cloth 컴포넌트 → Jolt Soft Body). 핸들 0 = 실패. 월드가 다시 만들어지면 (Play 시작) WorldSerial 이 바뀐다
	struct ClothSettings
	{
		float Compliance = 0.0f;         // 늘어남 (0 = 늘지 않음)
		float ShearCompliance = 0.0f;
		float BendCompliance = 1.0f;     // 접힘 (클수록 잘 접힌다, FLT_MAX = 굽힘 없음)
		bool LongRangeAttachment = true; // 고정 정점에서 멀어지지 않게 (늘어짐 방지)
		float Damping = 0.1f, Friction = 0.3f, GravityFactor = 1.0f, Thickness = 0.02f;
		uint32 Iterations = 6;
	};
	// 스킨 위의 천 (Skinned Mesh Renderer): 정점마다 본 4 개 · 가중치, 피부에서 벗어날 수 있는 거리 (0 = 피부에 붙음), 뒤 막이 (피부 안쪽으로 들어가지 않게)
	struct ClothSkin
	{
		std::vector<std::array<uint8, 4>> Bones;
		std::vector<std::array<float, 4>> Weights;
		int JointCount = 0;
		Matrix RestToWorld;                  // 바인드 정점 → 월드 (만들 때의 rest 정점 = 바인드 정점 × 이것)
		std::vector<float> MaxDistance;      // FLT_MAX = 자유
		std::vector<float> BackStopDistance; // FLT_MAX = 없음
		float BackStopRadius = 1.0f;
	};
	uint32 CreateCloth(GameObject* owner, const std::vector<Vec3>& worldVertices, const std::vector<uint32>& triangles, const std::vector<float>& invMass, const ClothSettings& s,
		const ClothSkin* skin = nullptr);
	// 스킨 천: 본 행렬 (바인드 정점 → 월드, 행 벡터) 로 피부 자리를 다시 — hardSkinAll = 모든 정점을 피부 자리로 (시작 · 순간 이동)
	void SkinCloth(uint32 handle, const std::vector<Matrix>& jointToWorld, bool hardSkinAll);
	void DestroyCloth(uint32 handle);
	bool GetClothVertices(uint32 handle, std::vector<Vec3>& world);
	// 스텝 전: 고정 정점 (월드) 을 옮기고 (속도 = 옮긴 만큼) 모든 정점에 가속 (바람)
	void DriveCloth(uint32 handle, const std::vector<uint32>& pinned, const std::vector<Vec3>& pinnedWorld, const Vec3& acceleration, float dt);
	// 천 전체를 옮긴다 (속도는 그대로) — 오브젝트가 순간 이동했을 때 (Unity 의 Cloth.ClearTransformMotion)
	void ShiftCloth(uint32 handle, const Vec3& delta);
	uint32 WorldSerial() const { return m_WorldSerial; }

	// 한 점에 dir 방향 충격을 줄 때의 실제 질량 1 / (1/M + (r×d)·I⁻¹(r×d)) — 회전으로 빠지는 몫까지 (바퀴 마찰이 한 스텝에 넘치지 않게). 다이내믹이 아니면 0
	float GetEffectiveMass(RigidBody* rb, const Vec3& point, const Vec3& dir);
	float GetHingeAngle(const HingeJoint* joint, bool velocity);   // 도 / 도/초 (Play 중, 없으면 0)

	// Editor
	void DebugRender() {}

private:
	void StepSimulation(float dt);
};
