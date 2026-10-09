#include "pch.h"
#include "Physics2DJoints.h"
#include "ComponentIndex.h"
#include "Physics2DComponents.h"
#include "CSharpScript.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"

namespace
{
    constexpr float Deg = 3.14159265358979f / 180.0f;
    constexpr float MinLength = 0.005f; // Box2D 가 안정적으로 다루는 가장 짧은 거리
    json FloatOrInf(float v) { return std::isinf(v) ? json("Infinity") : json(v); }
    float ReadFloat(const json& j, const char* key, float fallback)
    {
        if (!j.contains(key)) return fallback;
        const auto& v = j.at(key);
        if (v.is_string() && v.get<std::string>() == "Infinity") return std::numeric_limits<float>::infinity();
        return v.is_number() ? v.get<float>() : fallback;
    }
    Vec2 ReadVec(const json& j, const char* key, Vec2 fallback)
    {
        return j.contains(key) && j.at(key).is_array() && j.at(key).size() == 2
            ? Vec2(j.at(key)[0].get<float>(), j.at(key)[1].get<float>()) : fallback;
    }
    float Safe(float v, float fallback = 0) { return std::isfinite(v) ? v : fallback; }
    float Nonnegative(float v) { return (std::max)(0.0f, Safe(v)); }
    b2Vec2 B(Vec2 v) { return { Safe(v.x), Safe(v.y) }; }
    Vec2 V(b2Vec2 v) { return Vec2(v.x, v.y); }
    Vec2 WorldAnchor(GameObject* go, Vec2 anchor)
    {
        if (!go) return anchor;
        const Vec3 p = Vec3::Transform(Vec3(anchor.x, anchor.y, 0), go->GetTransform()->GetWorldMatrix());
        return Vec2(p.x, p.y);
    }
    Vec2 LocalAnchor(GameObject* go, Vec2 point)
    {
        if (!go) return point;
        const Matrix inverse = go->GetTransform()->GetWorldMatrix().Invert();
        const Vec3 p = Vec3::Transform(Vec3(point.x, point.y, go->GetTransform()->GetWorldMatrix()._43), inverse);
        return Vec2(p.x, p.y);
    }
    void InfField(const char* label, float& value)
    {
        const bool infinite = std::isinf(value);
        bool useInfinity = infinite;
        UnityGUI::Toggle((std::string(label) + " Infinity").c_str(), &useInfinity);
        if (useInfinity != infinite) value = useInfinity ? std::numeric_limits<float>::infinity() : 1000.0f;
        if (!useInfinity) { UnityGUI::Float(label, &value); value = Nonnegative(value); }
    }

    struct Record
    {
        std::shared_ptr<Joint2D> Component;
        b2JointId Id = b2_nullJointId;
        b2BodyId A = b2_nullBodyId, B = b2_nullBodyId;
        std::string Configuration, Parameters;
        uint64 ConnectedBody = 0;
        float ReferenceAngle = 0;
        b2Vec2 Axis = { 1, 0 };
        bool Configured = false, Seen = false;
    };
    std::unordered_map<Joint2D*, Record> Records;
    b2BodyId Ground = b2_nullBodyId;

