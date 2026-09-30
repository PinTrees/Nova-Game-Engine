#pragma once

class CollisionResolver
{
private:
	//float penetrationTolerance = 0.0005f;
	//float closingSpeedTolerance = 0.005f;
	float penetrationTolerance = 0.0005f;
	float closingSpeedTolerance = 0.005f;

public:
	void ResolveCollision(std::vector<Contact*>& contacts, float deltaTime); 

private:
	void AA(Contact* contact, float deltaTime);
	void sequentialImpulse(Contact* contact, float deltaTime);
};

