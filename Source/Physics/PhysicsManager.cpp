#include "pch.h"
#include "PhysicsManager.h"
#include "MonoBehaviour.h"
#include "Debug.h"
#include "CollisionDetector.h"
#include "CollisionResolver.h"
#include "Debug.h"

// SAT(Separating Axis Theorem)
// GJK / EPA(Gilbert - Johnson - Keerthi / Expanding Polytope Algorithm)

// Ãæµ¹ ½Ã Àû¿ëµÇ´Â Èû °è»ê.
// Ä§Åõ ±íÀÌ(Penetration Depth) ¹× Ãæµ¹ ¹ı¼± º¤ÅÍ(Contact Normal) °è»ê.
// Impulse ±â¹İ : Á÷°üÀûÀÎ ¹æ½Ä.
// Constraint ±â¹İ : LCP Solver »ç¿ë.
// À§Ä¡ º¸Á¤ : Ä§Åõ ±íÀÌ¸¸Å­ ¹°Ã¼¸¦ ¹Ğ¾î³»´Â À§Ä¡ º¸Á¤.

SINGLE_BODY(PhysicsManager)

PhysicsManager::PhysicsManager()
	: m_GravityAcceleration(9.8f)
{

}

PhysicsManager::~PhysicsManager()
{

}

void PhysicsManager::Init()
{
	SetGravity(9.8f);

	m_Detector = new CollisionDetector; 
	m_Resolver = new CollisionResolver;
}

void PhysicsManager::Start()
{
	auto gameObjects = SceneManager::GetI()->GetCurrentScene()->GetAllGameObjects();
	for (auto& gameObject : gameObjects)
	{
		auto rigidBody = gameObject->GetComponent<RigidBody>();

		if (rigidBody == nullptr)
		{
			// RigidBody ê°€ ì—†ëŠ” ì½œë¼ì´ë”ëŠ” ì •ì  ì½œë¼ì´ë”ë¡œ ë“±ë¡ (ì´ë²¤íŠ¸ íŒì • ì „ìš©)
			if (BoxCollider* sb = gameObject->GetComponent<BoxCollider>()) m_StaticColliders.push_back(sb);
			if (SphereCollider* ss = gameObject->GetComponent<SphereCollider>()) m_StaticColliders.push_back(ss);
			continue;
		}

		BoxCollider* box = gameObject->GetComponent<BoxCollider>();
		SphereCollider* sphere = gameObject->GetComponent<SphereCollider>();

		m_RigidBodies[gameObject->GetInstanceID()] = rigidBody;
		if (box) m_Colliders[gameObject->GetInstanceID()] = box;
		if (sphere) m_Colliders[gameObject->GetInstanceID()] = sphere;

		// °­Ã¼ÀÇ °ü¼º ¸ğ¸àÆ® ÅÙ¼­¸¦ µµÇü¿¡ µû¶ó ¼³Á¤ÇÔ
		Matrix3 inertiaTensor;
		if (sphere)
		{
			float value = 0.4f * rigidBody->GetMass();
			inertiaTensor.setDiagonal(Vec3(value, value, value));
		}
		else if (box)
		{
			Vec3 size = box->GetSize(); 
			Transform* transform = box->GetGameObject()->GetTransform();

			size = Vec3(size.x * transform->GetScale().x, size.y * transform->GetScale().y, size.z * transform->GetScale().z);
			size *= 0.15f;

			float mass = rigidBody->GetMass(); 
			float I_x = mass / 12.0f * (size.y * size.y + size.z * size.z);
			float I_y = mass / 12.0f * (size.x * size.x + size.z * size.z);
			float I_z = mass / 12.0f * (size.x * size.x + size.y * size.y);
			inertiaTensor.setDiagonal(Vec3(I_x, I_y, I_z));
		}
		rigidBody->SetAcceleration(Vec3(0.f, -9.8f, 0.f));
		rigidBody->SetInertiaTensor(inertiaTensor); 
	}
}

void PhysicsManager::Update(float deltaTime)
{
	Safe_Delete_Vec(m_Contacts); 

	/* ¹°Ã¼µéÀ» ÀûºĞÇÑ´Ù */
	for (auto& body : m_RigidBodies)
	{
		body.second->Integrate(deltaTime);
	}

	/* Ãæµ¹µéÀ» Ã³¸®ÇÑ´Ù */
	m_Detector->DetectCollision(m_Contacts, m_Colliders);
	m_Resolver->ResolveCollision(m_Contacts, deltaTime);

	/* Unity ì˜ OnTriggerEnter/Stay/Exit, OnCollisionEnter/Stay/Exit ì´ë²¤íŠ¸ */
	UpdateOverlapEvents();
}

