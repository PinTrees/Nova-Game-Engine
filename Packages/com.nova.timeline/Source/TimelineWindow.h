#pragma once
#include "EditorWindow.h"
#include <memory>

class PlayableDirector;
class TimelineAsset;

// Unity 의 Timeline 창 (Window > Sequencing > Timeline): 고른 GameObject 의 Playable Director 와 그 Timeline 을 편집한다.
//  - 위: 재생 · 멈춤 · 처음으로, 시간, + 트랙, 확대
//  - 왼쪽: 트랙 (이름 · 종류 · 음소거), 오른쪽: 눈금자 + 클립 막대 (끌어 옮기기, 양 끝 끌어 자르기) · 신호 마커 · 재생 헤드 (끌면 미리 보기)
//  - 아래: 고른 트랙 · 클립 · 마커의 값 (바인딩 · 시작 · 길이 · Clip In · 속도 · Ease · 파일 · 카메라 …)
//  - 오른쪽 클릭: 클립 · 마커 더하기, Delete: 지우기
class TimelineWindow : public EditorWindow
{
public:
	TimelineWindow();
	~TimelineWindow();
	static void Focus(PlayableDirector* director);
	static TimelineWindow* Instance() { return s_Instance; }

	void Update() override;
	void BeforeBegin() override;
	void OnRender() override;

private:
	static TimelineWindow* s_Instance;
	uint64 m_DirectorObject = 0;     // Playable Director 가 붙은 GameObject (fileID)
	int m_SelTrack = -1, m_SelClip = -1, m_SelMarker = -1;
	float m_PixelsPerSecond = 120.0f;
	float m_Scroll = 0.0f;           // 왼쪽 끝의 시간 (초)
	bool m_Preview = false;          // 편집 중 재생 (Play 단추)
	enum class Drag { None, Playhead, Move, TrimLeft, TrimRight, Marker } m_Drag = Drag::None;
	double m_DragStart = 0.0, m_DragOrigStart = 0.0, m_DragOrigDuration = 0.0, m_DragOrigClipIn = 0.0;
	double m_ContextTime = 0.0;
	int m_ContextTrack = -1;

	PlayableDirector* Director();
	void DrawToolbar(PlayableDirector* d, TimelineAsset* a);
	void DrawTracks(PlayableDirector* d, TimelineAsset* a, ImVec2 pos, ImVec2 size);
	void DrawProperties(PlayableDirector* d, TimelineAsset* a);
	void Preview(PlayableDirector* d);
};
