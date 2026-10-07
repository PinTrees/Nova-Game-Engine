#include "pch.h"
#include "CinemachinePosition.h"
#include "CinemachineCamera.h"
#include "UnityGUI.h"

// ---------------------------------------------------------------- Follow
void CinemachineFollow::MutateCameraState(CinemachineCamera* vcam, CmState& state, float dt)
{
	GameObject* target = vcam->Follow();
	if (target == nullptr)
		return;
	const Quaternion binding = TrackerSettings.Orientation(target, state.RawPosition, dt);
	const Vec3 tracked = TrackerSettings.TrackPosition(target->GetTransform()->GetPosition(), binding, dt);
	Vec3 offset = FollowOffset;
	if (TrackerSettings.BindingMode == CmTracker::LazyFollow)
		offset = Vec3(0.0f, FollowOffset.y, -Vec3(FollowOffset.x, 0.0f, FollowOffset.z).Length());   // 거리 · 높이만 (방향은 카메라 자신)
	state.RawPosition = tracked + Vec3::Transform(offset, binding);
}

Vec3* CinemachineFollow::VecProp(int i)
{
	switch (i)
	{
	case 0: return &FollowOffset;
	case 1: return &TrackerSettings.PositionDamping;
	case 2: return &TrackerSettings.RotationDamping;
	default: return nullptr;
	}
}

int* CinemachineFollow::IntProp(int i)
{
	return i == 0 ? &TrackerSettings.BindingMode : i == 1 ? &TrackerSettings.AngularDampingMode : nullptr;
}

void CinemachineFollow::OnInspectorGUI()
{
	UnityGUI::Vector3("Follow Offset", &FollowOffset.x);
	UnityGUI::Label("Tracker Settings", 0, true);
	TrackerSettings.Inspector();
}

GENERATE_COMPONENT_FUNC_TOJSON(CinemachineFollow)
{
	json j;
	j["type"] = "CinemachineFollow";
	j["enabled"] = m_Enabled;
	j["followOffset"] = CmCore::VecJson(FollowOffset);
	TrackerSettings.ToJson(j);
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CinemachineFollow)
{
	m_Enabled = j.value("enabled", true);
	FollowOffset = CmCore::VecFromJson(j, "followOffset", Vec3(0.0f, 0.0f, -10.0f));
	TrackerSettings.FromJson(j);
}

// ---------------------------------------------------------------- 축
void CmAxis::Validate()
{
	if (Max < Min)
		std::swap(Min, Max);
	if (Wrap && Max - Min > 1e-4f)
	{
		const float range = Max - Min;
		Value = Min + fmodf(fmodf(Value - Min, range) + range, range);
	}
	else
		Value = std::clamp(Value, Min, Max);
	Center = std::clamp(Center, Min, Max);
}

void CmAxis::Inspector(const char* label)
{
	UnityGUI::Label(label, 0, true);
	bool changed = false;
	changed |= UnityGUI::Float("Value", &Value, 1);
	changed |= UnityGUI::Vector2Pair("Range", "Min", &Min, "Max", &Max, 1);
	changed |= UnityGUI::Toggle("Wrap", &Wrap, 1);
	changed |= UnityGUI::Float("Center", &Center, 1);
	if (changed)
		Validate();
}

json CmAxis::ToJson() const
{
	return { { "value", Value }, { "min", Min }, { "max", Max }, { "wrap", Wrap }, { "center", Center } };
}

void CmAxis::FromJson(const json& j, const CmAxis& fallback)
{
	Value = j.value("value", fallback.Value);
	Min = j.value("min", fallback.Min);
	Max = j.value("max", fallback.Max);
	Wrap = j.value("wrap", fallback.Wrap);
	Center = j.value("center", fallback.Center);
	Validate();
}

// ---------------------------------------------------------------- Orbital Follow
namespace
{
	CmAxis DefaultHorizontal() { CmAxis a; a.Value = 0.0f; a.Min = -180.0f; a.Max = 180.0f; a.Wrap = true; return a; }
	CmAxis DefaultVertical() { CmAxis a; a.Value = 17.5f; a.Min = -10.0f; a.Max = 45.0f; a.Center = 17.5f; return a; }
	CmAxis DefaultRadial() { CmAxis a; a.Value = 1.0f; a.Min = 1.0f; a.Max = 1.0f; a.Center = 1.0f; return a; }
}

