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

// ====================================================================== Character · Configurable 공통
namespace
{
	json LimitJson(const SoftJointLimitData& l) { return { { "limit", l.Limit }, { "bounciness", l.Bounciness }, { "contactDistance", l.ContactDistance } }; }
	void ReadLimit(const json& j, const char* key, SoftJointLimitData& l)
	{
		if (!j.contains(key) || !j.at(key).is_object()) return;
		const json& o = j.at(key);
		l.Limit = o.value("limit", l.Limit);
		l.Bounciness = o.value("bounciness", l.Bounciness);
		l.ContactDistance = o.value("contactDistance", l.ContactDistance);
	}
	json SpringJson(const SoftJointLimitSpringData& s) { return { { "spring", s.Spring }, { "damper", s.Damper } }; }
	void ReadSpring(const json& j, const char* key, SoftJointLimitSpringData& s)
	{
		if (!j.contains(key) || !j.at(key).is_object()) return;
		s.Spring = j.at(key).value("spring", s.Spring);
		s.Damper = j.at(key).value("damper", s.Damper);
	}
	json DriveJson(const JointDriveData& d) { return { { "positionSpring", d.PositionSpring }, { "positionDamper", d.PositionDamper }, { "maximumForce", FloatOrInf(d.MaximumForce) } }; }
	void ReadDrive(const json& j, const char* key, JointDriveData& d)
	{
		if (!j.contains(key) || !j.at(key).is_object()) return;
		const json& o = j.at(key);
		d.PositionSpring = o.value("positionSpring", d.PositionSpring);
		d.PositionDamper = o.value("positionDamper", d.PositionDamper);
		d.MaximumForce = ReadFloatOrInf(o, "maximumForce", d.MaximumForce);
	}
	void HashLimit(size_t& h, const SoftJointLimitData& l) { HashF(h, l.Limit); HashF(h, l.Bounciness); HashF(h, l.ContactDistance); }
	void HashSpring(size_t& h, const SoftJointLimitSpringData& s) { HashF(h, s.Spring); HashF(h, s.Damper); }
	void HashDrive(size_t& h, const JointDriveData& d) { HashF(h, d.PositionSpring); HashF(h, d.PositionDamper); HashF(h, d.MaximumForce); }

	// Unity 처럼 접는 묶음 (안의 칸 이름이 같아도 ID 가 겹치지 않게)
	void LimitGUI(const char* label, SoftJointLimitData& l)
	{
		ImGui::PushID(label);
		if (UnityGUI::FoldoutPlain(label, 0, false))
		{
			UnityGUI::Float("Limit", &l.Limit, 1);
			UnityGUI::Float("Bounciness", &l.Bounciness, 1);
			UnityGUI::Float("Contact Distance", &l.ContactDistance, 1);
		}
		ImGui::PopID();
	}
	void SpringGUI(const char* label, SoftJointLimitSpringData& s)
	{
		ImGui::PushID(label);
		if (UnityGUI::FoldoutPlain(label, 0, false))
		{
			if (UnityGUI::Float("Spring", &s.Spring, 1)) s.Spring = (std::max)(0.0f, s.Spring);
			if (UnityGUI::Float("Damper", &s.Damper, 1)) s.Damper = (std::max)(0.0f, s.Damper);
		}
		ImGui::PopID();
	}
	void DriveGUI(const char* label, JointDriveData& d)
	{
		ImGui::PushID(label);
		if (UnityGUI::FoldoutPlain(label, 0, false))
		{
			if (UnityGUI::Float("Position Spring", &d.PositionSpring, 1)) d.PositionSpring = (std::max)(0.0f, d.PositionSpring);
			if (UnityGUI::Float("Position Damper", &d.PositionDamper, 1)) d.PositionDamper = (std::max)(0.0f, d.PositionDamper);
			InfFloat("Maximum Force", &d.MaximumForce);
		}
		ImGui::PopID();
	}

