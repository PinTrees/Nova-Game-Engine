#pragma once
#include "Component.h"

// Unity 의 2D 물리 컴포넌트 (Box2D v3 — Physics2DManager). 모두 GameObject 의 XY 평면, 회전 = Z 축.
//  - Rigidbody2D: Dynamic / Kinematic / Static, 질량 · 감쇠 · Gravity Scale · Collision Detection · Constraints
//  - Collider2D: 같은 GameObject 나 부모의 Rigidbody2D 에 붙는다 (없으면 움직이지 않는 정적 몸체)
//    Box · Circle · Capsule · Polygon (스프라이트 윤곽 자동, 오목해도 볼록 조각으로) · Edge (선)
//  - 처음 붙일 때 같은 GameObject 의 Sprite Renderer 크기에 맞춘다 (Unity 와 같음)

// 콜라이더 로컬 모양 (GameObject 공간, 크기 조절 전). Physics2DManager 가 월드 → 몸체 공간으로 바꾼다
struct Shape2D
{
	enum Kind { Circle, Polygon, Capsule, Segment } K = Polygon;
	std::vector<Vec2> Points;   // Polygon: 볼록 · 3..8 개 (반시계), Capsule / Segment: 두 점
	float Radius = 0.0f;        // Circle · Capsule 반지름, Polygon 모서리 둥글기
};

class NOVA_API Rigidbody2D : public Component
{
public:
	enum class BodyType { Dynamic = 0, Kinematic = 1, Static = 2 };
	Rigidbody2D();

	BodyType Type = BodyType::Dynamic;
	float Mass = 1.0f;
	float LinearDamping = 0.0f;
	float AngularDamping = 0.05f;
	float GravityScale = 1.0f;
	int CollisionDetection = 0;   // 0 Discrete, 1 Continuous (빠른 물체가 얇은 벽을 뚫지 않게)
	bool FreezePositionX = false, FreezePositionY = false, FreezeRotation = false;

	// 실행 중 (Play) — 몸체가 없으면 다음에 만들 때 쓴다
	Vec2 GetVelocity();
	void SetVelocity(const Vec2& v);
	float GetAngularVelocity();            // 도 / 초
	void SetAngularVelocity(float degPerSec);
	Vec2 GetPosition();
	float GetRotation();                   // 도
	void AddForce(const Vec2& f, bool impulse);
	void AddForceAtPosition(const Vec2& f, const Vec2& worldPoint, bool impulse);
	void AddTorque(float t, bool impulse);
	void MovePosition(const Vec2& p);      // Kinematic: 다음 물리 단계에 그곳으로 (사이를 쓸고 지나감)
	void MoveRotation(float degrees);
	void ApplyDynamicSettings();           // 값을 바꾼 뒤 몸체에 반영

	// Physics2DManager 가 쓴다
	uint64 Body = 0;                       // b2BodyId (0 = 없음)
	Vec2 PendingVelocity; float PendingAngular = 0.0f; bool HasPending = false;
	bool HasMoveTarget = false; Vec2 MoveTarget; float MoveAngle = 0.0f; bool HasMoveAngle = false;

	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "rigidbody"; }

	GENERATE_COMPONENT_BODY(Rigidbody2D)
};
REGISTER_COMPONENT(Rigidbody2D)

class NOVA_API Collider2D : public Component
{
public:
	Collider2D();
	bool IsTrigger = false;
	Vec2 Offset = Vec2(0, 0);
	float Friction = 0.4f;       // Physics Material 2D 의 기본값 (Unity)
	float Bounciness = 0.0f;

	// 로컬 모양 (Offset 포함, 크기 조절 전)
	virtual void BuildShapes(std::vector<Shape2D>& out) = 0;
	// Scene 뷰 윤곽 (로컬)
	virtual void Outline(std::vector<std::vector<Vec2>>& loops) = 0;
	// 같은 GameObject 의 Sprite Renderer 크기에 맞추기
	virtual void FitToSprite() {}
	Rigidbody2D* AttachedRigidbody();
	uint32 Revision = 1;          // 값이 바뀌면 + (모양을 다시 만든다)
	void Touch() { ++Revision; }