void PhysicsManager::DispatchEvent(Collider* self, Collider* other, bool trigger, int kind)
{
	if (self == nullptr || other == nullptr || self->GetGameObject() == nullptr)
		return;
	for (const auto& component : self->GetGameObject()->GetComponents())
	{
		MonoBehaviour* script = dynamic_cast<MonoBehaviour*>(component.get());
		if (script == nullptr)
			continue;
		if (trigger)
		{
			if (kind == 0) script->OnTriggerEnter(other);
			else if (kind == 1) script->OnTriggerStay(other);
			else script->OnTriggerExit(other);
		}
		else
		{
			if (kind == 0) script->OnCollisionEnter(other);
			else if (kind == 1) script->OnCollisionStay(other);
			else script->OnCollisionExit(other);
		}
	}
}

void PhysicsManager::UpdateOverlapEvents()
{
	// íŒì • ëŒ€ìƒ: RigidBody ê°€ ìˆëŠ” ì½œë¼ì´ë” + ì •ì  ì½œë¼ì´ë”. ë‘ ë¬¼ì²´ ì¤‘ ì ì–´ë„ í•˜ë‚˜ëŠ” RigidBody ê°€ ìˆì–´ì•¼ í•œë‹¤ (Unity ì™€ ë™ì¼).
	std::vector<Collider*> all;
	for (auto& c : m_Colliders) all.push_back(c.second);
	size_t rigidCount = all.size();
	for (Collider* s : m_StaticColliders) all.push_back(s);

	std::map<ULONGLONG, OverlapPair> current;
	for (size_t i = 0; i < all.size(); ++i)
	{
		for (size_t j = i + 1; j < all.size(); ++j)
		{
			if (i >= rigidCount && j >= rigidCount)
				continue;   // ì •ì  - ì •ì ì€ ì´ë²¤íŠ¸ ì—†ìŒ
			Collider* a = all[i];
			Collider* b = all[j];
			if (!a->IsEnabled() || !b->IsEnabled())
				continue;
			if (m_Detector->Overlaps(a, b))
			{
				UINT ida = a->GetInstanceID(), idb = b->GetInstanceID();
				COLLIDER_ID key;
				key.Left_id = (std::min)(ida, idb);
				key.RIght_id = (std::max)(ida, idb);
				current[key.ID] = { a, b, a->IsTrigger() || b->IsTrigger() };
			}
		}
	}

	for (auto& kv : current)
	{
		int kind = m_PrevOverlaps.find(kv.first) == m_PrevOverlaps.end() ? 0 : 1;
		DispatchEvent(kv.second.a, kv.second.b, kv.second.trigger, kind);
		DispatchEvent(kv.second.b, kv.second.a, kv.second.trigger, kind);
	}
	for (auto& kv : m_PrevOverlaps)
	{
		if (current.find(kv.first) == current.end())
		{
			DispatchEvent(kv.second.a, kv.second.b, kv.second.trigger, 2);
			DispatchEvent(kv.second.b, kv.second.a, kv.second.trigger, 2);
		}
	}
	m_PrevOverlaps = std::move(current);
}

void PhysicsManager::Exit()
{
	m_RigidBodies.clear();
	m_Colliders.clear();
	m_StaticColliders.clear();
	m_PrevOverlaps.clear();
}

void PhysicsManager::DebugRender()
{
	std::vector<ContactInfo*> contactInfo;
	GetContactInfo(contactInfo);

	for (auto cp : contactInfo)
	{
		Gizmo::DrawArrow(Vec3(cp->pointX, cp->pointY, cp->pointZ), Vec3(cp->normalX, cp->normalY, cp->normalZ));
	}

	Safe_Delete_Vec(contactInfo);
}

void PhysicsManager::GetContactInfo(std::vector<ContactInfo*>& contactInfo) const
{
	for (const auto& contact : m_Contacts)
	{
		for (const auto& cp : contact->contactPoint)
		{
			if (cp == Vec3::Zero)
				continue;

			ContactInfo* newContactInfo = new ContactInfo;
			newContactInfo->pointX = cp.x;
			newContactInfo->pointY = cp.y;
			newContactInfo->pointZ = cp.z;
			newContactInfo->normalX = contact->normal.x;
			newContactInfo->normalY = contact->normal.y;
			newContactInfo->normalZ = contact->normal.z;
			contactInfo.push_back(newContactInfo);
		}
	}
}
