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

	XMVECTOR MatrixRotation(FXMMATRIX m)
	{
		XMVECTOR s, q, t;
		return XMMatrixDecompose(&s, &q, &t, m) ? XMQuaternionNormalize(q) : XMQuaternionIdentity();
	}

	void SolveTwoBoneHint(AnimatorPose& pose, int upper, int lower, int end, FXMVECTOR goalIn, float posWeight, const XMVECTOR* hint, float hintWeight)
	{
		if (upper < 0 || lower < 0 || end < 0 || posWeight <= 1e-4f)
			return;
		const XMVECTOR goal = XMVectorLerp(Position(pose, end), goalIn, std::clamp(posWeight, 0.0f, 1.0f));
		if (hint && hintWeight > 1e-4f)
		{
			// 어깨 → 목표 축에 수직인 성분끼리: 지금 팔꿈치 (무릎) 방향 → 힌트 방향. 먼저 돌려 두면 아래 풀이가 그쪽으로 굽힌다
			const XMVECTOR a = Position(pose, upper), b = Position(pose, lower);
			const XMVECTOR axis = goal - a;
			if (XMVectorGetX(XMVector3LengthSq(axis)) > 1e-8f)
			{
				const XMVECTOR n = XMVector3Normalize(axis);
				const XMVECTOR cur = (b - a) - n * XMVector3Dot(b - a, n);
				const XMVECTOR want = (*hint - a) - n * XMVector3Dot(*hint - a, n);
				RotateBone(pose, upper, XMQuaternionSlerp(XMQuaternionIdentity(), FromTo(cur, want), std::clamp(hintWeight, 0.0f, 1.0f)));
			}
		}
		SolveTwoBone(pose, upper, lower, end, goal);
	}

	void RotateToward(AnimatorPose& pose, int bone, FXMVECTOR desired, float weight)
	{
		if (bone < 0 || weight <= 1e-4f)
			return;
		const XMVECTOR current = RotationOf(pose, bone);
		// 모델 공간: 지금 다음 delta = 원하는 것 (XMQuaternionMultiply(a, b) = a 다음 b)
		const XMVECTOR delta = XMQuaternionNormalize(XMQuaternionMultiply(XMQuaternionInverse(current), desired));
		RotateBone(pose, bone, XMQuaternionSlerp(XMQuaternionIdentity(), delta, std::clamp(weight, 0.0f, 1.0f)));
	}

	void LookAt(AnimatorPose& pose, FXMVECTOR target, float weight, float body, float head, float clamp)
	{
		const Humanoid::Avatar& av = pose.Avatar;
		const int headNode = av.Valid ? av.Node[Humanoid::Head] : -1;
		if (headNode < 0 || weight <= 1e-4f || av.BindGlobal.size() != pose.Global.size())
			return;
		// 바인드 자세의 앞: 오른 어깨 - 왼 어깨 (오른쪽) × 위 (Y)
		const int la = av.Node[Humanoid::LeftUpperArm], ra = av.Node[Humanoid::RightUpperArm];
		XMVECTOR forward = XMVectorSet(0, 0, 1, 0);
		if (la >= 0 && ra >= 0)
		{
			const XMVECTOR right = XMVectorSubtract(XMVectorSet(av.BindGlobal[ra]._41, av.BindGlobal[ra]._42, av.BindGlobal[ra]._43, 0),
				XMVectorSet(av.BindGlobal[la]._41, av.BindGlobal[la]._42, av.BindGlobal[la]._43, 0));
			const XMVECTOR f = XMVector3Cross(right, XMVectorSet(0, 1, 0, 0));
			if (XMVectorGetX(XMVector3LengthSq(f)) > 1e-8f)
				forward = XMVector3Normalize(f);
		}
		const float maxAngle = XM_PI * (1.0f - std::clamp(clamp, 0.0f, 1.0f));
		// 본의 지금 앞 = 바인드 앞을 (바인드 → 지금) 회전으로
		auto facing = [&](int node) {
			const XMVECTOR bind = MatrixRotation(XMLoadFloat4x4(&av.BindGlobal[(size_t)node]));
			const XMVECTOR delta = XMQuaternionMultiply(XMQuaternionInverse(bind), RotationOf(pose, node));
			return XMVector3Rotate(forward, delta);
		};
		auto turn = [&](int node, float w) {
			if (node < 0 || w <= 1e-4f)
				return;
			const XMVECTOR from = facing(node);
			const XMVECTOR to = XMVector3Normalize(target - Position(pose, headNode));
			XMVECTOR q = FromTo(from, to);
			// 제한: 바인드 앞에서 maxAngle 넘게 돌지 않게
			const float angle = acosf(std::clamp(XMVectorGetX(XMVector3Dot(XMVector3Normalize(from), to)), -1.0f, 1.0f));
			float f = 1.0f;
			if (angle > maxAngle && angle > 1e-4f)
				f = maxAngle / angle;
			RotateBone(pose, node, XMQuaternionSlerp(XMQuaternionIdentity(), q, std::clamp(w * f, 0.0f, 1.0f)));
		};
		turn(av.Node[Humanoid::Spine], weight * body * 0.3f);
		turn(av.Node[Humanoid::Chest] >= 0 ? av.Node[Humanoid::Chest] : -1, weight * body * 0.4f);
		turn(av.Node[Humanoid::Neck], weight * head * 0.4f);
		turn(headNode, weight * head);
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
		// 길이 0 인 뼈면 아래 한계가 위 한계를 넘는다 (std::clamp 는 Debug 에서 assert)
		const float lo = fabsf(l1 - l2) + 1e-4f;
		const float d = std::clamp(rawD, lo, (std::max)(lo, (l1 + l2) * 0.9995f));
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
