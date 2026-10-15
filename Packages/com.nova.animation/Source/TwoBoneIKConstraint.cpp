#include "pch.h"
#include "TwoBoneIKConstraint.h"
#include "SkinnedMeshRenderer.h"
#include "UnityGUI.h"

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

	std::shared_ptr<SkeletonAvataData> FindSkeleton(GameObject* go)
	{
		if (go == nullptr)
			return nullptr;
		if (auto* r = go->GetComponent<SkinnedMeshRenderer>())
			if (auto s = r->GetSkeleton())
				return s;
		for (GameObject* c : go->GetChildren())
			if (auto s = FindSkeleton(c))
				return s;
		return nullptr;
	}
}

TwoBoneIKConstraint::TwoBoneIKConstraint()
{
	m_InspectorTitleName = "Two Bone IK Constraint";
}

bool TwoBoneIKConstraint::AutoSetup(int limb)
{
	auto skeleton = FindSkeleton(m_pGameObject);
	if (!skeleton || limb < 0 || limb > 3)
		return false;
	const Humanoid::Avatar& av = Humanoid::Get(*skeleton);
	if (!av.Valid)
		return false;
	using namespace Humanoid;
	const int bones[4][3] = { { LeftUpperLeg, LeftLowerLeg, LeftFoot }, { RightUpperLeg, RightLowerLeg, RightFoot },
		{ LeftUpperArm, LeftLowerArm, LeftHand }, { RightUpperArm, RightLowerArm, RightHand } };
	std::string* names[3] = { &Root, &Mid, &Tip };
	for (int i = 0; i < 3; ++i)
	{
		const int node = av.Node[bones[limb][i]];
		if (node < 0 || node >= (int)skeleton->NodeNames.size())
			return false;
		*names[i] = skeleton->NodeNames[(size_t)node];
	}
	m_NodesFor = nullptr;
	return true;
}

void TwoBoneIKConstraint::ModifyPose(AnimatorPose& pose)
{
	GameObject* target = FindObject(Target);
	if (target == nullptr || Weight <= 1e-4f)
	{
		m_OffsetValid = false;
		return;
	}
	const std::string key = Root + "|" + Mid + "|" + Tip;
	if (m_NodesFor != &pose.Skeleton || m_NodesKey != key)
	{
		m_NodesFor = &pose.Skeleton;
		m_NodesKey = key;
		auto find = [&](const std::string& name) {
			for (size_t i = 0; i < pose.Skeleton.NodeNames.size(); ++i)
				if (pose.Skeleton.NodeNames[i] == name)
					return (int)i;
			return -1;
		};
		m_Nodes[0] = find(Root);
		m_Nodes[1] = find(Mid);
		m_Nodes[2] = find(Tip);
		m_OffsetValid = false;
	}
	if (m_Nodes[0] < 0 || m_Nodes[1] < 0 || m_Nodes[2] < 0 || m_Nodes[2] >= (int)pose.Global.size())
		return;
	const Vec3 p = target->GetTransform()->GetPosition();
	const Quaternion q = target->GetTransform()->GetRotation();
	const XMVECTOR targetModel = XMVector3TransformCoord(XMVectorSet(p.x, p.y, p.z, 1.0f), pose.WorldToModel);
	const XMVECTOR targetRot = XMQuaternionNormalize(XMQuaternionMultiply(XMVectorSet(q.x, q.y, q.z, q.w), MatrixRotation(pose.WorldToModel)));
	if ((MaintainTargetPositionOffset || MaintainTargetRotationOffset) && !m_OffsetValid)
	{
		// 처음 켜질 때의 차이 (애니메이션 자세의 끝 − 목표)
		XMStoreFloat3(&m_PosOffset, Position(pose, m_Nodes[2]) - targetModel);
		XMStoreFloat4(&m_RotOffset, XMQuaternionMultiply(RotationOf(pose, m_Nodes[2]), XMQuaternionInverse(targetRot)));
		m_OffsetValid = true;
	}
	XMVECTOR goal = targetModel;
	if (MaintainTargetPositionOffset)
		goal = goal + XMLoadFloat3(&m_PosOffset);
	XMVECTOR hint;
	const XMVECTOR* hintPtr = nullptr;
	if (GameObject* h = FindObject(Hint))
	{
		const Vec3 hp = h->GetTransform()->GetPosition();
		hint = XMVector3TransformCoord(XMVectorSet(hp.x, hp.y, hp.z, 1.0f), pose.WorldToModel);
		hintPtr = &hint;
	}
	SolveTwoBoneHint(pose, m_Nodes[0], m_Nodes[1], m_Nodes[2], goal, Weight * TargetPositionWeight, hintPtr, HintWeight);
	const XMVECTOR desired = MaintainTargetRotationOffset ? XMQuaternionMultiply(XMLoadFloat4(&m_RotOffset), targetRot) : targetRot;
	RotateToward(pose, m_Nodes[2], desired, Weight * TargetRotationWeight);
}