	// 앵커에서 축 하나를 그린다 (로컬 축 → 월드)
	void AxisGizmo(GameObject* go, const Vec3& anchor, const Vec3& localAxis, ImU32 c, float len = 0.4f)
	{
		const Matrix world = go->GetTransform()->GetWorldMatrix();
		Vec3 ax = Vec3::TransformNormal(localAxis, world);
		if (ax.LengthSquared() < 1e-8f)
			return;
		ax.Normalize();
		const Vec3 a = Vec3::Transform(anchor, world);
		Line(a, a + ax * len, c, 2.0f);
	}

	bool SelectedIs(GameObject* go)
	{
		return go && SceneViewOverlay::IsActive() && SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT &&
			SelectionManager::GetSelectedGameObject() == go;
	}
}

// ====================================================================== CharacterJoint
CharacterJoint::CharacterJoint()
{
	m_InspectorTitleName = "Character Joint";
}

size_t CharacterJoint::ParamsHash() const
{
	size_t h = Joint::ParamsHash();
	HashV(h, SwingAxis);
	HashSpring(h, TwistLimitSpring); HashLimit(h, LowTwistLimit); HashLimit(h, HighTwistLimit);
	HashSpring(h, SwingLimitSpring); HashLimit(h, Swing1Limit); HashLimit(h, Swing2Limit);
	return h;
}

void CharacterJoint::OnInspectorGUI()
{
	DrawCommonTop(true);
	UnityGUI::Vector3("Swing Axis", &SwingAxis.x);
	SpringGUI("Twist Limit Spring", TwistLimitSpring);
	LimitGUI("Low Twist Limit", LowTwistLimit);
	LimitGUI("High Twist Limit", HighTwistLimit);
	SpringGUI("Swing Limit Spring", SwingLimitSpring);
	LimitGUI("Swing 1 Limit", Swing1Limit);
	LimitGUI("Swing 2 Limit", Swing2Limit);
	UnityGUI::Toggle("Enable Projection", &EnableProjection);
	if (EnableProjection)
	{
		UnityGUI::Float("Projection Distance", &ProjectionDistance, 1);
		UnityGUI::Float("Projection Angle", &ProjectionAngle, 1);
	}
	DrawCommonBottom();
}

void CharacterJoint::OnDrawGizmos()
{
	DrawAnchorGizmo(true);
	if (SelectedIs(m_pGameObject))
		AxisGizmo(m_pGameObject, m_Anchor, SwingAxis, IM_COL32(90, 230, 90, 255));
}

GENERATE_COMPONENT_FUNC_TOJSON(CharacterJoint)
{
	json j;
	j["type"] = "CharacterJoint";
	SerializeCommon(j);
	j["swingAxis"] = Vec(SwingAxis);
	j["twistLimitSpring"] = SpringJson(TwistLimitSpring);
	j["lowTwistLimit"] = LimitJson(LowTwistLimit);
	j["highTwistLimit"] = LimitJson(HighTwistLimit);
	j["swingLimitSpring"] = SpringJson(SwingLimitSpring);
	j["swing1Limit"] = LimitJson(Swing1Limit);
	j["swing2Limit"] = LimitJson(Swing2Limit);
	j["enableProjection"] = EnableProjection;
	j["projectionDistance"] = ProjectionDistance;
	j["projectionAngle"] = ProjectionAngle;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CharacterJoint)
{
	DeserializeCommon(j);
	SwingAxis = ReadVec(j, "swingAxis", Vec3(0, 1, 0));
	ReadSpring(j, "twistLimitSpring", TwistLimitSpring);
	ReadLimit(j, "lowTwistLimit", LowTwistLimit);
	ReadLimit(j, "highTwistLimit", HighTwistLimit);
	ReadSpring(j, "swingLimitSpring", SwingLimitSpring);
	ReadLimit(j, "swing1Limit", Swing1Limit);
	ReadLimit(j, "swing2Limit", Swing2Limit);
	EnableProjection = j.value("enableProjection", false);
	ProjectionDistance = j.value("projectionDistance", 0.1f);
	ProjectionAngle = j.value("projectionAngle", 180.0f);
}

