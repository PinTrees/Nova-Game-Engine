#include "pch.h"
#include "AnimationPose.h"

namespace AnimationPose
{
	std::vector<int> MapChannels(const AnimationClip& clip, const SkeletonAvataData& skeleton)
	{
		std::unordered_map<std::string, int> byName;
		byName.reserve(skeleton.NodeNames.size());
		for (size_t i = 0; i < skeleton.NodeNames.size(); ++i)
			byName.emplace(skeleton.NodeNames[i], (int)i);

		std::vector<int> map(clip.Channels.size(), -1);
		for (size_t c = 0; c < clip.Channels.size(); ++c)
		{
			auto it = byName.find(clip.Channels[c].NodeName);
			if (it != byName.end())
				map[c] = it->second;
		}
		return map;
	}

	void SampleLocal(const SkeletonAvataData& skeleton, const AnimationClip* clip, const std::vector<int>& channelToNode,
		float t, std::vector<XMFLOAT4X4>& outLocal)
	{
		outLocal = skeleton.BindLocal;
		if (clip == nullptr)
			return;
		for (size_t c = 0; c < clip->Channels.size() && c < channelToNode.size(); ++c)
		{
			const int node = channelToNode[c];
			if (node >= 0 && node < (int)outLocal.size())
				clip->Channels[c].Sample(t, outLocal[node]);
		}
	}

	void ComputeGlobals(const SkeletonAvataData& skeleton, const std::vector<XMFLOAT4X4>& local, std::vector<XMFLOAT4X4>& outGlobal)
	{
		const size_t n = (std::min)(local.size(), skeleton.BoneHierarchy.size());
		outGlobal.resize(n);
		const XMMATRIX unit = XMMatrixScaling(skeleton.UnitScale, skeleton.UnitScale, skeleton.UnitScale);
		for (size_t i = 0; i < n; ++i)
		{
			const int parent = skeleton.BoneHierarchy[i];
			XMMATRIX m = XMLoadFloat4x4(&local[i]);
			if (parent >= 0 && parent < (int)i)
				m = m * XMLoadFloat4x4(&outGlobal[parent]);
			else
				m = m * unit;   // 루트: 파일 단위(cm) → 미터
			XMStoreFloat4x4(&outGlobal[i], m);
		}
	}
}
