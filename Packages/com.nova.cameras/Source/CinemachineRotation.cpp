#include "pch.h"
#include "CinemachineRotation.h"
#include "CinemachineCamera.h"
#include "UnityGUI.h"

namespace
{
	// 보는 점 = 대상 위치 + 대상 자기 공간의 오프셋
	Vec3 LookPoint(CinemachineCamera* vcam, const CmState& state, const Vec3& offset)
	{
		GameObject* look = vcam->LookAt();
		return state.ReferenceLookAt + (look ? Vec3::Transform(offset, look->GetTransform()->GetRotation()) : offset);
	}

	// 한 축 (가로 또는 세로): 대상의 화면 각도 → 이번에 돌 각도
	//  화면 자리 f (가운데 0, 가장자리 ±0.5) 의 각도 = atan(f · 2 · tan(반 시야각))
	float ComposeAxis(float targetAngle, float screen, bool deadZone, float deadZoneSize, bool hardLimits, float hardSize, float hardOffset,
		float tanHalf, float damping, float dt, bool snap, bool center)
	{
		auto A = [tanHalf](float f) { return atanf(f * 2.0f * tanHalf); };
		float move;
		if (snap)
			move = center ? targetAngle - A(screen) : 0.0f;
		else
		{
			const float half = deadZone ? deadZoneSize * 0.5f : 0.0f;
			const float lo = A(screen - half), hi = A(screen + half);
			const float err = targetAngle < lo ? targetAngle - lo : targetAngle > hi ? targetAngle - hi : 0.0f;
			move = CmCore::Damp(err, damping, dt);
		}
		if (hardLimits)
		{
			// 돈 뒤에도 대상이 Hard Limits 밖이면 그만큼 더 (따라가기 없이)
			const float lo = A(screen + hardOffset - hardSize * 0.5f), hi = A(screen + hardOffset + hardSize * 0.5f);
			const float rest = targetAngle - move;
			if (rest < lo)
				move = targetAngle - lo;
			else if (rest > hi)
				move = targetAngle - hi;
		}
		return move;
	}
}

// ---------------------------------------------------------------- Rotation Composer
void CinemachineRotationComposer::MutateCameraState(CinemachineCamera* vcam, CmState& state, float dt)
{
	if (!state.HasLookAt)
		return;
	const Vec3 L = LookPoint(vcam, state, TargetOffset);
	const Vec3 P = state.RawPosition;
	float yaw, pitch, roll;
	CmCore::ToYawPitchRoll(state.RawOrientation, yaw, pitch, roll);
	const float tanV = tanf(XMConvertToRadians(std::clamp(state.Lens.FieldOfView, 1.0f, 179.0f)) * 0.5f);
	const float tanH = tanV * CmCore::Aspect();
	const bool snap = dt < 0.0f;
	const float pitchLimit = XMConvertToRadians(89.0f);

	// 가로를 세상 위 축으로 돌리므로 위 · 아래를 볼 때는 조금 어긋난다 → 바로 맞출 때는 몇 번 되풀이
	for (int it = 0, n = snap ? 4 : 1; it < n; ++it)
	{
		const Quaternion q0 = Quaternion::CreateFromYawPitchRoll(yaw, pitch, 0.0f);
		Quaternion inv;
		q0.Inverse(inv);
		const Vec3 d = Vec3::Transform(L - P, inv);
		if (d.LengthSquared() < 1e-8f)
			break;
		if (d.z <= 1e-3f)
		{
			// 대상이 뒤 · 옆: 우선 바로 본다
			CmCore::ToYawPitchRoll(CmCore::LookRotation(L - P), yaw, pitch, roll);
			continue;
		}
		const float ax = atanf(d.x / d.z);    // 오른쪽 +
		const float ay = atanf(-d.y / d.z);   // 아래 + (화면 y 와 같은 방향)
		yaw += ComposeAxis(ax, ScreenPosition.x, DeadZoneEnabled, DeadZoneSize.x, HardLimitsEnabled, HardLimitsSize.x, HardLimitsOffset.x,
			tanH, Damping.x, dt, snap, CenterOnActivate);
		pitch += ComposeAxis(ay, ScreenPosition.y, DeadZoneEnabled, DeadZoneSize.y, HardLimitsEnabled, HardLimitsSize.y, HardLimitsOffset.y,
			tanV, Damping.y, dt, snap, CenterOnActivate);
		pitch = std::clamp(pitch, -pitchLimit, pitchLimit);
	}
	state.RawOrientation = Quaternion::CreateFromYawPitchRoll(yaw, pitch, 0.0f);
}

Vec3* CinemachineRotationComposer::VecProp(int i)
{
	switch (i)
	{
	case 0: return &TargetOffset;
	case 1: return &Damping;
	case 2: return &ScreenPosition;
	case 3: return &DeadZoneSize;
	case 4: return &HardLimitsSize;
	case 5: return &HardLimitsOffset;
	default: return nullptr;
	}
}

bool* CinemachineRotationComposer::BoolProp(int i)
{
	return i == 0 ? &DeadZoneEnabled : i == 1 ? &HardLimitsEnabled : i == 2 ? &CenterOnActivate : nullptr;
}

void CinemachineRotationComposer::Validate()
{
	Damping = Vec3::Max(Damping, Vec3());
	auto clamp2 = [](Vec3& v, float lo, float hi) {
		v.x = std::clamp(v.x, lo, hi);
		v.y = std::clamp(v.y, lo, hi);
		v.z = 0.0f;
	};
	clamp2(ScreenPosition, -1.5f, 1.5f);
	clamp2(DeadZoneSize, 0.0f, 2.0f);
	clamp2(HardLimitsSize, 0.0f, 3.0f);
	HardLimitsOffset.z = Damping.z = 0.0f;
}

