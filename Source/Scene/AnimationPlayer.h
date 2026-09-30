#pragma once
#include "Component.h"

class SkinnedMeshRenderer;

enum class UpdateMode
{
	Normal,
	Fixed,
	UnScale,
};

class AnimationPlayer : public Component
{
	using Super = Component;

private:
	shared_ptr<AnimationClip>		m_AnimationClip;
	shared_ptr<SkeletonAvataData>	m_SkeletonAvata;

	// Hidden
	string						m_AnimationFilePath;
	int							m_AnimationClipIndex;

	string						m_SkeletoneAvataFilePath;
	int							m_SkeletoneAvataIndex;

	bool		m_PlayAutomatically = true; 

	// Hidden
	float		m_TimePos;
	UpdateMode	m_UpdateMode; 

	// Runtime Value
	shared_ptr<SkinnedMeshRenderer> m_pSkinnedMeshRenderer; 
public:
	AnimationPlayer();
	virtual ~AnimationPlayer();

private:
	void GetFinalTransforms(float timePos, std::vector<XMFLOAT4X4>& finalTransforms) const;

public:
	virtual void Awake() override; 
	virtual void Update() override;
	virtual void LastUpdate() override;

public:
	// Editor
	virtual void OnInspectorGUI() override;

	GENERATE_COMPONENT_BODY(AnimationPlayer)  
};

REGISTER_COMPONENT(AnimationPlayer) 
 