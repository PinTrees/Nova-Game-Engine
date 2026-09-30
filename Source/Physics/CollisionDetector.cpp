#include "pch.h"
#include "CollisionDetector.h"

CollisionDetector::CollisionDetector()
	: friction(0.6f),
	objectRestitution(0.3f),
	groundRestitution(0.2f) 
{
}

void CollisionDetector::DetectCollision(std::vector<Contact*>& contacts, std::unordered_map<unsigned int, Collider*>& colliders)
{
	for (auto i = colliders.begin(); i != colliders.end(); ++i)
	{
		Collider* colliderPtrI = i->second;
		for (auto j = std::next(i, 1); j != colliders.end(); ++j)
		{
			Collider* colliderPtrJ = j->second;
			if (colliderPtrI->IsTrigger() || colliderPtrJ->IsTrigger())
				continue;   // Is Trigger: í†µê³¼ì‹œí‚¤ê³  ì´ë²¤íŠ¸ë§Œ ë°œìƒì‹œí‚¨ë‹¤ (PhysicsManager)
			if (typeid(*colliderPtrI) == typeid(SphereCollider))
			{
				SphereCollider* collider1 = static_cast<SphereCollider*>(colliderPtrI);
				if (typeid(*colliderPtrJ) == typeid(SphereCollider)) // ±¸ - ±¸ Ãæµ¹
				{
					SphereCollider* collider2 = static_cast<SphereCollider*>(colliderPtrJ);
					CheckSphereSphereCollision(contacts, collider1, collider2);
				}
				else if (typeid(*colliderPtrJ) == typeid(BoxCollider)) // ±¸ - Á÷À°¸éÃ¼ Ãæµ¹
				{
					BoxCollider* collider2 = static_cast<BoxCollider*>(colliderPtrJ);
					CheckSphereBoxCollision(contacts, collider1, collider2);
				}
			}
			else if (typeid(*colliderPtrI) == typeid(BoxCollider))
			{
				BoxCollider* collider1 = static_cast<BoxCollider*>(colliderPtrI);
				if (typeid(*colliderPtrJ) == typeid(SphereCollider)) // ±¸ - Á÷À°¸éÃ¼ Ãæµ¹
				{
					SphereCollider* collider2 = static_cast<SphereCollider*>(colliderPtrJ);
					CheckSphereBoxCollision(contacts, collider2, collider1);
				}
				else if (typeid(*colliderPtrJ) == typeid(BoxCollider)) // Á÷À°¸éÃ¼ - Á÷À°¸éÃ¼ Ãæµ¹
				{
					BoxCollider* collider2 = static_cast<BoxCollider*>(colliderPtrJ);
					CheckBoxBoxCollision(contacts, collider1, collider2);
				}
			}
		}

		/* Áö¸é°úÀÇ Ãæµ¹ °Ë»ç */
	}
}


bool CollisionDetector::Overlaps(Collider* a, Collider* b)
{
	std::vector<Contact*> temp;
	bool hit = false;
	SphereCollider* sa = dynamic_cast<SphereCollider*>(a);
	SphereCollider* sb = dynamic_cast<SphereCollider*>(b);
	BoxCollider* ba = dynamic_cast<BoxCollider*>(a);
	BoxCollider* bb = dynamic_cast<BoxCollider*>(b);

	if (sa && sb) hit = CheckSphereSphereCollision(temp, sa, sb);
	else if (sa && bb) hit = CheckSphereBoxCollision(temp, sa, bb);
	else if (ba && sb) hit = CheckSphereBoxCollision(temp, sb, ba);
	else if (ba && bb) hit = CheckBoxBoxCollision(temp, ba, bb);

	for (Contact* c : temp)
		delete c;
	return hit;
}

