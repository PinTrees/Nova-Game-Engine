#pragma once

class GameObject;
class CollisionDetector;
class CollisionResolver;

union COLLIDER_ID
{
	struct
	{
		UINT Left_id;
		UINT RIght_id;
	};

	ULONGLONG ID;
};

struct Contact
{
	RigidBody* bodies[2];		// Pointers to the involved rigid bodies
	Vec3 normal;				// Normal at the point of contact
	Vec3 contactPoint[2];		// Contact points on each body
	float penetration;			// Depth of penetration
	float restitution;			// Restitution coefficient
	float friction;				// Friction coefficient
	float normalImpulseSum;		// Accumulated normal impulse
	float tangentImpulseSum1;	// Accumulated tangential impulse for the first tangent
	float tangentImpulseSum2;	// Accumulated tangential impulse for the second tangent
};

struct ContactInfo
{
	float pointX;
	float pointY;
	float pointZ;
	float normalX;
	float normalY;
	float normalZ;
};

class PhysicsManager
{
	SINGLE_HEADER(PhysicsManager)

private:
	float m_GravityAcceleration;
	map<ULONGLONG, bool>	mMapColInfo;	// 충돌체 간의 이전 프레임 충돌 정보 

	std::unordered_map<unsigned int, RigidBody*> m_RigidBodies;
	std::unordered_map<unsigned int, Collider*> m_Colliders;
	std::vector<Contact*> m_Contacts;

	CollisionDetector* m_Detector; 
	CollisionResolver* m_Resolver;

public:
	void SetGravity(float gravity) { m_GravityAcceleration = gravity; }
	float GetGravity() const { return m_GravityAcceleration; }

public:
	void Init();
	void Start();
	void Update(float deltaTime);
	void Exit();

	// Editor Only
public:
	void DebugRender();

private:
	void GetContactInfo(std::vector<ContactInfo*>& contactInfo) const;
};

