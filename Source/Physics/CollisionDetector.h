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

    // ë‘ ì½œë¼ì´ë”ê°€ ê²¹ì³ ìˆëŠ”ì§€ë§Œ ê²€ì‚¬í•œë‹¤ (ì ‘ì´‰ ì •ë³´ë¥¼ ë§Œë“¤ì§€ ì•ŠìŒ). Trigger / Collision ì´ë²¤íŠ¸ íŒì •ì— ì‚¬ìš©.
    bool Overlaps(Collider* a, Collider* b);

private:
    bool CheckSphereSphereCollision(std::vector<Contact*>& contacts, SphereCollider* sphere1, SphereCollider* sphere2);
    bool CheckSphereBoxCollision(std::vector<Contact*>& contacts, SphereCollider* sphere, BoxCollider* box);
    bool CheckBoxBoxCollision(std::vector<Contact*>& contacts, BoxCollider* box1, BoxCollider* box2);

    /* ¼±ÀÌ µµÇüÀ» Åë°úÇÏ´ÂÁö °Ë»çÇÑ´Ù
        Ä«¸Ş¶ó·ÎºÎÅÍ hit point ±îÁöÀÇ °Å¸®¸¦ ¹İÈ¯ÇÑ´Ù
        hit ÇÏÁö ¾Ê´Â´Ù¸é À½¼ö¸¦ ¹İÈ¯ÇÑ´Ù */
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
    /* µÎ ¹Ú½º°¡ ÁÖ¾îÁø Ãà¿¡ ´ëÇØ ¾î´ÀÁ¤µµ °ãÄ¡´ÂÁö ¹İÈ¯ÇÑ´Ù */
    float calcPenetration(
        BoxCollider* box1,
        BoxCollider* box2,
        const Vector3& axis
    );

    /* Á÷À°¸éÃ¼ÀÇ ¸é-Á¡ Á¢ÃËÀÏ ¶§ Ãæµ¹Á¡À» Ã£´Â´Ù */
    void calcContactPointOnPlane(
        BoxCollider*box1,
        BoxCollider*box2,
        int minPenetrationAxisIdx,
        Contact* contact
    );

    /* Á÷À°¸éÃ¼ÀÇ ¼±-¼± Á¢ÃËÀÏ ¶§ Ãæµ¹Á¡À» Ã£´Â´Ù */
    void calcContactPointOnLine(
        BoxCollider* box1,
        BoxCollider* box2,
        int minPenetrationAxisIdx,
        Contact* contact
    );
};