CinemachineOrbitalFollow::CinemachineOrbitalFollow()
{
	m_InspectorTitleName = "Cinemachine Orbital Follow";
	HorizontalAxis = DefaultHorizontal();
	VerticalAxis = DefaultVertical();
	RadialAxis = DefaultRadial();
}

Vec3 CinemachineOrbitalFollow::OrbitOffset() const
{
	const float radial = (std::max)(0.0f, RadialAxis.Value);
	const float yaw = XMConvertToRadians(HorizontalAxis.Value);
	if (OrbitStyle == 0)
	{
		// 구: 세로 축 = 위로 올라간 각도 (도)
		const Quaternion q = Quaternion::CreateFromYawPitchRoll(yaw, XMConvertToRadians(VerticalAxis.Value), 0.0f);
		return Vec3::Transform(Vec3(0.0f, 0.0f, -Radius * radial), q);
	}
	// 세 고리: 세로 축의 범위 안 자리 (0 = Bottom, 0.5 = Center, 1 = Top) → 고리 사이 곡선의 높이 · 반지름
	const float range = VerticalAxis.Max - VerticalAxis.Min;
	const float t = range > 1e-5f ? std::clamp((VerticalAxis.Value - VerticalAxis.Min) / range, 0.0f, 1.0f) : 0.5f;
	const Vec2 b(Bottom.Radius, Bottom.Height), c(Center.Radius, Center.Height), top(Top.Radius, Top.Height);
	const Vec2 linear = t < 0.5f ? Vec2::Lerp(b, c, t * 2.0f) : Vec2::Lerp(c, top, t * 2.0f - 1.0f);
	// 가운데 고리를 지나는 2 차 곡선 (조절점 = 2c − (b + top)/2)
	const Vec2 control = c * 2.0f - (b + top) * 0.5f;
	const float u = 1.0f - t;
	const Vec2 curve = b * (u * u) + control * (2.0f * u * t) + top * (t * t);
	const Vec2 rh = Vec2::Lerp(linear, curve, std::clamp(SplineCurvature, 0.0f, 1.0f));
	const Quaternion q = Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
	return Vec3::Transform(Vec3(0.0f, rh.y, -rh.x), q) * radial;
}

void CinemachineOrbitalFollow::MutateCameraState(CinemachineCamera* vcam, CmState& state, float dt)
{
	GameObject* target = vcam->Follow();
	if (target == nullptr)
		return;
	HorizontalAxis.Validate();
	VerticalAxis.Validate();
	RadialAxis.Validate();
	const Quaternion binding = TrackerSettings.Orientation(target, state.RawPosition, dt);
	const Vec3 targetPos = target->GetTransform()->GetPosition() + Vec3::Transform(TargetOffset, binding);
	const Vec3 tracked = TrackerSettings.TrackPosition(targetPos, binding, dt);
	state.RawPosition = tracked + Vec3::Transform(OrbitOffset(), binding);
	// 보는 대상이 따라가는 대상과 같으면 Target Offset 을 본다 (Unity 와 같이)
	if (state.HasLookAt && vcam->LookAt() == target)
		state.ReferenceLookAt = targetPos;
}

Vec3* CinemachineOrbitalFollow::VecProp(int i)
{
	switch (i)
	{
	case 0: return &TargetOffset;
	case 1: return &TrackerSettings.PositionDamping;
	case 2: return &TrackerSettings.RotationDamping;
	default: return nullptr;
	}
}

int* CinemachineOrbitalFollow::IntProp(int i)
{
	switch (i)
	{
	case 0: return &OrbitStyle;
	case 1: return &TrackerSettings.BindingMode;
	case 2: return &TrackerSettings.AngularDampingMode;
	default: return nullptr;
	}
}

float* CinemachineOrbitalFollow::FloatProp(int i)
{
	switch (i)
	{
	case 0: return &Radius;
	case 1: return &TrackerSettings.QuaternionDamping;
	case 2: return &HorizontalAxis.Value;
	case 3: return &HorizontalAxis.Min;
	case 4: return &HorizontalAxis.Max;
	case 5: return &VerticalAxis.Value;
	case 6: return &VerticalAxis.Min;
	case 7: return &VerticalAxis.Max;
	case 8: return &RadialAxis.Value;
	case 9: return &RadialAxis.Min;
	case 10: return &RadialAxis.Max;
	case 11: return &HorizontalAxis.Center;
	case 12: return &VerticalAxis.Center;
	case 13: return &RadialAxis.Center;
	case 14: return &SplineCurvature;
	case 15: return &Top.Height;
	case 16: return &Top.Radius;
	case 17: return &Center.Height;
	case 18: return &Center.Radius;
	case 19: return &Bottom.Height;
	case 20: return &Bottom.Radius;
	default: return nullptr;
	}
}