	void OnDrawGizmos() override;
	bool UsesUnityInspector() const override { return true; }

protected:
	bool m_NeedsFit = true;       // 처음 붙였을 때 (저장된 값을 읽으면 false)
	void EnsureFitted();
	bool CommonInspector();       // Is Trigger · Offset · Material
	void CommonToJson(json& j) const;
	void CommonFromJson(const json& j);
};

class NOVA_API BoxCollider2D : public Collider2D
{
public:
	BoxCollider2D() { m_InspectorTitleName = "Box Collider 2D"; }
	Vec2 Size = Vec2(1, 1);
	float EdgeRadius = 0.0f;
	void BuildShapes(std::vector<Shape2D>& out) override;
	void Outline(std::vector<std::vector<Vec2>>& loops) override;
	void FitToSprite() override;
	void OnInspectorGUI() override;
	const char* InspectorIconName() const override { return "box_collider"; }
	GENERATE_COMPONENT_BODY(BoxCollider2D)
};
REGISTER_COMPONENT(BoxCollider2D)

class NOVA_API CircleCollider2D : public Collider2D
{
public:
	CircleCollider2D() { m_InspectorTitleName = "Circle Collider 2D"; }
	float Radius = 0.5f;
	void BuildShapes(std::vector<Shape2D>& out) override;
	void Outline(std::vector<std::vector<Vec2>>& loops) override;
	void FitToSprite() override;
	void OnInspectorGUI() override;
	const char* InspectorIconName() const override { return "sphere_collider"; }
	GENERATE_COMPONENT_BODY(CircleCollider2D)
};
REGISTER_COMPONENT(CircleCollider2D)

class NOVA_API CapsuleCollider2D : public Collider2D
{
public:
	CapsuleCollider2D() { m_InspectorTitleName = "Capsule Collider 2D"; }
	Vec2 Size = Vec2(0.5f, 1.0f);
	int Direction = 0;   // 0 Vertical, 1 Horizontal
	void BuildShapes(std::vector<Shape2D>& out) override;
	void Outline(std::vector<std::vector<Vec2>>& loops) override;
	void FitToSprite() override;
	void OnInspectorGUI() override;
	const char* InspectorIconName() const override { return "capsule_collider"; }
	GENERATE_COMPONENT_BODY(CapsuleCollider2D)
};
REGISTER_COMPONENT(CapsuleCollider2D)

class NOVA_API PolygonCollider2D : public Collider2D
{
public:
	PolygonCollider2D() { m_InspectorTitleName = "Polygon Collider 2D"; }
	std::vector<std::vector<Vec2>> Paths;   // 닫힌 윤곽 (오목해도 됨, 로컬 단위)
	void BuildShapes(std::vector<Shape2D>& out) override;
	void Outline(std::vector<std::vector<Vec2>>& loops) override;
	void FitToSprite() override;            // 스프라이트의 투명하지 않은 윤곽
	void OnInspectorGUI() override;
	const char* InspectorIconName() const override { return "mesh_collider"; }
	GENERATE_COMPONENT_BODY(PolygonCollider2D)
private:
	std::vector<Shape2D> m_Cache;            // 볼록 조각 (Paths 가 바뀔 때만 다시)
	uint32 m_CacheRevision = 0;
};
REGISTER_COMPONENT(PolygonCollider2D)

class NOVA_API EdgeCollider2D : public Collider2D
{
public:
	EdgeCollider2D() { m_InspectorTitleName = "Edge Collider 2D"; m_NeedsFit = false; }
	std::vector<Vec2> Points = { Vec2(-0.5f, 0.0f), Vec2(0.5f, 0.0f) };
	float EdgeRadius = 0.0f;
	void BuildShapes(std::vector<Shape2D>& out) override;
	void Outline(std::vector<std::vector<Vec2>>& loops) override;
	void OnInspectorGUI() override;
	const char* InspectorIconName() const override { return "mesh_collider"; }
	GENERATE_COMPONENT_BODY(EdgeCollider2D)
};
REGISTER_COMPONENT(EdgeCollider2D)
