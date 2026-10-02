#include "pch.h"
#include "HandsAnimator.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"

using namespace AnimatorIK;

namespace
{
	GameObject* FindObject(uint64 id)
	{
		if (id == 0)
			return nullptr;
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		GameObject* go = scene ? scene->FindByFileID(id) : nullptr;
		return go && go->IsActive() ? go : nullptr;
	}

	XMVECTOR RotationOfMatrix(FXMMATRIX m)
	{
		XMVECTOR s, q, t;
		return XMMatrixDecompose(&s, &q, &t, m) ? XMQuaternionNormalize(q) : XMQuaternionIdentity();
	}
}

HandsAnimator::HandsAnimator()
{
	m_InspectorTitleName = "Hands Animator";
}

void HandsAnimator::ModifyPose(AnimatorPose& pose)
{
	const Humanoid::Avatar& av = pose.Avatar;
	if (!av.Valid || m_pGameObject == nullptr)
		return;
	const float dt = (std::max)(pose.DeltaTime, 0.0f);
	const float master = std::clamp(Weight, 0.0f, 1.0f);
	const int upper[2] = { av.Node[Humanoid::LeftUpperArm], av.Node[Humanoid::RightUpperArm] };
	const int lower[2] = { av.Node[Humanoid::LeftLowerArm], av.Node[Humanoid::RightLowerArm] };
	const int hand[2] = { av.Node[Humanoid::LeftHand], av.Node[Humanoid::RightHand] };
	const XMVECTOR worldToModelRot = RotationOfMatrix(pose.WorldToModel);

	for (int i = 0; i < 2; ++i)
	{
		m_GizmoValid[i] = false;
		if (upper[i] < 0 || lower[i] < 0 || hand[i] < 0)
			continue;
		// 목표: 스크립트 값이 먼저, 없으면 Target 오브젝트
		GameObject* target = FindObject(Hands[i].Target);
		bool havePos = m_Script[i].UsePos || target != nullptr;
		bool haveRot = m_Script[i].UseRot || target != nullptr;
		if (havePos)
		{
			const Vec3 p = m_Script[i].UsePos ? m_Script[i].Pos : target->GetTransform()->GetPosition();
			m_LastGoal[i] = XMFLOAT3(p.x, p.y, p.z);
		}
		if (haveRot)
		{
			const Quaternion q = m_Script[i].UseRot ? m_Script[i].Rot : target->GetTransform()->GetRotation();
			m_LastRot[i] = XMFLOAT4(q.x, q.y, q.z, q.w);
		}
		// 목표가 없어지면 마지막 목표에서 천천히 풀어 준다
		m_PosBlend[i] = Damp(m_PosBlend[i], havePos ? std::clamp(Hands[i].PositionWeight, 0.0f, 1.0f) * master : 0.0f, BlendSpeed, dt);
		m_RotBlend[i] = Damp(m_RotBlend[i], haveRot ? std::clamp(Hands[i].RotationWeight, 0.0f, 1.0f) * master : 0.0f, BlendSpeed, dt);

		if (m_PosBlend[i] > 1e-3f)
		{
			const XMVECTOR goalModel = XMVector3TransformCoord(XMLoadFloat3(&m_LastGoal[i]), pose.WorldToModel);
			const XMVECTOR animated = Position(pose, hand[i]);
			const XMVECTOR goal = XMVectorLerp(animated, goalModel, m_PosBlend[i]);
			// Hint: 팔꿈치가 향할 쪽 → 지금 팔꿈치를 그쪽으로 먼저 돌려 두면 SolveTwoBone 이 그 방향으로 굽힌다
			if (GameObject* hint = FindObject(Hands[i].Hint))
			{
				const Vec3 h = hint->GetTransform()->GetPosition();
				const XMVECTOR hintModel = XMVector3TransformCoord(XMVectorSet(h.x, h.y, h.z, 1.0f), pose.WorldToModel);
				const XMVECTOR a = Position(pose, upper[i]), b = Position(pose, lower[i]);
				const XMVECTOR axis = goal - a;
				if (XMVectorGetX(XMVector3LengthSq(axis)) > 1e-8f)
				{
					// 어깨→목표 축에 수직인 성분끼리: 지금 팔꿈치 방향 → 힌트 방향
					const XMVECTOR n = XMVector3Normalize(axis);
					const XMVECTOR cur = (b - a) - n * XMVector3Dot(b - a, n);
					const XMVECTOR want = (hintModel - a) - n * XMVector3Dot(hintModel - a, n);
					RotateBone(pose, upper[i], XMQuaternionSlerp(XMQuaternionIdentity(), FromTo(cur, want), m_PosBlend[i]));
				}
			}
			SolveTwoBone(pose, upper[i], lower[i], hand[i], goal);
			m_GizmoValid[i] = true;
		}
		if (m_RotBlend[i] > 1e-3f)
		{
			// 목표 회전 (모델 공간) 다음 T-포즈 방향 = 손이 가질 방향. 지금 방향에서 그쪽으로 가중치만큼
			const XMVECTOR targetModel = XMQuaternionMultiply(XMLoadFloat4(&m_LastRot[i]), worldToModelRot);
			const XMVECTOR desired = XMQuaternionNormalize(XMQuaternionMultiply(XMLoadFloat4(&av.TPose[i == 0 ? Humanoid::LeftHand : Humanoid::RightHand]), targetModel));
			const XMVECTOR current = RotationOf(pose, hand[i]);
			const XMVECTOR delta = XMQuaternionNormalize(XMQuaternionMultiply(XMQuaternionInverse(current), desired));
			RotateBone(pose, hand[i], XMQuaternionSlerp(XMQuaternionIdentity(), delta, m_RotBlend[i]));
		}
		XMStoreFloat3(&m_Result[i], XMVector3TransformCoord(Position(pose, hand[i]), pose.ModelToWorld));
	}
}