bool CollisionDetector::CheckSphereSphereCollision(std::vector<Contact*>& contacts, SphereCollider* sphere1, SphereCollider* sphere2)
{
	Transform* sphere1Tr = sphere1->GetGameObject()->GetTransform();
	Transform* sphere2Tr = sphere2->GetGameObject()->GetTransform();

	RigidBody* sphere1Rb = sphere1->GetGameObject()->GetComponent<RigidBody>();
	RigidBody* sphere2Rb = sphere2->GetGameObject()->GetComponent<RigidBody>();

	float sphere1Radius = sphere1->GetRadius() * sphere1Tr->GetScale().x;
	float sphere2Radius = sphere2->GetRadius() * sphere2Tr->GetScale().x;

	/* µÎ ±¸ »çÀÌÀÇ °Å¸®¸¦ ±¸ÇÑ´Ù */
	float distanceSquared =
		(sphere1->GetWorldCenter() - sphere2->GetWorldCenter()).LengthSquared();

	/* µÎ ±¸ »çÀÌÀÇ °Å¸®°¡ µÎ ±¸ÀÇ ¹İÁö¸§ÀÇ ÇÕº¸´Ù ÀÛ´Ù¸é Ãæµ¹ÀÌ ¹ß»ıÇÑ °ÍÀÌ´Ù */
	float radiusSum = sphere1Radius + sphere2Radius;
	if (distanceSquared < radiusSum * radiusSum)
	{
		Vector3 centerToCenter = sphere1->GetWorldCenter() - sphere2->GetWorldCenter();
		centerToCenter.Normalize();

		/* Ãæµ¹ Á¤º¸¸¦ »ı¼ºÇÑ´Ù */
		Contact* newContact = new Contact;
		newContact->bodies[0] = sphere1Rb;
		newContact->bodies[1] = sphere2Rb;
		newContact->normal = centerToCenter;
		newContact->contactPoint[0] = Vector3(sphere1->GetWorldCenter() - centerToCenter * sphere1Radius);
		newContact->contactPoint[1] = Vector3(sphere2->GetWorldCenter() + centerToCenter * sphere2Radius);
		newContact->penetration = radiusSum - sqrtf(distanceSquared);
		newContact->restitution = objectRestitution;
		newContact->friction = friction;
		newContact->normalImpulseSum = 0.0f;
		newContact->tangentImpulseSum1 = 0.0f;
		newContact->tangentImpulseSum2 = 0.0f;

		contacts.push_back(newContact);
		return true;
	}
	else
		return false;
}

