#pragma once

class Scene;
class Collider2D;
class GameObject;

// 2D 물리 세계 (Box2D v3). Unity 와 같이 3D 물리와 따로 — 서로 부딪히지 않는다.
//  - Play 시작 (Scene::Enter) 에 세계를 만들고, 매 프레임 고정 간격 (3D 와 같은 Fixed Timestep, 단계마다 하위 4 번) 으로 나아간다
//  - 매 프레임 씬과 맞춘다: 새 / 지운 Rigidbody2D · Collider2D, 값 (Revision) · 크기 · 레이어가 바뀐 모양, 사용자가 옮긴 Transform
//  - 몸체 = Rigidbody2D 의 GameObject (자식의 콜라이더도 그 몸체에), Rigidbody2D 가 없으면 콜라이더 GameObject 마다 정적 몸체
//  - 결과: Dynamic · Kinematic 몸체의 위치 (x, y) · Z 회전을 Transform 에, C# OnCollision/OnTrigger Enter·Stay·Exit 2D
namespace Physics2DManager
{
	void Start(Scene* scene);
	void Exit();
	void Update(float deltaTime);
	NOVA_API bool Active();

	struct Hit
	{
		Collider2D* Collider = nullptr;
		Vec2 Point, Normal;
		float Distance = 0.0f, Fraction = 0.0f;
	};
	// Physics2D.Raycast: 가장 가까운 것 (layerMask = 레이어 비트, 트리거 = Queries Hit Triggers)
	NOVA_API bool Raycast(const Vec2& origin, const Vec2& direction, float distance, uint32 layerMask, Hit& hit);
	// Physics2D.OverlapPoint / OverlapCircle / OverlapBox: 겹친 콜라이더 하나 (없으면 nullptr)
	NOVA_API Collider2D* OverlapCircle(const Vec2& center, float radius, uint32 layerMask);
	NOVA_API Collider2D* OverlapBox(const Vec2& center, const Vec2& size, float angleDegrees, uint32 layerMask);
	// Collision2D 의 접촉 정보 (self 가 받은 충돌): 점 · 법선 (상대 → 나) · 상대 속도
	NOVA_API bool ContactInfo(GameObject* self, GameObject* other, Vec2& point, Vec2& normal, Vec2& relativeVelocity);
	NOVA_API Vec2 Gravity();
	NOVA_API void SetGravity(const Vec2& g);   // 실행 중만 (Physics2D.gravity)
	NOVA_API int BodyCount();
}
