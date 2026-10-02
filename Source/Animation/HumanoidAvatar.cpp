#include "pch.h"
#include "HumanoidAvatar.h"
#include "AnimationPose.h"

namespace Humanoid
{
	namespace
	{
		const char* kNames[BoneCount] = {
			"Hips", "Spine", "Chest", "UpperChest", "Neck", "Head",
			"LeftShoulder", "LeftUpperArm", "LeftLowerArm", "LeftHand",
			"RightShoulder", "RightUpperArm", "RightLowerArm", "RightHand",
			"LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "LeftToes",
			"RightUpperLeg", "RightLowerLeg", "RightFoot", "RightToes",
		};

		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return s;
		}

		std::vector<std::string> Tokens(const std::string& lower)
		{
			std::vector<std::string> out;
			std::string cur;
			for (char c : lower)
			{
				if (std::isalnum((unsigned char)c))
					cur += c;
				else if (!cur.empty())
				{
					out.push_back(cur);
					cur.clear();
				}
			}
			if (!cur.empty())
				out.push_back(cur);
			return out;
		}

		bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
		bool HasToken(const std::vector<std::string>& t, const char* tok) { return std::find(t.begin(), t.end(), tok) != t.end(); }

		struct NodeInfo
		{
			std::string L;                 // 소문자 이름
			std::vector<std::string> T;    // 토큰
			int Side = 0;                  // 1 왼쪽, 2 오른쪽
			bool Excluded = false;         // 보조 본 (twist, end, ik, target …)
			int Depth = 0;
			int Descendants = 0;
		};

		std::vector<NodeInfo> Describe(const SkeletonAvataData& s)
		{
			const int n = (int)s.NodeNames.size();
			std::vector<NodeInfo> info(n);
			for (int i = 0; i < n; ++i)
			{
				NodeInfo& d = info[i];
				d.L = Lower(s.NodeNames[i]);
				d.T = Tokens(d.L);
				d.Side = Has(d.L, "left") || HasToken(d.T, "l") ? 1 : (Has(d.L, "right") || HasToken(d.T, "r") ? 2 : 0);
				d.Excluded = Has(d.L, "twist") || Has(d.L, "end") || Has(d.L, "nub") || Has(d.L, "target") || Has(d.L, "weapon") ||
					Has(d.L, "roll") || Has(d.L, "socket") || Has(d.L, "helper") || Has(d.L, "attach") || Has(d.L, "footstep") ||
					HasToken(d.T, "ik") || d.L.rfind("ik", 0) == 0;
				const int p = i < (int)s.BoneHierarchy.size() ? s.BoneHierarchy[i] : -1;
				d.Depth = p >= 0 && p < i ? info[p].Depth + 1 : 0;
			}
			for (int i = n - 1; i >= 0; --i)
			{
				const int p = s.BoneHierarchy[i];
				if (p >= 0 && p < i)
					info[p].Descendants += info[i].Descendants + 1;
			}
			return info;
		}

		bool IsUnder(const SkeletonAvataData& s, int node, int ancestor)
		{
			if (ancestor < 0)
				return true;
			for (int p = node >= 0 ? s.BoneHierarchy[node] : -1; p >= 0; p = s.BoneHierarchy[p])
				if (p == ancestor)
					return true;
			return false;
		}

		// node 와 ancestor 사이의 노드들 (ancestor 쪽부터, 둘 다 빼고)
		std::vector<int> Between(const SkeletonAvataData& s, int node, int ancestor)
		{
			std::vector<int> out;
			for (int p = s.BoneHierarchy[node]; p >= 0 && p != ancestor; p = s.BoneHierarchy[p])
				out.push_back(p);
			std::reverse(out.begin(), out.end());
			return out;
		}

		XMFLOAT3 Pos(const std::vector<XMFLOAT4X4>& g, int node) { return XMFLOAT3(g[node]._41, g[node]._42, g[node]._43); }

		XMVECTOR RotationOf(const XMFLOAT4X4& m)
		{
			XMVECTOR s, r, t;
			if (!XMMatrixDecompose(&s, &r, &t, XMLoadFloat4x4(&m)))
				return XMQuaternionIdentity();
			return XMQuaternionNormalize(r);
		}

		XMVECTOR ScaleOf(const XMFLOAT4X4& m)
		{
			XMVECTOR s, r, t;
			if (!XMMatrixDecompose(&s, &r, &t, XMLoadFloat4x4(&m)))
				return XMVectorSet(1, 1, 1, 0);
			return s;
		}

