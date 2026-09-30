#include "pch.h"
#include "CollisionResolver.h"

void CollisionResolver::ResolveCollision(std::vector<Contact*>& contacts, float deltaTime)
{
    for (int i = 0; i < 1; ++i)
    {
        for (auto& contact : contacts)
        {
            //sequentialImpulse(contact, deltaTime); 
            AA(contact, deltaTime);
        }
    }
}

void CollisionResolver::AA(Contact* contact, float deltaTime)
{
    /* 두 물체의 역질량의 합을 계산한다 */
    float totalInverseMass = contact->bodies[0]->GetInverseMass();
    if (contact->bodies[1] != nullptr) // 오브젝트-지면 충돌이면 bodies[1] 은 nullptr
        totalInverseMass += contact->bodies[1]->GetInverseMass();

    /* 역질량의 합이 0 이면
        두 물체가 무한의 질량을 가지는 것이므로 함수를 종료한다 */
    if (totalInverseMass == 0.0f)
        return;

    float penetrationPerInverseMass = contact->penetration / totalInverseMass;

    /* 두 물체의 위치를 질량에 반비례하여 조정한다 */
    Vector3 deltaPosition = contact->normal * penetrationPerInverseMass;
    contact->bodies[0]->GetGameObject()->GetTransform()->Translate(deltaPosition * contact->bodies[0]->GetInverseMass());
    if (contact->bodies[1] != nullptr)
    {
        contact->bodies[1]->GetGameObject()->GetTransform()->Translate(-deltaPosition * contact->bodies[1]->GetInverseMass());
    }


    Vector3 contactPointFromCenter1 = contact->contactPoint[0] - contact->bodies[0]->GetGameObject()->GetTransform()->GetPosition();
    Vector3 contactPointFromCenter2;
    if (contact->bodies[1] != nullptr)
        contactPointFromCenter2 = contact->contactPoint[1] - contact->bodies[1]->GetGameObject()->GetTransform()->GetPosition();

    /* 두 물체의 충돌점의 속도를 계산한다 */
    Vector3 contactPointVelocity1 = contact->bodies[0]->GetVelocity() +
        contact->bodies[0]->GetRotationVelocity().Cross(contactPointFromCenter1);

    Vector3 contactPointVelocity2;
    if (contact->bodies[1] != nullptr)
    {
        contactPointVelocity2 = contact->bodies[1]->GetVelocity() +
            contact->bodies[1]->GetRotationVelocity().Cross(contactPointFromCenter2);
    }

    /* 상대 속도를 계산한다 */
    Vector3 relativeVelocity = contactPointVelocity2 - contactPointVelocity1;

    /* 충격량의 크기를 계산한다 */
    Vector3 contactNormal = contact->normal * -1.0f;
    float numerator = (relativeVelocity * (1.0f + contact->restitution)).Dot(contactNormal) * -1.0f;

    Vector3 termInDenominator1 = (contact->bodies[0]->GetInverseInertiaTensorWorld() * (contactPointFromCenter1.Cross(contactNormal)))
        .Cross(contactPointFromCenter1);
    Vector3 termInDenominator2;
    if (contact->bodies[1] != nullptr) {
        termInDenominator2 =
            (contact->bodies[1]->GetInverseInertiaTensorWorld() * (contactPointFromCenter2.Cross(contactNormal)))
            .Cross(contactPointFromCenter2);
    }
    float denominator = totalInverseMass + (termInDenominator1 + termInDenominator2).Dot(contactNormal);
    if (denominator == 0.0f)
        return;

    float impulse = numerator / denominator;
    if (impulse == 0.0f)
        return;
    Vector3 impulseVector = contactNormal * impulse;

    /* 두 물체의 선속도를 갱신한다 */
    contact->bodies[0]->SetVelocity(
        contact->bodies[0]->GetVelocity() - impulseVector * contact->bodies[0]->GetInverseMass()
    );

    if (contact->bodies[1] != nullptr)
    {
        contact->bodies[1]->SetVelocity(
            contact->bodies[1]->GetVelocity() + impulseVector * contact->bodies[1]->GetInverseMass()
        );
    }

    /* 두 물체의 각속도를 갱신한다 */
    Vector3 contactPointFromCenter = contact->contactPoint[0] - contact->bodies[0]->GetGameObject()->GetTransform()->GetPosition();
    contact->bodies[0]->SetRotationVelocity(
        contact->bodies[0]->GetRotationVelocity() - contact->bodies[0]->GetInverseInertiaTensorWorld()
        * (contactPointFromCenter.Cross(impulseVector))
    );

    if (contact->bodies[1] != nullptr)
    {
        contactPointFromCenter = contact->contactPoint[1] - contact->bodies[1]->GetGameObject()->GetTransform()->GetPosition();
        contact->bodies[1]->SetRotationVelocity(
            contact->bodies[1]->GetRotationVelocity() + contact->bodies[1]->GetInverseInertiaTensorWorld()
            * (contactPointFromCenter.Cross(impulseVector))
        );
    }
}

