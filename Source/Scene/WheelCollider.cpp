#include "pch.h"
#include "WheelCollider.h"
#include "RigidBody.h"
#include "Collider.h"
#include "PhysicsManager.h"
#include "PhysicsSettings.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"

namespace
{
	json CurveJson(const WheelFrictionCurveData& c)
	{
		return { { "extremumSlip", c.ExtremumSlip }, { "extremumValue", c.ExtremumValue }, { "asymptoteSlip", c.AsymptoteSlip },
			{ "asymptoteValue", c.AsymptoteValue }, { "stiffness", c.Stiffness } };
	}
	void ReadCurve(const json& j, const char* key, WheelFrictionCurveData& c)
	{
		if (!j.contains(key) || !j.at(key).is_object()) return;
		const json& o = j.at(key);
		c.ExtremumSlip = o.value("extremumSlip", c.ExtremumSlip);
		c.ExtremumValue = o.value("extremumValue", c.ExtremumValue);
		c.AsymptoteSlip = o.value("asymptoteSlip", c.AsymptoteSlip);
		c.AsymptoteValue = o.value("asymptoteValue", c.AsymptoteValue);
		c.Stiffness = o.value("stiffness", c.Stiffness);
	}
	void CurveGUI(const char* label, WheelFrictionCurveData& c)
	{
		ImGui::PushID(label);
		if (UnityGUI::FoldoutPlain(label, 0, false))
		{
			UnityGUI::Float("Extremum Slip", &c.ExtremumSlip, 1);
			UnityGUI::Float("Extremum Value", &c.ExtremumValue, 1);
			UnityGUI::Float("Asymptote Slip", &c.AsymptoteSlip, 1);
			UnityGUI::Float("Asymptote Value", &c.AsymptoteValue, 1);
			UnityGUI::Float("Stiffness", &c.Stiffness, 1);
		}
		ImGui::PopID();
	}
	Vec3 Unit(Vec3 v, const Vec3& fallback)
	{
		if (v.LengthSquared() < 1e-12f) return fallback;
		v.Normalize();
		return v;
	}
	void CountWheels(GameObject* go, int& n)
	{
		if (go->GetComponent<WheelCollider>())
			++n;
		for (GameObject* c : go->GetChildren())
			if (c->GetComponent<RigidBody>() == nullptr)   // 다른 차는 세지 않는다
				CountWheels(c, n);
	}
}

float WheelFrictionCurveData::Evaluate(float slip) const
{
	slip = fabsf(slip);
	float v;
	if (slip <= ExtremumSlip)
		v = ExtremumSlip > 1e-6f ? ExtremumValue * slip / ExtremumSlip : ExtremumValue;
	else if (slip < AsymptoteSlip)
		v = ExtremumValue + (AsymptoteValue - ExtremumValue) * (slip - ExtremumSlip) / (std::max)(1e-6f, AsymptoteSlip - ExtremumSlip);
	else
		v = AsymptoteValue;
	return v * (std::max)(0.0f, Stiffness);
}

WheelCollider::WheelCollider()
{
	m_InspectorTitleName = "Wheel Collider";
}

RigidBody* WheelCollider::Body() const
{
	for (GameObject* g = m_pGameObject; g; g = g->GetParent())
		if (RigidBody* rb = g->GetComponent<RigidBody>())
			return rb;
	return nullptr;
}

float WheelCollider::Rpm() const { return m_Omega * 60.0f / XM_2PI; }

void WheelCollider::GetWorldPose(Vec3& position, Quaternion& rotation)
{
	Transform* t = m_pGameObject->GetTransform();
	const Matrix w = t->GetWorldMatrix();
	const Vec3 up = Unit(Vec3::TransformNormal(Vec3(0, 1, 0), w), Vec3(0, 1, 0));
	const float ext = m_Extension >= 0.0f ? m_Extension : SuspensionDistance * std::clamp(SuspensionSpring.TargetPosition, 0.0f, 1.0f);
	position = Vec3::Transform(Center, w) - up * ext;
	// 바퀴 축 (로컬 X) 둘레 회전 → 조향 (로컬 Y) → 오브젝트 회전
	rotation = Quaternion::CreateFromAxisAngle(Vec3(1, 0, 0), m_Spin) * Quaternion::CreateFromAxisAngle(Vec3(0, 1, 0), XMConvertToRadians(SteerAngle)) * t->GetRotation();
}