    bool SameBody(b2BodyId a, b2BodyId b) { return memcmp(&a, &b, sizeof(a)) == 0; }
    b2BodyId Body(Rigidbody2D* rb)
    {
        b2BodyId id = b2_nullBodyId;
        if (rb) memcpy(&id, &rb->Body, sizeof(id));
        return id;
    }
    bool Active(GameObject* go)
    {
        for (auto* g = go; g; g = g->GetParent()) if (!g->IsActive()) return false;
        return go != nullptr;
    }
    void Destroy(Record& r)
    {
        if (b2Joint_IsValid(r.Id)) b2DestroyJoint(r.Id);
        r.Id = b2_nullJointId;
    }
    std::string Configuration(Joint2D& j, GameObject* other)
    {
        json c = { {"body", j.ConnectedBody}, {"anchor", {j.Anchor.x, j.Anchor.y}},
            {"connected", {j.ConnectedAnchor.x, j.ConnectedAnchor.y}},
            {"autoAnchor", j.AutoConfigureConnectedAnchor}, {"autoDistance", j.AutoConfigureDistance},
            {"angle", j.Angle}, {"autoAngle", j.AutoConfigureAngle} };
        for (GameObject* go : { j.GetGameObject(), other })
        {
            if (!go) { c["scale"].push_back(nullptr); continue; }
            const auto m = go->GetTransform()->GetWorldMatrix();
            // 회전의 반올림 오차로 자동 앵커가 매 단계 다시 설정되지 않게
            c["scale"].push_back({ roundf(Vec2(m._11, m._12).Length() * 10000), roundf(Vec2(m._21, m._22).Length() * 10000) });
        }
        return c.dump();
    }
    template<class Definition> void Common(Definition& d, Joint2D& j, Record& r, Vec2 a, Vec2 b)
    {
        d.bodyIdA = r.A; d.bodyIdB = r.B;
        d.localAnchorA = b2Body_GetLocalPoint(r.A, B(b));
        d.localAnchorB = b2Body_GetLocalPoint(r.B, B(a));
        d.collideConnected = j.EnableCollision;
        d.userData = &j;
    }
    b2JointId Create(b2WorldId world, Joint2D& j, Record& r, GameObject* other)
    {
        const Vec2 a = WorldAnchor(j.GetGameObject(), j.Anchor), b = WorldAnchor(other, j.ConnectedAnchor);
        const float lower = (std::min)(Safe(j.LowerLimit), Safe(j.UpperLimit));
        const float upper = (std::max)(Safe(j.LowerLimit), Safe(j.UpperLimit));
        switch (j.JointKind())
        {
        case Joint2D::Hinge:
        {
            auto d = b2DefaultRevoluteJointDef(); Common(d, j, r, a, b);
            d.referenceAngle = r.ReferenceAngle;
            d.enableLimit = j.UseLimits;
            d.lowerAngle = std::clamp(lower * Deg, -0.99f * 3.14159265f, 0.99f * 3.14159265f);
            d.upperAngle = std::clamp(upper * Deg, -0.99f * 3.14159265f, 0.99f * 3.14159265f);
            d.enableMotor = j.UseMotor; d.motorSpeed = Safe(j.MotorSpeed) * Deg; d.maxMotorTorque = Nonnegative(j.MaxMotorForce);
            return b2CreateRevoluteJoint(world, &d);
        }
        case Joint2D::Spring:
        case Joint2D::DistanceJoint:
        {
            auto d = b2DefaultDistanceJointDef(); Common(d, j, r, a, b);
            d.length = (std::max)(MinLength, Nonnegative(j.Distance));
            d.hertz = j.JointKind() == Joint2D::Spring ? Nonnegative(j.Frequency) : 0;
            d.dampingRatio = Nonnegative(j.DampingRatio);
            // Box2D v3 의 강체 모드는 제한을 무시한다. 0 Hz 스프링 + 제한이면
            // 줄처럼 느슨해질 수는 있고 최대 길이는 지킨다
            d.enableLimit = j.JointKind() == Joint2D::DistanceJoint && j.MaxDistanceOnly;
            d.enableSpring = d.hertz > 0 || d.enableLimit;
            d.minLength = MinLength; d.maxLength = d.length;
            return b2CreateDistanceJoint(world, &d);
        }
        case Joint2D::Wheel:
        {
            auto d = b2DefaultWheelJointDef(); Common(d, j, r, a, b);
            d.localAxisA = r.Axis; d.enableSpring = j.Frequency > 0;
            d.hertz = Nonnegative(j.Frequency); d.dampingRatio = Nonnegative(j.DampingRatio);
            d.enableMotor = j.UseMotor; d.motorSpeed = Safe(j.MotorSpeed) * Deg; d.maxMotorTorque = Nonnegative(j.MaxMotorForce);
            // 주파수 0 = 단단한 서스펜션 (축이 자유롭게 움직이지 않음)
            d.enableLimit = !d.enableSpring; d.lowerTranslation = d.upperTranslation = 0;
            return b2CreateWheelJoint(world, &d);
        }
        case Joint2D::Fixed:
        {
            auto d = b2DefaultWeldJointDef(); Common(d, j, r, a, b);
            d.referenceAngle = r.ReferenceAngle;
            d.linearHertz = d.angularHertz = Nonnegative(j.Frequency);
            d.linearDampingRatio = d.angularDampingRatio = Nonnegative(j.DampingRatio);
            return b2CreateWeldJoint(world, &d);
        }
        case Joint2D::Slider:
        {
            auto d = b2DefaultPrismaticJointDef(); Common(d, j, r, a, b);
            d.referenceAngle = r.ReferenceAngle; d.localAxisA = r.Axis;
            d.enableLimit = j.UseLimits; d.lowerTranslation = lower; d.upperTranslation = upper;
            d.enableMotor = j.UseMotor; d.motorSpeed = Safe(j.MotorSpeed); d.maxMotorForce = Nonnegative(j.MaxMotorForce);
            return b2CreatePrismaticJoint(world, &d);
        }
        }
        return b2_nullJointId;
    }
}

