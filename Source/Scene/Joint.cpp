#include "pch.h"
#include "Joint.h"
#include "PhysicsManager.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"

namespace
{
	void HashCombine(size_t& h, size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); }
	void HashF(size_t& h, float f) { HashCombine(h, std::hash<float>()(f)); }
	void HashV(size_t& h, const Vec3& v) { HashF(h, v.x); HashF(h, v.y); HashF(h, v.z); }

	// JSON 에 무한대를 쓸 수 없다 → "Infinity"
	json FloatOrInf(float f) { return std::isinf(f) ? json("Infinity") : json(f); }
	float ReadFloatOrInf(const json& j, const char* key, float def)
	{
		if (!j.contains(key)) return def;
		const json& v = j.at(key);
		if (v.is_string()) return std::numeric_limits<float>::infinity();
		return v.is_number() ? v.get<float>() : def;
	}
	json Vec(const Vec3& v) { return { v.x, v.y, v.z }; }
	Vec3 ReadVec(const json& j, const char* key, const Vec3& def)
	{
		if (!j.contains(key) || !j.at(key).is_array() || j.at(key).size() != 3) return def;
		return Vec3(j.at(key)[0].get<float>(), j.at(key)[1].get<float>(), j.at(key)[2].get<float>());
	}

	void Line(const Vec3& a, const Vec3& b, ImU32 c, float w = 1.5f)
	{
		SceneViewOverlay::DrawLine(XMFLOAT3(a.x, a.y, a.z), XMFLOAT3(b.x, b.y, b.z), c, w);
	}

	// 무한대는 "Infinity" 로 보여 주고 입력도 받는다 (Unity 와 같음)
	void InfFloat(const char* label, float* v)
	{
		float shown = std::isinf(*v) ? 0.0f : *v;
		bool inf = std::isinf(*v);
		UnityGUI::FieldRow row = UnityGUI::BeginFieldRow(label);
		ImGui::PushID(label);
		ImGui::SetCursorScreenPos(ImVec2(row.fieldX, row.p.y));
		if (inf)
		{
			ImGui::SetNextItemWidth(row.fieldW - 26.0f);
			char buf[16] = "Infinity";
			ImGui::InputText("##inf", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
		}
		else
		{
			ImGui::SetNextItemWidth(row.fieldW - 26.0f);
			if (ImGui::DragFloat("##v", &shown, 1.0f, 0.0f, 1e9f, "%.3f"))
				*v = (std::max)(0.0f, shown);
		}
		ImGui::SameLine(0, 4);
		if (ImGui::SmallButton(inf ? "#" : "inf"))
			*v = inf ? 1000.0f : std::numeric_limits<float>::infinity();
		ImGui::PopID();
		UnityGUI::EndFieldRow(row);
	}
}

// ====================================================================== Joint
Joint::Joint()
{
}

void Joint::SerializeCommon(json& j) const
{
	j["connectedBody"] = m_ConnectedBody;
	j["anchor"] = Vec(m_Anchor);
	j["axis"] = Vec(m_Axis);
	j["autoConfigureConnectedAnchor"] = m_AutoConfigureConnectedAnchor;
	j["connectedAnchor"] = Vec(m_ConnectedAnchor);
	j["breakForce"] = FloatOrInf(m_BreakForce);
	j["breakTorque"] = FloatOrInf(m_BreakTorque);
	j["enableCollision"] = m_EnableCollision;
}

void Joint::DeserializeCommon(const json& j)
{
	m_ConnectedBody = j.value("connectedBody", (uint64)0);
	m_Anchor = ReadVec(j, "anchor", Vec3::Zero);
	m_Axis = ReadVec(j, "axis", Vec3(1, 0, 0));
	m_AutoConfigureConnectedAnchor = j.value("autoConfigureConnectedAnchor", true);
	m_ConnectedAnchor = ReadVec(j, "connectedAnchor", Vec3::Zero);
	m_BreakForce = ReadFloatOrInf(j, "breakForce", std::numeric_limits<float>::infinity());
	m_BreakTorque = ReadFloatOrInf(j, "breakTorque", std::numeric_limits<float>::infinity());
	m_EnableCollision = j.value("enableCollision", false);
}

size_t Joint::ParamsHash() const
{
	size_t h = (size_t)JointKind();
	HashCombine(h, (size_t)m_ConnectedBody);
	HashV(h, m_Anchor);
	HashV(h, m_Axis);
	HashCombine(h, m_AutoConfigureConnectedAnchor);
	HashV(h, m_ConnectedAnchor);
	HashCombine(h, m_EnableCollision);
	return h;
}

