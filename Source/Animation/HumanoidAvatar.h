#pragma once
#include "SkinnedData.h"

// Unity 의 Humanoid Avatar 와 같은 생각: 스켈레톤마다 사람 본(Hips, Spine, 팔·다리 ...)이 어느 노드인지 자동으로 찾고,
// 본 이름이 다른 모델끼리 애니메이션을 옮긴다 (리타게팅).
//  - 매핑: 이름 단서(hips/pelvis, spine, neck, head, clavicle/shoulder, arm, hand, thigh/upleg/leg, foot …, 좌우 = left/right/_l/_r/ L )
//    + 계층(위팔 = 그쪽에서 가장 위의 arm, 손 = 그 아래 hand, 아래팔 = 그 사이) — Mixamo · UE 마네킹 · 3ds Max Biped · Unity 샘플 등
//  - 옮기기: 두 스켈레톤 모두 "T-포즈" 방향(팔은 옆으로, 다리는 아래, 척추는 위)을 기준으로, 모델 공간 회전 차이를 그대로 옮긴다.
//    A-포즈 바인드도 T-포즈로 보정하므로 팔 각도가 맞는다. Hips 이동은 다리 길이 비율로 줄이거나 늘린다.
namespace Humanoid
{
	enum Bone
	{
		Hips, Spine, Chest, UpperChest, Neck, Head,
		LeftShoulder, LeftUpperArm, LeftLowerArm, LeftHand,
		RightShoulder, RightUpperArm, RightLowerArm, RightHand,
		LeftUpperLeg, LeftLowerLeg, LeftFoot, LeftToes,
		RightUpperLeg, RightLowerLeg, RightFoot, RightToes,
		BoneCount
	};
	NOVA_API const char* BoneName(int bone);

	struct Avatar
	{
		const SkeletonAvataData* Skeleton = nullptr;
		int Node[BoneCount];
		bool Valid = false;            // 필수 본(Hips · Spine · Head · 팔 3 · 다리 3, 양쪽)을 다 찾았다
		int Found = 0;
		std::vector<XMFLOAT4X4> BindGlobal;
		XMFLOAT4 TPose[BoneCount];     // 모델 공간 T-포즈 방향 (쿼터니언)
		XMFLOAT3 HipsBind = {};
		float LegLength = 1.0f;
		std::vector<int> HumanOf;      // 노드 → 사람 본 (-1 = 아님)
	};

	// 스켈레톤의 아바타 (처음 한 번 계산해 둔다)
	NOVA_API const Avatar& Get(const SkeletonAvataData& skeleton);

	// source 스켈레톤의 클립을 시간 t 에 샘플해 target 스켈레톤의 로컬 포즈로 (사람 본이 아닌 노드는 바인드 그대로)
	NOVA_API void Retarget(const Avatar& source, const Avatar& target, const AnimationClip& clip, const std::vector<int>& sourceMap, float t,
		std::vector<XMFLOAT4X4>& outLocal);

	// source 모델 공간의 Hips 위치 → target 모델 공간 (다리 길이 비율) — 루트 모션
	NOVA_API XMFLOAT3 ScaleHips(const Avatar& source, const Avatar& target, const XMFLOAT3& sourceHips);
}
