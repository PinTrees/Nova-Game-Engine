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
	int m_StepCount = 0;

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
	bool Raycast(const Vec3& origin, const Vec3& direction, RaycastHit& hit, float maxDistance = 1000.0f, bool hitTriggers = false);

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
	float GetHingeAngle(const HingeJoint* joint, bool velocity);   // 도 / 도/초 (Play 중, 없으면 0)

	// Editor
	void DebugRender() {}

private:
	void StepSimulation(float dt);
};