		// 방향 from 을 to 로 돌리는 가장 짧은 회전 (행 벡터 규약: v * Rotation(q))
		XMVECTOR FromTo(XMVECTOR from, XMVECTOR to)
		{
			from = XMVector3Normalize(from);
			to = XMVector3Normalize(to);
			const float d = std::clamp(XMVectorGetX(XMVector3Dot(from, to)), -1.0f, 1.0f);
			if (d > 0.9999f)
				return XMQuaternionIdentity();
			XMVECTOR axis = XMVector3Cross(from, to);
			if (XMVectorGetX(XMVector3LengthSq(axis)) < 1e-8f)
				axis = fabsf(XMVectorGetX(from)) < 0.9f ? XMVector3Cross(from, XMVectorSet(1, 0, 0, 0)) : XMVector3Cross(from, XMVectorSet(0, 1, 0, 0));
			axis = XMVector3Normalize(axis);
			const float angle = acosf(d);
			XMVECTOR q = XMQuaternionRotationAxis(axis, angle);
			// 규약을 확인해 반대면 뒤집는다
			if (XMVectorGetX(XMVector3Dot(XMVector3Transform(from, XMMatrixRotationQuaternion(q)), to)) < d + 1e-4f)
				q = XMQuaternionRotationAxis(axis, -angle);
			return q;
		}

