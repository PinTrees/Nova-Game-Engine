#include "pch.h"
#include "LegsAnimator.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"

using namespace AnimatorIK;

LegsAnimator::LegsAnimator()
{
	m_InspectorTitleName = "Legs Animator";
}

bool LegsAnimator::Raycast(const XMFLOAT3& origin, float length, float& hitY, XMFLOAT3& normal) const
{
	RaycastHit hits[8];
	const int n = PhysicsManager::GetI()->RaycastAll(Vec3(origin.x, origin.y, origin.z), Vec3(0, -1, 0), length, hits, 8, false);
	for (int i = 0; i < n; ++i)
	{
		// 자기 몸(콜라이더)은 건너뛴다
		bool self = false;
		for (GameObject* g = hits[i].gameObject; g && !self; g = g->GetParent())
			self = g == m_pGameObject;
		if (self || hits[i].normal.y < 0.3f)
			continue;
		hitY = hits[i].point.y;
		normal = XMFLOAT3(hits[i].normal.x, hits[i].normal.y, hits[i].normal.z);
		return true;
	}
	return false;
}

void LegsAnimator::ModifyPose(AnimatorPose& pose)
{
	const Humanoid::Avatar& av = pose.Avatar;
	const float w = std::clamp(Weight, 0.0f, 1.0f);
	if (!av.Valid || w <= 0.0f || m_pGameObject == nullptr)
		return;
	const float dt = (std::max)(pose.DeltaTime, 0.0f);
	const float rootY = m_pGameObject->GetTransform()->GetPosition().y;
	const int upper[2] = { av.Node[Humanoid::LeftUpperLeg], av.Node[Humanoid::RightUpperLeg] };
	const int lower[2] = { av.Node[Humanoid::LeftLowerLeg], av.Node[Humanoid::RightLowerLeg] };
	const int foot[2] = { av.Node[Humanoid::LeftFoot], av.Node[Humanoid::RightFoot] };

	// 0) Foot Locking: 디딘 발 (발목이 서 있을 때 높이 근처 + 위아래로 거의 안 움직임) → 그 자리(수평)에 고정.
	//    몸이 움직여도 (애니메이션 속도와 조금 달라도) 발은 남고, 자리에서 Max Lock Distance 넘게 멀어지거나 발을 들면 놓는다
	XMFLOAT3 footWorld[2];
	for (int i = 0; i < 2; ++i)
	{
		XMStoreFloat3(&footWorld[i], XMVector3TransformCoord(Position(pose, foot[i]), pose.ModelToWorld));
		const XMFLOAT3 cur = footWorld[i];
		const float height = cur.y - rootY;   // 애니메이션 기준 발목 높이
		m_FootRest[i] = m_HavePrev ? (std::min)(m_FootRest[i] + 0.02f * dt, height) : height;   // 가장 낮던 높이 (천천히 풀림)
		bool planted = false;
		if (m_HavePrev && dt > 1e-5f)
			planted = height < m_FootRest[i] + 0.04f && fabsf(cur.y - m_PrevFoot[i].y) / dt < LockSpeed;
		m_PrevFoot[i] = cur;
		if (FootLocking)
		{
			const float ldx = cur.x - m_LockPos[i].x, ldz = cur.z - m_LockPos[i].z;
			if (m_Locked[i] && (!planted || sqrtf(ldx * ldx + ldz * ldz) > MaxLockDistance))
				m_Locked[i] = false;
			else if (!m_Locked[i] && planted)
			{
				m_Locked[i] = true;
				m_LockPos[i] = cur;
			}
		}
		else
			m_Locked[i] = false;
		m_LockBlend[i] = Damp(m_LockBlend[i], m_Locked[i] ? 1.0f : 0.0f, m_Locked[i] ? 30.0f : 10.0f, dt);
		// 고정 중(또는 풀리는 중)이면 고정 자리 쪽으로 (높이는 애니메이션 그대로 — 바닥은 아래에서)
		footWorld[i].x += (m_LockPos[i].x - cur.x) * m_LockBlend[i];
		footWorld[i].z += (m_LockPos[i].z - cur.z) * m_LockBlend[i];
	}
	m_HavePrev = true;

	// 1) 발마다 바닥 (애니메이션은 오브젝트 높이를 바닥으로 만든다 → 실제 바닥과의 차이만큼)
	for (int i = 0; i < 2; ++i)
	{
		float target = 0.0f, hitY = 0.0f;
		XMFLOAT3 normal(0, 1, 0);
		m_GizmoValid[i] = Raycast(XMFLOAT3(footWorld[i].x, rootY + RayStartHeight, footWorld[i].z), RayStartHeight + MaxStepDown, hitY, normal);
		if (m_GizmoValid[i])
		{
			target = std::clamp(hitY - rootY, -MaxStepDown, MaxStepUp);
			m_GizmoHit[i] = XMFLOAT3(footWorld[i].x, hitY, footWorld[i].z);
		}
		else
			normal = XMFLOAT3(0, 1, 0);
		m_FootOffset[i] = Damp(m_FootOffset[i], target, Smoothing, dt);
		XMStoreFloat3(&m_Normal[i], XMVector3Normalize(XMVectorLerp(XMLoadFloat3(&m_Normal[i]), XMLoadFloat3(&normal), 1.0f - expf(-Smoothing * dt))));
	}

	// 2) 엉덩이: 낮은 쪽 발이 닿게 내린다
	const float hipsTarget = AdjustHips ? (std::max)(-HipsMaxDown, (std::min)(0.0f, (std::min)(m_FootOffset[0], m_FootOffset[1]))) : 0.0f;
	m_HipsOffset = Damp(m_HipsOffset, hipsTarget, Smoothing, dt);
	if (fabsf(m_HipsOffset) > 1e-5f)
		TranslateBone(pose, av.Node[Humanoid::Hips], XMVector3TransformNormal(XMVectorSet(0, m_HipsOffset * w, 0, 0), pose.WorldToModel));

	// 2-1) Body Lean: 두 발 아래 바닥 기울기를 캐릭터 앞 방향으로 → 오르막이면 앞으로 (척추를 모델 X 축으로)
	{
		const XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&m_Normal[0]) + XMLoadFloat3(&m_Normal[1]));
		XMVECTOR fwd = XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), pose.ModelToWorld);
		fwd = XMVectorSetY(fwd, 0.0f);
		float slope = 0.0f;   // 도, + = 오르막
		if (XMVectorGetX(XMVector3LengthSq(fwd)) > 1e-8f && XMVectorGetY(n) > 0.1f)
			slope = XMConvertToDegrees(atanf(-XMVectorGetX(XMVector3Dot(n, XMVector3Normalize(fwd))) / XMVectorGetY(n)));
		const float leanTarget = std::clamp(slope * BodyLean, -MaxLean, MaxLean);
		m_Lean = Damp(m_Lean, leanTarget, Smoothing * 0.5f, dt);
		if (fabsf(m_Lean) > 0.01f)
			RotateBone(pose, av.Node[Humanoid::Spine], XMQuaternionRotationRollPitchYaw(XMConvertToRadians(m_Lean * w), 0.0f, 0.0f));
	}

	// 3) 다리 IK: 애니메이션 발 위치 + 바닥 차이 (가중치만큼)
	for (int i = 0; i < 2; ++i)
	{
		const XMVECTOR targetWorld = XMVectorSet(footWorld[i].x, footWorld[i].y + m_FootOffset[i] * w, footWorld[i].z, 1.0f);
		SolveTwoBone(pose, upper[i], lower[i], foot[i], XMVector3TransformCoord(targetWorld, pose.WorldToModel));
		// 4) 발바닥을 바닥 기울기에
		if (AlignFeet && AlignWeight > 0.0f)
		{
			const XMVECTOR upModel = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0, 1, 0, 0), pose.WorldToModel));
			const XMVECTOR nModel = XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&m_Normal[i]), pose.WorldToModel));
			const XMVECTOR q = XMQuaternionSlerp(XMQuaternionIdentity(), FromTo(upModel, nModel), std::clamp(AlignWeight * w, 0.0f, 1.0f));
			RotateBone(pose, foot[i], q);
		}
	}
}

