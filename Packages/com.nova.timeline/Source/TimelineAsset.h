#pragma once
#include <memory>
#include <string>
#include <vector>

// Unity Timeline 의 TimelineAsset (.playable, JSON). 트랙 · 클립 · 마커만 — 묶을 오브젝트 (바인딩) 는 PlayableDirector 가 가진다.
//  트랙 종류 (Unity 이름):
//   Animation   — 묶은 Animator 에 모델 클립 (asset = FBX · GLB, clipIndex). 겹치면 자동으로 섞고 Ease In / Out, 클립 사이는 Animator 그대로
//   Activation  — 묶은 GameObject 를 클립 동안 켜고 밖에서는 끈다
//   Audio       — 묶은 AudioSource 로 클립 (asset = 소리 파일) 을 그 시간에
//   Signal      — 마커 시간에 묶은 GameObject 스크립트의 메서드 (Unity Signal Emitter + Signal Receiver 를 하나로)
//   Cinemachine — 클립마다 카메라 오브젝트 (Cinemachine Camera) 를 그 동안만 켠다 — Brain 이 섞는다
struct TimelineClip
{
	std::string Name;
	double Start = 0.0, Duration = 1.0;
	double ClipIn = 0.0;            // 클립 안에서 시작할 시간 (초)
	double Speed = 1.0;
	double EaseIn = 0.0, EaseOut = 0.0;
	std::string Asset;              // Animation · Audio: 파일 경로
	int ClipIndex = 0;              // Animation: 파일 안 클립 번호
	bool Loop = false;              // Animation: 클립보다 길면 되풀이
	uint64_t Object = 0;            // Cinemachine: 켤 카메라 오브젝트 (fileID — 씬 오브젝트라 디렉터의 references 에도 같이 둔다)
	double End() const { return Start + Duration; }
};

struct TimelineMarker
{
	double Time = 0.0;
	std::string Method;
	std::string String;
	float Float = 0.0f;
	int Int = 0;
	bool Retroactive = false;       // Play 를 그 뒤 시간에서 시작해도 부른다
};

struct TimelineTrack
{
	std::string Id;                 // 바인딩 키 (만들 때 정해 바뀌지 않는다)
	std::string Type = "Animation";
	std::string Name;
	bool Muted = false;
	std::vector<TimelineClip> Clips;
	std::vector<TimelineMarker> Markers;
};

class TimelineAsset
{
public:
	std::string Path;
	double FrameRate = 30.0;
	double FixedDuration = 0.0;     // 0 = 마지막 클립 · 마커 끝 (Unity Duration Mode: Based On Clips)
	std::vector<TimelineTrack> Tracks;
	unsigned Revision = 0;

	double Duration() const;
	TimelineTrack* FindTrack(const std::string& idOrName);
	TimelineTrack& AddTrack(const std::string& type, const std::string& name);

	std::string ToJsonString() const;
	void ApplyJson(const std::string& text);
	bool Save() const;
	void Commit() { ++Revision; Save(); }

	static std::shared_ptr<TimelineAsset> Load(const std::string& path);
	static std::shared_ptr<TimelineAsset> Create(const std::string& path);
	static const char* const* TrackTypes(int& count);
};
