#pragma once
#include <cstdint>
#include <vector>

class GameObject;
class Component;
class Collider;
class RigidBody;
class CharacterController;
class Joint;
class Collider2D;
class Rigidbody2D;
class Joint2D;

// 물리 동기화 (3D · 2D) 가 스텝마다 모든 오브젝트의 컴포넌트를 dynamic_cast 로 다시 훑지 않게: 오브젝트마다 분류를 기억한다.
//  기억이 맞는지 = 오브젝트 InstanceID + 컴포넌트 InstanceID 목록 (순서 포함) 이 같은가. InstanceID 는 다시 쓰이지 않아
//  (컴포넌트를 더하기 · 빼기 · 바꾸기 · 순서 바꾸기 · 지운 주소에 새 오브젝트) 모두 다시 분류한다. 메인 스레드 전용
namespace ComponentIndex
{
	struct Entry
	{
		// 분류 (컴포넌트 순서 그대로). 켜짐 · 값은 쓰는 쪽이 매번 본다
		std::vector<Component*> Components;
		std::vector<Collider*> Colliders;         // 3D 콜라이더 (Character Controller 포함 — 형상 종류는 쓰는 쪽이)
		RigidBody* Rigid = nullptr;               // 첫 Rigidbody (GetComponent 와 같다)
		CharacterController* Character = nullptr; // 첫 Character Controller
		std::vector<Joint*> Joints;
		std::vector<Collider2D*> Colliders2D;
		Rigidbody2D* Rigid2D = nullptr;
		std::vector<Joint2D*> Joints2D;

		uint64_t ObjectId = ~0ull;
		std::vector<int> Ids;   // 컴포넌트 InstanceID (지문)
		uint32_t Seen = 0;
	};

	// go 의 분류 (지문이 다르면 다시 만든다). 돌려준 참조는 다음 Of 호출 전까지 쓴다 (표가 자라면 옮겨질 수 있다)
	const Entry& Of(GameObject* go);
	// 동기화 한 번이 끝날 때: 오래 보지 않은 오브젝트 (지워짐) 의 기억을 가끔 버린다
	void EndPass();
}