// ====================================================================== ConfigurableJoint
ConfigurableJoint::ConfigurableJoint()
{
	m_InspectorTitleName = "Configurable Joint";
}

size_t ConfigurableJoint::ParamsHash() const
{
	size_t h = Joint::ParamsHash();
	HashV(h, SecondaryAxis);
	for (int m : { XMotion, YMotion, ZMotion, AngularXMotion, AngularYMotion, AngularZMotion })
		HashCombine(h, (size_t)m);
	HashSpring(h, LinearLimitSpring); HashLimit(h, LinearLimit);
	HashSpring(h, AngularXLimitSpring); HashLimit(h, LowAngularXLimit); HashLimit(h, HighAngularXLimit);
	HashSpring(h, AngularYZLimitSpring); HashLimit(h, AngularYLimit); HashLimit(h, AngularZLimit);
	HashDrive(h, XDrive); HashDrive(h, YDrive); HashDrive(h, ZDrive);
	HashCombine(h, (size_t)RotationDriveMode);
	HashDrive(h, AngularXDrive); HashDrive(h, AngularYZDrive); HashDrive(h, SlerpDrive);
	return h;
}

void ConfigurableJoint::OnInspectorGUI()
{
	DrawCommonTop(true);
	UnityGUI::Vector3("Secondary Axis", &SecondaryAxis.x);
	static const char* kMotion[] = { "Locked", "Limited", "Free" };
	UnityGUI::Dropdown("X Motion", &XMotion, kMotion, 3);
	UnityGUI::Dropdown("Y Motion", &YMotion, kMotion, 3);
	UnityGUI::Dropdown("Z Motion", &ZMotion, kMotion, 3);
	UnityGUI::Dropdown("Angular X Motion", &AngularXMotion, kMotion, 3);
	UnityGUI::Dropdown("Angular Y Motion", &AngularYMotion, kMotion, 3);
	UnityGUI::Dropdown("Angular Z Motion", &AngularZMotion, kMotion, 3);
	SpringGUI("Linear Limit Spring", LinearLimitSpring);
	LimitGUI("Linear Limit", LinearLimit);
	SpringGUI("Angular X Limit Spring", AngularXLimitSpring);
	LimitGUI("Low Angular X Limit", LowAngularXLimit);
	LimitGUI("High Angular X Limit", HighAngularXLimit);
	SpringGUI("Angular YZ Limit Spring", AngularYZLimitSpring);
	LimitGUI("Angular Y Limit", AngularYLimit);
	LimitGUI("Angular Z Limit", AngularZLimit);
	UnityGUI::Vector3("Target Position", &TargetPosition.x);
	UnityGUI::Vector3("Target Velocity", &TargetVelocity.x);
	DriveGUI("X Drive", XDrive);
	DriveGUI("Y Drive", YDrive);
	DriveGUI("Z Drive", ZDrive);
	Vec3 euler = Transform::ToEulerAngles(TargetRotation);
	if (UnityGUI::Vector3("Target Rotation", &euler.x))
		TargetRotation = Transform::EulerToQuaternion(euler);
	UnityGUI::Vector3("Target Angular Velocity", &TargetAngularVelocity.x);
	static const char* kMode[] = { "X and YZ", "Slerp" };
	UnityGUI::Dropdown("Rotation Drive Mode", &RotationDriveMode, kMode, 2);
	if (RotationDriveMode == 0)
	{
		DriveGUI("Angular X Drive", AngularXDrive);
		DriveGUI("Angular YZ Drive", AngularYZDrive);
	}
	else
		DriveGUI("Slerp Drive", SlerpDrive);
	DrawCommonBottom();
}

void ConfigurableJoint::OnDrawGizmos()
{
	DrawAnchorGizmo(true);
	if (SelectedIs(m_pGameObject))
		AxisGizmo(m_pGameObject, m_Anchor, SecondaryAxis, IM_COL32(90, 230, 90, 255));
}

