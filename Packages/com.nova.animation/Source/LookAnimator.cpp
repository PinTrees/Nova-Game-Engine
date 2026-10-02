#include "pch.h"
#include "LookAnimator.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"

using namespace AnimatorIK;

LookAnimator::LookAnimator()
{
	m_InspectorTitleName = "Look Animator";
}

bool LookAnimator::TargetWorld(Vec3& out) const
{
	if (m_UseLookPosition)
	{
		out = m_LookPosition;
		return true;
	}
	if (Target == 0)
		return false;
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	GameObject* go = scene ? scene->FindByFileID(Target) : nullptr;
	if (go == nullptr || !go->IsActive())
		return false;
	out = go->GetTransform()->GetPosition() + TargetOffset;
	return true;
}

void LookAnimator::ModifyPose(AnimatorPose& pose)
{
	const Humanoid::Avatar& av = pose.Avatar;
	if (!av.Valid || m_pGameObject == nullptr)
		return;
	const float dt = (std::max)(pose.DeltaTime, 0.0f);
	const int head = av.Node[Humanoid::Head];

	// 대상 방향 → 캐릭터 기준 좌우(yaw)·위아래(pitch). 모델 공간: 앞 = +Z, 위 = +Y (Unity 와 같음)
	float yaw = 0.0f, pitch = 0.0f, blendTarget = 0.0f;
	Vec3 target;
	m_GizmoValid = false;
	if (TargetWorld(target))
	{
		const XMVECTOR headModel = Position(pose, head);
		const XMVECTOR targetModel = XMVector3TransformCoord(XMVectorSet(target.x, target.y, target.z, 1.0f), pose.WorldToModel);
		XMVECTOR dir = targetModel - headModel;
		if (XMVectorGetX(XMVector3LengthSq(dir)) > 1e-8f)
		{
			dir = XMVector3Normalize(dir);
			const float y = XMConvertToDegrees(atan2f(XMVectorGetX(dir), XMVectorGetZ(dir)));
			const float p = XMConvertToDegrees(asinf(std::clamp(XMVectorGetY(dir), -1.0f, 1.0f)));
			if (fabsf(y) <= StopAngle)
			{
				yaw = std::clamp(y, -MaxYaw, MaxYaw);
				pitch = std::clamp(p, -MaxPitchDown, MaxPitchUp);
				blendTarget = std::clamp(Weight, 0.0f, 1.0f);
			}
			XMStoreFloat3(&m_GizmoHead, XMVector3TransformCoord(headModel, pose.ModelToWorld));
			m_GizmoTarget = XMFLOAT3(target.x, target.y, target.z);
			m_GizmoValid = true;
		}
	}
	m_Yaw = Damp(m_Yaw, yaw, Speed, dt);
	m_Pitch = Damp(m_Pitch, pitch, Speed, dt);
	m_Blend = Damp(m_Blend, blendTarget, Speed, dt);
	if (m_Blend < 1e-3f)
		return;

	// 위로 볼수록 -X 회전 (DirectX 의 X 회전은 +Z 를 아래로 돌린다)
	const XMVECTOR total = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(-m_Pitch * m_Blend), XMConvertToRadians(m_Yaw * m_Blend), 0.0f);
	// 사슬에 나눠 (부모가 돌면 자식도 따라 도므로 곱 ≈ 전체)
	struct Link { int Node; float W; };
	const Link chain[] = { { av.Node[Humanoid::Spine], SpineWeight }, { av.Node[Humanoid::Chest], ChestWeight },
		{ av.Node[Humanoid::Neck], NeckWeight }, { head, HeadWeight } };
	float sum = 0.0f;
	for (const Link& l : chain)
		if (l.Node >= 0) sum += (std::max)(0.0f, l.W);
	if (sum <= 1e-4f)
		return;
	for (const Link& l : chain)
		if (l.Node >= 0 && l.W > 0.0f)
			RotateBone(pose, l.Node, XMQuaternionSlerp(XMQuaternionIdentity(), total, l.W / sum));
}