// Fixed 
// ±¸ÀÇ ¹Ú½º±âÁØ ·ÎÄÃ ½ºÄÉÀÏ ¹®Á¦ ¼öÁ¤ - ¹Ú½ºÀÇ ½ºÄÉÀÏÀÌ ÀÛÀ» °æ¿ì ±¸ÀÇ ¹İÁö¸§ÀÌ ¸Å¿ì Ä¿Áö´Â ¿À·ù ¼öÁ¤
bool CollisionDetector::CheckSphereBoxCollision(std::vector<Contact*>& contacts, SphereCollider* sphere, BoxCollider* box)
{
	Transform* sphereTr = sphere->GetGameObject()->GetTransform(); 
	Transform* boxTr = box->GetGameObject()->GetTransform(); 

	RigidBody* sphereRb = sphere->GetGameObject()->GetComponent<RigidBody>(); 
	RigidBody* boxRb = box->GetGameObject()->GetComponent<RigidBody>(); 

	/* ±¸ÀÇ Áß½ÉÀ» Á÷À°¸éÃ¼ÀÇ ·ÎÄÃ ÁÂÇ¥°è·Î º¯È¯ÇÑ´Ù */
	Matrix sphereInBoxLocalMatrix = sphereTr->GetWorldMatrix() * boxTr->GetWorldMatrix().Invert();
	const Vec3 sphereCenterWorld = sphere->GetWorldCenter();
	// êµ¬ì˜ Center ì™€ ìƒìì˜ Center ë¥¼ ëª¨ë‘ ë°˜ì˜í•œ ìƒì ë¡œì»¬ ì¢Œí‘œ
	Vec3 sphereInBoxLocalPosition = Vec3::Transform(sphereCenterWorld, boxTr->GetWorldMatrix().Invert()) - box->GetCenter();
	Vec3 sphereInBoxLocalScale = Vec3(
		Vec3(sphereInBoxLocalMatrix._11, sphereInBoxLocalMatrix._12, sphereInBoxLocalMatrix._13).Length(),
		Vec3(sphereInBoxLocalMatrix._21, sphereInBoxLocalMatrix._22, sphereInBoxLocalMatrix._23).Length(),
		Vec3(sphereInBoxLocalMatrix._31, sphereInBoxLocalMatrix._32, sphereInBoxLocalMatrix._33).Length());

	// ¹Ú½ºÀÇ Àı¹İ Å©±â
	Vec3 boxHalfSize = box->GetSize() * 0.5f;
	// ±¸ÀÇ ¹Ú½ºÁÂÇ¥Ãà ±âÁØ ¹İÁö¸§
	float sphereRadius = sphere->GetRadius() * min(sphereInBoxLocalScale.x, min(sphereInBoxLocalScale.y, sphereInBoxLocalScale.z))
		* max(boxTr->GetScale().x, max(boxTr->GetScale().y, boxTr->GetScale().z));
	// ±¸ÀÇ ¿ùµåÁÂÇ¥ ±âÁØ ¹İÁö¸§
	float sphereOriginal = sphere->GetRadius() * sphereTr->GetScale().x;

	/* ±¸ÀÇ Áß½É°ú °¡Àå °¡±î¿î Á÷À°¸éÃ¼ À§ÀÇ Á¡À» Ã£´Â´Ù */
	Vector3 closestPoint;
	/* x Ãà ¼ººĞ ºñ±³ */
	if (sphereInBoxLocalPosition.x > boxHalfSize.x)
		closestPoint.x = boxHalfSize.x;
	else if (sphereInBoxLocalPosition.x < -boxHalfSize.x)
		closestPoint.x = -boxHalfSize.x;
	else
		closestPoint.x = sphereInBoxLocalPosition.x;

	/* y Ãà ¼ººĞ ºñ±³ */
	if (sphereInBoxLocalPosition.y > boxHalfSize.y)
		closestPoint.y = boxHalfSize.y;
	else if (sphereInBoxLocalPosition.y < -boxHalfSize.y)
		closestPoint.y = -boxHalfSize.y;
	else
		closestPoint.y = sphereInBoxLocalPosition.y;

	/* z Ãà ¼ººĞ ºñ±³ */
	if (sphereInBoxLocalPosition.z > boxHalfSize.z)
		closestPoint.z = boxHalfSize.z;
	else if (sphereInBoxLocalPosition.z < -boxHalfSize.z)
		closestPoint.z = -boxHalfSize.z;
	else
		closestPoint.z = sphereInBoxLocalPosition.z;

	/* ±¸¿¡ °¡Àå °¡±î¿î Á÷À°¸éÃ¼ À§ÀÇ Á¡À» ¿ùµå ÁÂÇ¥°è·Î º¯È¯ÇÑ´Ù */
	Vec3 closestPointWorld = Vec3::Transform(closestPoint + box->GetCenter(), boxTr->GetWorldMatrix()); 

	/* À§ÀÇ °á°ú¿Í ±¸ÀÇ Áß½É »çÀÌÀÇ °Å¸®°¡ ±¸ÀÇ ¹İÁö¸§º¸´Ù ÀÛ´Ù¸é Ãæµ¹ÀÌ ¹ß»ıÇÑ °ÍÀÌ´Ù */
	// Á¤±³ÇÑ °è»êÀ» À§ÇØ ¿ùµåÁÂÇ¥¸¦ »ç¿ëÇÕ´Ï´Ù.
	float distanceSquared = (closestPointWorld - sphereCenterWorld).LengthSquared(); 
	if (distanceSquared < sphereRadius * sphereRadius) 
	{
		/* Ãæµ¹ Á¤º¸¸¦ »ı¼ºÇÑ´Ù */
		Contact* newContact = new Contact;
		newContact->bodies[0] = sphereRb;
		newContact->bodies[1] = boxRb;
		newContact->normal = sphereCenterWorld - closestPointWorld;
		newContact->normal.Normalize();
		newContact->contactPoint[0] = Vec3(sphereCenterWorld - newContact->normal * sphereOriginal);
		newContact->contactPoint[1] = Vec3(closestPointWorld);
		newContact->penetration = sphereRadius - sqrtf(distanceSquared); 
		newContact->restitution = objectRestitution;
		newContact->friction = friction;
		newContact->normalImpulseSum = 0.0f;
		newContact->tangentImpulseSum1 = 0.0f;
		newContact->tangentImpulseSum2 = 0.0f;

		contacts.push_back(newContact);
		return true;
	}
	else
		return false;
}

