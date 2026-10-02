#pragma once

// Unity 의 Project Settings > Physics (ProjectSettings/PhysicsSettings.json): Gravity, Layer Collision Matrix.
//  - 매트릭스: 레이어 a 와 b 가 부딪히는가 (대칭, 기본 = 모두 부딪힘). 트리거 · Character Controller · Raycast 의 레이어 마스크와 함께 쓴다
//  - Layer Overrides (콜라이더 · Rigidbody 의 Include / Exclude Layers) 는 매트릭스보다 먼저 (Unity 와 같음)
//  - Play 중 Physics.IgnoreLayerCollision 은 실행 중 값만 바꾼다 (Play 를 멈추면 설정 값으로 돌아감 — Begin/EndPlay)
namespace PhysicsSettings
{
	NOVA_API Vec3 Gravity();
	NOVA_API void SetGravity(const Vec3& g);     // 저장한다

	// 실행 중 값 (충돌 필터가 읽는다 — 작업 스레드에서도)
	NOVA_API bool LayersCollide(int a, int b);
	NOVA_API uint32 CollisionMask(int layer);   // 비트 b = a 가 b 와 부딪힘
	// 설정 값 (Project Settings) — 실행 중 값도 같이 바꾼다. save = 파일에 쓰기
	NOVA_API void SetLayersCollide(int a, int b, bool collide, bool save = true);
	NOVA_API void SetAllCollide(bool collide);
	// Physics.IgnoreLayerCollision: 실행 중 값만
	NOVA_API void IgnoreLayerCollisionRuntime(int a, int b, bool ignore);
	NOVA_API void ResetRuntime();   // 실행 중 값 = 설정 값 (Play 시작 · 끝)

	NOVA_API void Reload();
}