bool* CinemachineOrbitalFollow::BoolProp(int i)
{
	return i == 0 ? &HorizontalAxis.Wrap : i == 1 ? &VerticalAxis.Wrap : i == 2 ? &RadialAxis.Wrap : nullptr;
}

void CinemachineOrbitalFollow::Validate()
{
	OrbitStyle = std::clamp(OrbitStyle, 0, 1);
	Radius = (std::max)(0.0f, Radius);
	SplineCurvature = std::clamp(SplineCurvature, 0.0f, 1.0f);
	HorizontalAxis.Validate();
	VerticalAxis.Validate();
	RadialAxis.Validate();
}

void CinemachineOrbitalFollow::OnInspectorGUI()
{
	UnityGUI::Vector3("Target Offset", &TargetOffset.x);
	UnityGUI::Label("Tracker Settings", 0, true);
	TrackerSettings.Inspector();
	static const char* styles[] = { "Sphere", "Three Ring" };
	UnityGUI::Dropdown("Orbit Style", &OrbitStyle, styles, 2);
	if (OrbitStyle == 0)
	{
		if (UnityGUI::Float("Radius", &Radius))
			Radius = (std::max)(0.0f, Radius);
	}
	else
	{
		UnityGUI::Label("Orbits", 0, true);
		UnityGUI::Vector2Pair("Top", "Height", &Top.Height, "Radius", &Top.Radius, 1);
		UnityGUI::Vector2Pair("Center", "Height", &Center.Height, "Radius", &Center.Radius, 1);
		UnityGUI::Vector2Pair("Bottom", "Height", &Bottom.Height, "Radius", &Bottom.Radius, 1);
		UnityGUI::Slider("Spline Curvature", &SplineCurvature, 0.0f, 1.0f, 1);
	}
	HorizontalAxis.Inspector("Horizontal Axis");
	VerticalAxis.Inspector("Vertical Axis");
	RadialAxis.Inspector("Radial Axis");
	UnityGUI::HelpBox(OrbitStyle == 0 ? "Horizontal = degrees around the target, Vertical = degrees above it, Radial = radius scale. Add Cinemachine Input Axis Controller (C#) to drive them with the mouse."
		: "Vertical axis picks the place between the rings (range min = Bottom, max = Top). Add Cinemachine Input Axis Controller (C#) to drive the axes with the mouse.", false);
}

GENERATE_COMPONENT_FUNC_TOJSON(CinemachineOrbitalFollow)
{
	json j;
	j["type"] = "CinemachineOrbitalFollow";
	j["enabled"] = m_Enabled;
	j["orbitStyle"] = OrbitStyle;
	j["radius"] = Radius;
	j["top"] = { Top.Height, Top.Radius };
	j["center"] = { Center.Height, Center.Radius };
	j["bottom"] = { Bottom.Height, Bottom.Radius };
	j["splineCurvature"] = SplineCurvature;
	j["targetOffset"] = CmCore::VecJson(TargetOffset);
	j["horizontalAxis"] = HorizontalAxis.ToJson();
	j["verticalAxis"] = VerticalAxis.ToJson();
	j["radialAxis"] = RadialAxis.ToJson();
	TrackerSettings.ToJson(j);
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CinemachineOrbitalFollow)
{
	m_Enabled = j.value("enabled", true);
	OrbitStyle = j.value("orbitStyle", 0);
	Radius = j.value("radius", 10.0f);
	auto orbit = [&j](const char* key, Orbit fallback) {
		auto it = j.find(key);
		if (it != j.end() && it->is_array() && it->size() == 2)
			return Orbit{ (*it)[0].get<float>(), (*it)[1].get<float>() };
		return fallback;
	};
	Top = orbit("top", { 5.0f, 2.0f });
	Center = orbit("center", { 2.25f, 4.0f });
	Bottom = orbit("bottom", { 0.1f, 2.5f });
	SplineCurvature = j.value("splineCurvature", 0.5f);
	TargetOffset = CmCore::VecFromJson(j, "targetOffset", Vec3());
	HorizontalAxis.FromJson(j.value("horizontalAxis", json::object()), DefaultHorizontal());
	VerticalAxis.FromJson(j.value("verticalAxis", json::object()), DefaultVertical());
	RadialAxis.FromJson(j.value("radialAxis", json::object()), DefaultRadial());
	TrackerSettings.FromJson(j);
	Validate();
}