bool CollisionDetector::CheckBoxBoxCollision(std::vector<Contact*>& contacts, BoxCollider* box1, BoxCollider* box2)
{
	Transform* box1Tr = box1->GetGameObject()->GetTransform();
	Transform* box2Tr = box2->GetGameObject()->GetTransform();

	RigidBody* box1Rb = box1->GetGameObject()->GetComponent<RigidBody>();
	RigidBody* box2Rb = box2->GetGameObject()->GetComponent<RigidBody>();

	/* °ãÄ§ °Ë»çÀÇ ±âÁØÀÌ µÉ Ãà ÀúÀå */
	vector<Vector3> axes;

	/* box1 ÀÇ ¼¼ Ãà ÀúÀå */
	for (int i = 0; i < 3; ++i)
		axes.push_back(box1Tr->GetAxis(i));

	/* box2 ÀÇ ¼¼ Ãà ÀúÀå */
	for (int i = 0; i < 3; ++i)
		axes.push_back(box2Tr->GetAxis(i));

	/* °¢ Ãà »çÀÌÀÇ ¿ÜÀû ÀúÀå */
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			Vector3 crossProduct = axes[i].Cross(axes[3 + j]);
			crossProduct.Normalize();
			axes.push_back(crossProduct);
		}
	}

	float minPenetration = FLT_MAX;
	int minAxisIdx = 0;

	/* ¸ğµç Ãà¿¡ ´ëÇØ °ãÄ§ °Ë»ç */
	for (int i = 0; i < axes.size(); ++i)
	{
		float penetration = calcPenetration(box1, box2, axes[i]);

		/* ÇÑ ÃàÀÌ¶óµµ °ãÄ¡Áö ¾ÊÀ¸¸é Ãæµ¹ÀÌ ¹ß»ıÇÏÁö ¾ÊÀº °ÍÀÌ´Ù */
		if (penetration <= 0.0f)
			return false;

		/* °¡Àå Àû°Ô °ãÄ¡´Â Á¤µµ¿Í ±×¶§ÀÇ ±âÁØ ÃàÀ» ÃßÀûÇÑ´Ù */
		if (penetration <= minPenetration)
		{
			minPenetration = penetration;
			minAxisIdx = i;
		}
	}

	/* ¸ğµç Ãà¿¡ °ÉÃÄ °ãÄ§ÀÌ °¨ÁöµÆ´Ù¸é Ãæµ¹ÀÌ ¹ß»ıÇÑ °ÍÀÌ´Ù */
	Contact* newContact = new Contact;
	newContact->bodies[0] = box1Rb;
	newContact->bodies[1] = box2Rb;
	newContact->penetration = minPenetration;
	newContact->restitution = objectRestitution;
	newContact->friction = friction;
	newContact->normalImpulseSum = 0.0f;
	newContact->tangentImpulseSum1 = 0.0f;
	newContact->tangentImpulseSum2 = 0.0f;

	/* Ãæµ¹ ¹ı¼±À» ¹æÇâ¿¡ À¯ÀÇÇÏ¿© ¼³Á¤ÇÑ´Ù */
	Vector3 centerToCenter = box2Tr->GetPosition() - box1Tr->GetPosition();
	if (axes[minAxisIdx].Dot(centerToCenter) > 0)
		newContact->normal = axes[minAxisIdx] * -1.0f;
	else
		newContact->normal = axes[minAxisIdx];

	/* Ãæµ¹ ÁöÁ¡À» Ã£´Â´Ù */
	if (minAxisIdx < 6) // ¸é-Á¡ Á¢ÃËÀÏ ¶§
	{
		calcContactPointOnPlane(box1, box2, minAxisIdx, newContact);
	}
	else // ¼±-¼± Á¢ÃËÀÏ ¶§
	{
		calcContactPointOnLine(box1, box2, minAxisIdx, newContact);
	}

	contacts.push_back(newContact);
	return true;
}




