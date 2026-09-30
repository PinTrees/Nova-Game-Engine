#include "pch.h"
#include "AnimationPlayer.h"
#include "EditorGUI.h"

AnimationPlayer::AnimationPlayer()
{
	m_InspectorTitleName = "Animation"; 
}

AnimationPlayer::~AnimationPlayer()
{
}

void AnimationPlayer::GetFinalTransforms(float timePos, std::vector<XMFLOAT4X4>& finalTransforms) const
{
	uint32 numBones = m_pSkinnedMeshRenderer->GetFinalTransforms().size(); 
	std::vector<XMFLOAT4X4> toParentTransforms(numBones);

	m_AnimationClip->Interpolate(timePos, toParentTransforms);
	 
	//
	// Traverse the hierarchy and transform all the bones to the root space.
	//

	std::vector<XMFLOAT4X4> toRootTransforms(numBones);

	// The root bone has index 0.  The root bone has no parent, so its toRootTransform
	// is just its local bone transform.
	toRootTransforms[0] = toParentTransforms[0];

	// Now find the toRootTransform of the children.
	for (uint32 i = 1; i < numBones; ++i)
	{
		XMMATRIX toParent = XMLoadFloat4x4(&toParentTransforms[i]);

		int parentIndex = m_SkeletonAvata->BoneHierarchy[i];
		XMMATRIX parentToRoot = XMLoadFloat4x4(&toRootTransforms[parentIndex]);

		XMMATRIX toRoot = XMMatrixMultiply(toParent, parentToRoot);

		XMStoreFloat4x4(&toRootTransforms[i], toRoot);
	}

	// Premultiply by the bone offset transform to get the final transform.
	for (uint32 i = 0; i < numBones; ++i)
	{
		XMMATRIX offset = XMLoadFloat4x4(&(m_pSkinnedMeshRenderer->GetFinalTransforms()[i]));
		XMMATRIX toRoot = XMLoadFloat4x4(&toRootTransforms[i]);
		XMStoreFloat4x4(&finalTransforms[i], XMMatrixMultiply(offset, toRoot));
	}
}

void AnimationPlayer::Awake()
{
	m_pSkinnedMeshRenderer = m_pGameObject->GetComponent_SP<SkinnedMeshRenderer>();
	
	if (m_pSkinnedMeshRenderer == nullptr)
		return;
	if (m_AnimationClip == nullptr)
		return;
	if (m_SkeletonAvata == nullptr)
		return;

	m_pSkinnedMeshRenderer->GetFinalTransforms().resize(m_SkeletonAvata->BoneHierarchy.size());
	//for (int i = 0; i < m_pSkinnedMeshRenderer->GetFinalTransforms().size(); ++i)
	//{
	//	XMStoreFloat4x4(&m_pSkinnedMeshRenderer->GetFinalTransforms()[i], XMMatrixIdentity());
	//}
}

void AnimationPlayer::Update()
{
	if (m_pSkinnedMeshRenderer == nullptr)
		return;
	if (m_SkeletonAvata == nullptr)
		return;

	m_TimePos += DT;  
	GetFinalTransforms(m_TimePos, m_pSkinnedMeshRenderer->GetFinalTransforms()); 

	auto skinnedMesh = m_pSkinnedMeshRenderer->GetMesh();
	if (m_TimePos > m_AnimationClip->GetClipEndTime())
		m_TimePos = 0.0f;
}

void AnimationPlayer::LastUpdate()
{

}

void AnimationPlayer::OnInspectorGUI()
{
	EditorGUI::AnimationClipField("Animation Clip", m_AnimationClip, m_AnimationFilePath, m_AnimationClipIndex);
	EditorGUI::SkeletonAvataField("Skeleton Avata", m_SkeletonAvata, m_SkeletoneAvataFilePath, m_SkeletoneAvataIndex); 
	EditorGUI::BoolField("Play Automatically", m_PlayAutomatically);
}



GENERATE_COMPONENT_FUNC_TOJSON(AnimationPlayer)
{
	json j;
	SERIALIZE_TYPE(j, AnimationPlayer);
	SERIALIZE_STRING(j, m_AnimationFilePath, "m_AnimationFilePath");
	SERIALIZE_INT(j, m_AnimationClipIndex, "m_AnimationClipIndex"); 
	SERIALIZE_STRING(j, m_SkeletoneAvataFilePath, "m_SkeletoneAvataFilePath");
	SERIALIZE_INT(j, m_SkeletoneAvataIndex, "m_SkeletoneAvataIndex");

	SERIALIZE_BOOL(j, m_PlayAutomatically, "m_PlayAutomatically");

	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(AnimationPlayer)
{
	DE_SERIALIZE_STRING(j, m_AnimationFilePath, "m_AnimationFilePath"); 
	DE_SERIALIZE_INT(j, m_AnimationClipIndex, "m_AnimationClipIndex");
	DE_SERIALIZE_STRING(j, m_SkeletoneAvataFilePath, "m_SkeletoneAvataFilePath");
	DE_SERIALIZE_INT(j, m_SkeletoneAvataIndex, "m_SkeletoneAvataIndex");
	DE_SERIALIZE_BOOL(j, m_PlayAutomatically, "m_PlayAutomatically");

	if (m_AnimationFilePath != "") 
	{
		m_AnimationClip = ResourceManager::GetI()->LoadAnimationClip(m_AnimationFilePath, m_AnimationClipIndex);
	}
	if (m_SkeletoneAvataFilePath != "")
	{
		m_SkeletonAvata = ResourceManager::GetI()->LoadSkeletonAvata(m_SkeletoneAvataFilePath, m_SkeletoneAvataIndex);
	}
}