// ---------------------------------------------------------------- Third Person Follow
void CinemachineThirdPersonFollow::MutateCameraState(CinemachineCamera* vcam, CmState& state, float dt)
{
	GameObject* target = vcam->Follow();
	if (target == nullptr)
		return;
	Transform* t = target->GetTransform();
	Vec3 targetPos = t->GetPosition();
	const Quaternion targetRot = t->GetRotation();
	float yaw, pitch, roll;
	CmCore::ToYawPitchRoll(targetRot, yaw, pitch, roll);
	const Quaternion heading = Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);

	// 대상의 움직임을 대상 방향 (heading) 축마다 늦게 따라간다
	if (dt < 0.0f || !m_HasPrevious)
	{
		m_PreviousTarget = targetPos;
		m_HasPrevious = true;
	}
	Quaternion inv;
	heading.Inverse(inv);
	const Vec3 local = CmCore::Damp(Vec3::Transform(targetPos - m_PreviousTarget, inv), Damping, dt);
	targetPos = m_PreviousTarget + Vec3::Transform(local, heading);
	m_PreviousTarget = targetPos;

	// 대상 → 어깨 → 손 → 카메라
	Vec3 shoulderOffset = ShoulderOffset;
	shoulderOffset.x = -ShoulderOffset.x + (ShoulderOffset.x * 2.0f) * std::clamp(CameraSide, 0.0f, 1.0f);
	const Vec3 shoulder = targetPos + Vec3::Transform(shoulderOffset, heading);
	const Vec3 hand = shoulder + Vec3::Transform(Vec3(0.0f, VerticalArmLength, 0.0f), targetRot);
	const Vec3 forward = Vec3::Transform(Vec3(0.0f, 0.0f, 1.0f), targetRot);
	Vec3 cam = hand - forward * CameraDistance;

	if (AvoidObstacles && PhysicsManager::GetI()->IsSimulating())
	{
		// 손 → 카메라 사이 벽: 가까운 순으로 모두 맞혀 보고 대상 자신 · Ignore Tag · Collision Filter 밖의 레이어는 건너뛴다
		//  (손이 대상 콜라이더의 면 위에 있으면 Raycast 하나로는 매번 대상이 거리 0 으로 맞는다)
		Vec3 dir = cam - hand;
		const float len = dir.Length();
		float desiredPull = 0.0f;
		if (len > 1e-3f)
		{
			dir /= len;
			RaycastHit hits[16];
			const int n = PhysicsManager::GetI()->RaycastAll(hand, dir, len, hits, 16, false);
			for (int i = 0; i < n; ++i)
			{
				GameObject* g = hits[i].gameObject;
				if (g == nullptr || (((uint32)CollisionFilter >> g->GetLayerIndex()) & 1u) == 0)
					continue;
				if (!IgnoreTag.empty() && g->GetTag() == IgnoreTag)
					continue;
				bool self = false;
				for (GameObject* p = g; p != nullptr && !self; p = p->GetParent())
					self = p == target;
				if (self)
					continue;
				desiredPull = std::clamp(len - (hits[i].distance - CameraRadius), 0.0f, len);
				break;
			}
		}
		const float damp = desiredPull > m_Pull ? DampingIntoCollision : DampingFromCollision;
		m_Pull += dt < 0.0f ? desiredPull - m_Pull : CmCore::Damp(desiredPull - m_Pull, damp, dt);
		if (len > 1e-3f)
			cam = hand + dir * (std::max)(0.0f, len - m_Pull);
	}
	else
		m_Pull = 0.0f;

	state.RawPosition = cam;
	state.RawOrientation = targetRot;   // 대상이 보는 방향 그대로 (Rotation Control 없이 쓰는 것)
}

