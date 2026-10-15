#pragma once
#include "Component.h"
#include "TimelineAsset.h"
#include <map>
#include <set>

struct AnimationClip;

// Unity 의 PlayableDirector: Timeline (.playable) 을 재생한다. 트랙 → 묶은 오브젝트 (Bindings — 트랙 id 마다 GameObject fileID).
//  Play On Awake · Wrap Mode (Hold = 끝에서 마지막 장면 · Loop · None = 끝나면 멈춤) · Initial Time · Update Method (Game Time · Unscaled · Manual).
//  Play 중: 시간이 나아가며 트랙을 평가 — 신호 · 소리는 지나갈 때만. 편집 중: Timeline 창의 재생 헤드가 SetTime + Evaluate (미리 보기, 신호 · 소리 없이).
//  Stop: 켜고 끈 오브젝트를 처음대로 (Unity Post-Playback State = Revert), Animator 덮기를 푼다
class PlayableDirector : public Component
{
public:
	enum class Wrap { Hold = 0, Loop = 1, None = 2 };
	std::string AssetPath;
	bool PlayOnAwake = true;
	int WrapMode = (int)Wrap::Hold;
	double InitialTime = 0.0;
	int UpdateMethod = 0;                       // 0 Game Time, 1 Unscaled Game Time, 2 Manual
	std::map<std::string, uint64> Bindings;     // 트랙 id → GameObject

	PlayableDirector();
	~PlayableDirector();

	void Play();
	void Pause();
	void Resume();
	void Stop();
	// 지금 시간을 평가 (편집 중 미리 보기 · Manual)
	void Evaluate();
	void SetTime(double t);
	double GetTime() const { return m_Time; }
	double Duration();
	bool IsPlaying() const { return m_Playing; }
	std::shared_ptr<TimelineAsset> GetAsset();
	GameObject* BoundObject(const TimelineTrack& track) const;
	void SetBinding(const std::string& trackIdOrName, uint64 gameObject);

	void Start() override;
	void Update() override;
	void OnDestroy() override;
	void OnInspectorGUI() override;
	void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "timeline"; }

	GENERATE_COMPONENT_BODY(PlayableDirector)

private:
	std::shared_ptr<TimelineAsset> m_Asset;
	std::string m_AssetFor;
	unsigned long long m_AssetCheck = 0;
	double m_Time = 0.0;
	bool m_Playing = false;
	bool m_Started = false;                      // Play 를 한 번 불렀다 (Stop 이 되돌릴 것이 있다)
	std::map<uint64, bool> m_InitialActive;      // Activation · Cinemachine 이 건드린 오브젝트의 처음 상태
	std::set<const GameObject*> m_Overridden;    // Animation Track 이 덮은 오브젝트
	std::set<std::string> m_AudioStarted;        // "트랙 id/클립 번호" (Play 중 소리를 튼 클립)
	std::map<std::string, std::shared_ptr<AnimationClip>> m_Clips;

	void EvaluateRange(double prev, double now, bool events);
	void EvaluateAnimation(const TimelineTrack& track, GameObject* bound, double t);
	void SetActiveTracked(GameObject* go, bool active);
	void RevertAll();
	std::shared_ptr<AnimationClip> ClipOf(const TimelineClip& c);
};

REGISTER_PACKAGE_COMPONENT(PlayableDirector)
