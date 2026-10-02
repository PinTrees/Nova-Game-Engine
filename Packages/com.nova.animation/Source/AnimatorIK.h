#pragma once
#include "HumanoidAvatar.h"

// Animator 가 포즈를 계산한 뒤 (Play 중) 같은 GameObject 의 이 인터페이스 컴포넌트들이 포즈를 고친다 — LegsAnimator, LookAnimator.
// 포즈는 스켈레톤 모델 공간 (렌더러 오브젝트의 월드 행렬 = ModelToWorld). 본은 Humanoid 아바타로 찾는다.
struct AnimatorPose
{
	const SkeletonAvataData& Skeleton;
	const Humanoid::Avatar& Avatar;
	std::vector<XMFLOAT4X4>& Local;
	std::vector<XMFLOAT4X4>& Global;
	XMMATRIX ModelToWorld;
	XMMATRIX WorldToModel;
	float DeltaTime = 0.0f;
};

class IAnimatorPoseModifier
{
public:
	virtual ~IAnimatorPoseModifier() = default;
	virtual int PoseOrder() const { return 0; }   // 작은 것부터 (다리 → 시선)
	virtual void ModifyPose(AnimatorPose& pose) = 0;
};

namespace AnimatorIK
{
	XMVECTOR Position(const AnimatorPose& pose, int bone);
	// 모델 공간 회전 q 를 본 위치를 중심으로 (자손이 따라온다)
	void RotateBone(AnimatorPose& pose, int bone, FXMVECTOR q);
	// 모델 공간 이동 (자손이 따라온다)
	void TranslateBone(AnimatorPose& pose, int bone, FXMVECTOR offset);
	// 방향 from 을 to 로 (가장 짧은 회전, 행 벡터 규약)
	XMVECTOR FromTo(FXMVECTOR from, FXMVECTOR to);
	// 두 뼈 IK: upper → lower → end 가 target 에 닿게 (무릎·팔꿈치는 지금 굽은 쪽으로)
	void SolveTwoBone(AnimatorPose& pose, int upper, int lower, int end, FXMVECTOR target);
	// 스크립트·컴포넌트가 프레임마다 쓰는 지수 감쇠 (speed = 1/초)
	inline float Damp(float current, float target, float speed, float dt) { return current + (target - current) * (1.0f - expf(-speed * dt)); }
}