void WheelCollider::FixedUpdate()
{
	if (!Application::IsPlaying() || m_pGameObject == nullptr || !m_Enabled)
		return;
	RigidBody* rb = Body();
	if (rb == nullptr || rb->IsKinematic())
		return;
	PhysicsManager* pm = PhysicsManager::GetI();
	const float dt = pm->GetFixedTimestep();
	if (dt <= 0.0f)
		return;
	if (m_SprungMass <= 0.0f)
	{
		// 매달린 질량: 차체 질량을 바퀴 수로 나눈다 (Unity 는 무게 중심에 맞춰 나눈다 — 대칭이면 같다)
		int wheels = 0;
		CountWheels(rb->GetGameObject(), wheels);
		m_WheelCount = (std::max)(1, wheels);
		m_SprungMass = rb->GetMass() / (float)m_WheelCount;
	}

	const Matrix w = m_pGameObject->GetTransform()->GetWorldMatrix();
	const Vec3 up = Unit(Vec3::TransformNormal(Vec3(0, 1, 0), w), Vec3(0, 1, 0));
	const Vec3 top = Vec3::Transform(Center, w);   // 가장 눌린 바퀴 중심
	const float d = (std::max)(0.0f, SuspensionDistance);
	const float r = (std::max)(0.01f, Radius);

	// 바닥 찾기: 바퀴 중심에서 아래로 (자기 차 · 트리거는 건너뛴다)
	RaycastHit hits[8];
	const int n = pm->RaycastAll(top, -up, d + r, hits, 8, false);
	const RaycastHit* ground = nullptr;
	for (int i = 0; i < n && ground == nullptr; ++i)
	{
		if (hits[i].collider == nullptr || hits[i].collider->IsTrigger())
			continue;
		GameObject* owner = hits[i].gameObject;
		bool own = false;
		for (GameObject* g = owner; g && !own; g = g->GetParent())
			own = g == rb->GetGameObject();
		if (!own)
			ground = &hits[i];
	}
	const float restExt = d * std::clamp(SuspensionSpring.TargetPosition, 0.0f, 1.0f);
	const float prevExt = m_Extension >= 0.0f ? m_Extension : restExt;
	m_Grounded = ground != nullptr;
	m_Extension = m_Grounded ? std::clamp(ground->distance - r, 0.0f, d) : d;

	// 바퀴 각속도: 모터 · 감쇠 · 브레이크 (마찰 반작용은 아래에서)
	const float inertia = (std::max)(0.01f, 0.5f * (std::max)(0.1f, Mass) * r * r);
	m_Omega += (MotorTorque - WheelDampingRate * m_Omega) / inertia * dt;
	const float brake = (std::max)(0.0f, BrakeTorque) / inertia * dt;
	m_Omega = fabsf(m_Omega) <= brake ? 0.0f : m_Omega - (m_Omega > 0.0f ? brake : -brake);

	if (!m_Grounded)
	{
		m_Hit = GroundHit();
		m_Omega *= (std::max)(0.0f, 1.0f - 0.05f * dt);   // 공중: 천천히 멈춘다
		m_Spin = fmodf(m_Spin + m_Omega * dt, XM_2PI);
		return;
	}

	// 서스펜션 힘 (밀기만 — 당기지 않는다)
	const float g = fabsf(PhysicsSettings::Gravity().y);
	const float compressVel = (prevExt - m_Extension) / dt;
	float load = m_SprungMass * g + SuspensionSpring.Spring * (restExt - m_Extension) + SuspensionSpring.Damper * compressVel;
	if (m_Extension <= 1e-4f && compressVel > 0.0f)
		load += m_SprungMass * compressVel / dt;   // 끝까지 눌림: 더 내려가지 않게 (범프 스톱)
	load = (std::max)(0.0f, load);

	// 타이어 틀: 앞 = 오브젝트 앞을 조향만큼 돌려 바닥 면에, 옆 = 법선 × 앞
	const Vec3 nrm = Unit(ground->normal, up);
	const Vec3 fwd0 = Vec3::TransformNormal(Vec3::Transform(Vec3(0, 0, 1), Quaternion::CreateFromAxisAngle(Vec3(0, 1, 0), XMConvertToRadians(SteerAngle))), w);
	const Vec3 fwd = Unit(fwd0 - nrm * fwd0.Dot(nrm), Vec3(0, 0, 1));
	const Vec3 side = Unit(nrm.Cross(fwd), Vec3(1, 0, 0));
	const Vec3 p = ground->point;
	const Vec3 com = rb->GetWorldCenterOfMass();
	const Vec3 vP = rb->GetVelocity() + rb->GetAngularVelocity().Cross(p - com);
	const float vLong = vP.Dot(fwd), vLat = vP.Dot(side);

	// 앞 미끄러짐 → 힘. 한 스텝에 바퀴 속도가 구르는 속도를 넘지 않게 (작은 관성에서도 안정 — 힘 = 바퀴가 줄 수 있는 만큼)
	const float denom = (std::max)(fabsf(vLong), 1.0f);
	const float slipLong = (m_Omega * r - vLong) / denom;
	float fx = (slipLong >= 0.0f ? 1.0f : -1.0f) * ForwardFriction.Evaluate(slipLong) * load;
	const float rollOmega = vLong / r;
	float dOmega = -fx * r / inertia * dt;
	if (BrakeTorque > 0.0f && m_Omega == 0.0f && fabsf(fx) * r <= BrakeTorque)
	{
		// 브레이크가 마찰 토크를 버틴다: 바퀴는 잠긴 채 미끄럼 마찰이 그대로 차에 (한 스텝에 차 속도를 뒤집지 않게)
		dOmega = 0.0f;
		const float maxFx = 0.5f * pm->GetEffectiveMass(rb, p, fwd) / (float)m_WheelCount * fabsf(vLong) / dt;
		fx = std::clamp(fx, -maxFx, maxFx);
	}
	else if ((m_Omega - rollOmega) * (m_Omega + dOmega - rollOmega) < 0.0f)
	{
		dOmega = rollOmega - m_Omega;
		fx = -dOmega * inertia / (r * dt);
	}
	m_Omega += dOmega;

	// 옆 미끄러짐 (각) → 옆 힘. 한 스텝에 없앨 옆 속도 = 접점의 실제 질량 (회전 몫 포함) ÷ 바퀴 수의 절반까지 —
	// 접점은 무게 중심 아래라 옆 힘이 차를 굴린다. 매달린 질량만큼 다 없애면 바퀴 넷이 함께 넘쳐 좌우로 흔들리며 멈추지 않는다
	const float slipLat = atan2f(vLat, denom);
	float fy = -(vLat >= 0.0f ? 1.0f : -1.0f) * SidewaysFriction.Evaluate(slipLat) * load;
	const float maxFy = 0.5f * pm->GetEffectiveMass(rb, p, side) / (float)m_WheelCount * fabsf(vLat) / dt;
	fy = std::clamp(fy, -maxFy, maxFy);

	const Vec3 applyAt = p + up * ForceAppPointDistance;
	rb->AddForceAtPosition(nrm * load + fwd * fx + side * fy, applyAt, ForceMode::Force);

	m_Spin = fmodf(m_Spin + m_Omega * dt, XM_2PI);
	m_Hit.Point = p;
	m_Hit.Normal = nrm;
	m_Hit.ForwardDir = fwd;
	m_Hit.SidewaysDir = side;
	m_Hit.Force = load;
	m_Hit.ForwardSlip = slipLong;
	m_Hit.SidewaysSlip = slipLat;
	m_Hit.Object = ground->gameObject;
}