json Joint2D::toJson() const
{
    json j = { {"type", GetType()}, {"enabled", m_Enabled}, {"connectedBody", ConnectedBody},
        {"enableCollision", EnableCollision}, {"breakForce", FloatOrInf(BreakForce)}, {"breakTorque", FloatOrInf(BreakTorque)},
        {"anchor", {Anchor.x, Anchor.y}}, {"connectedAnchor", {ConnectedAnchor.x, ConnectedAnchor.y}},
        {"autoConfigureConnectedAnchor", AutoConfigureConnectedAnchor} };
    const auto kind = JointKind();
    if (kind == Hinge || kind == Wheel || kind == Slider)
    {
        j["useMotor"] = UseMotor; j["motor"] = { {"motorSpeed", MotorSpeed}, {"maxMotorTorque", MaxMotorForce} };
    }
    if (kind == Hinge || kind == Slider)
    {
        j["useLimits"] = UseLimits; j["limits"] = { {"min", LowerLimit}, {"max", UpperLimit} };
    }
    if (kind == Spring || kind == DistanceJoint)
    {
        j["autoConfigureDistance"] = AutoConfigureDistance; j["distance"] = Distance;
    }
    if (kind == DistanceJoint) j["maxDistanceOnly"] = MaxDistanceOnly;
    if (kind == Spring || kind == Fixed) { j["dampingRatio"] = DampingRatio; j["frequency"] = Frequency; }
    if (kind == Wheel) j["suspension"] = { {"dampingRatio", DampingRatio}, {"frequency", Frequency}, {"angle", Angle} };
    if (kind == Slider) { j["autoConfigureAngle"] = AutoConfigureAngle; j["angle"] = Angle; }
    return j;
}

void Joint2D::fromJson(const json& j)
{
	Component::MarkPhysicsDirty();   // 2D 물리가 다시 훑는다 (켜짐 · 값)
    m_Enabled = j.value("enabled", true);
    ConnectedBody = j.value("connectedBody", (uint64)0); EnableCollision = j.value("enableCollision", false);
    BreakForce = ReadFloat(j, "breakForce", std::numeric_limits<float>::infinity());
    BreakTorque = ReadFloat(j, "breakTorque", std::numeric_limits<float>::infinity());
    Anchor = ReadVec(j, "anchor", Vec2(0, 0)); ConnectedAnchor = ReadVec(j, "connectedAnchor", Vec2(0, 0));
    AutoConfigureConnectedAnchor = j.value("autoConfigureConnectedAnchor", JointKind() != Spring && JointKind() != DistanceJoint);
    UseMotor = j.value("useMotor", false); UseLimits = j.value("useLimits", false);
    if (j.contains("motor")) { MotorSpeed = j.at("motor").value("motorSpeed", 0.0f); MaxMotorForce = j.at("motor").value("maxMotorTorque", 10000.0f); }
    if (j.contains("limits")) { LowerLimit = j.at("limits").value("min", 0.0f); UpperLimit = j.at("limits").value("max", 0.0f); }
    AutoConfigureDistance = j.value("autoConfigureDistance", true); Distance = j.value("distance", 1.0f);
    MaxDistanceOnly = j.value("maxDistanceOnly", false);
    DampingRatio = j.value("dampingRatio", JointKind() == Fixed ? 1.0f : 0.0f);
    Frequency = j.value("frequency", JointKind() == Fixed ? 0.0f : 1.0f);
    Angle = j.value("angle", 0.0f); AutoConfigureAngle = j.value("autoConfigureAngle", true);
    if (j.contains("suspension"))
    {
        DampingRatio = j.at("suspension").value("dampingRatio", 0.7f);
        Frequency = j.at("suspension").value("frequency", 2.0f); Angle = j.at("suspension").value("angle", 90.0f);
    }
}

