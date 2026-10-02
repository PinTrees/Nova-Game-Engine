#include "pch.h"
#include "AnimatorIK.h"

namespace AnimatorIK
{
	namespace
	{
		XMMATRIX ParentGlobal(const AnimatorPose& pose, int bone)
		{
			const int parent = pose.Skeleton.BoneHierarchy[bone];
			if (parent >= 0)
				return XMLoadFloat4x4(&pose.Global[parent]);
			const float u = pose.Skeleton.UnitScale;
			return XMMatrixScaling(u, u, u);
		}

		// bone 의 새 전역 행렬 → 로컬, 그리고 자손 전역을 다시 (부모가 늘 앞이다)
		void SetGlobal(AnimatorPose& pose, int bone, FXMMATRIX g)
		{
			XMVECTOR det;
			XMStoreFloat4x4(&pose.Global[bone], g);
			XMStoreFloat4x4(&pose.Local[bone], g * XMMatrixInverse(&det, ParentGlobal(pose, bone)));
			const int n = (int)pose.Global.size();
			std::vector<char> moved(n, 0);
			moved[bone] = 1;
			for (int i = bone + 1; i < n; ++i)
			{
				const int p = pose.Skeleton.BoneHierarchy[i];
				if (p < 0 || !moved[p])
					continue;
				moved[i] = 1;
				XMStoreFloat4x4(&pose.Global[i], XMLoadFloat4x4(&pose.Local[i]) * XMLoadFloat4x4(&pose.Global[p]));
			}
		}
	}

	XMVECTOR Position(const AnimatorPose& pose, int bone)
	{
		const XMFLOAT4X4& g = pose.Global[bone];
		return XMVectorSet(g._41, g._42, g._43, 1.0f);
	}

	XMVECTOR RotationOf(const AnimatorPose& pose, int bone)
	{
		XMVECTOR s, q, t;
		return XMMatrixDecompose(&s, &q, &t, XMLoadFloat4x4(&pose.Global[bone])) ? XMQuaternionNormalize(q) : XMQuaternionIdentity();
	}

	void RotateBone(AnimatorPose& pose, int bone, FXMVECTOR q)
	{
		if (bone < 0)
			return;
		const XMVECTOR p = Position(pose, bone);
		const XMMATRIX g = XMLoadFloat4x4(&pose.Global[bone]) * XMMatrixTranslationFromVector(XMVectorNegate(XMVectorSetW(p, 0.0f)))
			* XMMatrixRotationQuaternion(q) * XMMatrixTranslationFromVector(XMVectorSetW(p, 0.0f));
		SetGlobal(pose, bone, g);
	}

	void TranslateBone(AnimatorPose& pose, int bone, FXMVECTOR offset)
	{
		if (bone < 0)
			return;
		SetGlobal(pose, bone, XMLoadFloat4x4(&pose.Global[bone]) * XMMatrixTranslationFromVector(XMVectorSetW(offset, 0.0f)));
	}

	XMVECTOR FromTo(FXMVECTOR fromIn, FXMVECTOR toIn)
	{
		// 길이 0 이면 방향이 없다 → 회전 없음 (축 0 이면 DirectXMath assert)
		if (XMVectorGetX(XMVector3LengthSq(fromIn)) < 1e-12f || XMVectorGetX(XMVector3LengthSq(toIn)) < 1e-12f)
			return XMQuaternionIdentity();
		const XMVECTOR from = XMVector3Normalize(fromIn), to = XMVector3Normalize(toIn);
		const float d = std::clamp(XMVectorGetX(XMVector3Dot(from, to)), -1.0f, 1.0f);
		if (d > 0.99999f)
			return XMQuaternionIdentity();
		XMVECTOR axis = XMVector3Cross(from, to);
		if (XMVectorGetX(XMVector3LengthSq(axis)) < 1e-10f)
			axis = XMVector3Cross(from, fabsf(XMVectorGetX(from)) < 0.9f ? XMVectorSet(1, 0, 0, 0) : XMVectorSet(0, 1, 0, 0));
		axis = XMVector3Normalize(axis);
		const float angle = acosf(d);
		XMVECTOR q = XMQuaternionRotationAxis(axis, angle);
		if (XMVectorGetX(XMVector3Dot(XMVector3Transform(from, XMMatrixRotationQuaternion(q)), to)) < d + 1e-5f)
			q = XMQuaternionRotationAxis(axis, -angle);   // 규약이 반대면
		return q;
	}

	void SolveTwoBone(AnimatorPose& pose, int upper, int lower, int end, FXMVECTOR target)
	{
		if (upper < 0 || lower < 0 || end < 0)
			return;
		const XMVECTOR a = Position(pose, upper), b = Position(pose, lower), c = Position(pose, end);
		const float l1 = XMVectorGetX(XMVector3Length(b - a)), l2 = XMVectorGetX(XMVector3Length(c - b));
		if (l1 < 1e-5f || l2 < 1e-5f)
			return;
		XMVECTOR at = target - a;
		const float rawD = XMVectorGetX(XMVector3Length(at));
		if (rawD < 1e-5f)
			return;
		const XMVECTOR dir = at / rawD;
		const float d = std::clamp(rawD, fabsf(l1 - l2) + 1e-4f, (l1 + l2) * 0.9995f);
		// 굽는 쪽: 지금 무릎이 a→target 선에서 떨어진 방향
		XMVECTOR pole = (b - a) - dir * XMVector3Dot(b - a, dir);
		if (XMVectorGetX(XMVector3LengthSq(pole)) < 1e-10f)
			pole = XMVector3Cross(dir, XMVectorSet(1, 0, 0, 0));
		pole = XMVector3Normalize(pole);
		const float cosA = std::clamp((l1 * l1 + d * d - l2 * l2) / (2.0f * l1 * d), -1.0f, 1.0f);
		const float sinA = sqrtf((std::max)(0.0f, 1.0f - cosA * cosA));
		const XMVECTOR knee = a + dir * (l1 * cosA) + pole * (l1 * sinA);
		RotateBone(pose, upper, FromTo(b - a, knee - a));
		const XMVECTOR b2 = Position(pose, lower), c2 = Position(pose, end);
		const XMVECTOR goal = a + dir * d;   // 닿을 수 있는 만큼
		RotateBone(pose, lower, FromTo(c2 - b2, goal - b2));
	}
}