void LegsAnimator::OnInspectorGUI()
{
	UnityGUI::Slider("Weight", &Weight, 0.0f, 1.0f);
	UnityGUI::Label("Ground", 0, true);
	if (UnityGUI::Float("Ray Start Height", &RayStartHeight, 1)) RayStartHeight = (std::max)(0.05f, RayStartHeight);
	if (UnityGUI::Float("Max Step Down", &MaxStepDown, 1)) MaxStepDown = (std::max)(0.0f, MaxStepDown);
	if (UnityGUI::Float("Max Step Up", &MaxStepUp, 1)) MaxStepUp = (std::max)(0.0f, MaxStepUp);
	UnityGUI::Label("Body", 0, true);
	UnityGUI::Toggle("Adjust Hips", &AdjustHips, 1);
	if (AdjustHips && UnityGUI::Float("Hips Max Down", &HipsMaxDown, 2)) HipsMaxDown = (std::max)(0.0f, HipsMaxDown);
	UnityGUI::Toggle("Align Feet", &AlignFeet, 1);
	if (AlignFeet) UnityGUI::Slider("Align Weight", &AlignWeight, 0.0f, 1.0f, 2);
	if (UnityGUI::Float("Smoothing", &Smoothing, 1)) Smoothing = (std::max)(0.1f, Smoothing);
	UnityGUI::Label("Foot Locking", 0, true);
	UnityGUI::Toggle("Lock Planted Feet", &FootLocking, 1);
	if (FootLocking)
	{
		if (UnityGUI::Float("Lock Speed", &LockSpeed, 2)) LockSpeed = (std::max)(0.01f, LockSpeed);
		if (UnityGUI::Float("Max Lock Distance", &MaxLockDistance, 2)) MaxLockDistance = (std::max)(0.01f, MaxLockDistance);
	}
	UnityGUI::Label("Slopes", 0, true);
	UnityGUI::Slider("Body Lean", &BodyLean, 0.0f, 1.0f, 1);
	UnityGUI::Slider("Max Lean", &MaxLean, 0.0f, 45.0f, 1);
	UnityGUI::Toggle("Show Gizmos", &ShowGizmos);
	bool hasAnimator = false;
	if (m_pGameObject)
		for (const auto& c : m_pGameObject->GetComponents())
			hasAnimator |= c && c->GetType() == "Animator";
	if (!hasAnimator)
		UnityGUI::HelpBox("Needs an Animator on this GameObject (the legs are found from its Humanoid avatar).", true);
	else
		UnityGUI::HelpBox("Works in Play mode on top of the Animator: feet follow the colliders under them, hips go down for the lower foot.", false);
}