void Joint::DrawCommonTop(bool withAxis)
{
	UnityGUI::GameObjectField("Connected Body", &m_ConnectedBody);
	UnityGUI::Vector3("Anchor", &m_Anchor.x);
	if (withAxis)
		UnityGUI::Vector3("Axis", &m_Axis.x);
	UnityGUI::Toggle("Auto Configure Connected Anchor", &m_AutoConfigureConnectedAnchor);
	if (!m_AutoConfigureConnectedAnchor)
		UnityGUI::Vector3("Connected Anchor", &m_ConnectedAnchor.x);
}

void Joint::DrawCommonBottom()
{
	InfFloat("Break Force", &m_BreakForce);
	InfFloat("Break Torque", &m_BreakTorque);
	UnityGUI::Toggle("Enable Collision", &m_EnableCollision);
	bool hasBody = m_pGameObject && m_pGameObject->GetComponent<RigidBody>() != nullptr;
	if (!hasBody)
		UnityGUI::HelpBox("A joint needs a Rigidbody on the same GameObject.");
}

void Joint::DrawAnchorGizmo(bool withAxis)
{
	if (m_pGameObject == nullptr || !SceneViewOverlay::IsActive() ||
		SelectionManager::GetSelectedObjectType() != SelectionType::GAMEOBJECT || SelectionManager::GetSelectedGameObject() != m_pGameObject)
		return;
	const Matrix world = m_pGameObject->GetTransform()->GetWorldMatrix();
	const Vec3 a = Vec3::Transform(m_Anchor, world);
	const ImU32 c = IM_COL32(255, 160, 40, 255);
	const float s = 0.08f;
	Line(a - Vec3(s, 0, 0), a + Vec3(s, 0, 0), c);
	Line(a - Vec3(0, s, 0), a + Vec3(0, s, 0), c);
	Line(a - Vec3(0, 0, s), a + Vec3(0, 0, s), c);
	if (withAxis)
	{
		Vec3 ax = Vec3::TransformNormal(m_Axis, world);
		if (ax.LengthSquared() > 1e-8f)
		{
			ax.Normalize();
			Line(a - ax * 0.5f, a + ax * 0.5f, IM_COL32(255, 210, 60, 255), 2.0f);
		}
	}
	// 이은 쪽 (상대 바디의 앵커 또는 월드 점)
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	GameObject* other = m_ConnectedBody && scene ? scene->FindByFileID(m_ConnectedBody) : nullptr;
	Vec3 b = a;
	if (!m_AutoConfigureConnectedAnchor)
		b = other ? Vec3::Transform(m_ConnectedAnchor, other->GetTransform()->GetWorldMatrix()) : m_ConnectedAnchor;
	else if (other)
		b = other->GetTransform()->GetPosition();
	if ((b - a).LengthSquared() > 1e-6f)
		Line(a, b, IM_COL32(90, 200, 255, 255));
}

void Joint::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	auto it = map.find(m_ConnectedBody);
	if (it != map.end())
		m_ConnectedBody = it->second;
}

void Joint::OnDestroy()
{
	PhysicsManager::GetI()->RemoveJoint(this);
}

// ====================================================================== FixedJoint
FixedJoint::FixedJoint()
{
	m_InspectorTitleName = "Fixed Joint";
}

void FixedJoint::OnInspectorGUI()
{
	DrawCommonTop(false);
	DrawCommonBottom();
}

GENERATE_COMPONENT_FUNC_TOJSON(FixedJoint)
{
	json j;
	j["type"] = "FixedJoint";
	SerializeCommon(j);
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(FixedJoint)
{
	DeserializeCommon(j);
}

// ====================================================================== HingeJoint
HingeJoint::HingeJoint()
{
	m_InspectorTitleName = "Hinge Joint";
}

size_t HingeJoint::ParamsHash() const
{
	size_t h = Joint::ParamsHash();
	HashCombine(h, UseSpring); HashF(h, Spring.Spring); HashF(h, Spring.Damper); HashF(h, Spring.TargetPosition);
	HashCombine(h, UseMotor); HashF(h, Motor.TargetVelocity); HashF(h, Motor.Force); HashCombine(h, Motor.FreeSpin);
	HashCombine(h, UseLimits); HashF(h, Limits.Min); HashF(h, Limits.Max);
	return h;
}

float HingeJoint::GetAngle() const { return PhysicsManager::GetI()->GetHingeAngle(this, false); }
float HingeJoint::GetVelocity() const { return PhysicsManager::GetI()->GetHingeAngle(this, true); }

void HingeJoint::OnInspectorGUI()
{
	DrawCommonTop(true);
	UnityGUI::Toggle("Use Spring", &UseSpring);
	if (UnityGUI::FoldoutPlain("Spring", 0, false))
	{
		UnityGUI::Float("Spring", &Spring.Spring, 1);
		UnityGUI::Float("Damper", &Spring.Damper, 1);
		UnityGUI::Float("Target Position", &Spring.TargetPosition, 1);
	}
	UnityGUI::Toggle("Use Motor", &UseMotor);
	if (UnityGUI::FoldoutPlain("Motor", 0, false))
	{
		UnityGUI::Float("Target Velocity", &Motor.TargetVelocity, 1);
		UnityGUI::Float("Force", &Motor.Force, 1);
		UnityGUI::Toggle("Free Spin", &Motor.FreeSpin, 1);
	}
	UnityGUI::Toggle("Use Limits", &UseLimits);
	if (UnityGUI::FoldoutPlain("Limits", 0, false))
	{
		UnityGUI::Float("Min", &Limits.Min, 1);
		UnityGUI::Float("Max", &Limits.Max, 1);
		UnityGUI::Float("Bounciness", &Limits.Bounciness, 1);
	}
	DrawCommonBottom();
	if (Application::IsPlaying())
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "%.1f deg   %.1f deg/s", GetAngle(), GetVelocity());
		UnityGUI::ValueLabel("Angle / Velocity", buf);
	}
}