		void Build(Avatar& a, const SkeletonAvataData& s)
		{
			a.Skeleton = &s;
			std::fill(std::begin(a.Node), std::end(a.Node), -1);
			const int n = (int)s.NodeNames.size();
			a.HumanOf.assign(n, -1);
			if (n == 0 || s.BoneHierarchy.size() != (size_t)n || s.BindLocal.size() != (size_t)n)
				return;
			AnimationPose::ComputeGlobals(s, s.BindLocal, a.BindGlobal);
			const std::vector<NodeInfo> info = Describe(s);

			// 조건에 맞는 노드 중 가장 위쪽 (깊이가 같으면 앞)
			auto findMin = [&](auto&& pred, int under) {
				int best = -1;
				for (int i = 0; i < n; ++i)
					if (!info[i].Excluded && pred(info[i]) && i != under && IsUnder(s, i, under) && (best < 0 || info[i].Depth < info[best].Depth))
						best = i;
				return best;
			};

			int* N = a.Node;
			N[Hips] = findMin([](const NodeInfo& d) { return Has(d.L, "hips") || Has(d.L, "pelvis"); }, -1);
			if (N[Hips] < 0)
				return;
			const int hips = N[Hips];
			N[Head] = findMin([](const NodeInfo& d) { return Has(d.L, "head") && !Has(d.L, "headtop"); }, hips);
			N[Neck] = findMin([](const NodeInfo& d) { return Has(d.L, "neck"); }, hips);
			if (N[Neck] >= 0 && N[Head] >= 0 && !IsUnder(s, N[Head], N[Neck]))
				N[Neck] = -1;
			// 척추: Hips 와 목(없으면 머리) 사이
			if (N[Head] >= 0)
			{
				std::vector<int> chain = Between(s, N[Neck] >= 0 ? N[Neck] : N[Head], hips);
				if (!chain.empty()) N[Spine] = chain[0];
				if (chain.size() >= 2) N[Chest] = chain[1];
				if (chain.size() >= 3) N[UpperChest] = chain.back();
			}
			for (int side = 1; side <= 2; ++side)
			{
				const int o = side == 1 ? 0 : 4;   // 팔: Left… / Right… 간격
				const int lg = side == 1 ? 0 : 4;  // 다리
				auto sideIs = [side](const NodeInfo& d) { return d.Side == side; };
				// 다리: 그쪽에서 가장 위의 thigh / upleg / leg → 그 아래 foot → 사이 = 종아리
				const int upLeg = findMin([&](const NodeInfo& d) {
					return sideIs(d) && (Has(d.L, "thigh") || Has(d.L, "upleg") || Has(d.L, "upperleg") ||
						(Has(d.L, "leg") && !Has(d.L, "lowleg") && !Has(d.L, "lowerleg") && !Has(d.L, "foreleg")));
				}, hips);
				if (upLeg >= 0)
				{
					N[LeftUpperLeg + lg] = upLeg;
					const int foot = findMin([](const NodeInfo& d) { return Has(d.L, "foot"); }, upLeg);
					if (foot >= 0)
					{
						const std::vector<int> mid = Between(s, foot, upLeg);
						if (!mid.empty())
						{
							N[LeftLowerLeg + lg] = mid.back();
							N[LeftFoot + lg] = foot;
						}
						// 발가락: 발의 가장 큰 자식
						int toe = -1;
						for (int c = 0; c < n; ++c)
							if (s.BoneHierarchy[c] == foot && !info[c].Excluded && (toe < 0 || info[c].Descendants > info[toe].Descendants))
								toe = c;
						N[LeftToes + lg] = toe;
					}
				}
				// 팔: 그쪽에서 가장 위의 arm (forearm / lowerarm 아님) → 그 아래 hand → 사이 = 아래팔
				const int upArm = findMin([&](const NodeInfo& d) {
					return sideIs(d) && Has(d.L, "arm") && !Has(d.L, "forearm") && !Has(d.L, "lowerarm") && !Has(d.L, "lowarm");
				}, hips);
				if (upArm >= 0)
				{
					N[LeftUpperArm + o] = upArm;
					const int hand = findMin([](const NodeInfo& d) { return Has(d.L, "hand"); }, upArm);
					if (hand >= 0)
					{
						const std::vector<int> mid = Between(s, hand, upArm);
						if (!mid.empty())
						{
							N[LeftLowerArm + o] = mid.back();
							N[LeftHand + o] = hand;
						}
					}
					const int shoulder = findMin([&](const NodeInfo& d) {
						return sideIs(d) && (Has(d.L, "clavicle") || Has(d.L, "shoulder") || Has(d.L, "collar"));
					}, hips);
					if (shoulder >= 0 && IsUnder(s, upArm, shoulder))
						N[LeftShoulder + o] = shoulder;
				}
			}

			const Bone required[] = { Hips, Spine, Head, LeftUpperArm, LeftLowerArm, LeftHand, RightUpperArm, RightLowerArm, RightHand,
				LeftUpperLeg, LeftLowerLeg, LeftFoot, RightUpperLeg, RightLowerLeg, RightFoot };
			a.Valid = true;
			for (Bone b : required)
				a.Valid = a.Valid && N[b] >= 0;
			for (int b = 0; b < BoneCount; ++b)
				if (N[b] >= 0)
				{
					++a.Found;
					a.HumanOf[N[b]] = b;
				}
			if (!a.Valid)
				return;

			// ---- T-포즈 방향: 바인드 방향 · (뼈가 가리키는 쪽 → 표준 방향 보정)
			const XMFLOAT3 hp = Pos(a.BindGlobal, hips);
			a.HipsBind = hp;
			auto vec = [&](int from, int to) {
				const XMFLOAT3 p0 = Pos(a.BindGlobal, from), p1 = Pos(a.BindGlobal, to);
				return XMVectorSet(p1.x - p0.x, p1.y - p0.y, p1.z - p0.z, 0.0f);
			};
			auto length = [&](int from, int to) { return XMVectorGetX(XMVector3Length(vec(from, to))); };
			a.LegLength = 0.5f * (length(N[LeftUpperLeg], N[LeftLowerLeg]) + length(N[LeftLowerLeg], N[LeftFoot]) +
				length(N[RightUpperLeg], N[RightLowerLeg]) + length(N[RightLowerLeg], N[RightFoot]));
			if (a.LegLength < 1e-4f)
				a.LegLength = 1.0f;

			XMVECTOR corr[BoneCount];
			for (int b = 0; b < BoneCount; ++b)
				corr[b] = XMQuaternionIdentity();
			const XMVECTOR up = XMVectorSet(0, 1, 0, 0), down = XMVectorSet(0, -1, 0, 0);
			auto outward = [&](int hand, float fallback) {
				const float dx = Pos(a.BindGlobal, hand).x - hp.x;
				return XMVectorSet(fabsf(dx) > 1e-4f ? (dx > 0 ? 1.0f : -1.0f) : fallback, 0, 0, 0);
			};
			// 척추 사슬: 다음 사람 본 쪽이 위
			const Bone spineChain[] = { Spine, Chest, UpperChest, Neck, Head };
			for (int k = 0; k < 4; ++k)
			{
				const Bone b = spineChain[k];
				if (N[b] < 0)
					continue;
				for (int j = k + 1; j < 5; ++j)
					if (N[spineChain[j]] >= 0)
					{
						corr[b] = FromTo(vec(N[b], N[spineChain[j]]), up);
						break;
					}
			}
			for (int side = 0; side < 2; ++side)
			{
				const int o = side * 4;
				const XMVECTOR out = outward(N[LeftHand + o], side == 0 ? -1.0f : 1.0f);
				if (N[LeftShoulder + o] >= 0)
					corr[LeftShoulder + o] = FromTo(vec(N[LeftShoulder + o], N[LeftUpperArm + o]), out);
				corr[LeftUpperArm + o] = FromTo(vec(N[LeftUpperArm + o], N[LeftLowerArm + o]), out);
				corr[LeftLowerArm + o] = FromTo(vec(N[LeftLowerArm + o], N[LeftHand + o]), out);
				corr[LeftHand + o] = corr[LeftLowerArm + o];   // 손은 아래팔을 따라 펴진다
				corr[LeftUpperLeg + o] = FromTo(vec(N[LeftUpperLeg + o], N[LeftLowerLeg + o]), down);
				corr[LeftLowerLeg + o] = FromTo(vec(N[LeftLowerLeg + o], N[LeftFoot + o]), down);
				corr[LeftFoot + o] = corr[LeftLowerLeg + o];
				corr[LeftToes + o] = corr[LeftFoot + o];
			}
			// 머리는 목(없으면 척추 끝)을 따라
			for (int k = 3; k >= 0; --k)
				if (N[spineChain[k]] >= 0) { corr[Head] = corr[spineChain[k]]; break; }
			for (int b = 0; b < BoneCount; ++b)
			{
				const XMVECTOR bind = N[b] >= 0 ? RotationOf(a.BindGlobal[N[b]]) : XMQuaternionIdentity();
				XMStoreFloat4(&a.TPose[b], XMQuaternionNormalize(XMQuaternionMultiply(bind, corr[b])));   // 바인드 다음 보정
			}
		}

