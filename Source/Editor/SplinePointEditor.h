#pragma once

class EditorCamera;

// Scene 뷰에서 곡선 점(로컬 위치 목록)을 편집하는 공용 도구 (호수 윤곽, 강, 지형 스플라인).
//  점 끌기 = 지형 위로 옮김(놓는 높이는 Source 가 정함), Ctrl+클릭 = 가장 가까운 변에 점 추가(열린 곡선은 끝 바깥이면 늘림),
//  Shift+클릭 / Delete = 선택한 점 지우기. 켜져 있는 동안 이동 핸들·클릭 선택은 쉰다. Undo 는 씬 Undo(컴포넌트 JSON)
struct SplinePointSource
{
	virtual ~SplinePointSource() = default;
	virtual int Count() const = 0;
	virtual Vec3 LocalPos(int i) const = 0;
	virtual void SetLocalPos(int i, const Vec3& p) = 0;
	virtual void Insert(int at, int copyFrom, const Vec3& local) = 0;   // copyFrom 점의 값(폭 등)을 복사해 at 에
	virtual void Erase(int i) = 0;
	virtual XMMATRIX World() const = 0;
	virtual bool Closed() const = 0;
	virtual int MinPoints() const = 0;
	virtual Vec3 DisplayWorld(int i) const;                            // 핸들을 그릴 월드 위치 (기본 = 점 위치)
	virtual Vec3 Place(const Vec3& hitWorld, int index) const = 0;    // 끌기·추가 때 놓을 월드 위치
	virtual float FallbackPlaneY() const = 0;                         // 지형이 없을 때 마우스 평면 높이
	virtual const char* EndLabel(int i) const { (void)i; return nullptr; }
	virtual bool KeepLocalY() const { return false; }                 // 끌어도 로컬 y 를 바꾸지 않음 (호수)
	virtual void Changed() {}
	virtual const char* Noun() const { return "Point"; }
};

namespace SplinePointEditor
{
	struct State
	{
		bool Edit = false;
		int Selected = -1;
		int Dragging = -1;
		const void* Owner = nullptr;   // 편집 중인 컴포넌트 (바뀌면 선택 초기화)
	};

	void SetOwner(State& s, const void* owner);
	// Inspector: Edit Points 버튼 + 안내. 켜짐/꺼짐이 바뀌면 true
	bool EditButton(State& s, int count);
	void DrawPoints(const SplinePointSource& src, const State& s, ImU32 color);
	// Scene 뷰 (SceneViewOverlay::Begin ~ End 사이). 편집 중이면 true
	bool SceneGUI(SplinePointSource& src, State& s, EditorCamera* camera, const ImVec2& viewMin, const ImVec2& viewMax, bool viewHovered);
}