Vec3* CinemachineThirdPersonFollow::VecProp(int i)
{
	return i == 0 ? &Damping : i == 1 ? &ShoulderOffset : nullptr;
}

float* CinemachineThirdPersonFollow::FloatProp(int i)
{
	switch (i)
	{
	case 0: return &VerticalArmLength;
	case 1: return &CameraSide;
	case 2: return &CameraDistance;
	case 3: return &CameraRadius;
	case 4: return &DampingIntoCollision;
	case 5: return &DampingFromCollision;
	default: return nullptr;
	}
}

void CinemachineThirdPersonFollow::Validate()
{
	Damping = Vec3::Max(Damping, Vec3());
	CameraSide = std::clamp(CameraSide, 0.0f, 1.0f);
	CameraDistance = (std::max)(0.0f, CameraDistance);
	CameraRadius = (std::max)(0.0f, CameraRadius);
	DampingIntoCollision = (std::max)(0.0f, DampingIntoCollision);
	DampingFromCollision = (std::max)(0.0f, DampingFromCollision);
}

void CinemachineThirdPersonFollow::OnInspectorGUI()
{
	bool changed = false;
	changed |= UnityGUI::Vector3("Damping", &Damping.x);
	changed |= UnityGUI::Vector3("Shoulder Offset", &ShoulderOffset.x);
	changed |= UnityGUI::Float("Vertical Arm Length", &VerticalArmLength);
	changed |= UnityGUI::Slider("Camera Side", &CameraSide, 0.0f, 1.0f);
	changed |= UnityGUI::Float("Camera Distance", &CameraDistance);
	UnityGUI::Label("Avoid Obstacles", 0, true);
	changed |= UnityGUI::Toggle("Enabled", &AvoidObstacles, 1);
	if (AvoidObstacles)
	{
		uint32 mask = (uint32)CollisionFilter;
		if (UnityGUI::MaskField("Collision Filter", &mask, 1))
			CollisionFilter = (int)mask;
		UnityGUI::TextField("Ignore Tag", &IgnoreTag, 1);
		changed |= UnityGUI::Float("Camera Radius", &CameraRadius, 1);
		changed |= UnityGUI::Float("Damping Into Collision", &DampingIntoCollision, 1);
		changed |= UnityGUI::Float("Damping From Collision", &DampingFromCollision, 1);
	}
	if (changed)
		Validate();
	UnityGUI::HelpBox("The camera looks where the Tracking Target looks: rotate the target (for example a camera root under the player) to aim. Use with Rotation Control = None.", false);
}

GENERATE_COMPONENT_FUNC_TOJSON(CinemachineThirdPersonFollow)
{
	json j;
	j["type"] = "CinemachineThirdPersonFollow";
	j["enabled"] = m_Enabled;
	j["damping"] = CmCore::VecJson(Damping);
	j["shoulderOffset"] = CmCore::VecJson(ShoulderOffset);
	j["verticalArmLength"] = VerticalArmLength;
	j["cameraSide"] = CameraSide;
	j["cameraDistance"] = CameraDistance;
	j["avoidObstacles"] = AvoidObstacles;
	j["collisionFilter"] = CollisionFilter;
	j["ignoreTag"] = IgnoreTag;
	j["cameraRadius"] = CameraRadius;
	j["dampingIntoCollision"] = DampingIntoCollision;
	j["dampingFromCollision"] = DampingFromCollision;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CinemachineThirdPersonFollow)
{
	m_Enabled = j.value("enabled", true);
	Damping = CmCore::VecFromJson(j, "damping", Vec3(0.1f, 0.5f, 0.3f));
	ShoulderOffset = CmCore::VecFromJson(j, "shoulderOffset", Vec3(0.5f, -0.4f, 0.0f));
	VerticalArmLength = j.value("verticalArmLength", 0.4f);
	CameraSide = j.value("cameraSide", 1.0f);
	CameraDistance = j.value("cameraDistance", 2.0f);
	AvoidObstacles = j.value("avoidObstacles", false);
	CollisionFilter = j.value("collisionFilter", 1);
	IgnoreTag = j.value("ignoreTag", std::string());
	CameraRadius = j.value("cameraRadius", 0.2f);
	DampingIntoCollision = j.value("dampingIntoCollision", 0.0f);
	DampingFromCollision = j.value("dampingFromCollision", 2.0f);
	Validate();
}
