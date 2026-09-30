#pragma once
#include "Component.h"

class SkinnedMeshRenderer;
struct AnimationClip;
class SkeletonAvataData;

enum class UpdateMode
{
	Normal,
	Fixed,
	UnScale,
};

// Unity 의 (레거시) Animation 컴포넌트.
//  - 기본 클립(Animation)과 클립 목록(Animations)을 가지고, Play Automatically 면 시작할 때 기본 클립을 반복 재생한다.
//  - 같은 GameObject 와 자식들의 Skinned Mesh Renderer 에 포즈를 넣는다 (클립 채널은 노드 이름으로 연결).
//  - 클립 참조는 "FBX 경로 + 클립 번호" 로 저장한다.
class AnimationPlayer : public Component
{
	using Super = Component;

public:
	struct ClipRef
	{
		std::string Path;
		int Index = 0;
		shared_ptr<AnimationClip> Clip;
	};

private:
	ClipRef					m_DefaultClip;          // Inspector 의 "Animation"
	std::vector<ClipRef>	m_Clips;                // Inspector 의 "Animations"

	bool		m_PlayAutomatically = true;
	bool		m_AnimatePhysics = false;
	int			m_CullingType = 0;                  // Always Animate / Based On Renderers
	UpdateMode	m_UpdateMode = UpdateMode::Normal;

	// 재생 상태
	shared_ptr<AnimationClip>	m_Playing;
	float						m_TimePos = 0.0f;
	bool						m_IsPlaying = false;

	// 캐시: 채널 → 노드 연결 (클립, 스켈레톤이 바뀌면 다시 만든다)
	const AnimationClip*		m_MappedClip = nullptr;
	const SkeletonAvataData*	m_MappedSkeleton = nullptr;
	std::vector<int>			m_ChannelToNode;
	std::vector<XMFLOAT4X4>		m_Local, m_Global;

	// 이전 형식 호환 (m_AnimationFilePath / m_SkeletoneAvataFilePath)
	std::string m_LegacySkeletonPath;
	int m_LegacySkeletonIndex = 0;

public:
	AnimationPlayer();
	virtual ~AnimationPlayer();

	// ---- Unity API ----
	void SetClip(const std::string& fbxPath, int clipIndex);      // 기본 클립 지정 (목록에도 추가)
	bool Play();                                                  // 기본 클립
	bool Play(const std::string& clipName);                       // 목록에서 이름으로
	void Stop();
	bool IsPlaying() const { return m_IsPlaying; }
	float GetTime() const { return m_TimePos; }
	void SetTime(float t) { m_TimePos = t; }
	shared_ptr<AnimationClip> GetPlayingClip() const { return m_Playing; }

	// 현재 시간의 포즈를 Skinned Mesh Renderer 들에 적용 (에디터 미리보기에도 사용)
	void Sample();

public:
	virtual void Start() override;
	virtual void Update() override;
	virtual void FixedUpdate() override;

public:
	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "animation"; }

private:
	void Advance(float dt);
	void CollectRenderers(GameObject* go, std::vector<SkinnedMeshRenderer*>& out);

	GENERATE_COMPONENT_BODY(AnimationPlayer)
};

REGISTER_COMPONENT(AnimationPlayer)