void HandsAnimator::OnInspectorGUI()
{
	UnityGUI::Slider("Weight", &Weight, 0.0f, 1.0f);
	if (UnityGUI::Float("Blend Speed", &BlendSpeed, 0)) BlendSpeed = (std::max)(0.1f, BlendSpeed);
	const char* names[2] = { "Left Hand", "Right Hand" };
	for (int i = 0; i < 2; ++i)
	{
		ImGui::PushID(i);
		UnityGUI::Label(names[i], 0, true);
		UnityGUI::GameObjectField("Target", &Hands[i].Target, 1);
		UnityGUI::GameObjectField("Hint (Elbow)", &Hands[i].Hint, 1);
		UnityGUI::Slider("Position Weight", &Hands[i].PositionWeight, 0.0f, 1.0f, 1);
		UnityGUI::Slider("Rotation Weight", &Hands[i].RotationWeight, 0.0f, 1.0f, 1);
		ImGui::PopID();
	}
	UnityGUI::Toggle("Show Gizmos", &ShowGizmos);
	bool hasAnimator = false;
	if (m_pGameObject)
		for (const auto& c : m_pGameObject->GetComponents())
			hasAnimator |= c && c->GetType() == "Animator";
	if (!hasAnimator)
		UnityGUI::HelpBox("Needs an Animator on this GameObject (the arms are found from its Humanoid avatar).", true);
	else if (Hands[0].Target == 0 && Hands[1].Target == 0)
		UnityGUI::HelpBox("Drag an object into Target (for example a weapon grip or a point on a wall), or call SetIKPosition from a script. "
			"Rotate the target to shape the hand: no rotation = the hand of the T-pose.", false);
}

void HandsAnimator::OnDrawGizmos()
{
	if (!ShowGizmos || !Application::IsPlaying() || m_pGameObject == nullptr || !SceneViewOverlay::IsActive() ||
		SelectionManager::GetSelectedObjectType() != SelectionType::GAMEOBJECT || SelectionManager::GetSelectedGameObject() != m_pGameObject)
		return;
	for (int i = 0; i < 2; ++i)
		if (m_GizmoValid[i])
		{
			const ImU32 c = i == 0 ? IM_COL32(90, 200, 255, 255) : IM_COL32(255, 160, 80, 255);
			const XMFLOAT3 g = m_LastGoal[i];
			SceneViewOverlay::DrawLine(XMFLOAT3(g.x - 0.05f, g.y, g.z), XMFLOAT3(g.x + 0.05f, g.y, g.z), c, 2.0f);
			SceneViewOverlay::DrawLine(XMFLOAT3(g.x, g.y - 0.05f, g.z), XMFLOAT3(g.x, g.y + 0.05f, g.z), c, 2.0f);
			SceneViewOverlay::DrawLine(XMFLOAT3(g.x, g.y, g.z - 0.05f), XMFLOAT3(g.x, g.y, g.z + 0.05f), c, 2.0f);
			SceneViewOverlay::DrawLine(m_Result[i], g, c, 1.0f);
		}
}

void HandsAnimator::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	for (Hand& h : Hands)
		for (uint64* id : { &h.Target, &h.Hint })
			if (auto it = map.find(*id); it != map.end())
				*id = it->second;
}

GENERATE_COMPONENT_FUNC_TOJSON(HandsAnimator)
{
	json j;
	j["type"] = "HandsAnimator";
	j["enabled"] = m_Enabled;
	j["weight"] = Weight;
	j["blendSpeed"] = BlendSpeed;
	j["showGizmos"] = ShowGizmos;
	json hands = json::array();
	for (const Hand& h : Hands)
		hands.push_back({ { "target", h.Target }, { "hint", h.Hint }, { "positionWeight", h.PositionWeight }, { "rotationWeight", h.RotationWeight } });
	j["hands"] = hands;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(HandsAnimator)
{
	m_Enabled = j.value("enabled", true);
	Weight = j.value("weight", 1.0f);
	BlendSpeed = j.value("blendSpeed", 8.0f);
	ShowGizmos = j.value("showGizmos", true);
	if (j.contains("hands") && j["hands"].is_array())
		for (size_t i = 0; i < 2 && i < j["hands"].size(); ++i)
		{
			const json& h = j["hands"][i];
			Hands[i].Target = h.value("target", (uint64)0);
			Hands[i].Hint = h.value("hint", (uint64)0);
			Hands[i].PositionWeight = h.value("positionWeight", 1.0f);
			Hands[i].RotationWeight = h.value("rotationWeight", 1.0f);
		}
}