void Joint2D::OnInspectorGUI()
{
    UnityGUI::GameObjectField("Connected Body", &ConnectedBody);
    Scene* scene = SceneManager::GetI()->GetCurrentScene();
    GameObject* other = scene && ConnectedBody ? scene->FindByFileID(ConnectedBody) : nullptr;
    if (ConnectedBody && (!other || !other->GetComponent<Rigidbody2D>())) UnityGUI::HelpBox("Connected Body needs a Rigidbody 2D.");
    if (other == m_pGameObject) UnityGUI::HelpBox("A joint cannot connect a Rigidbody 2D to itself.");
    UnityGUI::Vector2Pair("Anchor", "X", &Anchor.x, "Y", &Anchor.y);
    UnityGUI::Toggle("Auto Configure Connected Anchor", &AutoConfigureConnectedAnchor);
    ImGui::BeginDisabled(AutoConfigureConnectedAnchor);
    UnityGUI::Vector2Pair("Connected Anchor", "X", &ConnectedAnchor.x, "Y", &ConnectedAnchor.y);
    ImGui::EndDisabled();
    const auto kind = JointKind();
    if (kind == Spring || kind == DistanceJoint)
    {
        UnityGUI::Toggle("Auto Configure Distance", &AutoConfigureDistance);
        ImGui::BeginDisabled(AutoConfigureDistance); UnityGUI::Float("Distance", &Distance); ImGui::EndDisabled();
        Distance = Nonnegative(Distance);
    }
    if (kind == DistanceJoint) UnityGUI::Toggle("Max Distance Only", &MaxDistanceOnly);
    if (kind == Spring || kind == Wheel || kind == Fixed)
    {
        UnityGUI::Float("Damping Ratio", &DampingRatio); UnityGUI::Float("Frequency", &Frequency);
        DampingRatio = Nonnegative(DampingRatio); Frequency = Nonnegative(Frequency);
    }
    if (kind == Wheel || kind == Slider)
    {
        if (kind == Slider) UnityGUI::Toggle("Auto Configure Angle", &AutoConfigureAngle);
        ImGui::BeginDisabled(kind == Slider && AutoConfigureAngle); UnityGUI::Float("Angle", &Angle); ImGui::EndDisabled();
    }
    if (kind == Hinge || kind == Wheel || kind == Slider)
    {
        UnityGUI::Toggle("Use Motor", &UseMotor);
        ImGui::BeginDisabled(!UseMotor);
        UnityGUI::Float("Motor Speed", &MotorSpeed); UnityGUI::Float(kind == Slider ? "Max Motor Force" : "Max Motor Torque", &MaxMotorForce);
        ImGui::EndDisabled(); MaxMotorForce = Nonnegative(MaxMotorForce);
    }
    if (kind == Hinge || kind == Slider)
    {
        UnityGUI::Toggle("Use Limits", &UseLimits); ImGui::BeginDisabled(!UseLimits);
        UnityGUI::Vector2Pair("Limits", "Min", &LowerLimit, "Max", &UpperLimit); ImGui::EndDisabled();
    }
    UnityGUI::Toggle("Enable Collision", &EnableCollision); InfField("Break Force", BreakForce); InfField("Break Torque", BreakTorque);
    if (Application::IsPlaying())
    {
        char text[96]; snprintf(text, sizeof(text), "%.2f, %.2f N / %.2f Nm", ReactionForce.x, ReactionForce.y, ReactionTorque);
        UnityGUI::ValueLabel("Reaction", text);
    }
}

void Joint2D::OnDrawGizmos()
{
    if (!m_pGameObject || !SceneViewOverlay::IsActive() || SelectionManager::GetSelectedGameObject() != m_pGameObject) return;
    Scene* scene = SceneManager::GetI()->GetCurrentScene();
    auto* other = scene && ConnectedBody ? scene->FindByFileID(ConnectedBody) : nullptr;
    const Vec2 a = WorldAnchor(m_pGameObject, Anchor), b = AutoConfigureConnectedAnchor && !Application::IsPlaying() ? a : WorldAnchor(other, ConnectedAnchor);
    const float z = m_pGameObject->GetTransform()->GetWorldMatrix()._43;
    auto line = [z](Vec2 p, Vec2 q, ImU32 color) { SceneViewOverlay::DrawLine(XMFLOAT3(p.x, p.y, z), XMFLOAT3(q.x, q.y, z), color, 1.5f); };
    for (Vec2 p : { a, b })
    {
        line(p - Vec2(0.08f, 0), p + Vec2(0.08f, 0), IM_COL32(255, 170, 50, 255));
        line(p - Vec2(0, 0.08f), p + Vec2(0, 0.08f), IM_COL32(255, 170, 50, 255));
    }
    line(a, b, IM_COL32(90, 200, 255, 255));
    if (JointKind() == Wheel || JointKind() == Slider)
    {
        const Vec2 axis(cosf(Angle * Deg), sinf(Angle * Deg));
        line(a - axis * 0.5f, a + axis * 0.5f, IM_COL32(120, 255, 120, 255));
    }
}