void CollisionResolver::sequentialImpulse(Contact* contact, float deltaTime)
{
    float effectiveMass;

    float totalInvMass = contact->bodies[0]->GetInverseMass();
    if (contact->bodies[1] != nullptr)
        totalInvMass += contact->bodies[1]->GetInverseMass();
    if (totalInvMass == 0.0f)
        return;

    Vector3 contactPointFromCenter1 = contact->contactPoint[0] - contact->bodies[0]->GetGameObject()->GetTransform()->GetPosition();
    Vector3 contactPointFromCenter2;
    if (contact->bodies[1] != nullptr)
        contactPointFromCenter2 = contact->contactPoint[1] - contact->bodies[1]->GetGameObject()->GetTransform()->GetPosition();

    Vector3 termInDenominator1 = (contact->bodies[0]->GetInverseInertiaTensorWorld() * (contactPointFromCenter1.Cross(contact->normal)))
        .Cross(contactPointFromCenter1);
    Vector3 termInDenominator2;
    if (contact->bodies[1] != nullptr) {
        termInDenominator2 =
            (contact->bodies[1]->GetInverseInertiaTensorWorld() * (contactPointFromCenter2.Cross(contact->normal)))
            .Cross(contactPointFromCenter2);
    }
    effectiveMass = totalInvMass + (termInDenominator1 + termInDenominator2).Dot(contact->normal);
    if (effectiveMass == 0.0f)
        return;

    float closingSpeed = contact->normal.Dot(contact->bodies[0]->GetVelocity())
        + contact->bodies[0]->GetRotationVelocity().Dot(contactPointFromCenter1.Cross(contact->normal));
    if (contact->bodies[1] != nullptr)
    {
        closingSpeed -= contact->normal.Dot(contact->bodies[1]->GetVelocity())
            + contact->bodies[1]->GetRotationVelocity().Dot(contactPointFromCenter2.Cross(contact->normal));
    }

    /* bias 계산 */
    float bias = 0.0f;
    /* Baumgarte Stabilization */
    float baumgarte = 0.0f;
    if (contact->penetration > penetrationTolerance)
        baumgarte = (-0.1f / deltaTime) * (contact->penetration - penetrationTolerance);
    bias += baumgarte;

    /* bias 에 restitution term 추가 */
    float restitutionTerm = 0.0f;
    if (closingSpeed > closingSpeedTolerance)
        restitutionTerm = contact->restitution * (closingSpeed - closingSpeedTolerance);
    bias -= restitutionTerm;

    float impulse = -(closingSpeed + bias) / effectiveMass;
    if (isnan(impulse) != 0)
    {
        std::cout << "ERROR::CollisionResolver::sequentialImpulse()::impulse is nan" << std::endl;
        return;
    }

    /* 충격량의 누적값을 clamp */
    float prevImpulseSum = contact->normalImpulseSum;
    contact->normalImpulseSum += impulse;
    if (contact->normalImpulseSum < 0.0f)
        contact->normalImpulseSum = 0.0f;
    impulse = contact->normalImpulseSum - prevImpulseSum;

    /* 속도 & 각속도 변화 */
    Vector3 linearImpulse = contact->normal * impulse;
    Vector3 angularImpulse1 = contactPointFromCenter1.Cross(contact->normal) * impulse;
    Vector3 angularImpulse2 = contactPointFromCenter2.Cross(contact->normal) * impulse;

    contact->bodies[0]->SetVelocity(
        contact->bodies[0]->GetVelocity() + linearImpulse * contact->bodies[0]->GetInverseMass()
    );
    contact->bodies[0]->SetRotationVelocity(
        contact->bodies[0]->GetRotationVelocity() + contact->bodies[0]->GetInverseInertiaTensorWorld() * angularImpulse1
    );
    if (contact->bodies[1] != nullptr)
    {
        contact->bodies[1]->SetVelocity(
            contact->bodies[1]->GetVelocity() - linearImpulse * contact->bodies[1]->GetInverseMass()
        );
        contact->bodies[1]->SetRotationVelocity(
            contact->bodies[1]->GetRotationVelocity() - contact->bodies[1]->GetInverseInertiaTensorWorld() * angularImpulse2
        );
    }

    /* 충돌 법선에 수직하는 벡터 찾기(erin catto 방법) */
    Vector3 tangent1, tangent2;
    if (contact->normal.x >= 0.57735f)
        tangent1 = Vector3(contact->normal.y, -contact->normal.x, 0.0f);
    else
        tangent1 = Vector3(0.0f, contact->normal.z, -contact->normal.y);
    tangent2 = contact->normal.Cross(tangent1);

    /* tangent1 벡터에 대한 마찰 계산 */
    termInDenominator1 = (contact->bodies[0]->GetInverseInertiaTensorWorld() * (contactPointFromCenter1.Cross(tangent1)))
        .Cross(contactPointFromCenter1);
    if (contact->bodies[1] != nullptr) {
        termInDenominator2 =
            (contact->bodies[1]->GetInverseInertiaTensorWorld() * (contactPointFromCenter2.Cross(tangent1)))
            .Cross(contactPointFromCenter2);
    }
    effectiveMass = totalInvMass + (termInDenominator1 + termInDenominator2).Dot(tangent1);

    closingSpeed = tangent1.Dot(contact->bodies[0]->GetVelocity())
        + contact->bodies[0]->GetRotationVelocity().Dot(contactPointFromCenter1.Cross(tangent1));
    if (contact->bodies[1] != nullptr)
    {
        closingSpeed -= tangent1.Dot(contact->bodies[1]->GetVelocity())
            + contact->bodies[1]->GetRotationVelocity().Dot(contactPointFromCenter2.Cross(tangent1));
    }
    impulse = -closingSpeed / effectiveMass;
    if (isnan(impulse) != 0)
    {
        std::cout << "ERROR::CollisionResolver::sequentialImpulse()::tangential impulse1 is nan" << std::endl;
        return;
    }

    /* 충격량의 누적값을 clamp */
    prevImpulseSum = contact->tangentImpulseSum1;
    contact->tangentImpulseSum1 += impulse;
    if (contact->tangentImpulseSum1 < (-contact->friction * contact->normalImpulseSum))
        contact->tangentImpulseSum1 = -contact->friction * contact->normalImpulseSum;
    else if (contact->tangentImpulseSum1 > (contact->friction * contact->normalImpulseSum))
        contact->tangentImpulseSum1 = contact->friction * contact->normalImpulseSum;
    impulse = contact->tangentImpulseSum1 - prevImpulseSum;

    /* 속도 & 각속도 변화 */
    linearImpulse = tangent1 * impulse;
    angularImpulse1 = contactPointFromCenter1.Cross(tangent1) * impulse;
    angularImpulse2 = contactPointFromCenter2.Cross(tangent1) * impulse;

    contact->bodies[0]->SetVelocity(
        contact->bodies[0]->GetVelocity() + linearImpulse * contact->bodies[0]->GetInverseMass()
    );
    contact->bodies[0]->SetRotationVelocity(
        contact->bodies[0]->GetRotationVelocity() + contact->bodies[0]->GetInverseInertiaTensorWorld() * angularImpulse1
    );
    if (contact->bodies[1] != nullptr)
    {
        contact->bodies[1]->SetVelocity(
            contact->bodies[1]->GetVelocity() - linearImpulse * contact->bodies[1]->GetInverseMass()
        );
        contact->bodies[1]->SetRotationVelocity(
            contact->bodies[1]->GetRotationVelocity() - contact->bodies[1]->GetInverseInertiaTensorWorld() * angularImpulse2
        );
    }

    /* tangent2 벡터에 대한 마찰 계산 */
    termInDenominator1 = (contact->bodies[0]->GetInverseInertiaTensorWorld() * (contactPointFromCenter1.Cross(tangent2)))
        .Cross(contactPointFromCenter1);
    if (contact->bodies[1] != nullptr) {
        termInDenominator2 =
            (contact->bodies[1]->GetInverseInertiaTensorWorld() * (contactPointFromCenter2.Cross(tangent2)))
            .Cross(contactPointFromCenter2);
    }
    effectiveMass = totalInvMass + (termInDenominator1 + termInDenominator2).Dot(tangent2);

    closingSpeed = tangent2.Dot(contact->bodies[0]->GetVelocity())
        + contact->bodies[0]->GetRotationVelocity().Dot(contactPointFromCenter1.Cross(tangent2));
    if (contact->bodies[1] != nullptr)
    {
        closingSpeed -= tangent2.Dot(contact->bodies[1]->GetVelocity())
            + contact->bodies[1]->GetRotationVelocity().Dot(contactPointFromCenter2.Cross(tangent2));
    }
    impulse = -closingSpeed / effectiveMass;
    if (isnan(impulse) != 0)
    {
        std::cout << "ERROR::CollisionResolver::sequentialImpulse()::tangential impulse2 is nan" << std::endl;
        return;
    }

    /* 충격량의 누적값을 clamp */
    prevImpulseSum = contact->tangentImpulseSum2;
    contact->tangentImpulseSum2 += impulse;
    if (contact->tangentImpulseSum2 < (-contact->friction * contact->normalImpulseSum))
        contact->tangentImpulseSum2 = -contact->friction * contact->normalImpulseSum;
    else if (contact->tangentImpulseSum2 > (contact->friction * contact->normalImpulseSum))
        contact->tangentImpulseSum2 = contact->friction * contact->normalImpulseSum;
    impulse = contact->tangentImpulseSum2 - prevImpulseSum;

    /* 속도 & 각속도 변화 */
    linearImpulse = tangent2 * impulse;
    angularImpulse1 = contactPointFromCenter1.Cross(tangent2) * impulse;
    angularImpulse2 = contactPointFromCenter2.Cross(tangent2) * impulse;

    contact->bodies[0]->SetVelocity(
        contact->bodies[0]->GetVelocity() + linearImpulse * contact->bodies[0]->GetInverseMass()
    );
    contact->bodies[0]->SetRotationVelocity(
        contact->bodies[0]->GetRotationVelocity() + contact->bodies[0]->GetInverseInertiaTensorWorld() * angularImpulse1
    );
    if (contact->bodies[1] != nullptr)
    {
        contact->bodies[1]->SetVelocity(
            contact->bodies[1]->GetVelocity() - linearImpulse * contact->bodies[1]->GetInverseMass()
        );
        contact->bodies[1]->SetRotationVelocity(
            contact->bodies[1]->GetRotationVelocity() - contact->bodies[1]->GetInverseInertiaTensorWorld() * angularImpulse2
        );
    }
}