void WheelCollider::OnInspectorGUI()
{
	if (UnityGUI::Float("Mass", &Mass)) Mass = (std::max)(0.1f, Mass);
	if (UnityGUI::Float("Radius", &Radius)) Radius = (std::max)(0.01f, Radius);
	if (UnityGUI::Float("Wheel Damping Rate", &WheelDampingRate)) WheelDampingRate = (std::max)(0.0f, WheelDampingRate);
	if (UnityGUI::Float("Suspension Distance", &SuspensionDistance)) SuspensionDistance = (std::max)(0.0f, SuspensionDistance);
	UnityGUI::Float("Force App Point Distance", &ForceAppPointDistance);
	UnityGUI::Vector3("Center", &Center.x);
	ImGui::PushID("Suspension Spring");
	if (UnityGUI::FoldoutPlain("Suspension Spring", 0, true))
	{
		if (UnityGUI::Float("Spring", &SuspensionSpring.Spring, 1)) SuspensionSpring.Spring = (std::max)(0.0f, SuspensionSpring.Spring);
		if (UnityGUI::Float("Damper", &SuspensionSpring.Damper, 1)) SuspensionSpring.Damper = (std::max)(0.0f, SuspensionSpring.Damper);
		if (UnityGUI::Float("Target Position", &SuspensionSpring.TargetPosition, 1)) SuspensionSpring.TargetPosition = std::clamp(SuspensionSpring.TargetPosition, 0.0f, 1.0f);
	}
	ImGui::PopID();
	CurveGUI("Forward Friction", ForwardFriction);
	CurveGUI("Sideways Friction", SidewaysFriction);
	if (Body() == nullptr)
		UnityGUI::HelpBox("A Wheel Collider needs a Rigidbody on this object or a parent (the car body).");
	if (Application::IsPlaying())
	{
		char buf[96];
		snprintf(buf, sizeof(buf), "%s, %.0f rpm, load %.0f N", m_Grounded ? "grounded" : "in the air", Rpm(), m_Hit.Force);
		UnityGUI::ValueLabel("State", buf);
	}
}

