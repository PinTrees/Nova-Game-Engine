#pragma once

class CollisionDetector
{
private:
    float friction;
    float objectRestitution;
    float groundRestitution;

public:
    CollisionDetector();
      
public:
    void DetectCollision(std::vector<Contact*>& contacts, std::unordered_map<unsigned int, Collider*>& colliders);

private:
    bool CheckSphereSphereCollision(std::vector<Contact*>& contacts, SphereCollider* sphere1, SphereCollider* sphere2);
    bool CheckSphereBoxCollision(std::vector<Contact*>& contacts, SphereCollider* sphere, BoxCollider* box);
    bool CheckBoxBoxCollision(std::vector<Contact*>& contacts, BoxCollider* box1, BoxCollider* box2);

    /* 선이 도형을 통과하는지 검사한다
        카메라로부터 hit point 까지의 거리를 반환한다
        hit 하지 않는다면 음수를 반환한다 */
    float rayAndSphere(
        const Vector3& origin,
        const Vector3& direction,
        const SphereCollider&
    );

    float rayAndBox(
        const Vector3& origin,
        const Vector3& direction,
        const BoxCollider&
    );

private:
    /* 두 박스가 주어진 축에 대해 어느정도 겹치는지 반환한다 */
    float calcPenetration(
        BoxCollider* box1,
        BoxCollider* box2,
        const Vector3& axis
    );

    /* 직육면체의 면-점 접촉일 때 충돌점을 찾는다 */
    void calcContactPointOnPlane(
        BoxCollider*box1,
        BoxCollider*box2,
        int minPenetrationAxisIdx,
        Contact* contact
    );

    /* 직육면체의 선-선 접촉일 때 충돌점을 찾는다 */
    void calcContactPointOnLine(
        BoxCollider* box1,
        BoxCollider* box2,
        int minPenetrationAxisIdx,
        Contact* contact
    );
};

