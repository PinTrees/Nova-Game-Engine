#include "pch.h"
#include "ComponentIndex.h"
#include "GameObject.h"
#include "Collider.h"
#include "RigidBody.h"
#include "CharacterController.h"
#include "Joint.h"
#include "MonoBehaviour.h"
#include "Physics2DComponents.h"
#include "Physics2DJoints.h"
#include <unordered_map>

namespace
{
	std::unordered_map<const GameObject*, ComponentIndex::Entry> s_Entries;
	uint32_t s_Pass = 1;

	void Classify(ComponentIndex::Entry& e, GameObject* go)
	{
		e = ComponentIndex::Entry();
		e.ObjectId = go->GetInstanceID();
		const auto& components = go->GetComponents();
		e.Components.reserve(components.size());
		e.Ids.reserve(components.size());
		for (const auto& sp : components)
		{
			Component* c = sp.get();
			e.Components.push_back(c);
			e.Ids.push_back(c ? c->GetInstanceID() : -1);
			if (c == nullptr || dynamic_cast<MonoBehaviour*>(c))
				continue;   // 스크립트는 물리 분류에 들지 않는다
			if (Collider* col = dynamic_cast<Collider*>(c))
			{
				e.Colliders.push_back(col);
				if (!e.Character)
					e.Character = dynamic_cast<CharacterController*>(col);
				continue;
			}
			if (RigidBody* rb = dynamic_cast<RigidBody*>(c))
			{
				if (!e.Rigid) e.Rigid = rb;
				continue;
			}
			if (Joint* j = dynamic_cast<Joint*>(c))
			{
				e.Joints.push_back(j);
				continue;
			}
			if (Collider2D* col2 = dynamic_cast<Collider2D*>(c))
			{
				e.Colliders2D.push_back(col2);
				continue;
			}
			if (Rigidbody2D* rb2 = dynamic_cast<Rigidbody2D*>(c))
			{
				if (!e.Rigid2D) e.Rigid2D = rb2;
				continue;
			}
			if (Joint2D* j2 = dynamic_cast<Joint2D*>(c))
				e.Joints2D.push_back(j2);
		}
	}

	bool Matches(const ComponentIndex::Entry& e, GameObject* go)
	{
		if (e.ObjectId != go->GetInstanceID())
			return false;   // 지운 오브젝트의 주소에 새 오브젝트
		const auto& components = go->GetComponents();
		if (components.size() != e.Ids.size())
			return false;
		for (size_t i = 0; i < components.size(); ++i)
		{
			Component* c = components[i].get();
			if ((c ? c->GetInstanceID() : -1) != e.Ids[i])
				return false;
		}
		return true;
	}
}

namespace ComponentIndex
{
	const Entry& Of(GameObject* go)
	{
		Entry& e = s_Entries[go];
		if (!Matches(e, go))
			Classify(e, go);
		e.Seen = s_Pass;
		return e;
	}

	void EndPass()
	{
		// 64 번에 한 번: 그동안 보지 못한 오브젝트 (지워지거나 다른 씬) 의 기억을 버린다
		if (++s_Pass % 64 != 0)
			return;
		const uint32_t keepAfter = s_Pass - 64;
		for (auto it = s_Entries.begin(); it != s_Entries.end();)
			it = it->second.Seen < keepAfter ? s_Entries.erase(it) : std::next(it);
	}
}
