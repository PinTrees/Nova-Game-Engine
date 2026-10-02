#pragma once
#include "SkinnedData.h"

// 스켈레톤 포즈 계산 도우미 (행 벡터 규약: 전역 = 로컬 * 부모 전역)
namespace AnimationPose
{
	// 클립의 채널 → 스켈레톤 노드 인덱스 (이름으로 연결, 없으면 -1)
	NOVA_API std::vector<int> MapChannels(const AnimationClip& clip, const SkeletonAvataData& skeleton);

	// 바인드 포즈 로컬 행렬을 채운 뒤, 클립이 있으면 시간 t 의 채널 값으로 덮어쓴다
	NOVA_API void SampleLocal(const SkeletonAvataData& skeleton, const AnimationClip* clip, const std::vector<int>& channelToNode,
		float t, std::vector<XMFLOAT4X4>& outLocal);

	// 로컬 → 전역 (루트에 단위 변환 UnitScale 을 곱한다)
	NOVA_API void ComputeGlobals(const SkeletonAvataData& skeleton, const std::vector<XMFLOAT4X4>& local, std::vector<XMFLOAT4X4>& outGlobal);
}

namespace AnimationPose
{
	// 두 로컬 포즈를 섞는다 (w = 0 → a, 1 → b). 위치/크기는 선형, 회전은 구면 보간.
	NOVA_API void Blend(const std::vector<XMFLOAT4X4>& a, const std::vector<XMFLOAT4X4>& b, float w, std::vector<XMFLOAT4X4>& out);
}