GENERATE_COMPONENT_FUNC_TOJSON(ConfigurableJoint)
{
	json j;
	j["type"] = "ConfigurableJoint";
	SerializeCommon(j);
	j["secondaryAxis"] = Vec(SecondaryAxis);
	j["xMotion"] = XMotion; j["yMotion"] = YMotion; j["zMotion"] = ZMotion;
	j["angularXMotion"] = AngularXMotion; j["angularYMotion"] = AngularYMotion; j["angularZMotion"] = AngularZMotion;
	j["linearLimitSpring"] = SpringJson(LinearLimitSpring);
	j["linearLimit"] = LimitJson(LinearLimit);
	j["angularXLimitSpring"] = SpringJson(AngularXLimitSpring);
	j["lowAngularXLimit"] = LimitJson(LowAngularXLimit);
	j["highAngularXLimit"] = LimitJson(HighAngularXLimit);
	j["angularYZLimitSpring"] = SpringJson(AngularYZLimitSpring);
	j["angularYLimit"] = LimitJson(AngularYLimit);
	j["angularZLimit"] = LimitJson(AngularZLimit);
	j["targetPosition"] = Vec(TargetPosition);
	j["targetVelocity"] = Vec(TargetVelocity);
	j["xDrive"] = DriveJson(XDrive); j["yDrive"] = DriveJson(YDrive); j["zDrive"] = DriveJson(ZDrive);
	j["targetRotation"] = { TargetRotation.x, TargetRotation.y, TargetRotation.z, TargetRotation.w };
	j["targetAngularVelocity"] = Vec(TargetAngularVelocity);
	j["rotationDriveMode"] = RotationDriveMode;
	j["angularXDrive"] = DriveJson(AngularXDrive);
	j["angularYZDrive"] = DriveJson(AngularYZDrive);
	j["slerpDrive"] = DriveJson(SlerpDrive);
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(ConfigurableJoint)
{
	DeserializeCommon(j);
	SecondaryAxis = ReadVec(j, "secondaryAxis", Vec3(0, 1, 0));
	auto motion = [&](const char* key) { return std::clamp(j.value(key, (int)Free), 0, 2); };
	XMotion = motion("xMotion"); YMotion = motion("yMotion"); ZMotion = motion("zMotion");
	AngularXMotion = motion("angularXMotion"); AngularYMotion = motion("angularYMotion"); AngularZMotion = motion("angularZMotion");
	ReadSpring(j, "linearLimitSpring", LinearLimitSpring);
	ReadLimit(j, "linearLimit", LinearLimit);
	ReadSpring(j, "angularXLimitSpring", AngularXLimitSpring);
	ReadLimit(j, "lowAngularXLimit", LowAngularXLimit);
	ReadLimit(j, "highAngularXLimit", HighAngularXLimit);
	ReadSpring(j, "angularYZLimitSpring", AngularYZLimitSpring);
	ReadLimit(j, "angularYLimit", AngularYLimit);
	ReadLimit(j, "angularZLimit", AngularZLimit);
	TargetPosition = ReadVec(j, "targetPosition", Vec3::Zero);
	TargetVelocity = ReadVec(j, "targetVelocity", Vec3::Zero);
	ReadDrive(j, "xDrive", XDrive); ReadDrive(j, "yDrive", YDrive); ReadDrive(j, "zDrive", ZDrive);
	TargetRotation = Quaternion::Identity;
	if (j.contains("targetRotation") && j["targetRotation"].is_array() && j["targetRotation"].size() == 4)
		TargetRotation = Quaternion(j["targetRotation"][0].get<float>(), j["targetRotation"][1].get<float>(), j["targetRotation"][2].get<float>(), j["targetRotation"][3].get<float>());
	TargetAngularVelocity = ReadVec(j, "targetAngularVelocity", Vec3::Zero);
	RotationDriveMode = std::clamp(j.value("rotationDriveMode", 0), 0, 1);
	ReadDrive(j, "angularXDrive", AngularXDrive);
	ReadDrive(j, "angularYZDrive", AngularYZDrive);
	ReadDrive(j, "slerpDrive", SlerpDrive);
}