void Joint2D::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
    if (auto it = map.find(ConnectedBody); it != map.end()) ConnectedBody = it->second;
}
void Joint2D::OnDestroy() { Physics2DJointsRuntime::Remove(this); }

namespace Physics2DJointsRuntime
{
    void Remove(Joint2D* joint)
    {
        auto it = Records.find(joint);
        if (it != Records.end()) { Destroy(it->second); Records.erase(it); }
    }
    // Joint 2D 가 없는 씬: 구조 (물리 · 연결 번호 · 오브젝트 수) 가 그대로면 다시 훑지 않는다 (Physics2DManager 의 Sync 와 같은 규칙)
    Scene* s_EmptyScene = nullptr;
    uint32_t s_EmptyPhysics = 0, s_EmptyBinding = 0;
    size_t s_EmptyObjects = 0;
    void Reset()
    {
        for (auto& [key, r] : Records)
        {
            Destroy(r); r.Component->Broken = false; r.Component->ReactionForce = Vec2(0, 0);
            r.Component->ReactionTorque = r.Component->JointAngle = r.Component->JointSpeed = r.Component->JointTranslation = r.Component->MotorForce = 0;
        }
        Records.clear();
        s_EmptyScene = nullptr;
        if (b2Body_IsValid(Ground)) b2DestroyBody(Ground);
        Ground = b2_nullBodyId;
    }
    void Sync(Scene* scene, b2WorldId world)
    {
        if (scene && Records.empty() && s_EmptyScene == scene && s_EmptyPhysics == Component::s_PhysicsSerial && s_EmptyBinding == Component::s_BindingSerial &&
            s_EmptyObjects == scene->GameObjectsView().size())
            return;
        s_EmptyScene = nullptr;
        bool anyJoint = false;
        for (auto& [key, r] : Records) r.Seen = false;
        if (scene) for (auto* go : scene->GetAllGameObjects())
        {
            if (!Active(go)) continue;
            // 분류는 ComponentIndex 가 기억한다 (예전: 스텝마다 모든 컴포넌트를 dynamic_pointer_cast — 원자적 참조 수 증감)
            const ComponentIndex::Entry& e = ComponentIndex::Of(go);
            if (e.Joints2D.empty()) continue;
            anyJoint = true;
            Rigidbody2D* const ownRb = e.Rigid2D;
            const std::vector<Joint2D*> joints = e.Joints2D;   // 아래에서 다른 오브젝트의 분류를 만들 수 있다
            for (Joint2D* raw : joints)
            {
                if (!raw->IsEnabled() || raw->Broken) continue;
                std::shared_ptr<Joint2D> j;
                for (const auto& c : go->GetComponents())
                    if (c.get() == raw) { j = std::static_pointer_cast<Joint2D>(c); break; }
                if (!j) continue;
                auto* rb = ownRb;
                const auto ownerBody = Body(rb);
                auto* other = j->ConnectedBody ? scene->FindByFileID(j->ConnectedBody) : nullptr;
                auto* connected = other ? ComponentIndex::Of(other).Rigid2D : nullptr;
                if (!rb || !rb->IsEnabled() || !b2Body_IsValid(ownerBody) || other == go) continue;
                if (j->ConnectedBody && (!Active(other) || !connected || !connected->IsEnabled() || !b2Body_IsValid(Body(connected)))) continue;
                if (!b2Body_IsValid(Ground)) { auto d = b2DefaultBodyDef(); Ground = b2CreateBody(world, &d); }
                const auto connectedBody = connected ? Body(connected) : Ground;
                if (b2Body_GetType(ownerBody) == b2_staticBody && b2Body_GetType(connectedBody) == b2_staticBody) continue;
                Record& r = Records[j.get()]; r.Component = j; r.Seen = true;
                std::string config = Configuration(*j, other);
                if (!r.Configured || r.Configuration != config)
                {
                    const Vec2 a = WorldAnchor(go, j->Anchor);
                    if (j->AutoConfigureConnectedAnchor) j->ConnectedAnchor = LocalAnchor(other, a);
                    const Vec2 b = WorldAnchor(other, j->ConnectedAnchor);
                    if (j->AutoConfigureDistance && (j->JointKind() == Joint2D::Spring || j->JointKind() == Joint2D::DistanceJoint)) j->Distance = (a - b).Length();
                    if (j->JointKind() == Joint2D::Slider && j->AutoConfigureAngle && (a - b).LengthSquared() > 1e-8f) j->Angle = atan2f(a.y - b.y, a.x - b.x) / Deg;
                    if (!r.Configured || r.ConnectedBody != j->ConnectedBody)
                        r.ReferenceAngle = b2Rot_GetAngle(b2Body_GetRotation(ownerBody)) - b2Rot_GetAngle(b2Body_GetRotation(connectedBody));
                    r.Axis = b2Body_GetLocalVector(connectedBody, b2Vec2{ cosf(Safe(j->Angle) * Deg), sinf(Safe(j->Angle) * Deg) });
                    r.Configuration = Configuration(*j, other); r.Configured = true; r.ConnectedBody = j->ConnectedBody;
                    r.Parameters.clear();
                }
                json settings = j->toJson(); settings.erase("breakForce"); settings.erase("breakTorque");
                const auto params = settings.dump();
                if (!b2Joint_IsValid(r.Id) || !SameBody(r.A, connectedBody) || !SameBody(r.B, ownerBody) || r.Parameters != params)
                {
                    Destroy(r); r.A = connectedBody; r.B = ownerBody;
                    r.Id = Create(world, *j, r, other); r.Parameters = params;
                    if (b2Body_GetType(r.A) != b2_staticBody) b2Body_SetAwake(r.A, true);
                    if (b2Body_GetType(r.B) != b2_staticBody) b2Body_SetAwake(r.B, true);
                }
            }
        }
        for (auto it = Records.begin(); it != Records.end();)
            if (!it->second.Seen) { Destroy(it->second); it = Records.erase(it); } else ++it;
        if (scene && !anyJoint && Records.empty())
        {
            s_EmptyScene = scene;
            s_EmptyPhysics = Component::s_PhysicsSerial;
            s_EmptyBinding = Component::s_BindingSerial;
            s_EmptyObjects = scene->GameObjectsView().size();
        }
    }
    void AfterStep()
    {
        for (auto& [key, r] : Records)
        {
            auto j = r.Component;
            if (j->Broken || !b2Joint_IsValid(r.Id)) continue;
            j->ReactionForce = V(b2Joint_GetConstraintForce(r.Id)); j->ReactionTorque = b2Joint_GetConstraintTorque(r.Id);
            j->JointSpeed = (b2Body_GetAngularVelocity(r.B) - b2Body_GetAngularVelocity(r.A)) / Deg;
            if (j->JointKind() == Joint2D::Hinge) { j->JointAngle = b2RevoluteJoint_GetAngle(r.Id) / Deg; j->MotorForce = b2RevoluteJoint_GetMotorTorque(r.Id); }
            if (j->JointKind() == Joint2D::Wheel) j->MotorForce = b2WheelJoint_GetMotorTorque(r.Id);
            if (j->JointKind() == Joint2D::Slider)
            {
                j->JointTranslation = b2PrismaticJoint_GetTranslation(r.Id); j->JointSpeed = b2PrismaticJoint_GetSpeed(r.Id);
                j->MotorForce = b2PrismaticJoint_GetMotorForce(r.Id);
            }
            if (j->ReactionForce.Length() <= j->BreakForce && fabsf(j->ReactionTorque) <= j->BreakTorque) continue;
            j->Broken = true; Destroy(r);
            auto* owner = j->GetGameObject(); const uint64 ownerId = owner->GetFileID();
            SceneManager::GetI()->AddLastUpdate([j, owner, ownerId]() {
                auto* scene = SceneManager::GetI()->GetCurrentScene();
                if (!scene || !GameObject::IsAlive(owner) || scene->FindByFileID(ownerId) != owner) return;
                bool attached = false;
                for (const auto& c : owner->GetComponents()) if (c == j) attached = true;
                if (!attached) return;
                const auto components = owner->GetComponents(); // 콜백이 추가 · 제거를 예약할 수 있어 복사본으로
                for (const auto& c : components)
                    if (auto* s = dynamic_cast<CSharpScript*>(c.get()); s && s->IsEnabled()) s->OnJointBreak2D((int)j->JointKind(), j->GetInstanceID());
                if (GameObject::IsAlive(owner))
                    for (const auto& c : owner->GetComponents()) if (c == j) { scene->DestroyComponent(j.get()); break; }
            });
        }
    }
}