float CollisionDetector::calcPenetration(BoxCollider* box1, BoxCollider* box2, const Vector3& axis)
{
	Transform* box1Tr = box1->GetGameObject()->GetTransform(); 
	Transform* box2Tr = box2->GetGameObject()->GetTransform();

	Vec3 box1HalfSize = box1->GetSize() * box1Tr->GetScale() / 2;
	Vec3 box2HalfSize = box2->GetSize() * box2Tr->GetScale() / 2;

	/* µÎ ¹Ú½ºÀÇ Áß½É °£ °Å¸®¸¦ °è»êÇÑ´Ù */
	Vector3 centerToCenter = box2Tr->GetPosition() - box1Tr->GetPosition();
	float projectedCenterToCenter = abs(centerToCenter.Dot(axis));

	/* µÎ ¹Ú½º¸¦ ÁÖ¾îÁø Ãà¿¡ »ç¿µ½ÃÅ² ±æÀÌÀÇ ÇÕÀ» °è»êÇÑ´Ù */
	float projectedSum =
		  abs((box1Tr->GetAxis(0) * box1HalfSize.x).Dot(axis)) 
		+ abs((box1Tr->GetAxis(1) * box1HalfSize.y).Dot(axis)) 
		+ abs((box1Tr->GetAxis(2) * box1HalfSize.z).Dot(axis))
		+ abs((box2Tr->GetAxis(0) * box2HalfSize.x).Dot(axis))
		+ abs((box2Tr->GetAxis(1) * box2HalfSize.y).Dot(axis))
		+ abs((box2Tr->GetAxis(2) * box2HalfSize.z).Dot(axis));

	/* "»ç¿µ½ÃÅ² ±æÀÌÀÇ ÇÕ - Áß½É °£ °Å¸®" °¡ °ãÄ£ Á¤µµÀÌ´Ù */
	return projectedSum - projectedCenterToCenter;
}