void LegsAnimator::OnDrawGizmos()
{
	if (!ShowGizmos || !Application::IsPlaying() || m_pGameObject == nullptr || !SceneViewOverlay::IsActive() ||
		SelectionManager::GetSelectedObjectType() != SelectionType::GAMEOBJECT || SelectionManager::GetSelectedGameObject() != m_pGameObject)
		return;
	for (int i = 0; i < 2; ++i)
		if (m_GizmoValid[i])
		{
			const XMFLOAT3 p = m_GizmoHit[i];
			const XMFLOAT3 n(p.x + m_Normal[i].x * 0.3f, p.y + m_Normal[i].y * 0.3f, p.z + m_Normal[i].z * 0.3f);
			const ImU32 c = i == 0 ? IM_COL32(90, 200, 255, 255) : IM_COL32(255, 160, 80, 255);
			SceneViewOverlay::DrawLine(XMFLOAT3(p.x - 0.1f, p.y, p.z), XMFLOAT3(p.x + 0.1f, p.y, p.z), c, 2.0f);
			SceneViewOverlay::DrawLine(XMFLOAT3(p.x, p.y, p.z - 0.1f), XMFLOAT3(p.x, p.y, p.z + 0.1f), c, 2.0f);
			SceneViewOverlay::DrawLine(p, n, c, 1.5f);
		}
}

GENERATE_COMPONENT_FUNC_TOJSON(LegsAnimator)
{
	json j;
	j["type"] = "LegsAnimator";
	j["enabled"] = m_Enabled;
	j["weight"] = Weight;
	j["rayStartHeight"] = RayStartHeight;
	j["maxStepDown"] = MaxStepDown;
	j["maxStepUp"] = MaxStepUp;
	j["adjustHips"] = AdjustHips;
	j["hipsMaxDown"] = HipsMaxDown;
	j["alignFeet"] = AlignFeet;
	j["alignWeight"] = AlignWeight;
	j["smoothing"] = Smoothing;
	j["footLocking"] = FootLocking;
	j["lockSpeed"] = LockSpeed;
	j["maxLockDistance"] = MaxLockDistance;
	j["bodyLean"] = BodyLean;
	j["maxLean"] = MaxLean;
	j["showGizmos"] = ShowGizmos;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(LegsAnimator)
{
	m_Enabled = j.value("enabled", true);
	Weight = j.value("weight", 1.0f);
	RayStartHeight = j.value("rayStartHeight", 0.6f);
	MaxStepDown = j.value("maxStepDown", 0.6f);
	MaxStepUp = j.value("maxStepUp", 0.6f);
	AdjustHips = j.value("adjustHips", true);
	HipsMaxDown = j.value("hipsMaxDown", 0.45f);
	AlignFeet = j.value("alignFeet", true);
	AlignWeight = j.value("alignWeight", 0.8f);
	Smoothing = j.value("smoothing", 14.0f);
	FootLocking = j.value("footLocking", true);
	LockSpeed = j.value("lockSpeed", 0.35f);
	MaxLockDistance = j.value("maxLockDistance", 0.3f);
	BodyLean = j.value("bodyLean", 0.5f);
	MaxLean = j.value("maxLean", 15.0f);
	ShowGizmos = j.value("showGizmos", true);
}
