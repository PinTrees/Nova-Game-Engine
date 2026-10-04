#pragma once
#include "Component.h"
#include <box2d/box2d.h>

class Scene;

// connectedBody ID 는 3D Joint 처럼 GameObject 를 가리킨다
// Box2D Joint 는 현재 Physics2D 월드가 가지며 저장하지 않는다
class NOVA_API Joint2D : public Component
{
public:
    enum Kind { Hinge, Spring, DistanceJoint, Wheel, Fixed, Slider };
    virtual Kind JointKind() const = 0;
    uint64 ConnectedBody = 0;
    bool EnableCollision = false;
    float BreakForce = std::numeric_limits<float>::infinity();
    float BreakTorque = std::numeric_limits<float>::infinity();
    Vec2 Anchor = Vec2(0, 0), ConnectedAnchor = Vec2(0, 0);
    bool AutoConfigureConnectedAnchor = true;

    bool UseMotor = false, UseLimits = false;
    float MotorSpeed = 0, MaxMotorForce = 10000;
    float LowerLimit = 0, UpperLimit = 0;
    bool AutoConfigureDistance = true, MaxDistanceOnly = false;
    float Distance = 1, DampingRatio = 0, Frequency = 1;
    float Angle = 0;
    bool AutoConfigureAngle = true;

    // 읽기 전용 시뮬레이션 결과 — 끊어져 지워지기 전 콜백에서도 읽을 수 있다
    bool Broken = false;
    Vec2 ReactionForce = Vec2(0, 0);
    float ReactionTorque = 0, JointAngle = 0, JointSpeed = 0, JointTranslation = 0, MotorForce = 0;

    json toJson() const override;
    void fromJson(const json& j) override;
    void OnInspectorGUI() override;
    void OnDrawGizmos() override;
    void OnDestroy() override;
    void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;
    bool UsesUnityInspector() const override { return true; }
    const char* InspectorIconName() const override { return "rigidbody"; }
};

class NOVA_API AnchoredJoint2D : public Joint2D {};

#define NOVA_JOINT2D_CLASS(Type, KindValue, Title, Defaults) \
class NOVA_API Type : public AnchoredJoint2D { \
public: Type() { m_InspectorTitleName = Title; Defaults } \
    Kind JointKind() const override { return KindValue; } \
    std::string GetType() const override { return #Type; } \
    static std::shared_ptr<Component> CreateInstance() { return std::make_shared<Type>(); } \
    friend void to_json(json& j, const Type& obj) { j = obj.toJson(); } \
    friend void from_json(const json& j, Type& obj) { obj.fromJson(j); } \
private: static const bool registered; \
}; \
REGISTER_COMPONENT(Type)

NOVA_JOINT2D_CLASS(HingeJoint2D, Hinge, "Hinge Joint 2D", )
NOVA_JOINT2D_CLASS(SpringJoint2D, Spring, "Spring Joint 2D", AutoConfigureConnectedAnchor = false;)
NOVA_JOINT2D_CLASS(DistanceJoint2D, DistanceJoint, "Distance Joint 2D", AutoConfigureConnectedAnchor = false;)
NOVA_JOINT2D_CLASS(WheelJoint2D, Wheel, "Wheel Joint 2D", Angle = 90; Frequency = 2; DampingRatio = 0.7f;)
NOVA_JOINT2D_CLASS(FixedJoint2D, Fixed, "Fixed Joint 2D", Frequency = 0; DampingRatio = 1;)
NOVA_JOINT2D_CLASS(SliderJoint2D, Slider, "Slider Joint 2D", )
#undef NOVA_JOINT2D_CLASS

namespace Physics2DJointsRuntime
{
    void Sync(Scene* scene, b2WorldId world);
    void AfterStep();
    void Remove(Joint2D* joint);
    void Reset();
}