// Fixed
// ¸é-Á¡ Á¢ÃËÀÏ ¶§
void CollisionDetector::calcContactPointOnPlane(
	BoxCollider* box1,
	BoxCollider* box2,
	int minAxisIdx,
	Contact* contact
)
{
	Transform* box1Tr = box1->GetGameObject()->GetTransform(); 
	Transform* box2Tr = box2->GetGameObject()->GetTransform(); 

	Vec3 box1HalfSize = box1->GetSize() * 0.5f; 
	Vec3 box2HalfSize = box2->GetSize() * 0.5f;

	/* Ãæµ¹ Á¤Á¡ */
	Vec3* contactPoint1;
	Vec3* contactPoint2;

	if (minAxisIdx < 3) // Ãæµ¹¸éÀÌ box1 ÀÇ ¸éÀÏ ¶§
	{
		contactPoint2 = new Vec3(box2HalfSize.x, box2HalfSize.y, box2HalfSize.z);

		if (box2Tr->GetAxis(0).Dot(contact->normal) < 0)
			contactPoint2->x *= -1.0f;
		if (box2Tr->GetAxis(1).Dot(contact->normal) < 0)
			contactPoint2->y *= -1.0f;
		if (box2Tr->GetAxis(2).Dot(contact->normal) < 0)
			contactPoint2->z *= -1.0f;

		/* ¿ùµå ÁÂÇ¥·Î º¯È¯ÇÑ´Ù */
		*contactPoint2 = Vec3::Transform(*contactPoint2, box2Tr->GetWorldMatrix());
		contactPoint1 = new Vec3(*contactPoint2 - contact->normal * contact->penetration);
	}
	else // Ãæµ¹¸éÀÌ box2 ÀÇ ¸éÀÏ ¶§
	{
		contactPoint1 = new Vec3(box1HalfSize.x, box1HalfSize.y, box1HalfSize.z);

		if (box1Tr->GetAxis(0).Dot(contact->normal) > 0)
			contactPoint1->x *= -1.0f;
		if (box1Tr->GetAxis(1).Dot(contact->normal) > 0)
			contactPoint1->y *= -1.0f;
		if (box1Tr->GetAxis(2).Dot(contact->normal) > 0)
			contactPoint1->z *= -1.0f;

		/* ¿ùµå ÁÂÇ¥·Î º¯È¯ÇÑ´Ù */
		*contactPoint1 = Vec3::Transform(*contactPoint1, box1Tr->GetWorldMatrix()); 
		contactPoint2 = new Vec3(*contactPoint1 - contact->normal * contact->penetration);
	}

	contact->contactPoint[0] = *contactPoint1;
	contact->contactPoint[1] = *contactPoint2;
}




