#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct SkeletonAvataData;

// Unity 의 Avatar Mask (.mask 에셋): Animator 레이어가 어느 본을 움직이나.
//  - Humanoid: 몸 부위 13 개 (AvatarMaskBodyPart 순서) — 사람 본 (HumanoidAvatar) 과 그 아래 사람 본이 아닌 노드 (손가락 · 무기 소켓 …)
//  - Transform: 노드 이름 (경로의 마지막 이름) 마다 켜고 끄기 — 목록에 있으면 그 값, 없으면 Humanoid 결과
//  JSON: { "humanoid": { "Body": true, "LeftLeg": false, … }, "transforms": [ { "path": "Root/Hips/Spine", "active": true } ] }
class AvatarMask
{
public:
	enum BodyPart
	{
		Root, Body, Head, LeftLeg, RightLeg, LeftArm, RightArm, LeftFingers, RightFingers,
		LeftFootIK, RightFootIK, LeftHandIK, RightHandIK, PartCount
	};
	static const char* PartName(int part);

	std::string Path;
	bool Parts[PartCount];
	struct TransformEntry { std::string Path; bool Active = true; };
	std::vector<TransformEntry> Transforms;
	unsigned Revision = 0;

	AvatarMask();
	bool Save() const;
	std::string ToJsonString() const;
	void ApplyJson(const std::string& text);
	void Commit() { ++Revision; Save(); }

	// 노드마다 0 · 1 (스켈레톤마다 한 번 계산, Revision 이 바뀌면 다시)
	const std::vector<float>& NodeWeights(const SkeletonAvataData& skeleton) const;
	// 노드가 속한 몸 부위 (사람 본 조상으로)
	static int PartOfNode(const SkeletonAvataData& skeleton, int node);

	// 에셋 캐시 (경로 → 마스크, 파일이 바뀌면 다시 읽는다)
	static std::shared_ptr<AvatarMask> Load(const std::string& path);
	static std::shared_ptr<AvatarMask> Create(const std::string& path);
	// 상체 마스크 (Body · Head · 팔 · 손가락 · 손 IK) — 새 마스크 기본값과 다른 흔한 꼴
	static void SetUpperBody(AvatarMask& mask);

private:
	struct NodeCache { unsigned Revision = ~0u; std::vector<float> Weights; };
	mutable std::map<const SkeletonAvataData*, NodeCache> m_Cache;   // 노드 맵 — 다른 스켈레톤을 넣어도 참조가 그대로
	mutable std::mutex m_Lock;                                        // 자세 평가가 작업 스레드에서 부른다
	long long m_FileStamp = 0;
	unsigned long long m_CheckedTick = 0;   // 파일 시각을 마지막으로 본 때 (0.5 초에 한 번만)
};