void WheelCollider::OnDrawGizmos()
{
	if (m_pGameObject == nullptr || !SceneViewOverlay::IsActive())
		return;
	// Unity 처럼 바퀴 둘레 (초록) + 서스펜션 선 — 차 (부모) 를 골라도 보인다
	GameObject* sel = SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT ? SelectionManager::GetSelectedGameObject() : nullptr;
	bool show = false;
	for (GameObject* g = m_pGameObject; g && !show; g = g->GetParent())
		show = g == sel;
	if (!show)
		return;
	Vec3 pos;
	Quaternion rot;
	GetWorldPose(pos, rot);
	const Matrix w = m_pGameObject->GetTransform()->GetWorldMatrix();
	const Vec3 up = Unit(Vec3::TransformNormal(Vec3(0, 1, 0), w), Vec3(0, 1, 0));
	const Vec3 fwd = Unit(Vec3::Transform(Vec3(0, 0, 1), rot), Vec3(0, 0, 1));
	const Vec3 upW = Unit(Vec3::Transform(Vec3(0, 1, 0), rot), up);
	const ImU32 c = IM_COL32(120, 230, 120, 255);
	const int seg = 32;
	for (int i = 0; i < seg; ++i)
	{
		const float a0 = XM_2PI * i / seg, a1 = XM_2PI * (i + 1) / seg;
		const Vec3 p0 = pos + (fwd * cosf(a0) + upW * sinf(a0)) * Radius;
		const Vec3 p1 = pos + (fwd * cosf(a1) + upW * sinf(a1)) * Radius;
		SceneViewOverlay::DrawLine(XMFLOAT3(p0.x, p0.y, p0.z), XMFLOAT3(p1.x, p1.y, p1.z), c, 1.5f);
	}
	const Vec3 top = Vec3::Transform(Center, w);
	const Vec3 bottom = top - up * SuspensionDistance;
	SceneViewOverlay::DrawLine(XMFLOAT3(top.x, top.y, top.z), XMFLOAT3(bottom.x, bottom.y, bottom.z), IM_COL32(240, 240, 120, 255), 1.5f);
	SceneViewOverlay::DrawLine(XMFLOAT3(pos.x, pos.y, pos.z), XMFLOAT3(pos.x + upW.x * Radius, pos.y + upW.y * Radius, pos.z + upW.z * Radius), c, 1.5f);
}

GENERATE_COMPONENT_FUNC_TOJSON(WheelCollider)
{
	json j;
	j["type"] = "WheelCollider";
	j["enabled"] = m_Enabled;
	j["center"] = { Center.x, Center.y, Center.z };
	j["radius"] = Radius;
	j["mass"] = Mass;
	j["wheelDampingRate"] = WheelDampingRate;
	j["suspensionDistance"] = SuspensionDistance;
	j["forceAppPointDistance"] = ForceAppPointDistance;
	j["suspensionSpring"] = { { "spring", SuspensionSpring.Spring }, { "damper", SuspensionSpring.Damper }, { "targetPosition", SuspensionSpring.TargetPosition } };
	j["forwardFriction"] = CurveJson(ForwardFriction);
	j["sidewaysFriction"] = CurveJson(SidewaysFriction);
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(WheelCollider)
{
	m_Enabled = j.value("enabled", true);
	if (j.contains("center") && j["center"].is_array() && j["center"].size() == 3)
		Center = Vec3(j["center"][0].get<float>(), j["center"][1].get<float>(), j["center"][2].get<float>());
	Radius = j.value("radius", 0.5f);
	Mass = j.value("mass", 20.0f);
	WheelDampingRate = j.value("wheelDampingRate", 0.25f);
	SuspensionDistance = j.value("suspensionDistance", 0.3f);
	ForceAppPointDistance = j.value("forceAppPointDistance", 0.0f);
	if (j.contains("suspensionSpring") && j["suspensionSpring"].is_object())
	{
		SuspensionSpring.Spring = j["suspensionSpring"].value("spring", 35000.0f);
		SuspensionSpring.Damper = j["suspensionSpring"].value("damper", 4500.0f);
		SuspensionSpring.TargetPosition = j["suspensionSpring"].value("targetPosition", 0.5f);
	}
	ReadCurve(j, "forwardFriction", ForwardFriction);
	ReadCurve(j, "sidewaysFriction", SidewaysFriction);
}