void TwoBoneIKConstraint::OnInspectorGUI()
{
	using namespace UnityGUI;
	Slider("Weight", &Weight, 0.0f, 1.0f);
	TextField("Root", &Root);
	TextField("Mid", &Mid);
	TextField("Tip", &Tip);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14);
	const char* limbs[4] = { "Left Foot", "Right Foot", "Left Hand", "Right Hand" };
	ImGui::TextDisabled("Auto Setup:");
	for (int i = 0; i < 4; ++i)
	{
		ImGui::SameLine();
		if (ImGui::SmallButton(limbs[i]))
			AutoSetup(i);
	}
	GameObjectField("Target", &Target);
	GameObjectField("Hint", &Hint);
	Slider("Target Position Weight", &TargetPositionWeight, 0.0f, 1.0f);
	Slider("Target Rotation Weight", &TargetRotationWeight, 0.0f, 1.0f);
	Slider("Hint Weight", &HintWeight, 0.0f, 1.0f);
	if (Toggle("Maintain Target Position Offset", &MaintainTargetPositionOffset) | Toggle("Maintain Target Rotation Offset", &MaintainTargetRotationOffset))
		m_OffsetValid = false;
	if (Target == 0)
		HelpBox("Drag the object the tip should reach into Target (Play mode). The Hint is where the middle joint (elbow, knee) points.", false);
}

void TwoBoneIKConstraint::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	for (uint64* id : { &Target, &Hint })
		if (auto it = map.find(*id); it != map.end())
			*id = it->second;
}

GENERATE_COMPONENT_FUNC_TOJSON(TwoBoneIKConstraint)
{
	json j;
	j["type"] = "TwoBoneIKConstraint";
	j["enabled"] = m_Enabled;
	j["root"] = Root;
	j["mid"] = Mid;
	j["tip"] = Tip;
	j["target"] = Target;
	j["hint"] = Hint;
	j["weight"] = Weight;
	j["targetPositionWeight"] = TargetPositionWeight;
	j["targetRotationWeight"] = TargetRotationWeight;
	j["hintWeight"] = HintWeight;
	j["maintainTargetPositionOffset"] = MaintainTargetPositionOffset;
	j["maintainTargetRotationOffset"] = MaintainTargetRotationOffset;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(TwoBoneIKConstraint)
{
	m_Enabled = j.value("enabled", true);
	Root = j.value("root", std::string());
	Mid = j.value("mid", std::string());
	Tip = j.value("tip", std::string());
	Target = j.value("target", (uint64)0);
	Hint = j.value("hint", (uint64)0);
	Weight = j.value("weight", 1.0f);
	TargetPositionWeight = j.value("targetPositionWeight", 1.0f);
	TargetRotationWeight = j.value("targetRotationWeight", 1.0f);
	HintWeight = j.value("hintWeight", 1.0f);
	MaintainTargetPositionOffset = j.value("maintainTargetPositionOffset", false);
	MaintainTargetRotationOffset = j.value("maintainTargetRotationOffset", false);
	m_NodesFor = nullptr;
	m_OffsetValid = false;
}