void CinemachineRotationComposer::OnInspectorGUI()
{
	bool changed = false;
	changed |= UnityGUI::Vector3("Target Offset", &TargetOffset.x);
	changed |= UnityGUI::Vector2Pair("Damping", "X", &Damping.x, "Y", &Damping.y);
	UnityGUI::Label("Composition", 0, true);
	changed |= UnityGUI::Vector2Pair("Screen Position", "X", &ScreenPosition.x, "Y", &ScreenPosition.y, 1);
	changed |= UnityGUI::Toggle("Dead Zone", &DeadZoneEnabled, 1);
	if (DeadZoneEnabled)
		changed |= UnityGUI::Vector2Pair("Size", "X", &DeadZoneSize.x, "Y", &DeadZoneSize.y, 2);
	changed |= UnityGUI::Toggle("Hard Limits", &HardLimitsEnabled, 1);
	if (HardLimitsEnabled)
	{
		changed |= UnityGUI::Vector2Pair("Size", "X", &HardLimitsSize.x, "Y", &HardLimitsSize.y, 2);
		changed |= UnityGUI::Vector2Pair("Offset", "X", &HardLimitsOffset.x, "Y", &HardLimitsOffset.y, 2);
	}
	changed |= UnityGUI::Toggle("Center On Activate", &CenterOnActivate);
	if (changed)
		Validate();
}

GENERATE_COMPONENT_FUNC_TOJSON(CinemachineRotationComposer)
{
	json j;
	j["type"] = "CinemachineRotationComposer";
	j["enabled"] = m_Enabled;
	j["targetOffset"] = CmCore::VecJson(TargetOffset);
	j["damping"] = { Damping.x, Damping.y };
	j["screenPosition"] = { ScreenPosition.x, ScreenPosition.y };
	j["deadZone"] = DeadZoneEnabled;
	j["deadZoneSize"] = { DeadZoneSize.x, DeadZoneSize.y };
	j["hardLimits"] = HardLimitsEnabled;
	j["hardLimitsSize"] = { HardLimitsSize.x, HardLimitsSize.y };
	j["hardLimitsOffset"] = { HardLimitsOffset.x, HardLimitsOffset.y };
	j["centerOnActivate"] = CenterOnActivate;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CinemachineRotationComposer)
{
	m_Enabled = j.value("enabled", true);
	TargetOffset = CmCore::VecFromJson(j, "targetOffset", Vec3());
	Damping = CmCore::VecFromJson(j, "damping", Vec3(0.5f, 0.5f, 0.0f));
	ScreenPosition = CmCore::VecFromJson(j, "screenPosition", Vec3());
	DeadZoneEnabled = j.value("deadZone", false);
	DeadZoneSize = CmCore::VecFromJson(j, "deadZoneSize", Vec3(0.2f, 0.2f, 0.0f));
	HardLimitsEnabled = j.value("hardLimits", true);
	HardLimitsSize = CmCore::VecFromJson(j, "hardLimitsSize", Vec3(0.8f, 0.8f, 0.0f));
	HardLimitsOffset = CmCore::VecFromJson(j, "hardLimitsOffset", Vec3());
	CenterOnActivate = j.value("centerOnActivate", true);
	Validate();
}

// ---------------------------------------------------------------- Hard Look At
void CinemachineHardLookAt::MutateCameraState(CinemachineCamera* vcam, CmState& state, float)
{
	if (!state.HasLookAt)
		return;
	const Vec3 d = LookPoint(vcam, state, LookAtOffset) - state.RawPosition;
	if (d.LengthSquared() > 1e-8f)
		state.RawOrientation = CmCore::LookRotation(d);
}

void CinemachineHardLookAt::OnInspectorGUI()
{
	UnityGUI::Vector3("Look At Offset", &LookAtOffset.x);
}

GENERATE_COMPONENT_FUNC_TOJSON(CinemachineHardLookAt)
{
	json j;
	j["type"] = "CinemachineHardLookAt";
	j["enabled"] = m_Enabled;
	j["lookAtOffset"] = CmCore::VecJson(LookAtOffset);
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CinemachineHardLookAt)
{
	m_Enabled = j.value("enabled", true);
	LookAtOffset = CmCore::VecFromJson(j, "lookAtOffset", Vec3());
}

// ---------------------------------------------------------------- Rotate With Follow Target
void CinemachineRotateWithFollowTarget::MutateCameraState(CinemachineCamera* vcam, CmState& state, float dt)
{
	GameObject* target = vcam->Follow();
	if (target == nullptr)
		return;
	const Quaternion goal = target->GetTransform()->GetRotation();
	state.RawOrientation = dt < 0.0f ? goal : Quaternion::Slerp(state.RawOrientation, goal, CmCore::Damp(1.0f, Damping, dt));
}

void CinemachineRotateWithFollowTarget::OnInspectorGUI()
{
	if (UnityGUI::Float("Damping", &Damping))
		Validate();
}

GENERATE_COMPONENT_FUNC_TOJSON(CinemachineRotateWithFollowTarget)
{
	json j;
	j["type"] = "CinemachineRotateWithFollowTarget";
	j["enabled"] = m_Enabled;
	j["damping"] = Damping;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CinemachineRotateWithFollowTarget)
{
	m_Enabled = j.value("enabled", true);
	Damping = (std::max)(0.0f, j.value("damping", 0.0f));
}