		std::map<const SkeletonAvataData*, std::unique_ptr<Avatar>>& Cache()
		{
			static std::map<const SkeletonAvataData*, std::unique_ptr<Avatar>> cache;
			return cache;
		}
	}

	const char* BoneName(int bone) { return bone >= 0 && bone < BoneCount ? kNames[bone] : "?"; }

	const Avatar& Get(const SkeletonAvataData& skeleton)
	{
		auto& c = Cache()[&skeleton];
		if (c == nullptr)
		{
			c = std::make_unique<Avatar>();
			Build(*c, skeleton);
			EditorLog::Write("Animation", "humanoid avatar %s: %s, %d/%d bones", skeleton.Name.c_str(), c->Valid ? "valid" : "not humanoid", c->Found, (int)BoneCount);
		}
		return *c;
	}

	XMFLOAT3 ScaleHips(const Avatar& source, const Avatar& target, const XMFLOAT3& p)
	{
		const float k = target.LegLength / source.LegLength;
		return XMFLOAT3(target.HipsBind.x + (p.x - source.HipsBind.x) * k, target.HipsBind.y + (p.y - source.HipsBind.y) * k,
			target.HipsBind.z + (p.z - source.HipsBind.z) * k);
	}

	void Retarget(const Avatar& src, const Avatar& dst, const AnimationClip& clip, const std::vector<int>& sourceMap, float t,
		std::vector<XMFLOAT4X4>& outLocal)
	{
		const SkeletonAvataData& ss = *src.Skeleton;
		const SkeletonAvataData& ds = *dst.Skeleton;
		thread_local std::vector<XMFLOAT4X4> srcLocal, srcGlobal, dstGlobal;
		AnimationPose::SampleLocal(ss, &clip, sourceMap, t, srcLocal);
		AnimationPose::ComputeGlobals(ss, srcLocal, srcGlobal);

		const int n = (int)ds.NodeNames.size();
		dstGlobal.resize(n);
		outLocal.resize(n);
		const XMMATRIX unit = XMMatrixScaling(ds.UnitScale, ds.UnitScale, ds.UnitScale);
		for (int i = 0; i < n; ++i)
		{
			const int parent = ds.BoneHierarchy[i];
			const XMMATRIX parentG = parent >= 0 && parent < i ? XMLoadFloat4x4(&dstGlobal[parent]) : unit;
			const int h = dst.HumanOf[i];
			XMMATRIX g;
			if (h >= 0 && src.Node[h] >= 0)
			{
				// 모델 공간에서 T-포즈 → 지금 자세 회전을 그대로
				const XMVECTOR qa = RotationOf(srcGlobal[src.Node[h]]);
				const XMVECTOR delta = XMQuaternionMultiply(XMQuaternionInverse(XMLoadFloat4(&src.TPose[h])), qa);
				const XMVECTOR q = XMQuaternionNormalize(XMQuaternionMultiply(XMLoadFloat4(&dst.TPose[h]), delta));
				XMVECTOR pos;
				if (h == Hips)
				{
					const XMFLOAT3 p = ScaleHips(src, dst, Pos(srcGlobal, src.Node[Hips]));
					pos = XMVectorSet(p.x, p.y, p.z, 1.0f);
				}
				else
				{
					// 뼈 길이는 target 그대로: 바인드 로컬 위치를 부모 자세로
					const XMFLOAT4X4& bl = ds.BindLocal[i];
					pos = XMVector3TransformCoord(XMVectorSet(bl._41, bl._42, bl._43, 1.0f), parentG);
				}
				g = XMMatrixScalingFromVector(ScaleOf(dst.BindGlobal[i])) * XMMatrixRotationQuaternion(q) * XMMatrixTranslationFromVector(pos);
			}
			else
				g = XMLoadFloat4x4(&ds.BindLocal[i]) * parentG;
			XMStoreFloat4x4(&dstGlobal[i], g);
			XMVECTOR det;
			XMStoreFloat4x4(&outLocal[i], g * XMMatrixInverse(&det, parentG));
		}
	}
}