void HingeJoint::OnDrawGizmos()
{
	DrawAnchorGizmo(true);
}

GENERATE_COMPONENT_FUNC_TOJSON(HingeJoint)
{
	json j;
	j["type"] = "HingeJoint";
	SerializeCommon(j);
	j["useSpring"] = UseSpring;
	j["spring"] = { { "spring", Spring.Spring }, { "damper", Spring.Damper }, { "targetPosition", Spring.TargetPosition } };
	j["useMotor"] = UseMotor;
	j["motor"] = { { "targetVelocity", Motor.TargetVelocity }, { "force", Motor.Force }, { "freeSpin", Motor.FreeSpin } };
	j["useLimits"] = UseLimits;
	j["limits"] = { { "min", Limits.Min }, { "max", Limits.Max }, { "bounciness", Limits.Bounciness } };
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(HingeJoint)
{
	DeserializeCommon(j);
	UseSpring = j.value("useSpring", false);
	if (j.contains("spring") && j["spring"].is_object())
	{
		Spring.Spring = j["spring"].value("spring", 0.0f);
		Spring.Damper = j["spring"].value("damper", 0.0f);
		Spring.TargetPosition = j["spring"].value("targetPosition", 0.0f);
	}
	UseMotor = j.value("useMotor", false);
	if (j.contains("motor") && j["motor"].is_object())
	{
		Motor.TargetVelocity = j["motor"].value("targetVelocity", 0.0f);
		Motor.Force = j["motor"].value("force", 0.0f);
		Motor.FreeSpin = j["motor"].value("freeSpin", false);
	}
	UseLimits = j.value("useLimits", false);
	if (j.contains("limits") && j["limits"].is_object())
	{
		Limits.Min = j["limits"].value("min", 0.0f);
		Limits.Max = j["limits"].value("max", 0.0f);
		Limits.Bounciness = j["limits"].value("bounciness", 0.0f);
	}
}

// ====================================================================== SpringJoint
SpringJoint::SpringJoint()
{
	m_InspectorTitleName = "Spring Joint";
}

size_t SpringJoint::ParamsHash() const
{
	size_t h = Joint::ParamsHash();
	HashF(h, SpringValue); HashF(h, Damper); HashF(h, MinDistance); HashF(h, MaxDistance);
	return h;
}

void SpringJoint::OnInspectorGUI()
{
	DrawCommonTop(false);
	if (UnityGUI::Float("Spring", &SpringValue)) SpringValue = (std::max)(0.0f, SpringValue);
	if (UnityGUI::Float("Damper", &Damper)) Damper = (std::max)(0.0f, Damper);
	if (UnityGUI::Float("Min Distance", &MinDistance)) MinDistance = (std::max)(0.0f, MinDistance);
	if (UnityGUI::Float("Max Distance", &MaxDistance)) MaxDistance = (std::max)(0.0f, MaxDistance);
	DrawCommonBottom();
}

GENERATE_COMPONENT_FUNC_TOJSON(SpringJoint)
{
	json j;
	j["type"] = "SpringJoint";
	SerializeCommon(j);
	j["spring"] = SpringValue;
	j["damper"] = Damper;
	j["minDistance"] = MinDistance;
	j["maxDistance"] = MaxDistance;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(SpringJoint)
{
	DeserializeCommon(j);
	SpringValue = j.value("spring", 10.0f);
	Damper = j.value("damper", 0.2f);
	MinDistance = j.value("minDistance", 0.0f);
	MaxDistance = j.value("maxDistance", 0.0f);
}
