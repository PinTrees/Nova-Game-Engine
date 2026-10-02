#pragma once

// Unity 의 Project Settings > Physics 2D (ProjectSettings/Physics2DSettings.json).
//  - Gravity (기본 0, -9.81), Layer Collision Matrix (3D 와 따로 — Unity 와 같음), Queries Hit Triggers
//  - Play 중 Physics2D.IgnoreLayerCollision 은 실행 중 값만 (멈추면 설정 값으로)
namespace Physics2DSettings
{
	NOVA_API Vec2 Gravity();
	NOVA_API void SetGravity(const Vec2& g);   // 저장

	NOVA_API bool QueriesHitTriggers();
	NOVA_API void SetQueriesHitTriggers(bool v);

	NOVA_API bool LayersCollide(int a, int b);
	NOVA_API uint32 CollisionMask(int layer);
	NOVA_API void SetLayersCollide(int a, int b, bool collide, bool save = true);
	NOVA_API void SetAllCollide(bool collide);
	NOVA_API void IgnoreLayerCollisionRuntime(int a, int b, bool ignore);
	NOVA_API void ResetRuntime();
	NOVA_API uint32 Version();   // 매트릭스가 바뀔 때마다 + (모양 필터를 다시 정할 때)

	NOVA_API void Reload();
}