void CollisionDetector::calcContactPointOnLine(
	BoxCollider* box1,
	BoxCollider* box2,
	int minAxisIdx,
	Contact* contact
)
{
	Transform* box1Tr = box1->GetGameObject()->GetTransform(); 
	Transform* box2Tr = box2->GetGameObject()->GetTransform(); 

	Vec3 box1HalfSize = box1->GetSize() * 0.5f; 
	Vec3 box2HalfSize = box2->GetSize() * 0.5f; 

	/* Á¢ÃËÇÑ º¯ À§ÀÇ Á¤Á¡À» Ã£´Â´Ù */
	Vec3 vertexOne(box1HalfSize.x, box1HalfSize.y, box1HalfSize.z);
	Vec3 vertexTwo(box2HalfSize.x, box2HalfSize.y, box2HalfSize.z);

	if (box1Tr->GetAxis(0).Dot(contact->normal) > 0)
		vertexOne.x *= -1.0f;
	if (box1Tr->GetAxis(1).Dot(contact->normal) > 0)
		vertexOne.y *= -1.0f;
	if (box1Tr->GetAxis(2).Dot(contact->normal) > 0)
		vertexOne.z *= -1.0f;

	if (box2Tr->GetAxis(0).Dot(contact->normal) < 0)
		vertexTwo.x *= -1.0f;
	if (box2Tr->GetAxis(1).Dot(contact->normal) < 0)
		vertexTwo.y *= -1.0f;
	if (box2Tr->GetAxis(2).Dot(contact->normal) < 0)
		vertexTwo.z *= -1.0f;

	/* º¯ÀÇ ¹æÇâÀ» Ã£´Â´Ù */
	Vector3 directionOne, directionTwo;

	switch (minAxisIdx)
	{
	case 6: // box1 ÀÇ x Ãà X box2 ÀÇ x Ãà
		directionOne = box1Tr->GetAxis(0);
		if (vertexOne.x > 0) directionOne *= -1.0f;
		directionTwo = box2Tr->GetAxis(0);
		if (vertexTwo.x > 0) directionTwo *= -1.0f;
		break;

	case 7: // box1 ÀÇ x Ãà X box2 ÀÇ y Ãà
		directionOne = box1Tr->GetAxis(0);
		if (vertexOne.x > 0) directionOne *= -1.0f;
		directionTwo = box2Tr->GetAxis(1);
		if (vertexTwo.y > 0) directionTwo *= -1.0f;
		break;

	case 8: // box1 ÀÇ x Ãà X box2 ÀÇ z Ãà
		directionOne = box1Tr->GetAxis(0);
		if (vertexOne.x > 0) directionOne *= -1.0f;
		directionTwo = box2Tr->GetAxis(2);
		if (vertexTwo.z > 0) directionTwo *= -1.0f;
		break;

	case 9: // box1 ÀÇ y Ãà X box2 ÀÇ x Ãà
		directionOne = box1Tr->GetAxis(1);
		if (vertexOne.y > 0) directionOne *= -1.0f;
		directionTwo = box2Tr->GetAxis(0);
		if (vertexTwo.x > 0) directionTwo *= -1.0f;
		break;

	case 10: // box1 ÀÇ y Ãà X box2 ÀÇ y Ãà
		directionOne = box1Tr->GetAxis(1);
		if (vertexOne.y > 0) directionOne *= -1.0f;
		directionTwo = box2Tr->GetAxis(1);
		if (vertexTwo.y > 0) directionTwo *= -1.0f;
		break;

	case 11: // box1 ÀÇ y Ãà X box2 ÀÇ z Ãà
		directionOne = box1Tr->GetAxis(1);
		if (vertexOne.y > 0) directionOne *= -1.0f;
		directionTwo = box2Tr->GetAxis(2);
		if (vertexTwo.z > 0) directionTwo *= -1.0f;
		break;

	case 12: // box1 ÀÇ z Ãà X box2 ÀÇ x Ãà
		directionOne = box1Tr->GetAxis(2);
		if (vertexOne.z > 0) directionOne *= -1.0f;
		directionTwo = box2Tr->GetAxis(0);
		if (vertexTwo.x > 0) directionTwo *= -1.0f;
		break;

	case 13: // box1 ÀÇ z Ãà X box2 ÀÇ y Ãà
		directionOne = box1Tr->GetAxis(2);
		if (vertexOne.z > 0) directionOne *= -1.0f;
		directionTwo = box2Tr->GetAxis(1);
		if (vertexTwo.y > 0) directionTwo *= -1.0f;
		break;

	case 14: // box1 ÀÇ z Ãà X box2 ÀÇ z Ãà
		directionOne = box1Tr->GetAxis(2);
		if (vertexOne.z > 0) directionOne *= -1.0f;
		directionTwo = box2Tr->GetAxis(2);
		if (vertexTwo.z > 0) directionTwo *= -1.0f;
		break;

	default:
		break;
	}

	/* Á¤Á¡À» ¿ùµå ÁÂÇ¥°è·Î º¯È¯ÇÑ´Ù */
	vertexOne = Vec3::Transform(vertexOne, box1Tr->GetWorldMatrix());
	vertexTwo = Vec3::Transform(vertexTwo, box2Tr->GetWorldMatrix());

	/* box2 ÀÇ º¯°ú °¡Àå °¡±î¿î box1 À§ÀÇ Á¡À» Ã£´Â´Ù */
	float k = directionOne.Dot(directionTwo);
	Vector3* closestPointOne = new Vector3(vertexOne + directionOne * ((vertexTwo - vertexOne).Dot(directionOne - directionTwo * k) / (1 - k * k)));
	/* box1 ÀÇ º¯°ú °¡Àå °¡±î¿î box2 À§ÀÇ Á¡À» Ã£´Â´Ù */
	Vector3* closestPointTwo = new Vector3(vertexTwo + directionTwo * ((*closestPointOne - vertexTwo).Dot(directionTwo)));

	contact->contactPoint[0] = *closestPointOne;
	contact->contactPoint[1] = *closestPointTwo;
}