void LookAnimator::OnInspectorGUI()
{
	UnityGUI::GameObjectField("Target", &Target);
	UnityGUI::Vector3("Target Offset", &TargetOffset.x);
	UnityGUI::Slider("Weight", &Weight, 0.0f, 1.0f);
	UnityGUI::Label("Limits", 0, true);
	UnityGUI::Slider("Max Yaw", &MaxYaw, 0.0f, 120.0f, 1);
	UnityGUI::Slider("Max Pitch Up", &MaxPitchUp, 0.0f, 90.0f, 1);
	UnityGUI::Slider("Max Pitch Down", &MaxPitchDown, 0.0f, 90.0f, 1);
	UnityGUI::Slider("Stop Angle", &StopAngle, 0.0f, 180.0f, 1);
	if (UnityGUI::Float("Speed", &Speed, 1)) Speed = (std::max)(0.1f, Speed);
	UnityGUI::Label("Bone Weights", 0, true);
	UnityGUI::Slider("Spine", &SpineWeight, 0.0f, 1.0f, 1);
	UnityGUI::Slider("Chest", &ChestWeight, 0.0f, 1.0f, 1);
	UnityGUI::Slider("Neck", &NeckWeight, 0.0f, 1.0f, 1);
	UnityGUI::Slider("Head", &HeadWeight, 0.0f, 1.0f, 1);
	UnityGUI::Toggle("Show Gizmos", &ShowGizmos);
	if (Target == 0)
		UnityGUI::HelpBox("Drag what to look at (for example the player or the camera) into Target, or call SetLookAtPosition from a script.", false);
}

void LookAnimator::OnDrawGizmos()
{
	if (!ShowGizmos || !m_GizmoValid || !Application::IsPlaying() || m_pGameObject == nullptr || !SceneViewOverlay::IsActive() ||
		SelectionManager::GetSelectedObjectType() != SelectionType::GAMEOBJECT || SelectionManager::GetSelectedGameObject() != m_pGameObject)
		return;
	SceneViewOverlay::DrawLine(m_GizmoHead, m_GizmoTarget, m_Blend > 0.05f ? IM_COL32(120, 230, 120, 255) : IM_COL32(160, 160, 160, 200), 1.5f);
}

void LookAnimator::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	if (auto it = map.find(Target); it != map.end())
		Target = it->second;
}

GENERATE_COMPONENT_FUNC_TOJSON(LookAnimator)
{
	json j;
	j["type"] = "LookAnimator";
	j["enabled"] = m_Enabled;
	j["target"] = Target;
	j["targetOffset"] = { TargetOffset.x, TargetOffset.y, TargetOffset.z };
	j["weight"] = Weight;
	j["maxYaw"] = MaxYaw;
	j["maxPitchUp"] = MaxPitchUp;
	j["maxPitchDown"] = MaxPitchDown;
	j["stopAngle"] = StopAngle;
	j["speed"] = Speed;
	j["boneWeights"] = { SpineWeight, ChestWeight, NeckWeight, HeadWeight };
	j["showGizmos"] = ShowGizmos;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(LookAnimator)
{
	m_Enabled = j.value("enabled", true);
	Target = j.value("target", (uint64)0);
	if (j.contains("targetOffset") && j["targetOffset"].is_array() && j["targetOffset"].size() == 3)
		TargetOffset = Vec3(j["targetOffset"][0].get<float>(), j["targetOffset"][1].get<float>(), j["targetOffset"][2].get<float>());
	Weight = j.value("weight", 1.0f);
	MaxYaw = j.value("maxYaw", 70.0f);
	MaxPitchUp = j.value("maxPitchUp", 35.0f);
	MaxPitchDown = j.value("maxPitchDown", 45.0f);
	StopAngle = j.value("stopAngle", 120.0f);
	Speed = j.value("speed", 6.0f);
	if (j.contains("boneWeights") && j["boneWeights"].is_array() && j["boneWeights"].size() == 4)
	{
		SpineWeight = j["boneWeights"][0]; ChestWeight = j["boneWeights"][1];
		NeckWeight = j["boneWeights"][2]; HeadWeight = j["boneWeights"][3];
	}
	ShowGizmos = j.value("showGizmos", true);
}
