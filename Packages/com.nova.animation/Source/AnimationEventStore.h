#pragma once
#include <memory>
#include <string>
#include <vector>

// 애니메이션 이벤트 (Unity AnimationEvent) 저장소 — 모델 파일 옆 "<모델>.animevents" (JSON).
//  Unity 는 모델 .meta 의 clipAnimations[i].events 에 두지만, NOVA 는 .meta 를 바꾸면 모델을 다시 가져오므로 따로 둔다.
//  { "clips": { "0": [ { "time": 0.4, "functionName": "OnHit", "stringParameter": "", "floatParameter": 0, "intParameter": 0 } ] } }
//  키 = 파일 안 클립 번호 (ClipIndex), 시간 = 초 (클립 처음부터)
namespace AnimationEventStore
{
	struct Event
	{
		float Time = 0.0f;
		std::string Function;
		std::string String;
		float Float = 0.0f;
		int Int = 0;
	};
	using EventList = std::vector<Event>;

	// 그 모델 · 클립의 이벤트 (시간 순). 없으면 nullptr. 파일이 바뀌면 다시 읽는다 (0.5 초에 한 번 본다)
	std::shared_ptr<const EventList> Get(const std::string& modelPath, int clipIndex);
	// 바꾸고 저장 (시간 순으로 정렬)
	bool Set(const std::string& modelPath, int clipIndex, const EventList& events);
	std::string FilePath(const std::string& modelPath);
}
